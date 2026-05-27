#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/OffsetFaceOp.h"
#include <TopoDS.hxx>

REGISTER_PLUGIN(OffsetFace, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Offset Face", "Feature",
        materializr::SelectionContext::HasFaces, 501,
        [](materializr::PluginContext& ctx) {
            const auto& sel = ctx.selection().getSelection();
            for (const auto& entry : sel) {
                if (entry.type == SelectionType::Face && entry.bodyId >= 0 && !entry.shape.IsNull()) {
                    auto op = std::make_unique<OffsetFaceOp>();
                    op->setBody(entry.bodyId);
                    op->setFace(TopoDS::Face(entry.shape));
                    op->setDistance(1.0);
                    if (ctx.history().pushOperation(std::move(op), ctx.document())) {
                        ctx.markMeshesDirty();
                    }
                    break;
                }
            }
        }, nullptr});
})
