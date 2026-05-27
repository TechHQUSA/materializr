#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/SplitBodyOp.h"
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

REGISTER_PLUGIN(SplitBody, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Split Body", "Feature",
        materializr::SelectionContext::HasBodies, 502,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            if (!sel.empty() && sel[0].bodyId >= 0) {
                auto op = std::make_unique<SplitBodyOp>();
                op->setBody(sel[0].bodyId);
                op->setSplitPlane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
                if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                    ctx.markMeshesDirty();
                }
            }
        }, nullptr});
})
