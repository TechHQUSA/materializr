#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/PatternOp.h"
#include <cstdio>

REGISTER_PLUGIN(Pattern, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Linear Pattern", "Pattern",
        materializr::SelectionContext::HasBodies, 300,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            if (!sel.empty() && sel[0].bodyId >= 0) {
                auto op = std::make_unique<PatternOp>();
                op->setBody(sel[0].bodyId);
                op->setType(PatternType::Linear);
                op->setCount(3);
                op->setLinearSpacing(5.0, 0.0, 0.0);
                if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                    ctx.markMeshesDirty();
                }
            }
        }, nullptr});

    ctx.registerToolbarButton({"Radial Pattern", "Pattern",
        materializr::SelectionContext::HasBodies, 301,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            if (!sel.empty() && sel[0].bodyId >= 0) {
                auto op = std::make_unique<PatternOp>();
                op->setBody(sel[0].bodyId);
                op->setType(PatternType::Radial);
                op->setCount(6);
                op->setRadialAxis(0.0, 0.0, 1.0);
                op->setTotalAngle(360.0);
                if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                    ctx.markMeshesDirty();
                }
            }
        }, nullptr});
})
