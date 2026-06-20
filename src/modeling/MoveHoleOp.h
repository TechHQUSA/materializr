#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Vec.hxx>
#include <vector>
#include <string>

// Slide a THROUGH-HOLE laterally across the face it pierces — round, square,
// polygon, any straight prismatic section. Reuses the Move button + gizmo, but
// under the hood it's a boolean re-cut, NOT a face loft: it fills the hole back
// to solid where it was and cuts an identical hole at the new position
//   result = (body ∪ void) − translate(void, move)
// where `void` is the exact hole solid (the opening profile extruded through the
// body). No opposite-cap heuristic, so it never fills cavities the way a face
// loft can. Pockets (blind holes) are detected and REFUSED for now — moving a
// pocket is a separate feature (the cap/depth need their own handling).
class MoveHoleOp : public Operation {
public:
    MoveHoleOp() = default;
    ~MoveHoleOp() override = default;

    void setBody(int bodyId) { m_bodyId = bodyId; }
    // A wall face of the hole the user clicked (one cylindrical face for a round
    // hole, one of the flats for a square/polygon hole — the rest are gathered).
    void setSeedWall(const TopoDS_Face& wall) { m_seedWall = wall; }
    void setMoveVector(const gp_Vec& v) { m_move = v; }

    int getBodyId() const { return m_bodyId; }
    gp_Vec getMoveVector() const { return m_move; }
    // True if the last execute() refused because the selected hole is a pocket.
    bool wasPocket() const { return m_wasPocket; }
    const TopoDS_Shape& getPreviousShape() const { return m_previousShape; }

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Move Hole"; }
    std::string description() const override;
    void renderProperties() override {}
    std::string typeId() const override { return "move_hole"; }
    std::vector<int> plannedBodyIds() const override { return {m_bodyId}; }
    OperationDiff captureDiff() const override;

    // Build the exact hole-void solid from a clicked wall face. Returns false if
    // the selection isn't a recognizable through-hole; sets isPocket=true (and
    // returns false) when it's a blind pocket. `entryNormal` is the outward
    // normal of the face the hole opens through (the plane the move slides in).
    // Static so the interactive layer can validate/preview a selection cheaply.
    // `entryOpening` (optional) receives the entry mouth's loop — the hole's top
    // rim — for the interactive move highlight.
    static bool buildVoid(const TopoDS_Shape& body, const TopoDS_Face& seedWall,
                          TopoDS_Shape& voidOut, gp_Vec& entryNormal,
                          bool& isPocket, TopoDS_Wire* entryOpening = nullptr);

private:
    int m_bodyId = -1;
    TopoDS_Face m_seedWall;
    gp_Vec m_move{0.0, 0.0, 0.0};
    bool m_wasPocket = false;
    TopoDS_Shape m_previousShape; // for undo
};
