#include "BlendCut.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
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

// A run of collinear fragments sharing one plane pair: swept as ONE wedge
// over the union of their spans, which is what carries the bevel across the
// gap a hole or pocket bit out of the edge.
struct Group {
    EdgeInfo rep;      // representative — dirs/normals valid for all members
    double tmin, tmax; // span along rep.line covered by every member
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

// Of the two in-plane directions perpendicular to the edge, the one whose
// probe point lands INSIDE the face — i.e. pointing into the face's material.
bool inFaceDir(const TopoDS_Face& f, const gp_Dir& n, const gp_Pnt& pm,
               const gp_Dir& t, double probeEps, gp_Dir& out) {
    gp_Dir cand = n.Crossed(t);
    for (double eps : {probeEps, probeEps * 0.1}) {
        for (int flip = 0; flip < 2; ++flip) {
            gp_Dir d = flip ? cand.Reversed() : cand;
            gp_Pnt probe = pm.Translated(gp_Vec(d) * eps);
            BRepClass_FaceClassifier cls(f, probe, 1e-6);
            if (cls.State() == TopAbs_IN || cls.State() == TopAbs_ON) {
                out = d;
                return true;
            }
        }
    }
    return false;
}

bool planesMatch(const gp_Pln& a, const gp_Pln& b) {
    if (!a.Axis().Direction().IsParallel(b.Axis().Direction(), 1e-4))
        return false;
    return a.Distance(b.Location()) < 1e-5;
}

bool analyzeEdge(const TopoDS_Edge& e,
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
    if (!inFaceDir(f1, out.nRef, out.pm, t, probeEps, out.dRefDir) ||
        !inFaceDir(f2, out.nOther, out.pm, t, probeEps, out.dOtherDir))
        return false;
    // Convex only: walking along either face away from the edge must go to
    // the material side of the other face's plane. Near-tangent (almost
    // flat) edges are refused too — the wedge degenerates there.
    if (out.dRefDir.Dot(out.nOther) > -0.05) return false;
    if (out.dOtherDir.Dot(out.nRef) > -0.05) return false;
    return true;
}

double paramOn(const gp_Lin& l, const gp_Pnt& p) {
    return gp_Vec(l.Location(), p).Dot(gp_Vec(l.Direction()));
}

// The wedge for one group: triangle (apex just outside the corner, the two
// setback points A/B exactly where the chamfer plane meets the faces) swept
// along the merged span. Also reports the wedge's A–B face — the exact
// chamfer plane — so the caller can find the bevel on the cut result.
TopoDS_Shape makeWedge(const Group& g, double dRef, double dOther,
                       TopoDS_Face& outBlendTemplate) {
    const EdgeInfo& e = g.rep;
    const double len = g.tmax - g.tmin;
    if (len < 1e-6) return TopoDS_Shape();
    gp_Pnt P0 = g.rep.line.Location().Translated(
        gp_Vec(g.rep.line.Direction()) * g.tmin);
    gp_Pnt A = P0.Translated(gp_Vec(e.dRefDir) * dRef);
    gp_Pnt B = P0.Translated(gp_Vec(e.dOtherDir) * dOther);
    gp_Vec outv = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    if (outv.Magnitude() < 1e-9) return TopoDS_Shape();
    outv.Normalize();
    // Apex slightly OUTSIDE the corner so the wedge's side faces cross the
    // body's faces transversally instead of lying exactly on them (tangent
    // boolean inputs are where cuts get fragile). Immediately outside a
    // convex corner is empty space, so a small bulge cannot over-cut.
    const double bulge = std::min(dRef, dOther) * 0.05;
    gp_Pnt C = P0.Translated(outv * bulge);

    BRepBuilderAPI_MakePolygon poly;
    poly.Add(C);
    poly.Add(A);
    poly.Add(B);
    poly.Close();
    if (!poly.IsDone()) return TopoDS_Shape();
    TopoDS_Wire w = poly.Wire();
    BRepBuilderAPI_MakeFace mf(w, Standard_True);
    if (!mf.IsDone()) return TopoDS_Shape();
    BRepPrimAPI_MakePrism prism(mf.Face(),
                                gp_Vec(g.rep.line.Direction()) * len);
    if (!prism.IsDone()) return TopoDS_Shape();

    // The profile edge running A–B sweeps into the chamfer-plane face.
    for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(TopoDS::Edge(ex.Current()), v1, v2);
        gp_Pnt q1 = BRep_Tool::Pnt(v1), q2 = BRep_Tool::Pnt(v2);
        if ((q1.Distance(A) < 1e-7 && q2.Distance(B) < 1e-7) ||
            (q1.Distance(B) < 1e-7 && q2.Distance(A) < 1e-7)) {
            const TopTools_ListOfShape& gen =
                prism.Generated(ex.Current());
            if (!gen.IsEmpty() && gen.First().ShapeType() == TopAbs_FACE)
                outBlendTemplate = TopoDS::Face(gen.First());
            break;
        }
    }
    if (outBlendTemplate.IsNull()) return TopoDS_Shape();
    return prism.Shape();
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
        TopTools_IndexedDataMapOfShapeListOfShape efm;
        TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, efm);
        const double probeEps = 0.1 * std::min(dRef, dOther);
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

        // All-or-nothing: one unsupported edge refuses the whole fallback so
        // a multi-edge chamfer never comes back half-built.
        std::vector<EdgeInfo> infos;
        for (const auto& e : edges) {
            EdgeInfo info;
            if (!analyzeEdge(e, efm, hasRefPln ? &refPln : nullptr, probeEps,
                             info))
                return false;
            infos.push_back(info);
        }

        // Merge collinear fragments on the same plane pair into one span.
        std::vector<Group> groups;
        for (const auto& info : infos) {
            Group* home = nullptr;
            for (auto& g : groups) {
                if (!g.rep.line.Direction().IsParallel(info.line.Direction(),
                                                       1e-4))
                    continue;
                if (g.rep.line.Distance(info.pm) > 1e-5) continue;
                const bool straight =
                    planesMatch(g.rep.plnRef, info.plnRef) &&
                    planesMatch(g.rep.plnOther, info.plnOther);
                const bool crossed =
                    planesMatch(g.rep.plnRef, info.plnOther) &&
                    planesMatch(g.rep.plnOther, info.plnRef);
                if (!straight && !crossed) continue;
                // Crossed = the per-edge "first face" ordering flipped between
                // fragments. Harmless when both setbacks are equal; genuinely
                // ambiguous for an asymmetric chamfer with no shared
                // reference face, so refuse rather than guess.
                if (crossed && !straight && asymmetric && !hasRefPln)
                    return false;
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

        BRep_Builder bb;
        TopoDS_Compound tools;
        bb.MakeCompound(tools);
        std::vector<TopoDS_Face> blendTemplates;
        for (const auto& g : groups) {
            TopoDS_Face tmpl;
            TopoDS_Shape wedge = makeWedge(g, dRef, dOther, tmpl);
            if (wedge.IsNull()) return false;
            bb.Add(tools, wedge);
            blendTemplates.push_back(tmpl);
        }

        BRepAlgoAPI_Cut cut(body, tools);
        if (!cut.IsDone()) return false;
        TopoDS_Shape res = cut.Shape();
        if (res.IsNull()) return false;

        // The cut must have actually removed material (a sign error in the
        // wedge directions would miss the body entirely and silently produce
        // a no-op "chamfer"), and the result must be sound.
        GProp_GProps gin, gout;
        BRepGProp::VolumeProperties(body, gin);
        BRepGProp::VolumeProperties(res, gout);
        if (gout.Mass() < 1e-9 || gout.Mass() >= gin.Mass() - 1e-9)
            return false;
        if (!BRepCheck_Analyzer(res).IsValid()) return false;

        // Bevel faces on the result: the surviving pieces of each wedge's
        // chamfer-plane face. Modified() is authoritative; fall back to a
        // same-plane scan if a builder doesn't report tool-face history.
        for (const auto& tmpl : blendTemplates) {
            bool found = false;
            try {
                for (const TopoDS_Shape& m : cut.Modified(tmpl))
                    if (m.ShapeType() == TopAbs_FACE) {
                        outBlendFaces.push_back(m);
                        found = true;
                    }
            } catch (...) {}
            if (!found) {
                gp_Pln tp = BRepAdaptor_Surface(tmpl).Plane();
                for (TopExp_Explorer ex(res, TopAbs_FACE); ex.More();
                     ex.Next()) {
                    BRepAdaptor_Surface s(TopoDS::Face(ex.Current()));
                    if (s.GetType() != GeomAbs_Plane) continue;
                    if (planesMatch(tp, s.Plane())) {
                        outBlendFaces.push_back(ex.Current());
                        found = true;
                    }
                }
            }
            if (!found) return false; // a wedge left no bevel — over-cut or miss
        }

        ledger.capture(cut, body, TopAbs_EDGE);
        ledger.captureAdd(cut, body, TopAbs_FACE);
        outShape = res;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace blendcut
} // namespace materializr
