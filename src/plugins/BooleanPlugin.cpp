#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/BooleanOp.h"
#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Distinct body ids in the current selection, in selection order.
std::vector<int> distinctSelectedBodies(materializr::PluginContext& ctx) {
    std::vector<int> bodies;
    for (const auto& s : ctx.selection().getSelection()) {
        if (s.bodyId < 0) continue;
        bool found = false;
        for (int b : bodies) if (b == s.bodyId) { found = true; break; }
        if (!found) bodies.push_back(s.bodyId);
    }
    return bodies;
}

// Fold every body after the first into bodies[0] (target = the surviving first
// body, each other a tool, consumed). Union/Intersect combine all; Subtract
// cuts all the rest out of the first.
void runChainedBoolean(materializr::PluginContext& ctx,
                       const std::vector<int>& bodies, BooleanMode mode) {
    bool allOk = true;
    for (size_t i = 1; i < bodies.size() && allOk; ++i) {
        auto op = std::make_unique<BooleanOp>();
        op->setTargetBodyId(bodies[0]);
        op->setToolBodyId(bodies[i]);
        op->setMode(mode);
        allOk = ctx.history().pushOperation(std::move(op), ctx.document());
    }
    if (allOk) { ctx.markMeshesDirty(); ctx.selection().clear(); }
    else std::fprintf(stderr, "Boolean failed (%zu bodies)\n", bodies.size());
}

// Subtract is order-dependent (A − B keeps A), so unlike Union/Intersect we ask
// which body to KEEP. These hold the selection while the picker modal is up.
std::vector<int> g_subtractPending;
bool g_subtractOpenRequested = false;

} // anonymous namespace

REGISTER_PLUGIN(Boolean, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Union", "Boolean",
        materializr::SelectionContext::MultipleBodies, 100,
        [](materializr::PluginContext& ctx) {
            auto b = distinctSelectedBodies(ctx);
            if (b.size() >= 2) runChainedBoolean(ctx, b, BooleanMode::Union);
        },
        nullptr,
        "Merge the selected bodies into one (A ∪ B). Overlapping volumes fuse."});

    ctx.registerToolbarButton({"Subtract", "Boolean",
        materializr::SelectionContext::MultipleBodies, 101,
        [](materializr::PluginContext& ctx) {
            // Order matters — ask which body to keep (the rest cut out of it).
            auto b = distinctSelectedBodies(ctx);
            if (b.size() >= 2) { g_subtractPending = b; g_subtractOpenRequested = true; }
        },
        nullptr,
        "Cut the other selected bodies out of one you pick (A − B)."});

    ctx.registerToolbarButton({"Intersect", "Boolean",
        materializr::SelectionContext::MultipleBodies, 102,
        [](materializr::PluginContext& ctx) {
            auto b = distinctSelectedBodies(ctx);
            if (b.size() >= 2) runChainedBoolean(ctx, b, BooleanMode::Intersect);
        },
        nullptr,
        "Keep only the volume the selected bodies share (A ∩ B)."});

    // "Keep which body?" picker for Subtract, rendered each frame.
    materializr::OverlayContribution overlay;
    overlay.name = "BooleanSubtractPicker";
    overlay.render = [](materializr::PluginContext& ctx) {
        if (g_subtractPending.empty()) return;
        if (g_subtractOpenRequested) {
            ImGui::OpenPopup("Subtract##boolpick");
            g_subtractOpenRequested = false;
        }
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Subtract##boolpick", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Keep which body? The others are subtracted from it.");
            ImGui::Separator();
            int chosen = -1;
            for (int id : g_subtractPending) {
                std::string label = ctx.document().getBodyName(id);
                if (label.empty()) label = "Body " + std::to_string(id);
                ImGui::PushID(id);
                if (ImGui::Button(label.c_str(), ImVec2(240, 0))) chosen = id;
                ImGui::PopID();
            }
            ImGui::Separator();
            bool cancel = ImGui::Button("Cancel");
            if (cancel || chosen >= 0) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();

            if (chosen >= 0) {
                // Keep `chosen` first; the rest become tools cut from it.
                std::vector<int> ordered{chosen};
                for (int id : g_subtractPending) if (id != chosen) ordered.push_back(id);
                g_subtractPending.clear();
                runChainedBoolean(ctx, ordered, BooleanMode::Subtract);
            } else if (cancel) {
                g_subtractPending.clear();
            }
        }
    };
    ctx.registerOverlay(std::move(overlay));
})
