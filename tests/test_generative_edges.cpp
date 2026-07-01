// experiment/generative-edges — Phase 1: a filleted CORNER edge is anchored to
// the sketch VERTEX it sits over, so it survives a sketch DIMENSION edit that
// relocates the corner (where ordinal/carrier matching fails).
//
// The decisive test: build a box from a rectangle sketch, fillet one vertical
// corner, then WIDEN the sketch. With generative anchoring the fillet re-binds
// to the moved corner and re-executes; without it (control), it fails — which
// is exactly today's "a downstream fillet couldn't follow it" behaviour.

#include "core/Document.h"
#include "modeling/Sketch.h"
#include "modeling/ExtrudeOp.h"
#include "modeling/FilletOp.h"

#include <gtest/gtest.h>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax3.hxx>
#include <cmath>
#include <memory>

using materializr::Sketch;

namespace {

// Rectangle sketch on the XY plane with corners (0,0)-(w,h). Returns the sketch
// and its 4 corner point ids (CCW from origin).
std::shared_ptr<Sketch> makeRect(double w, double h, int pid[4]) {
    auto sk = std::make_shared<Sketch>();
    sk->setPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))));
    pid[0] = sk->addPoint({0.0f, 0.0f});
    pid[1] = sk->addPoint({(float)w, 0.0f});
    pid[2] = sk->addPoint({(float)w, (float)h});
    pid[3] = sk->addPoint({0.0f, (float)h});
    sk->addLine(pid[0], pid[1]);
    sk->addLine(pid[1], pid[2]);
    sk->addLine(pid[2], pid[3]);
    sk->addLine(pid[3], pid[0]);
    return sk;
}

// The vertical (Z-parallel) edge of `body` sitting over sketch-plane (x,y).
TopoDS_Edge verticalEdgeAt(const TopoDS_Shape& body, double x, double y) {
    for (TopExp_Explorer ex(body, TopAbs_EDGE); ex.More(); ex.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
        if (c.GetType() != GeomAbs_Line) continue;
        if (std::abs(c.Line().Direction().Dot(gp_Dir(0, 0, 1))) < 0.999) continue;
        gp_Pnt m = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
        if (std::hypot(m.X() - x, m.Y() - y) < 1e-6)
            return TopoDS::Edge(ex.Current());
    }
    return {};
}

int onlyBodyId(Document& d) { return d.getAllBodyIds().front(); }

double bboxWidthX(Document& d, int id) {
    Bnd_Box b; BRepBndLib::Add(d.getBody(id), b);
    Standard_Real x0, y0, z0, x1, y1, z1; b.Get(x0, y0, z0, x1, y1, z1);
    return x1 - x0;
}

} // namespace

// THE headline test: fillet follows a sketch-dimension edit via its anchor.
TEST(GenerativeEdges, FilletFollowsSketchResize) {
    Document doc;
    int pid[4];
    auto sk = makeRect(20.0, 10.0, pid);
    int sid = doc.addSketch(sk);

    ExtrudeOp ext;
    ext.setSketchSource(sid);
    ext.setDistance(10.0);
    ASSERT_TRUE(ext.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(ext.execute(doc));
    int body = onlyBodyId(doc);

    // Fillet the corner over point pid[2] = (20,10), anchored to the sketch.
    TopoDS_Edge corner = verticalEdgeAt(doc.getBody(body), 20.0, 10.0);
    ASSERT_FALSE(corner.IsNull());
    FilletOp f;
    f.setBody(body);
    f.setEdges({corner});
    f.setRadius(2.0);
    f.setSourceSketch(sid);
    ASSERT_TRUE(f.execute(doc));
    const double vFilleted20 = [&]{ GProp_GProps g; BRepGProp::VolumeProperties(doc.getBody(body), g); return g.Mass(); }();

    // WIDEN the rectangle: move the two right-hand corners x: 20 -> 40.
    sk->movePoint(pid[1], {40.0f, 0.0f});
    sk->movePoint(pid[2], {40.0f, 10.0f});

    // Re-derive the base (as the cascade does: extrude replays first) ...
    ASSERT_TRUE(ext.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(ext.execute(doc));
    EXPECT_NEAR(bboxWidthX(doc, body), 40.0, 1e-6) << "base should have widened to 40";

    // ... then the fillet must FOLLOW the moved corner to (40,10).
    ASSERT_TRUE(f.execute(doc))
        << "generative anchor should re-find the corner after the resize";

    // Sanity: still a valid filleted solid, wider than before, fillet intact.
    GProp_GProps g; BRepGProp::VolumeProperties(doc.getBody(body), g);
    EXPECT_GT(g.Mass(), vFilleted20) << "wider body must have more volume";
    EXPECT_NEAR(bboxWidthX(doc, body), 40.0, 1e-6);
}

// Control: WITHOUT an anchor (no setSourceSketch), the same resize+re-execute
// fails — proving it's the anchoring, not something else, doing the work.
TEST(GenerativeEdges, WithoutAnchorResizeStillFails) {
    Document doc;
    int pid[4];
    auto sk = makeRect(20.0, 10.0, pid);
    int sid = doc.addSketch(sk);

    ExtrudeOp ext;
    ext.setSketchSource(sid);
    ext.setDistance(10.0);
    ASSERT_TRUE(ext.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(ext.execute(doc));
    int body = onlyBodyId(doc);

    TopoDS_Edge corner = verticalEdgeAt(doc.getBody(body), 20.0, 10.0);
    ASSERT_FALSE(corner.IsNull());
    FilletOp f;
    f.setBody(body);
    f.setEdges({corner});
    f.setRadius(2.0);
    // NB: no setSourceSketch — this is the current (unanchored) behaviour.
    ASSERT_TRUE(f.execute(doc));

    sk->movePoint(pid[1], {40.0f, 0.0f});
    sk->movePoint(pid[2], {40.0f, 10.0f});
    ASSERT_TRUE(ext.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(ext.execute(doc));

    EXPECT_FALSE(f.execute(doc))
        << "without an anchor the fillet cannot follow the resized corner "
           "(this is the limitation the feature fixes)";
}

// The anchor round-trips through serialize/deserialize.
TEST(GenerativeEdges, AnchorSerializesAndParses) {
    Document doc;
    int pid[4];
    auto sk = makeRect(20.0, 10.0, pid);
    int sid = doc.addSketch(sk);
    ExtrudeOp ext; ext.setSketchSource(sid); ext.setDistance(10.0);
    ASSERT_TRUE(ext.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(ext.execute(doc));
    int body = onlyBodyId(doc);

    FilletOp f;
    f.setBody(body);
    f.setEdges({verticalEdgeAt(doc.getBody(body), 20.0, 10.0)});
    f.setRadius(2.0);
    f.setSourceSketch(sid);
    ASSERT_TRUE(f.execute(doc));

    const std::string blob = f.serializeParams();
    EXPECT_NE(blob.find("anchor="), std::string::npos);

    FilletOp f2;
    ASSERT_TRUE(f2.deserializeParams(blob));
    EXPECT_EQ(f2.getSourceSketch(), sid);
}
