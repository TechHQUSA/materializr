#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <string>
#include <vector>

// Merge coplanar faces back into single faces — the repair half of issue #81.
//
// An imported STEP part usually arrives with flat surfaces already split into
// several coplanar pieces, and editing it splits more. The pieces read as one
// surface, but they stay separate faces, so the user sees a seam line across an
// otherwise flat face and anything that treats a face as a unit misbehaves:
// Unfold sees several faces where there is one panel, and Sketch-on-face binds
// to whichever sliver was picked.
//
// The ops themselves now prevent NEW seams (see UnifyTolerance.h). This op is
// for geometry that is ALREADY split.
//
// TWO SCOPES, and the difference matters:
//
//   * whole body (setBody alone) — conservative. Runs at the same angular
//     tolerance as every other unify site, so it only merges faces that are
//     EXACTLY coplanar. Safe to point at a whole part.
//
//   * picked faces (setFaces) — the user asserting "these are one face".
//     Restricted to the edges BETWEEN the picked faces, so nothing else in the
//     body can dissolve, and because the scope is bounded it escalates the
//     angular tolerance up to 1e-2 rad. That matters: measured on the reporting
//     user's nacelle, all 41 remaining seams were between planes 1e-4..1e-2 rad
//     apart — not exactly coplanar, so no tolerance safe for a whole body will
//     ever touch them. Every result is still checked valid and volume-preserving
//     before it is accepted.
//
// execute() refuses (and so adds no history step) when nothing merges, so
// running it on clean geometry is a no-op the user is told about rather than an
// empty step in the timeline.
class MergeFacesOp : public Operation {
public:
    MergeFacesOp();
    ~MergeFacesOp() override = default;

    void setBody(int id);
    // Restrict the merge to these faces of the body. Empty => whole body.
    void setFaces(const std::vector<TopoDS_Shape>& faces);

    int getBodyId() const { return m_bodyId; }
    int getFacesBefore() const { return m_facesBefore; }
    int getFacesAfter() const { return m_facesAfter; }
    bool isFaceScoped() const { return !m_faces.empty() || !m_anchors.empty(); }

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Merge Faces"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "mergefaces"; }
    OperationDiff captureDiff() const override;
    std::vector<int> plannedBodyIds() const override { return {m_bodyId}; }
    std::string serializeParams() const override;
    bool deserializeParams(const std::string& blob) override;
    bool rehydrateFromReload(const ReloadState& state, Document& doc) override;

private:
    // Normal + point per picked face, so a replay onto a rebuilt body can find
    // the same faces again. Same anchor scheme ShellOp uses for its openings —
    // the picked faces have no stable name of their own on an imported body.
    struct FaceAnchor { gp_Dir normal; gp_Pnt point; };

    void captureAnchors(const TopoDS_Shape& body);
    bool rebindFaces(const TopoDS_Shape& body);

    int m_bodyId = -1;
    TopoDS_Shape m_previousShape;
    std::vector<TopoDS_Shape> m_faces;
    std::vector<FaceAnchor> m_anchors;
    // For the History label. Persisted so a reloaded step still reads
    // "Merge Faces (189 -> 183)" instead of losing its own description.
    int m_facesBefore = 0;
    int m_facesAfter = 0;
};
