#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../modeling/ConstructionPlaneOp.h"

REGISTER_PLUGIN(ConstructionPlane, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Construction Plane", "Create",
        materializr::SelectionContext::Always, 50,
        [](materializr::PluginContext& ctx) {
            auto op = std::make_unique<ConstructionPlaneOp>();
            op->setType(PlaneCreationType::XY);
            op->setOffset(0.0);
            op->setName("Custom Plane");
            ctx.history().pushOperation(std::move(op), ctx.document());
            ctx.markMeshesDirty();
        }, nullptr});

    ctx.registerCommand({"New Construction Plane", "",
        [](materializr::PluginContext& ctx) {
            auto op = std::make_unique<ConstructionPlaneOp>();
            op->setType(PlaneCreationType::XY);
            op->setOffset(0.0);
            op->setName("Custom Plane");
            ctx.history().pushOperation(std::move(op), ctx.document());
            ctx.markMeshesDirty();
        }});
})
