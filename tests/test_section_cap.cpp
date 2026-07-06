// Section-view cap generation (fix for materializr-cad/materializr#15).
// A section-clipped solid must show a filled cross-section cap, not a hollow
// shell. computeSectionCap() builds the cap triangles from the body/plane
// intersection; here we assert the cap area matches the true cross-section.

#include "viewport/SectionCap.h"

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax2.hxx>

#include <cmath>
#include <vector>

using materializr::computeSectionCap;

// Sum triangle areas from an (x,y,z per vertex, TRIANGLES) buffer.
static double capArea(const std::vector<float>& p) {
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

// Solid 20mm cube cut at z=10 -> cap is the full 20x20 = 400 mm^2 face.
TEST(SectionCap, SolidBoxCapEqualsCrossSection) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    gp_Pln plane(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1));

    std::vector<float> pos;
    ASSERT_TRUE(computeSectionCap(box, plane, pos));
    ASSERT_FALSE(pos.empty());

    for (size_t i = 0; i + 2 < pos.size(); i += 3)
        EXPECT_NEAR(pos[i + 2], 10.0f, 1e-3f); // every cap vertex on the plane

    EXPECT_NEAR(capArea(pos), 400.0, 1.0);
}

// Cube with a 5mm-radius bore -> cap is an annulus of 400 - pi*r^2.
TEST(SectionCap, HollowBoxCapIsAnnulus) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    gp_Ax2 axis(gp_Pnt(10, 10, 0), gp_Dir(0, 0, 1));
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(axis, 5.0, 20.0).Shape();
    TopoDS_Shape hollow = BRepAlgoAPI_Cut(box, bore).Shape();
    gp_Pln plane(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1));

    std::vector<float> pos;
    ASSERT_TRUE(computeSectionCap(hollow, plane, pos));

    const double expected = 400.0 - M_PI * 25.0;
    EXPECT_NEAR(capArea(pos), expected, 2.0); // slack for arc tessellation
}

// Plane clear of the body -> no cap produced, buffer untouched.
TEST(SectionCap, NoIntersectionNoCap) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    gp_Pln plane(gp_Pnt(0, 0, 30), gp_Dir(0, 0, 1));

    std::vector<float> pos;
    EXPECT_FALSE(computeSectionCap(box, plane, pos));
    EXPECT_TRUE(pos.empty());
}

// Plane grazing a boundary face (no material removed) must NOT cap: the body
// [0,20] on z sits entirely on one side of z=0 and z=20. Without the strict
// straddle check the coplanar base/top face would be filled as a false cap
// that z-fights the real face.
TEST(SectionCap, TangentPlaneNoCap) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    std::vector<float> pos;

    EXPECT_FALSE(computeSectionCap(box, gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), pos));
    EXPECT_TRUE(pos.empty());
    EXPECT_FALSE(computeSectionCap(box, gp_Pln(gp_Pnt(0, 0, 20), gp_Dir(0, 0, 1)), pos));
    EXPECT_TRUE(pos.empty());
}

// NOTE: these area assertions cannot detect a kept-side sign flip. The cut face
// is congruent for either half-space, so keeping +normal instead of -normal
// yields the identical cap geometry (same area, same plane). The kept-side
// convention is guarded by construction (refPnt = loc - normal) and reviewed
// against the shader's discard sign, not by these tests.
