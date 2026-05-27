#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/TransformOp.h"
#include <cstdio>

REGISTER_PLUGIN(Transform, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Rotate 45\xC2\xB0", "Transform",
        materializr::SelectionContext::HasBodies, 200,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            if (!sel.empty() && sel[0].bodyId >= 0) {
                auto op = std::make_unique<TransformOp>();
                op->setBodyId(sel[0].bodyId);
                op->setType(TransformType::Rotate);
                op->setRotation(0.0, 1.0, 0.0, 45.0);
                if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                    ctx.markMeshesDirty();
                }
            }
        }, nullptr});

    ctx.registerToolbarButton({"Scale 1.5x", "Transform",
        materializr::SelectionContext::HasBodies, 201,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            if (!sel.empty() && sel[0].bodyId >= 0) {
                auto op = std::make_unique<TransformOp>();
                op->setBodyId(sel[0].bodyId);
                op->setType(TransformType::Scale);
                op->setScale(1.5);
                if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                    ctx.markMeshesDirty();
                }
            }
        }, nullptr});
})
