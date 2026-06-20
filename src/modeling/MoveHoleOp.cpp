#include "MoveHoleOp.h"

#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>
#include <cstdio>
#include <cmath>

namespace {

bool isPlanar(const TopoDS_Face& f) {
    Handle(Geom_Surface) s = BRep_Tool::Surface(f);
    return !s.IsNull() && s->IsKind(STANDARD_TYPE(Geom_Plane));
}

bool wireHasEdge(const TopoDS_Wire& w, const TopoDS_Edge& e) {
    for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next())
        if (ex.Current().IsSame(e)) return true;
    return false;
}

// Outward normal of a planar face at its parametric centre.
gp_Vec faceNormal(const TopoDS_Face& f) {
    BRepGProp_Face gf(f);
    double u1, u2, v1, v2;
    gf.Bounds(u1, u2, v1, v2);
    gp_Pnt p; gp_Vec n;
    gf.Normal(0.5 * (u1 + u2), 0.5 * (v1 + v2), p, n);
    if (n.Magnitude() > 1e-12) n.Normalize();
    return n;
}

} // namespace

bool MoveHoleOp::buildVoid(const TopoDS_Shape& body,
                           const std::vector<TopoDS_Face>& selectedWalls,
                           TopoDS_Shape& voidOut, gp_Vec& entryNormal,
                           bool& isPocket, TopoDS_Wire* entryOpening) {
    isPocket = false;
    if (body.IsNull() || selectedWalls.empty()) return false;

    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

    // Does edge e touch a face in `set` other than `self`?
    auto edgeTouchesSet = [&](const TopoDS_Edge& e, const TopTools_MapOfShape& set,
                              const TopoDS_Face& self) {
        if (!edgeFaces.Contains(e)) return false;
        const TopTools_ListOfShape& fl = edgeFaces.FindFromKey(e);
        for (TopTools_ListIteratorOfListOfShape it(fl); it.More(); it.Next()) {
            TopoDS_Face f = TopoDS::Face(it.Value());
            if (!f.IsSame(self) && set.Contains(f)) return true;
        }
        return false;
    };
    auto wireBordersSet = [&](const TopoDS_Wire& W, const TopTools_MapOfShape& set,
                              const TopoDS_Face& self) {
        for (TopExp_Explorer we(W, TopAbs_EDGE); we.More(); we.Next())
            if (edgeTouchesSet(TopoDS::Edge(we.Current()), set, self)) return true;
        return false;
    };

    // SELECTION-DRIVEN void: bounded by exactly the SELECTED walls, plus any
    // CONNECTOR face fully wedged between them (a counterbore's step annulus when
    // BOTH its cylinders are selected). So one segment selected → that segment
    // moves; all segments selected → the whole stepped hole moves; a simple hole
    // (one wall) → the whole hole.
    TopTools_MapOfShape wallSet;
    for (const auto& w : selectedWalls) wallSet.Add(w);
    bool changed = true;
    while (changed) {
        changed = false;
        for (TopExp_Explorer fx(body, TopAbs_FACE); fx.More(); fx.Next()) {
            TopoDS_Face F = TopoDS::Face(fx.Current());
            if (wallSet.Contains(F)) continue;
            bool anyWire = false, allShared = true;
            for (TopoDS_Iterator wi(F); wi.More(); wi.Next()) {
                if (wi.Value().ShapeType() != TopAbs_WIRE) continue;
                anyWire = true;
                if (!wireBordersSet(TopoDS::Wire(wi.Value()), wallSet, F)) {
                    allShared = false; break;
                }
            }
            if (anyWire && allShared) { wallSet.Add(F); changed = true; }
        }
    }

    // Sew the wall faces, then cap every interface where the wall set meets the
    // rest of the body. A planar boundary face met along its OUTER wire (a step
    // annulus, a pocket floor) is used AS-IS so any hole through it (the shank)
    // is preserved; a bore opening (an outer face's INNER wire) or a non-planar
    // junction gets a synthesized planar cap over the interface loop.
    BRepBuilderAPI_Sewing sew(1e-6);
    for (TopExp_Explorer fx(body, TopAbs_FACE); fx.More(); fx.Next()) {
        TopoDS_Face F = TopoDS::Face(fx.Current());
        if (wallSet.Contains(F)) sew.Add(F);
    }

    int mouthCount = 0;
    bool haveEntry = false;
    for (TopExp_Explorer fx(body, TopAbs_FACE); fx.More(); fx.Next()) {
        TopoDS_Face F = TopoDS::Face(fx.Current());
        if (wallSet.Contains(F)) continue;
        TopoDS_Wire outer = isPlanar(F) ? BRepTools::OuterWire(F) : TopoDS_Wire();
        // Real boundary face: planar, and its OUTER wire borders the wall set.
        if (!outer.IsNull() && wireBordersSet(outer, wallSet, F)) {
            sew.Add(F);
            continue;
        }
        // Otherwise cap each bordering (inner / non-planar) interface loop.
        for (TopoDS_Iterator wi(F); wi.More(); wi.Next()) {
            if (wi.Value().ShapeType() != TopAbs_WIRE) continue;
            TopoDS_Wire W = TopoDS::Wire(wi.Value());
            if (!wireBordersSet(W, wallSet, F)) continue;
            BRepBuilderAPI_MakeFace mf(W, Standard_True /*only plane*/);
            if (!mf.IsDone()) {
                std::fprintf(stderr, "[MoveHole] cap face failed\n");
                return false;
            }
            sew.Add(mf.Face());
            if (isPlanar(F)) {          // an opening through an outer face = a mouth
                ++mouthCount;
                if (!haveEntry) {
                    entryNormal = faceNormal(F);
                    if (entryOpening) *entryOpening = W;
                    haveEntry = true;
                }
            }
        }
    }

    if (mouthCount < 1 || !haveEntry || entryNormal.Magnitude() < 1e-9) {
        isPocket = true; // no opening to the outside → can't form a movable void
        std::fprintf(stderr, "[MoveHole] refused: no open mouth in selection\n");
        return false;
    }

    sew.Perform();
    TopoDS_Shape sewn = sew.SewedShape();
    if (sewn.IsNull()) { std::fprintf(stderr, "[MoveHole] sewing failed\n"); return false; }

    TopExp_Explorer shx(sewn, TopAbs_SHELL);
    if (!shx.More()) { std::fprintf(stderr, "[MoveHole] no closed shell\n"); return false; }
    BRepBuilderAPI_MakeSolid ms(TopoDS::Shell(shx.Current()));
    if (!ms.IsDone()) { std::fprintf(stderr, "[MoveHole] make solid failed\n"); return false; }
    TopoDS_Solid solid = ms.Solid();
    BRepLib::OrientClosedSolid(solid); // normalize so it's a positive-volume void
    if (!BRepCheck_Analyzer(solid).IsValid()) {
        std::fprintf(stderr, "[MoveHole] void solid invalid\n");
        return false;
    }
    voidOut = solid;
    return true;
}

bool MoveHoleOp::execute(Document& doc) {
    m_wasPocket = false;
    TopoDS_Shape body = doc.getBody(m_bodyId);
    if (body.IsNull() || m_seedWalls.empty()) return false;
    m_previousShape = body;

    TopoDS_Shape voidSolid;
    gp_Vec entryNormal;
    if (!buildVoid(body, m_seedWalls, voidSolid, entryNormal, m_wasPocket))
        return false; // unrecognized selection → caller toasts

    // Project the requested move onto the entry plane (a hole slides ACROSS its
    // face, never along the bore — that would just deepen/shorten it).
    gp_Vec move = m_move;
    double along = move.Dot(entryNormal);
    move -= entryNormal * along;
    if (move.Magnitude() < 1e-9) return false; // no in-plane motion

    try {
        // Fill the old hole back to solid, then cut the same void at the new spot.
        BRepAlgoAPI_Fuse fuse(body, voidSolid);
        fuse.Build();
        if (!fuse.IsDone() || fuse.Shape().IsNull()) return false;

        gp_Trsf t; t.SetTranslation(move);
        TopoDS_Shape movedVoid = BRepBuilderAPI_Transform(voidSolid, t, true).Shape();

        BRepAlgoAPI_Cut cut(fuse.Shape(), movedVoid);
        cut.Build();
        if (!cut.IsDone() || cut.Shape().IsNull()) return false;

        TopoDS_Shape result = cut.Shape();
        // The fill-fuse leaves the patched disk as a separate face coplanar with
        // the original face, with a ghost circular edge between them. Merge same-
        // surface faces and drop the redundant seam so the old location looks
        // untouched (also tidies the new hole's edges).
        try {
            ShapeUpgrade_UnifySameDomain unify(result, Standard_True /*edges*/,
                                               Standard_True /*faces*/,
                                               Standard_False /*concat bsplines*/);
            unify.Build();
            if (!unify.Shape().IsNull()) result = unify.Shape();
        } catch (...) { /* keep the un-unified result rather than fail the move */ }

        if (!BRepCheck_Analyzer(result).IsValid()) {
            std::fprintf(stderr, "[MoveHole] result invalid\n");
            return false;
        }
        doc.updateBody(m_bodyId, result);
        return true;
    } catch (...) {
        std::fprintf(stderr, "[MoveHole] OCCT exception\n");
        return false;
    }
}

bool MoveHoleOp::undo(Document& doc) {
    if (m_previousShape.IsNull()) return false;
    doc.updateBody(m_bodyId, m_previousShape);
    return true;
}

OperationDiff MoveHoleOp::captureDiff() const {
    // No params/rehydrate yet → reloads as a baked ReplayOp. Reporting the
    // pre-op body lets that replay restore the right shape, so the moved hole
    // survives save/reload (just not re-editable across sessions for now).
    OperationDiff d;
    if (m_bodyId >= 0 && !m_previousShape.IsNull())
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    return d;
}

std::string MoveHoleOp::description() const {
    double mag = m_move.Magnitude();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Move hole %.1f mm", mag);
    return buf;
}
