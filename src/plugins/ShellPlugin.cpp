#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/ShellOp.h"
#include <TopoDS.hxx>

REGISTER_PLUGIN(Shell, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Shell", "Feature",
        materializr::SelectionContext::HasFaces, 500,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            for (const auto& entry : sel) {
                if (entry.type == SelectionType::Face && entry.bodyId >= 0 && !entry.shape.IsNull()) {
                    auto op = std::make_unique<ShellOp>();
                    op->setBody(entry.bodyId);
                    op->setThickness(1.0);
                    op->addFaceToRemove(TopoDS::Face(entry.shape));
                    if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                        ctx.markMeshesDirty();
                    }
                    break;
                }
            }
        }, nullptr});
})
