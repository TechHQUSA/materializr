// Section View clips the mesh in the fragment shader; without a cap a solid
// reads as a hollow shell. computeSectionCap() slices the body's triangulation
// (the mesh the viewport draws) and fills the loops as a planar face. These
// tests mesh every shape the way the renderer does at Medium quality first.
#include "viewport/SectionCap.h"
#include "core/MeshParams.h"

#include <gtest/gtest.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <vector>

using materializr::computeSectionCap;

namespace {

void meshLikeRenderer(const TopoDS_Shape& s) {
    BRepMesh_IncrementalMesh m(s, materializr::meshParams(0.1, 0.3, true));
}

double capArea(const std::vector<float>& p) {
    double area = 0.0;
    for (size_t i = 0; i + 9 <= p.size(); i += 9) {
        const double ux = p[i + 3] - p[i], uy = p[i + 4] - p[i + 1], uz = p[i + 5] - p[i + 2];
        const double vx = p[i + 6] - p[i], vy = p[i + 7] - p[i + 1], vz = p[i + 8] - p[i + 2];
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        area += 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);
    }
    return area;
}

// Every cap vertex lies on the plane z = height.
void expectOnPlane(const std::vector<float>& pos, float height) {
    for (size_t i = 0; i + 2 < pos.size(); i += 3)
        EXPECT_NEAR(pos[i + 2], height, 1e-3f);
}

TopoDS_Shape boredBox() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    gp_Ax2 axis(gp_Pnt(10, 10, 0), gp_Dir(0, 0, 1));
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(axis, 5.0, 20.0).Shape();
    return BRepAlgoAPI_Cut(box, bore).Shape();
}

} // namespace

TEST(SectionCap, SolidBoxCapEqualsCrossSection) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    meshLikeRenderer(box);
    std::vector<float> pos;
    ASSERT_TRUE(computeSectionCap(box, gp_Pln(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1)), pos));
    ASSERT_FALSE(pos.empty());
    expectOnPlane(pos, 10.0f);
    EXPECT_NEAR(capArea(pos), 400.0, 1e-6);
}

TEST(SectionCap, HollowBoxCapIsAnnulus) {
    TopoDS_Shape hollow = boredBox();
    meshLikeRenderer(hollow);
    std::vector<float> pos;
    ASSERT_TRUE(computeSectionCap(hollow, gp_Pln(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1)), pos));
    // The bore is a polygon at the mesh's resolution (about 21 sides at
    // 0.1 mm / 0.3 rad), so the hole comes out ~1.2 mm^2 smaller than the circle.
    EXPECT_NEAR(capArea(pos), 400.0 - M_PI * 25.0, 2.0);
}

TEST(SectionCap, IslandInsideAHoleIsFilled) {
    // A pin standing inside the bore: outer square, hole, and a material loop
    // at nesting depth two. Area = square - bore + pin.
    TopoDS_Shape hollow = boredBox();
    gp_Ax2 axis(gp_Pnt(10, 10, 0), gp_Dir(0, 0, 1));
    TopoDS_Shape pin = BRepPrimAPI_MakeCylinder(axis, 2.0, 20.0).Shape();
    TopoDS_Shape shape = BRepAlgoAPI_Fuse(hollow, pin).Shape();
    meshLikeRenderer(shape);
    std::vector<float> pos;
    ASSERT_TRUE(computeSectionCap(shape, gp_Pln(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1)), pos));
    EXPECT_NEAR(capArea(pos), 400.0 - M_PI * 25.0 + M_PI * 4.0, 2.0);
}

TEST(SectionCap, RowOfHolesCutThroughTheirCentresIsManyRegions) {
    // The plane through a row of hole centres leaves 21 separate rectangles.
    TopoDS_Shape plate = BRepPrimAPI_MakeBox(300.0, 40.0, 10.0).Shape();
    TopoDS_Compound holes;
    BRep_Builder bb;
    bb.MakeCompound(holes);
    for (int i = 0; i < 20; ++i)
        bb.Add(holes, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(7.5 + i * 15.0, 20.0, -1.0), gp_Dir(0, 0, 1)), 3.0, 12.0).Shape());
    TopoDS_Shape shape = BRepAlgoAPI_Cut(plate, holes).Shape();
    meshLikeRenderer(shape);
    std::vector<float> pos;
    ASSERT_TRUE(computeSectionCap(shape, gp_Pln(gp_Pnt(0, 20, 0), gp_Dir(0, 1, 0)), pos));
    // Each hole edge is a polygon of the mesh; where the plane crosses a chord
    // rather than a node the hole reads up to R(1-cos(pi/N)) narrower, so the
    // area lands a little above the exact 1800 (about 3 mm^2 at Medium).
    EXPECT_NEAR(capArea(pos), 300.0 * 10.0 - 20 * 6.0 * 10.0, 20.0);
}

TEST(SectionCap, MovedBodyIsCutWhereItStands) {
    // Same TShape carrying a Location: the slice must use the placed nodes.
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    meshLikeRenderer(box);
    gp_Trsf t;
    t.SetTranslation(gp_Vec(100.0, 0.0, 50.0));
    TopoDS_Shape moved = box.Moved(TopLoc_Location(t));
    std::vector<float> pos;
    ASSERT_TRUE(computeSectionCap(moved, gp_Pln(gp_Pnt(0, 0, 60), gp_Dir(0, 0, 1)), pos));
    expectOnPlane(pos, 60.0f);
    EXPECT_NEAR(capArea(pos), 400.0, 1e-6);
    for (size_t i = 0; i < pos.size(); i += 3) EXPECT_GE(pos[i], 99.9f);
    EXPECT_FALSE(computeSectionCap(moved, gp_Pln(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1)), pos));
}

TEST(SectionCap, UnmeshedShapeGivesNoCap) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    std::vector<float> pos;
    EXPECT_FALSE(computeSectionCap(box, gp_Pln(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1)), pos));
    EXPECT_TRUE(pos.empty());
}

TEST(SectionCap, NoIntersectionNoCap) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    meshLikeRenderer(box);
    std::vector<float> pos;
    EXPECT_FALSE(computeSectionCap(box, gp_Pln(gp_Pnt(0, 0, 30), gp_Dir(0, 0, 1)), pos));
    EXPECT_TRUE(pos.empty());
}

TEST(SectionCap, TangentPlaneNoCap) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    meshLikeRenderer(box);
    std::vector<float> pos;
    EXPECT_FALSE(computeSectionCap(box, gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), pos));
    EXPECT_TRUE(pos.empty());
    EXPECT_FALSE(computeSectionCap(box, gp_Pln(gp_Pnt(0, 0, 20), gp_Dir(0, 0, 1)), pos));
    EXPECT_TRUE(pos.empty());
}
