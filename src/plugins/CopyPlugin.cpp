#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/CopyOp.h"

REGISTER_PLUGIN(Copy, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Duplicate", "Edit",
        materializr::SelectionContext::HasBodies, 800,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            if (!sel.empty() && sel[0].bodyId >= 0) {
                auto op = std::make_unique<CopyOp>();
                op->setSourceBodyId(sel[0].bodyId);
                op->setOffset(2.0, 0.0, 0.0);
                if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                    ctx.markMeshesDirty();
                }
            }
        }, nullptr});
})
