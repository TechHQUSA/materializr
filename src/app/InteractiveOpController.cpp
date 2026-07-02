#include "ui/UiTheme.h"
#include "InteractiveOpController.h"
#include "touch_mode.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../core/Operation.h"
#include <imgui.h>
#include <cstdio>

namespace materializr {

bool InteractiveOpController::begin(const IopContext& ctx) {
    int body = onBegin(ctx);
    if (body < 0) return false;
    try {
        m_snapshot = ctx.doc.getBody(body);
    } catch (...) { return false; }
    if (m_snapshot.IsNull()) return false;
    m_bodyId = body;
    m_active = true;
    m_commitRequested = false;
    m_pendingUpdate = false;
    m_lastUpdateMs = 0.0;
    timedUpdate(ctx); // seeds m_lastUpdateMs so the first change knows the cost
    return true;
}

void InteractiveOpController::timedUpdate(const IopContext& ctx) {
    auto t0 = std::chrono::steady_clock::now();
    update(ctx);
    m_lastUpdateMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
}

void InteractiveOpController::update(const IopContext& ctx) {
    if (!m_active || m_bodyId < 0) return;
    if (!wantsLivePreview(ctx)) {
        // Live preview suppressed (recomputing it per change would freeze the
        // UI). Keep the snapshot shown and mark preview "ok" so Confirm still
        // computes + pushes the op once. pushOperation re-runs execute() and
        // refuses on failure, so a heavy commit that fails just does nothing.
        ctx.doc.updateBody(m_bodyId, m_snapshot);
        ctx.markMeshesDirty();
        m_previewOk = true;
        return;
    }
    // Reset to the snapshot, then run a fresh op against it so the live
    // preview tracks the current values exactly without compounding edits.
    ctx.doc.updateBody(m_bodyId, m_snapshot);
    ctx.markMeshesDirty();
    m_previewOk = false;
    try {
        std::unique_ptr<Operation> op = buildOp(ctx);
        if (op && op->execute(ctx.doc)) {
            ctx.markMeshesDirty();
            m_previewOk = true;
        } else {
            ctx.doc.updateBody(m_bodyId, m_snapshot);
        }
    } catch (...) {
        ctx.doc.updateBody(m_bodyId, m_snapshot);
    }
}

void InteractiveOpController::commit(const IopContext& ctx) {
    if (!m_active) return;
    // Roll back the preview; History::pushOperation re-runs the op cleanly
    // against the snapshot.
    ctx.doc.updateBody(m_bodyId, m_snapshot);
    if (!m_previewOk) {
        cancel(ctx);
        return;
    }
    std::unique_ptr<Operation> op = buildOp(ctx);
    if (op) {
        if (!wantsLivePreview(ctx) && ctx.progress && ctx.deferHeavy) {
            // Heavy op (live preview was off): defer it to run BETWEEN frames
            // with a progress reporter so the window stays alive and the user
            // can cancel. A cancel makes execute() fail → pushOperation refuses
            // → body stays at the snapshot (clean no-op).
            op->setProgressReporter(ctx.progress);
            History* hist = &ctx.history;
            Document* doc = &ctx.doc;
            auto markDirty = ctx.markMeshesDirty;
            Operation* raw = op.release();
            ctx.deferHeavy([hist, doc, raw, markDirty]() {
                std::unique_ptr<Operation> o(raw);
                hist->pushOperation(std::move(o), *doc);
                if (markDirty) markDirty();
            });
        } else {
            ctx.history.pushOperation(std::move(op), ctx.doc);
        }
    }
    ctx.selection.clear();
    ctx.markMeshesDirty();
    cleanup();
}

void InteractiveOpController::cancel(const IopContext& ctx) {
    if (m_bodyId >= 0 && !m_snapshot.IsNull()) {
        ctx.doc.updateBody(m_bodyId, m_snapshot);
    }
    ctx.markMeshesDirty();
    cleanup();
}

void InteractiveOpController::cleanup() {
    m_active = false;
    m_commitRequested = false;
    m_previewOk = false;
    m_pendingUpdate = false;
    m_lastUpdateMs = 0.0;
    m_bodyId = -1;
    m_snapshot.Nullify();
    onCleanup();
}

void InteractiveOpController::renderPanel(const IopContext& ctx) {
    if (!m_active) return;

    // Same pinned top-right anchor + flag set every hand-written panel
    // used — known stable, no hover flicker.
    const float w = panelWidth();
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x +
                                       ImGui::GetWindowWidth() - w - 20.0f,
                                   ImGui::GetWindowPos().y + 50.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    char id[64];
    std::snprintf(id, sizeof(id), "##iop_%s", title());
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextColored(materializr::accentText(), "%s", title());
    ImGui::Separator();

    bool changed = false;
    panelBody(ctx, changed);
    // Adaptive pacing (see header): if the last recompute was cheap, preview
    // live on every change; if it was expensive, coalesce the drag and only
    // recompute once the value has been still for a short settle window. The
    // 1s post-input grace (hasActiveWork) keeps frames coming, so the deferred
    // recompute reliably fires. Commit always rebuilds from the current value,
    // so a still-pending preview never commits stale geometry.
    constexpr double kBudgetMs = 40.0;   // >~40ms preview ⇒ too heavy to run live
    constexpr double kSettleMs = 120.0;  // quiet time before a deferred recompute
    auto now = std::chrono::steady_clock::now();
    if (changed) {
        m_lastChange = now;
        if (m_lastUpdateMs <= kBudgetMs) timedUpdate(ctx);
        else m_pendingUpdate = true;
    } else if (m_pendingUpdate) {
        double sinceMs = std::chrono::duration<double, std::milli>(
            now - m_lastChange).count();
        if (sinceMs >= kSettleMs) { m_pendingUpdate = false; timedUpdate(ctx); }
    }

    ImGui::Spacing();
    bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    bool esc   = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    bool doCommit = m_commitRequested || enter ||
                    ImGui::Button(materializr::btnConfirm(), ImVec2(120, 0));
    if (!doCommit) ImGui::SameLine();
    bool doCancel = !doCommit &&
                    (esc || ImGui::Button(materializr::btnCancel(), ImVec2(120, 0)));
    ImGui::End();

    if (doCommit) commit(ctx);
    else if (doCancel) cancel(ctx);
}

} // namespace materializr
