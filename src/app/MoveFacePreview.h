#pragma once

#include "core/Document.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <functional>
#include <memory>

class MoveFaceOp;

namespace materializr {

// Runs a Move Face preview's real MoveFaceOp::execute() off the main
// thread. Unlike PushPullPreview there is exactly ONE body involved (Move
// Face never touches a second body) and no Precomputed/setPrecomputed step:
// `configure` never calls MoveFaceOp::setSketchIds() during a preview (only
// commitMoveFace() does, on the live document), so execute() has no effect
// beyond replacing the scratch document's body - run() just hands that shape
// back for the caller to land with a plain Document::updateBody().
struct MoveFacePreviewResult {
    bool ok = false;
    TopoDS_Shape shape;
    double millis = 0.0; // execute() wall time on the worker, for diagnostics
};

class MoveFacePreviewJob {
public:
    // Main thread. `originalBody` is the gesture's snapshot (NOT the live
    // document - the live document may carry a stale preview from a prior
    // frame). `configure` sets kind/vector/rotation/scale/twist/loop-motion
    // on the scratch op; pass MoveFaceController::configureFaceOp bound to
    // the controller. Null when the face is not a live sub-shape of
    // originalBody (BRepBuilderAPI_Copy::ModifiedShape would throw).
    static std::unique_ptr<MoveFacePreviewJob> prepare(
        const TopoDS_Shape& originalBody, const TopoDS_Face& face,
        const std::function<void(MoveFaceOp&)>& configure);
    ~MoveFacePreviewJob();

    // Worker thread. Safe to call exactly once.
    MoveFacePreviewResult run();

private:
    MoveFacePreviewJob();
    std::unique_ptr<Document> m_scratch;
    int m_scratchBodyId = -1;
    std::unique_ptr<MoveFaceOp> m_op;
};

} // namespace materializr
