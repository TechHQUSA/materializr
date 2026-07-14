// #55: a chamfer applied to an edge that a surface feature (drilled hole)
// already crosses. OCCT's native blend fails there; the swept-wedge cut
// fallback must produce EXACTLY the geometry of "chamfer first, feature
// after" — the reorder the user would otherwise rebuild by hand. Also checks
// the fallback refuses what it can't honestly build (concave edges) and that
// the native path still handles a plain box untouched.
#include <gtest/gtest.h>

#include "core/Document.h"
#include "modeling/BlendCut.h"
#include "modeling/ChamferOp.h"
#include "modeling/GenerationLedger.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <vector>

using namespace materializr;

namespace {

double volumeOf(const TopoDS_Shape& s) {
    GProp_GProps g;
    BRepGProp::VolumeProperties(s, g);
    return g.Mass();
}

// Box 40x20x10; the "front top" edge runs along X at y=0, z=10.
TopoDS_Shape plainBox() { return BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape(); }

// Vertical 3mm-radius hole centred ON the front top edge at x=20 — bites a
// half-cylinder channel through it, fragmenting the edge in two.
TopoDS_Shape edgeCrossingHole() {
    gp_Ax2 ax(gp_Pnt(20.0, 0.0, -1.0), gp_Dir(0.0, 0.0, 1.0));
    return BRepPrimAPI_MakeCylinder(ax, 3.0, 12.0).Shape();
}

// Shallow rectangular pocket crossing the edge, floor at z=9 — INSIDE the
// 2mm bevel depth. This is the config where the native blend genuinely
// fails on OCCT 7.9.3 (probe_chamfer_fail), so it exercises the fallback
// end-to-end through ChamferOp.
TopoDS_Shape shallowPocketTool() {
    return BRepPrimAPI_MakeBox(gp_Pnt(17.0, -1.0, 9.0),
                               gp_Pnt(23.0, 4.0, 11.0)).Shape();
}

// All straight edges whose vertices sit at y≈0, z≈10 (the front-top edge or
// its fragments after the hole).
std::vector<TopoDS_Edge> frontTopEdges(const TopoDS_Shape& s) {
    std::vector<TopoDS_Edge> out;
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
        if (BRepAdaptor_Curve(e).GetType() != GeomAbs_Line) continue;
        bool ok = true;
        int nv = 0;
        for (TopExp_Explorer vx(e, TopAbs_VERTEX); vx.More(); vx.Next(), ++nv) {
            gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vx.Current()));
            if (std::abs(p.Y()) > 1e-7 || std::abs(p.Z() - 10.0) > 1e-7)
                ok = false;
        }
        if (ok && nv == 2) {
            bool dup = false;
            for (const auto& u : out)
                if (u.IsSame(e)) dup = true;
            if (!dup) out.push_back(e);
        }
    }
    return out;
}

// The planar face lying on z = 10 (the top).
TopoDS_Face topFace(const TopoDS_Shape& s) {
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& f = TopoDS::Face(ex.Current());
        BRepAdaptor_Surface surf(f);
        if (surf.GetType() != GeomAbs_Plane) continue;
        gp_Pln p = surf.Plane();
        if (p.Axis().Direction().IsParallel(gp_Dir(0, 0, 1), 1e-6) &&
            std::abs(p.Distance(gp_Pnt(0, 0, 10))) < 1e-7)
            return f;
    }
    return TopoDS_Face();
}

// The geometry the user actually wants: native chamfer on the PRISTINE box's
// edge (measured along the top face), then the feature cut through it.
double chamferThenCutVolume(const TopoDS_Shape& tool, double dTop,
                            double dOther) {
    TopoDS_Shape box = plainBox();
    std::vector<TopoDS_Edge> es = frontTopEdges(box);
    if (es.size() != 1) return -1.0;
    TopoDS_Face top = topFace(box);
    if (top.IsNull()) return -1.0;
    BRepFilletAPI_MakeChamfer mk(box);
    mk.Add(dTop, dOther, es.front(), top);
    mk.Build();
    if (!mk.IsDone()) return -1.0;
    return volumeOf(BRepAlgoAPI_Cut(mk.Shape(), tool).Shape());
}

} // namespace

// Native path untouched: a plain box chamfer must still build (and NOT via
// the fallback — the bevel of a native chamfer is reported by the builder,
// but the simplest regression-proof is that it succeeds and stays valid).
TEST(BlendCut, NativePathStillWorksOnPlainBox) {
    Document doc;
    int id = doc.addBody(plainBox(), "Box");
    std::vector<TopoDS_Edge> es = frontTopEdges(doc.getBody(id));
    ASSERT_EQ(es.size(), 1u);
    ChamferOp op;
    op.setBody(id);
    op.setEdges(es);
    op.setDistance(2.0);
    ASSERT_TRUE(op.execute(doc));
    const TopoDS_Shape& out = doc.getBody(id);
    EXPECT_TRUE(BRepCheck_Analyzer(out).IsValid());
    EXPECT_LT(volumeOf(out), volumeOf(plainBox()));
    EXPECT_FALSE(op.getGeneratedFaces().empty());
}

// The core contract: hole first, then cutChamfer over the two fragments =
// chamfer first, then hole. Same removal set, so the volumes are EQUAL.
TEST(BlendCut, SymmetricChamferAcrossHoleMatchesReorder) {
    TopoDS_Shape holed = BRepAlgoAPI_Cut(plainBox(), edgeCrossingHole()).Shape();
    std::vector<TopoDS_Edge> frags = frontTopEdges(holed);
    ASSERT_EQ(frags.size(), 2u) << "hole should fragment the edge in two";

    topo::GenerationLedger ledger;
    TopoDS_Shape out;
    std::vector<TopoDS_Shape> blends;
    ASSERT_TRUE(blendcut::cutChamfer(holed, frags, 2.0, 2.0, TopoDS_Face(),
                                     ledger, out, blends));
    EXPECT_TRUE(BRepCheck_Analyzer(out).IsValid());
    EXPECT_FALSE(blends.empty());

    double ref = chamferThenCutVolume(edgeCrossingHole(), 2.0, 2.0);
    ASSERT_GT(ref, 0.0);
    EXPECT_NEAR(volumeOf(out), ref, 1e-4);
}

// Same but asymmetric, with the setbacks aimed via the top face — including
// across the hole, where the reference face is the HOLED top (an inner wire,
// same plane).
TEST(BlendCut, AsymmetricChamferAcrossHoleMatchesReorder) {
    TopoDS_Shape holed = BRepAlgoAPI_Cut(plainBox(), edgeCrossingHole()).Shape();
    std::vector<TopoDS_Edge> frags = frontTopEdges(holed);
    ASSERT_EQ(frags.size(), 2u);
    TopoDS_Face top = topFace(holed);
    ASSERT_FALSE(top.IsNull());

    topo::GenerationLedger ledger;
    TopoDS_Shape out;
    std::vector<TopoDS_Shape> blends;
    ASSERT_TRUE(blendcut::cutChamfer(holed, frags, 3.0, 1.5, top,
                                     ledger, out, blends));
    EXPECT_TRUE(BRepCheck_Analyzer(out).IsValid());

    double ref = chamferThenCutVolume(edgeCrossingHole(), 3.0, 1.5);
    ASSERT_GT(ref, 0.0);
    EXPECT_NEAR(volumeOf(out), ref, 1e-4);
}

// Through the real op, on a config where the NATIVE build genuinely fails
// (probe_chamfer_fail: shallow pocket whose floor sits inside the bevel
// depth): ChamferOp must come back SUCCESSFUL with the reorder-equivalent
// geometry — the fallback wiring end-to-end: ledger, generated faces,
// lineage ids.
TEST(BlendCut, ChamferOpFallsBackAcrossShallowPocket) {
    Document doc;
    int id = doc.addBody(
        BRepAlgoAPI_Cut(plainBox(), shallowPocketTool()).Shape(), "Pocketed");
    std::vector<TopoDS_Edge> frags = frontTopEdges(doc.getBody(id));
    ASSERT_EQ(frags.size(), 2u);

    ChamferOp op;
    op.setBody(id);
    op.setEdges(frags);
    op.setDistance(2.0);
    ASSERT_TRUE(op.execute(doc));

    const TopoDS_Shape& out = doc.getBody(id);
    EXPECT_TRUE(BRepCheck_Analyzer(out).IsValid());
    double ref = chamferThenCutVolume(shallowPocketTool(), 2.0, 2.0);
    ASSERT_GT(ref, 0.0);
    EXPECT_NEAR(volumeOf(out), ref, 1e-4);

    // Click-to-edit machinery: bevel faces claimed, lineage ids minted.
    EXPECT_FALSE(op.getGeneratedFaces().empty());
    EXPECT_NE(op.serializeParams().find("genids="), std::string::npos);
    EXPECT_NE(doc.bodyFaceIds(id), nullptr);
}

// The other native-failing class: chamfer distance LARGER than the corner
// clearance to a through-hole (d=3.5 vs r=3 hole on the edge).
TEST(BlendCut, ChamferOpFallsBackWhenBevelExceedsClearance) {
    Document doc;
    int id = doc.addBody(
        BRepAlgoAPI_Cut(plainBox(), edgeCrossingHole()).Shape(), "Holed");
    std::vector<TopoDS_Edge> frags = frontTopEdges(doc.getBody(id));
    ASSERT_EQ(frags.size(), 2u);

    ChamferOp op;
    op.setBody(id);
    op.setEdges(frags);
    op.setDistance(3.5);
    ASSERT_TRUE(op.execute(doc));

    const TopoDS_Shape& out = doc.getBody(id);
    EXPECT_TRUE(BRepCheck_Analyzer(out).IsValid());
    double ref = chamferThenCutVolume(edgeCrossingHole(), 3.5, 3.5);
    ASSERT_GT(ref, 0.0);
    EXPECT_NEAR(volumeOf(out), ref, 1e-4);
}

// A cut can only REMOVE material, so a concave (inside-corner) edge must be
// refused — silently "chamfering" it with a cut would dig a groove instead
// of adding the bevel sliver.
TEST(BlendCut, ConcaveEdgeRefused) {
    TopoDS_Shape lower = BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape();
    TopoDS_Shape upper = BRepPrimAPI_MakeBox(
        gp_Pnt(0.0, 0.0, 10.0), gp_Pnt(40.0, 10.0, 20.0)).Shape();
    TopoDS_Shape lShape = BRepAlgoAPI_Fuse(lower, upper).Shape();

    // The concave junction runs along X at y=10, z=10.
    std::vector<TopoDS_Edge> concave;
    for (TopExp_Explorer ex(lShape, TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
        if (BRepAdaptor_Curve(e).GetType() != GeomAbs_Line) continue;
        bool ok = true;
        for (TopExp_Explorer vx(e, TopAbs_VERTEX); vx.More(); vx.Next()) {
            gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vx.Current()));
            if (std::abs(p.Y() - 10.0) > 1e-7 || std::abs(p.Z() - 10.0) > 1e-7)
                ok = false;
        }
        if (ok) { concave.push_back(e); break; }
    }
    ASSERT_FALSE(concave.empty());

    topo::GenerationLedger ledger;
    TopoDS_Shape out;
    std::vector<TopoDS_Shape> blends;
    EXPECT_FALSE(blendcut::cutChamfer(lShape, concave, 2.0, 2.0,
                                      TopoDS_Face(), ledger, out, blends));
}
