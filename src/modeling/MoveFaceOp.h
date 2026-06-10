#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Vec.hxx>
#include <vector>
#include <string>

// Slide a face WITHIN ITS OWN PLANE (lateral move, never along the normal —
// that's PushPull). The whole body shears to follow: the selected face's plane
// shifts by the in-plane move vector, the opposite end stays pinned, linear in
// between — so a box's top slid sideways becomes a parallelepiped with slanted
// walls, and any other features lean proportionally. Implemented as one affine
// gp_GTrsf shear (BRepBuilderAPI_GTransform): no booleans, topology always
// valid by construction. The move vector is projected onto the face plane.
class MoveFaceOp : public Operation {
public:
    MoveFaceOp() = default;
    ~MoveFaceOp() override = default;

    void setBody(int bodyId) { m_bodyId = bodyId; }
    void setFace(const TopoDS_Face& f) { m_face = f; }
    // The full translation (direction * distance) applied to the face.
    void setMoveVector(const gp_Vec& v) { m_move = v; }
    // Sketches lying ON the moved face — they slide with it (translated by the
    // in-plane move vector) as part of the same atomic op.
    void setSketchIds(std::vector<int> ids) { m_sketchIds = std::move(ids); }
    // Per-loop motion (three hole states, set by how much of the hole is
    // selected). moveOuter = the face outline slides. Per hole i (face-wire
    // order): holeSlant[i] = its TOP ring follows (top edge picked → slants),
    // holeVertical[i] = BOTH rings follow (cylinder wall picked → straight tube).
    // Neither = the hole stays put while the face moves around it. So:
    //   top ring slides    ⟺ holeSlant[i] OR holeVertical[i]
    //   bottom ring slides ⟺ holeVertical[i]
    void setLoopMotion(bool moveOuter, std::vector<bool> holeSlant,
                       std::vector<bool> holeVertical) {
        m_moveOuter = moveOuter;
        m_holeSlant = std::move(holeSlant);
        m_holeVertical = std::move(holeVertical);
    }

    int getBodyId() const { return m_bodyId; }
    gp_Vec getMoveVector() const { return m_move; }
    const TopoDS_Shape& getPreviousShape() const { return m_previousShape; }

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Move Face"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "moveface"; }
    OperationDiff captureDiff() const override;
    std::vector<int> plannedBodyIds() const override { return {m_bodyId}; }
    std::string serializeParams() const override;
    bool deserializeParams(const std::string& blob) override;
    bool rehydrateFromReload(const ReloadState& state, Document& doc) override;

private:
    int m_bodyId = -1;
    TopoDS_Face m_face;
    gp_Vec m_move{0.0, 0.0, 0.0};
    std::vector<int> m_sketchIds; // sketches that ride along (translated by m_move)
    bool m_moveOuter = true;            // does the face outline slide?
    std::vector<bool> m_holeSlant;     // per-hole: top ring follows (slant)
    std::vector<bool> m_holeVertical;  // per-hole: both rings follow (straight tube)
    gp_Vec m_appliedMove{0.0, 0.0, 0.0}; // in-plane move actually applied (for undo)
    TopoDS_Shape m_previousShape; // for undo
    TopoDS_Shape m_resultShape;
    // Ordinal index of m_face within the pre-op body shape, for reload
    // (SubShapeIndex.h). Empty/unresolved → the step replays as a ReplayOp.
    std::vector<int> m_faceIndices;
};
