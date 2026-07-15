#include "BlendCut.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace materializr {
namespace blendcut {
namespace {

struct EdgeInfo {
    TopoDS_Edge edge;
    gp_Lin line;               // underlying infinite line of the edge
    gp_Pnt p0, p1, pm;         // endpoints + midpoint
    TopoDS_Face fRef, fOther;  // dRef is measured along fRef
    gp_Pln plnRef, plnOther;
    gp_Dir nRef, nOther;       // outward face normals at the edge
    gp_Dir dRefDir, dOtherDir; // in-face directions away from the edge
};

// A run of collinear fragments sharing one plane pair: swept as ONE tool
// over the union of their spans, which is what carries the blend across the
// gap a hole or pocket bit out of the edge.
struct Group {
    EdgeInfo rep;      // representative — dirs/normals valid for all members
    double tmin, tmax; // span along rep.line covered by every member
};

// A cutting solid plus the face of it that IS the blend surface (chamfer
// plane / fillet cylinder) — the caller finds the bevel on the cut result
// through it.
struct Tool {
    TopoDS_Shape solid;
    TopoDS_Face blendTemplate;
};

bool outwardNormal(const TopoDS_Face& f, gp_Dir& out) {
    try {
        BRepGProp_Face gf(f);
        Standard_Real u0, u1, v0, v1;
        gf.Bounds(u0, u1, v0, v1);
        gp_Pnt p;
        gp_Vec n;
        gf.Normal((u0 + u1) * 0.5, (v0 + v1) * 0.5, p, n);
        if (n.Magnitude() < 1e-12) return false;
        out = gp_Dir(n);
        return true;
    } catch (...) { return false; }
}

bool onFace(const TopoDS_Face& f, const gp_Pnt& p) {
    BRepClass_FaceClassifier cls(f, p, 1e-6);
    return cls.State() == TopAbs_IN || cls.State() == TopAbs_ON;
}

bool planesMatch(const gp_Pln& a, const gp_Pln& b) {
    if (!a.Axis().Direction().IsParallel(b.Axis().Direction(), 1e-4))
        return false;
    return a.Distance(b.Location()) < 1e-5;
}

bool analyzeEdge(const TopoDS_Shape& body, const TopoDS_Edge& e,
                 const TopTools_IndexedDataMapOfShapeListOfShape& efm,
                 const gp_Pln* refPln, double probeEps, EdgeInfo& out) {
    BRepAdaptor_Curve c(e);
    if (c.GetType() != GeomAbs_Line) return false;
    out.edge = e;
    out.line = c.Line();
    out.p0 = c.Value(c.FirstParameter());
    out.p1 = c.Value(c.LastParameter());
    out.pm = c.Value((c.FirstParameter() + c.LastParameter()) * 0.5);

    if (!efm.Contains(e)) return false;
    const TopTools_ListOfShape& fs = efm.FindFromKey(e);
    if (fs.Extent() < 2) return false;
    TopoDS_Face f1 = TopoDS::Face(fs.First());
    TopoDS_Face f2;
    for (const TopoDS_Shape& f : fs)
        if (!f.IsSame(f1)) { f2 = TopoDS::Face(f); break; }
    if (f2.IsNull()) return false;
    if (BRepAdaptor_Surface(f1).GetType() != GeomAbs_Plane ||
        BRepAdaptor_Surface(f2).GetType() != GeomAbs_Plane)
        return false;
    // dRef belongs to the asymmetric reference when one of this edge's pair
    // lies on its PLANE (by plane, not face identity: a pocket may have
    // fragmented the reference face, and the fragment bordering this edge is
    // a different TopoDS face on the same plane), else to the first face
    // (native Add() semantics).
    if (refPln && planesMatch(BRepAdaptor_Surface(f2).Plane(), *refPln))
        std::swap(f1, f2);
    out.fRef = f1;
    out.fOther = f2;
    out.plnRef = BRepAdaptor_Surface(f1).Plane();
    out.plnOther = BRepAdaptor_Surface(f2).Plane();

    if (!outwardNormal(f1, out.nRef) || !outwardNormal(f2, out.nOther))
        return false;
    gp_Dir t = out.line.Direction();
    // In-plane directions perpendicular to the edge, oriented INTO the solid's
    // material — derived from the face NORMALS, not a face classifier. The old
    // classifier probe (inFaceDir) landed in a drilled hole / open tray when a
    // feature fragmented the face near the edge and picked the wrong side, so
    // the wedge came out "backwards, out the back of the edge" (#57). For a
    // convex edge the material sits on the negative side of the OTHER face's
    // plane, so choose each direction's sign to make d·n(other) < 0.
    gp_Dir d1 = out.nRef.Crossed(t);
    gp_Dir d2 = out.nOther.Crossed(t);
    if (d1.Dot(out.nOther) > 0) d1.Reverse();
    if (d2.Dot(out.nRef) > 0) d2.Reverse();
    out.dRefDir = d1;
    out.dOtherDir = d2;

    // Convex only, decided from the SOLID (robust where the face classifier is
    // not): a point just off the edge along the OUTWARD normal bisector is
    // OUTSIDE a convex edge and INSIDE a concave one. A cut can only remove
    // material, so a concave edge (native OCCT would FILL it) is refused here.
    gp_Vec bis(gp_Vec(out.nRef) + gp_Vec(out.nOther));
    if (bis.Magnitude() < 1e-6) return false;
    bis.Normalize();
    const double off = std::min(probeEps, 0.05);
    BRepClass3d_SolidClassifier sc(body, out.pm.Translated(bis * off), 1e-7);
    if (sc.State() != TopAbs_OUT) return false;
    return true;
}

double paramOn(const gp_Lin& l, const gp_Pnt& p) {
    return gp_Vec(l.Location(), p).Dot(gp_Vec(l.Direction()));
}

// Analyze every selected edge (all-or-nothing: one unsupported edge refuses
// the whole fallback so a multi-edge blend never comes back half-built) and
// merge collinear fragments on the same plane pair into single-span groups.
bool buildGroups(const TopoDS_Shape& body,
                 const std::vector<TopoDS_Edge>& edges, const gp_Pln* refPln,
                 double probeEps, bool asymmetric,
                 std::vector<Group>& groups) {
    TopTools_IndexedDataMapOfShapeListOfShape efm;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, efm);

    std::vector<EdgeInfo> infos;
    for (const auto& e : edges) {
        EdgeInfo info;
        if (!analyzeEdge(body, e, efm, refPln, probeEps, info)) return false;
        infos.push_back(info);
    }

    for (const auto& info : infos) {
        Group* home = nullptr;
        for (auto& g : groups) {
            if (!g.rep.line.Direction().IsParallel(info.line.Direction(),
                                                   1e-4))
                continue;
            if (g.rep.line.Distance(info.pm) > 1e-5) continue;
            const bool straight = planesMatch(g.rep.plnRef, info.plnRef) &&
                                  planesMatch(g.rep.plnOther, info.plnOther);
            const bool crossed = planesMatch(g.rep.plnRef, info.plnOther) &&
                                 planesMatch(g.rep.plnOther, info.plnRef);
            if (!straight && !crossed) continue;
            // Crossed = the per-edge "first face" ordering flipped between
            // fragments. Harmless when both setbacks are equal; genuinely
            // ambiguous for an asymmetric chamfer with no shared reference
            // face, so refuse rather than guess.
            if (crossed && !straight && asymmetric && !refPln) return false;
            home = &g;
            break;
        }
        if (!home) {
            groups.push_back({info, 0.0, 0.0});
            home = &groups.back();
            home->tmin = home->tmax = paramOn(info.line, info.p0);
        }
        for (const gp_Pnt* p : {&info.p0, &info.p1}) {
            double t = paramOn(home->rep.line, *p);
            home->tmin = std::min(home->tmin, t);
            home->tmax = std::max(home->tmax, t);
        }
    }
    return !groups.empty();
}

// Profile plane basis for one group: the corner point at the span start and
// the sweep vector covering the merged span.
bool groupSpan(const Group& g, gp_Pnt& P0, gp_Vec& sweep) {
    const double len = g.tmax - g.tmin;
    if (len < 1e-6) return false;
    P0 = g.rep.line.Location().Translated(gp_Vec(g.rep.line.Direction()) *
                                          g.tmin);
    sweep = gp_Vec(g.rep.line.Direction()) * len;
    return true;
}

// Sweep a closed planar profile wire and report the face generated by
// `blendEdge` (the profile edge that IS the blend cross-section).
TopoDS_Shape sweepProfile(const TopoDS_Wire& w, const TopoDS_Edge& blendEdge,
                          const gp_Vec& sweep, TopoDS_Face& outTemplate) {
    BRepBuilderAPI_MakeFace mf(w, Standard_True);
    if (!mf.IsDone()) return TopoDS_Shape();
    BRepPrimAPI_MakePrism prism(mf.Face(), sweep);
    if (!prism.IsDone()) return TopoDS_Shape();
    const TopTools_ListOfShape& gen = prism.Generated(blendEdge);
    if (gen.IsEmpty() || gen.First().ShapeType() != TopAbs_FACE)
        return TopoDS_Shape();
    outTemplate = TopoDS::Face(gen.First());
    return prism.Shape();
}

// The chamfer tool: a triangular wedge — apex just outside the corner, the
// two setback points A/B exactly where the chamfer plane meets the faces.
bool makeChamferTool(const Group& g, double dRef, double dOther, Tool& out) {
    const EdgeInfo& e = g.rep;
    gp_Pnt P0;
    gp_Vec sweep;
    if (!groupSpan(g, P0, sweep)) return false;
    gp_Pnt A = P0.Translated(gp_Vec(e.dRefDir) * dRef);
    gp_Pnt B = P0.Translated(gp_Vec(e.dOtherDir) * dOther);
    // The setbacks must land ON their faces (checked at the representative
    // fragment's midpoint) — otherwise the blend is bigger than the face it
    // runs along and the "chamfer" would silently eat unrelated geometry.
    if (!onFace(e.fRef, e.pm.Translated(gp_Vec(e.dRefDir) * dRef)))
        return false;
    if (!onFace(e.fOther, e.pm.Translated(gp_Vec(e.dOtherDir) * dOther)))
        return false;
    gp_Vec outv = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    if (outv.Magnitude() < 1e-9) return false;
    outv.Normalize();
    // Apex slightly OUTSIDE the corner so the tool's side faces cross the
    // body's faces transversally instead of lying exactly on them (tangent
    // boolean inputs are where cuts get fragile). Immediately outside a
    // convex corner is empty space, so a small bulge cannot over-cut.
    gp_Pnt C = P0.Translated(outv * (std::min(dRef, dOther) * 0.05));

    BRepBuilderAPI_MakePolygon poly;
    poly.Add(C);
    poly.Add(A);
    poly.Add(B);
    poly.Close();
    if (!poly.IsDone()) return false;
    // The profile edge running A–B sweeps into the chamfer-plane face.
    TopoDS_Edge abEdge;
    for (TopExp_Explorer ex(poly.Wire(), TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(TopoDS::Edge(ex.Current()), v1, v2);
        gp_Pnt q1 = BRep_Tool::Pnt(v1), q2 = BRep_Tool::Pnt(v2);
        if ((q1.Distance(A) < 1e-7 && q2.Distance(B) < 1e-7) ||
            (q1.Distance(B) < 1e-7 && q2.Distance(A) < 1e-7)) {
            abEdge = TopoDS::Edge(ex.Current());
            break;
        }
    }
    if (abEdge.IsNull()) return false;
    out.solid = sweepProfile(poly.Wire(), abEdge, sweep, out.blendTemplate);
    return !out.solid.IsNull();
}

// The fillet tool: same wedge region but bounded by the arc of radius r
// tangent to both faces — apex outside the corner, straight sides to the
// tangency points A/B, arc A→B bulging toward the corner. Cutting it leaves
// exactly the convex fillet cylinder.
bool makeFilletTool(const Group& g, double r, Tool& out) {
    const EdgeInfo& e = g.rep;
    gp_Pnt P0;
    gp_Vec sweep;
    if (!groupSpan(g, P0, sweep)) return false;
    const double c = gp_Vec(e.nRef).Dot(gp_Vec(e.nOther));
    if (c <= -1.0 + 1e-9) return false; // knife edge — no wedge to round
    // Fillet centre: at distance r from BOTH planes, on the material side.
    gp_Vec w = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    if (w.Magnitude() < 1e-9) return false;
    w.Normalize();
    w.Reverse(); // inward bisector
    const double s = r * std::sqrt(2.0 / (1.0 + c));
    gp_Pnt O = P0.Translated(w * s);
    // Tangency points: feet of O on each plane.
    gp_Pnt A = O.Translated(gp_Vec(e.nRef) * r);
    gp_Pnt B = O.Translated(gp_Vec(e.nOther) * r);
    // Same honesty guard as the chamfer: the tangency lines must land ON
    // their faces (checked at the representative fragment's midpoint).
    {
        gp_Vec mid(P0, e.pm);
        if (!onFace(e.fRef, A.Translated(mid)) ||
            !onFace(e.fOther, B.Translated(mid)))
            return false;
    }
    // Arc through the point of the circle nearest the corner (it bulges
    // toward the edge — the removed region lies between arc and corner).
    gp_Vec toCorner(O, P0);
    if (toCorner.Magnitude() < 1e-12) return false;
    toCorner.Normalize();
    gp_Pnt M = O.Translated(toCorner * r);
    GC_MakeArcOfCircle arc(A, M, B);
    if (!arc.IsDone()) return false;
    gp_Vec outv = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    outv.Normalize();
    gp_Pnt C = P0.Translated(outv * (r * 0.05));

    BRepBuilderAPI_MakeEdge ca(C, A), bc(B, C), ab(arc.Value());
    if (!ca.IsDone() || !bc.IsDone() || !ab.IsDone()) return false;
    BRepBuilderAPI_MakeWire mw(ca.Edge(), ab.Edge(), bc.Edge());
    if (!mw.IsDone()) return false;
    // MakeWire may rework the edges it was fed (shared vertices, orientation)
    // so Generated() must be asked about the wire's OWN arc edge — the only
    // circular one of the three.
    TopoDS_Edge arcEdge;
    for (TopExp_Explorer ex(mw.Wire(), TopAbs_EDGE); ex.More(); ex.Next()) {
        if (BRepAdaptor_Curve(TopoDS::Edge(ex.Current())).GetType() ==
            GeomAbs_Circle) {
            arcEdge = TopoDS::Edge(ex.Current());
            break;
        }
    }
    if (arcEdge.IsNull()) return false;
    out.solid = sweepProfile(mw.Wire(), arcEdge, sweep, out.blendTemplate);
    return !out.solid.IsNull();
}

// Does `f` lie on the same support surface as the blend template? Covers the
// two surfaces the tools produce: the chamfer plane and the fillet cylinder.
bool sameSupportSurface(const TopoDS_Face& tmpl, const TopoDS_Face& f) {
    BRepAdaptor_Surface st(tmpl), sf(f);
    if (st.GetType() != sf.GetType()) return false;
    if (st.GetType() == GeomAbs_Plane)
        return planesMatch(st.Plane(), sf.Plane());
    if (st.GetType() == GeomAbs_Cylinder) {
        gp_Cylinder a = st.Cylinder(), b = sf.Cylinder();
        if (std::abs(a.Radius() - b.Radius()) > 1e-6) return false;
        if (!a.Axis().Direction().IsParallel(b.Axis().Direction(), 1e-4))
            return false;
        gp_Lin axisA(a.Axis());
        return axisA.Distance(b.Axis().Location()) < 1e-5;
    }
    return false;
}

// Subtract the tools from the body, validate, and collect the blend faces on
// the result. Captures the cut's generation maps into `ledger` on success.
bool applyCut(const TopoDS_Shape& body, const std::vector<Tool>& tools,
              topo::GenerationLedger& ledger, TopoDS_Shape& outShape,
              std::vector<TopoDS_Shape>& outBlendFaces) {
    BRep_Builder bb;
    TopoDS_Compound comp;
    bb.MakeCompound(comp);
    for (const auto& t : tools) bb.Add(comp, t.solid);

    BRepAlgoAPI_Cut cut(body, comp);
    if (!cut.IsDone()) return false;
    TopoDS_Shape res = cut.Shape();
    if (res.IsNull()) return false;

    // The cut must have actually removed material (a sign error in the tool
    // directions would miss the body entirely and silently produce a no-op
    // "blend"), and the result must be sound.
    GProp_GProps gin, gout;
    BRepGProp::VolumeProperties(body, gin);
    BRepGProp::VolumeProperties(res, gout);
    if (gout.Mass() < 1e-9 || gout.Mass() >= gin.Mass() - 1e-9) return false;
    if (!BRepCheck_Analyzer(res).IsValid()) return false;

    // Blend faces on the result: the surviving pieces of each tool's blend
    // face. Modified() is authoritative; fall back to a same-surface scan if
    // a builder doesn't report tool-face history.
    for (const auto& t : tools) {
        bool found = false;
        try {
            for (const TopoDS_Shape& m : cut.Modified(t.blendTemplate))
                if (m.ShapeType() == TopAbs_FACE) {
                    outBlendFaces.push_back(m);
                    found = true;
                }
        } catch (...) {}
        if (!found) {
            for (TopExp_Explorer ex(res, TopAbs_FACE); ex.More(); ex.Next()) {
                if (sameSupportSurface(t.blendTemplate,
                                       TopoDS::Face(ex.Current()))) {
                    outBlendFaces.push_back(ex.Current());
                    found = true;
                }
            }
        }
        if (!found) return false; // a tool left no blend — over-cut or miss
    }

    ledger.capture(cut, body, TopAbs_EDGE);
    ledger.captureAdd(cut, body, TopAbs_FACE);
    outShape = res;
    return true;
}

} // namespace

bool cutChamfer(const TopoDS_Shape& body,
                const std::vector<TopoDS_Edge>& edges,
                double dRef, double dOther,
                const TopoDS_Face& refFace,
                topo::GenerationLedger& ledger,
                TopoDS_Shape& outShape,
                std::vector<TopoDS_Shape>& outBlendFaces) {
    outShape.Nullify();
    outBlendFaces.clear();
    if (body.IsNull() || edges.empty() || dRef <= 0.0 || dOther <= 0.0)
        return false;
    try {
        const bool asymmetric = std::abs(dRef - dOther) > 1e-12;
        gp_Pln refPln;
        bool hasRefPln = false;
        if (!refFace.IsNull()) {
            BRepAdaptor_Surface rs(refFace);
            if (rs.GetType() != GeomAbs_Plane) {
                if (asymmetric) return false; // can't aim dRef reliably
            } else {
                refPln = rs.Plane();
                hasRefPln = true;
            }
        }

        std::vector<Group> groups;
        if (!buildGroups(body, edges, hasRefPln ? &refPln : nullptr,
                         0.1 * std::min(dRef, dOther), asymmetric, groups))
            return false;

        std::vector<Tool> tools;
        for (const auto& g : groups) {
            Tool t;
            if (!makeChamferTool(g, dRef, dOther, t)) return false;
            tools.push_back(t);
        }
        return applyCut(body, tools, ledger, outShape, outBlendFaces);
    } catch (...) {
        return false;
    }
}

bool cutFillet(const TopoDS_Shape& body,
               const std::vector<TopoDS_Edge>& edges, double radius,
               topo::GenerationLedger& ledger, TopoDS_Shape& outShape,
               std::vector<TopoDS_Shape>& outBlendFaces) {
    outShape.Nullify();
    outBlendFaces.clear();
    if (body.IsNull() || edges.empty() || radius <= 0.0) return false;
    try {
        std::vector<Group> groups;
        if (!buildGroups(body, edges, nullptr, 0.1 * radius, false, groups))
            return false;

        std::vector<Tool> tools;
        for (const auto& g : groups) {
            Tool t;
            if (!makeFilletTool(g, radius, t)) return false;
            tools.push_back(t);
        }
        return applyCut(body, tools, ledger, outShape, outBlendFaces);
    } catch (...) {
        return false;
    }
}

} // namespace blendcut
} // namespace materializr
