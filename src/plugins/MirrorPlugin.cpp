#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/MirrorOp.h"
#include <cstdio>

REGISTER_PLUGIN(Mirror, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Mirror XZ", "Transform",
        materializr::SelectionContext::HasBodies, 210,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            if (!sel.empty() && sel[0].bodyId >= 0) {
                auto op = std::make_unique<MirrorOp>();
                op->setBody(sel[0].bodyId);
                op->setPlane(MirrorPlane::XZ);
                op->setKeepOriginal(true);
                if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                    ctx.markMeshesDirty();
                }
            }
        }, nullptr});
})
