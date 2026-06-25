#include "MoveHoleOp.h"
#include "SubShapeIndex.h"
#include <cstdio>
#include <cstdlib>

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
// The TopTools_ListIteratorOfListOfShape typedef comes from TopTools_ListOfShape.hxx;
// the standalone <...ListIteratorOfListOfShape.hxx> shim header was removed in OCCT
// 8.0 (the vcpkg/MSVC Windows build), so include the list header instead.
#include <TopTools_ListOfShape.hxx>
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

bool MoveHoleOp::buildVoid(const TopoDS_Shape& body, const TopoDS_Face& seedWall,
                           TopoDS_Shape& voidOut, gp_Vec& entryNormal,
                           bool& isPocket, TopoDS_Wire* entryOpening) {
    isPocket = false;
    if (body.IsNull() || seedWall.IsNull()) return false;

    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

    // Gather the hole's ACTUAL interior faces (walls, cones, counterbore steps —
    // any segment), and the two outer MOUTHS the bore opens through. This is
    // section-agnostic AND profile-agnostic: it reconstructs the exact void by
    // its real faces, so a countersink (cone+shank) or a counterbore (two
    // cylinders + a step annulus) rebuilds correctly, not just a constant prism.
    //
    // BFS from the clicked wall. For each face's edge, the adjacent face is a
    // MOUTH if it's planar and the edge lies on one of its INNER wires (the bore
    // pierces it → that inner wire is the opening). Otherwise it's another
    // interior face of the hole (another wall, a cone, or a step — whose own
    // outer boundary the edge sits on) → keep walking. A pocket floor would also
    // be gathered as an interior face, leaving only ONE mouth, which we reject.
    std::vector<TopoDS_Face> walls;
    TopTools_MapOfShape inSet;
    std::vector<std::pair<TopoDS_Face, TopoDS_Wire>> mouths; // (cap face, opening loop)
    TopTools_MapOfShape mouthSeen;

    std::vector<TopoDS_Face> stack;
    stack.push_back(seedWall);
    inSet.Add(seedWall);
    walls.push_back(seedWall);
    while (!stack.empty()) {
        TopoDS_Face W = stack.back(); stack.pop_back();
        for (TopExp_Explorer ex(W, TopAbs_EDGE); ex.More(); ex.Next()) {
            const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
            if (!edgeFaces.Contains(e)) continue;
            const TopTools_ListOfShape& fl = edgeFaces.FindFromKey(e);
            for (TopTools_ListIteratorOfListOfShape it(fl); it.More(); it.Next()) {
                TopoDS_Face f = TopoDS::Face(it.Value());
                if (f.IsSame(W) || inSet.Contains(f)) continue;
                // Mouth? planar + the edge is on one of f's inner wires.
                TopoDS_Wire opening;
                if (isPlanar(f)) {
                    TopoDS_Wire outer = BRepTools::OuterWire(f);
                    if (!wireHasEdge(outer, e)) {
                        for (TopoDS_Iterator wi(f); wi.More(); wi.Next()) {
                            if (wi.Value().ShapeType() != TopAbs_WIRE) continue;
                            TopoDS_Wire w = TopoDS::Wire(wi.Value());
                            if (w.IsSame(outer)) continue;
                            if (wireHasEdge(w, e)) { opening = w; break; }
                        }
                    }
                }
                if (!opening.IsNull()) {
                    if (!mouthSeen.Contains(f)) {
                        mouthSeen.Add(f);
                        mouths.emplace_back(f, opening);
                    }
                } else {
                    inSet.Add(f);
                    walls.push_back(f);
                    stack.push_back(f);
                }
            }
        }
    }

    // A through-hole opens at exactly two mouths. Exactly ONE mouth (+ a gathered
    // floor) = a real blind hole / pocket: recognized but unsupported, so flag it
    // and let the caller explain. ZERO mouths means the clicked face isn't a hole
    // wall at all (a plain outer face / cube side); >2 is an unrecognized profile.
    // In BOTH of those, leave isPocket false so the caller falls through to
    // ordinary Move Face instead of falsely refusing it as a "pocket".
    if (mouths.size() != 2) {
        isPocket = (mouths.size() == 1);
        std::fprintf(stderr, "[MoveHole] not a through-hole: %zu mouths%s\n",
                     mouths.size(), isPocket ? " (blind/pocket)" : "");
        return false;
    }
    entryNormal = faceNormal(mouths[0].first);
    if (entryNormal.Magnitude() < 1e-9) return false;
    if (entryOpening) *entryOpening = mouths[0].second; // the hole's top rim

    // Sew the interior faces + a cap over each mouth opening into a closed shell,
    // then a solid — the exact hole void, whatever its axial profile. Caps reuse
    // the mouths' real inner-wire edges, so they sew to the walls seamlessly.
    BRepBuilderAPI_Sewing sew(1e-6);
    for (const auto& w : walls) sew.Add(w);
    for (const auto& m : mouths) {
        BRepBuilderAPI_MakeFace mf(m.second, Standard_True /*only plane*/);
        if (!mf.IsDone()) {
            std::fprintf(stderr, "[MoveHole] could not cap a mouth\n");
            return false;
        }
        sew.Add(mf.Face());
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
    if (body.IsNull() || m_seedWall.IsNull()) return false;
    m_previousShape = body;

    TopoDS_Shape voidSolid;
    gp_Vec entryNormal;
    if (!buildVoid(body, m_seedWall, voidSolid, entryNormal, m_wasPocket))
        return false; // pocket or unrecognized → caller toasts

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
    OperationDiff d;
    if (m_bodyId >= 0 && !m_previousShape.IsNull())
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    return d;
}

std::string MoveHoleOp::serializeParams() const {
    // body + move vector as plain numbers; the seed wall as an ordinal index
    // into the INPUT shape's canonical face map (see SubShapeIndex.h).
    char buf[160];
    std::snprintf(buf, sizeof(buf), "body=%d;move=%.9g,%.9g,%.9g",
                  m_bodyId, m_move.X(), m_move.Y(), m_move.Z());
    std::string blob = buf;
    if (!m_previousShape.IsNull() && !m_seedWall.IsNull()) {
        std::vector<TopoDS_Shape> faces{m_seedWall};
        std::string idx = SubShapeIndex::serialize(m_previousShape, faces,
                                                   TopAbs_FACE);
        if (!idx.empty()) blob += ";wall=" + idx;
    }
    return blob;
}

bool MoveHoleOp::deserializeParams(const std::string& blob) {
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if (key == "body") { m_bodyId = std::atoi(val.c_str()); any = true; }
        else if (key == "move") {
            double x = 0, y = 0, z = 0;
            std::sscanf(val.c_str(), "%lf,%lf,%lf", &x, &y, &z);
            m_move = gp_Vec(x, y, z);
            any = true;
        } else if (key == "wall") {
            m_seedWallIndices = SubShapeIndex::parse(val);
            any = true;
        }
        pos = end + 1;
    }
    return any;
}

bool MoveHoleOp::rehydrateFromReload(const ReloadState& state, Document& /*doc*/) {
    if (m_bodyId < 0) return false;

    m_previousShape.Nullify();
    for (const auto& [id, shp] : state.modifiedBefore)
        if (id == m_bodyId) { m_previousShape = shp; break; }
    if (m_previousShape.IsNull()) return false;

    // The seed wall must resolve against the reloaded input shape, else the BFS
    // in buildVoid can't find the hole — decline so it falls back to a baked op.
    if (m_seedWallIndices.empty()) return false;
    std::vector<TopoDS_Shape> resolved;
    if (!SubShapeIndex::resolveAll(m_previousShape, m_seedWallIndices,
                                   TopAbs_FACE, resolved) ||
        resolved.empty()) {
        return false;
    }
    m_seedWall = TopoDS::Face(resolved[0]);
    return true;
}

std::string MoveHoleOp::description() const {
    double mag = m_move.Magnitude();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Move hole %.1f mm", mag);
    return buf;
}
