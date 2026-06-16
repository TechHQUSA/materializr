#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/BooleanOp.h"
#include <cstdio>
#include <vector>

namespace {

void doBooleanOp(materializr::PluginContext& ctx, BooleanMode mode) {
    const auto& sel = ctx.selection().getSelection();
    std::vector<int> distinctBodies;
    for (const auto& s : sel) {
        if (s.bodyId >= 0) {
            bool found = false;
            for (int b : distinctBodies) { if (b == s.bodyId) { found = true; break; } }
            if (!found) distinctBodies.push_back(s.bodyId);
        }
    }
    if (distinctBodies.size() >= 2) {
        // Fold EVERY selected body into the first, not just the first two:
        // union/intersect combine them all; subtract cuts all the rest out of
        // the first. Each step is a BooleanOp (target = the surviving first
        // body, tool = the next); the tool body is consumed each time, so the
        // first id stays the running result.
        bool allOk = true;
        for (size_t i = 1; i < distinctBodies.size() && allOk; ++i) {
            auto op = std::make_unique<BooleanOp>();
            op->setTargetBodyId(distinctBodies[0]);
            op->setToolBodyId(distinctBodies[i]);
            op->setMode(mode);
            allOk = ctx.history().pushOperation(std::move(op), ctx.document());
        }
        if (allOk) {
            ctx.markMeshesDirty();
            ctx.selection().clear();
        } else {
            std::fprintf(stderr, "Boolean operation failed (combined %zu bodies)\n",
                         distinctBodies.size());
        }
    }
}

} // anonymous namespace

REGISTER_PLUGIN(Boolean, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Union", "Boolean",
        materializr::SelectionContext::MultipleBodies, 100,
        [](materializr::PluginContext& ctx) { doBooleanOp(ctx, BooleanMode::Union); },
        nullptr,
        "Merge the selected bodies into one (A ∪ B). Overlapping volumes fuse."});

    ctx.registerToolbarButton({"Subtract", "Boolean",
        materializr::SelectionContext::MultipleBodies, 101,
        [](materializr::PluginContext& ctx) { doBooleanOp(ctx, BooleanMode::Subtract); },
        nullptr,
        "Cut the second selected body out of the first (A − B)."});

    ctx.registerToolbarButton({"Intersect", "Boolean",
        materializr::SelectionContext::MultipleBodies, 102,
        [](materializr::PluginContext& ctx) { doBooleanOp(ctx, BooleanMode::Intersect); },
        nullptr,
        "Keep only the volume the selected bodies share (A ∩ B)."});
})
