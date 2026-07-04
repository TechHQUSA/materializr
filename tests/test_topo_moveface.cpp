// Tail: MoveFaceOp target face via topo::Ref. Move a wall outward, then a
// dimension edit MOVES that wall; on replay the op re-resolves the face by its
// sketch-anchored name and pushes the RIGHT wall again — instead of using the
// stale handle (old body's face) and failing / moving nothing.

#include "modeling/TopoName.h"
#include "modeling/MoveFaceOp.h"
#include "core/Document.h"
#include "modeling/Sketch.h"
#include "modeling/ExtrudeOp.h"

#include <gtest/gtest.h>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <memory>

using materializr::Sketch;
using namespace materializr;

namespace {

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

TopoDS_Face topCap(const TopoDS_Shape& body) {
    TopoDS_Face best; double bestZ = -1e18;
    for (TopExp_Explorer ex(body, TopAbs_FACE); ex.More(); ex.Next()) {
        TopoDS_Face f = TopoDS::Face(ex.Current());
        GProp_GProps g; BRepGProp::SurfaceProperties(f, g);
        if (g.CentreOfMass().Z() > bestZ) { bestZ = g.CentreOfMass().Z(); best = f; }
    }
    return best;
}

double maxZ(const TopoDS_Shape& s) {
    Bnd_Box b; BRepBndLib::Add(s, b);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    b.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return zmax;
}
double maxX(const TopoDS_Shape& s) {
    Bnd_Box b; BRepBndLib::Add(s, b);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    b.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return xmax;
}

} // namespace

TEST(TopoMoveFace, MovedFaceTargetFollowsResize) {
    Document doc;
    int pid[4];
    auto sk = makeRect(20.0, 10.0, pid);
    int sid = doc.addSketch(sk);
    ExtrudeOp ext; ext.setSketchSource(sid); ext.setDistance(10.0);
    ASSERT_TRUE(ext.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(ext.execute(doc));
    int body = doc.getAllBodyIds().front();

    // SLIDE the TOP cap +X by 3 (Translate is an in-plane slide) -> the top
    // shifts to x 3..23, so the body's max X becomes 23.
    MoveFaceOp mv;
    mv.setBody(body);
    mv.setFace(topCap(doc.getBody(body)));
    mv.setKind(MoveFaceOp::Kind::Translate);
    mv.setMoveVector(gp_Vec(3, 0, 0));
    ASSERT_TRUE(mv.execute(doc));
    EXPECT_NEAR(maxX(doc.getBody(body)), 23.0, 1e-6) << "top slid to x=23";

    // WIDEN the sketch 20 -> 40: the right wall relocates to x=40. Re-derive
    // the base, then replay the move-face.
    sk->movePoint(pid[1], {40.0f, 0.0f});
    sk->movePoint(pid[2], {40.0f, 10.0f});
    ASSERT_TRUE(ext.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(ext.execute(doc));
    ASSERT_NEAR(maxX(doc.getBody(body)), 40.0, 1e-6) << "base widened to x=40";

    // The stale m_face (old body's cap) must be re-resolved on the rebuilt body;
    // sliding the re-found cap +3 puts the top at x 3..43 -> max X 43. A stale
    // handle would fail / no-op (leaving max X at 40).
    ASSERT_TRUE(mv.execute(doc)) << "move-face must re-resolve the cap after rebuild";
    EXPECT_NEAR(maxX(doc.getBody(body)), 43.0, 1e-6)
        << "re-found top cap slid to x=43 on the widened body";
}
