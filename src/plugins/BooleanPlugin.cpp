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
        auto op = std::make_unique<BooleanOp>();
        op->setTargetBodyId(distinctBodies[0]);
        op->setToolBodyId(distinctBodies[1]);
        op->setMode(mode);
        if (ctx.history().pushOperation(std::move(op), ctx.document())) {
            ctx.markMeshesDirty();
            ctx.selection().clear();
        } else {
            std::fprintf(stderr, "Boolean operation failed\n");
        }
    }
}

} // anonymous namespace

REGISTER_PLUGIN(Boolean, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Union", "Boolean",
        materializr::SelectionContext::MultipleBodies, 100,
        [](materializr::PluginContext& ctx) { doBooleanOp(ctx, BooleanMode::Union); },
        nullptr});

    ctx.registerToolbarButton({"Subtract", "Boolean",
        materializr::SelectionContext::MultipleBodies, 101,
        [](materializr::PluginContext& ctx) { doBooleanOp(ctx, BooleanMode::Subtract); },
        nullptr});

    ctx.registerToolbarButton({"Intersect", "Boolean",
        materializr::SelectionContext::MultipleBodies, 102,
        [](materializr::PluginContext& ctx) { doBooleanOp(ctx, BooleanMode::Intersect); },
        nullptr});
})
