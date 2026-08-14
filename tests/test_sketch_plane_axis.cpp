// Which way a face sketch's grid runs.
//
// Reported: sketching on a symmetric tapered face put the grid on one of the
// DIAGONAL sides, on a part the user had deliberately built symmetric about a
// world axis. The rule was "align X to the face's longest straight edge", and a
// trapezoid's two longest edges are its diagonals.
//
// That rule existed for a reason — a lofted cap's parametric X sits ~45 degrees
// off its visible edges — so it is not simply reverted. The rule now prefers a
// straight edge that agrees with the world frame and falls back to the longest
// edge, which keeps the loft case working and stops a part rotated deliberately
// off-axis from being force-aligned.
#include <gtest/gtest.h>

#include "modeling/SketchPlaneAxis.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>

#include <cmath>

using materializr::sketchPlaneXDirection;

namespace {

const gp_Dir kUp(0, 0, 1);
const gp_Dir kWorldX(1, 0, 0);

// A face from four corners in the z=0 plane.
TopoDS_Face quad(gp_Pnt a, gp_Pnt b, gp_Pnt c, gp_Pnt d) {
    BRepBuilderAPI_MakePolygon poly(a, b, c, d, Standard_True);
    return BRepBuilderAPI_MakeFace(poly.Wire()).Face();
}

// Angle between two in-plane directions, ignoring sense (an edge and its
// reverse are the same axis), in degrees.
double axisAngleDeg(const gp_Dir& a, const gp_Dir& b) {
    const double d = std::abs(gp_Vec(a) * gp_Vec(b));
    return std::acos(std::min(1.0, d)) * 180.0 / M_PI;
}

} // namespace

TEST(SketchPlaneAxis, SymmetricTaperFollowsTheWorldNotTheDiagonal) {
    // The reported shape: symmetric about x=0, short top, wide bottom, and the
    // two DIAGONAL sides are much the longest edges.
    const TopoDS_Face f = quad(gp_Pnt(-2, 0, 0), gp_Pnt(2, 0, 0),
                               gp_Pnt(6, 40, 0), gp_Pnt(-6, 40, 0));
    const gp_Dir x = sketchPlaneXDirection(f, kUp, kWorldX);

    // The diagonals run about 5.7 degrees off vertical and are ~40mm long
    // against the 4mm top and 12mm bottom — the old longest-edge rule took one
    // of them. The horizontal ends are the world-aligned pair.
    EXPECT_LT(axisAngleDeg(x, kWorldX), 1.0);
}

TEST(SketchPlaneAxis, ANearlyAlignedDiagonalDoesNotWinOnLength) {
    // The cockpit face, to scale. Its long side runs 4.51 degrees off world Z
    // while an 81.6mm side is exactly on world X. The first attempt at this
    // rule used a 5-degree ABSOLUTE tolerance, so the diagonal counted as
    // "aligned" and then beat the exact edge on length — the grid stayed on
    // the diagonal and the fix shipped doing nothing. Rank by angle first.
    const double off = 4.51 * M_PI / 180.0;
    const double L = 130.40;
    // A quad: exact 81.6 along X, then a long side 4.51 degrees off Y.
    const TopoDS_Face f = quad(gp_Pnt(0, 0, 0),
                               gp_Pnt(81.6, 0, 0),
                               gp_Pnt(81.6 + L * std::sin(off), L * std::cos(off), 0),
                               gp_Pnt(L * std::sin(off), L * std::cos(off), 0));
    const gp_Dir x = sketchPlaneXDirection(f, kUp, kWorldX);
    EXPECT_LT(axisAngleDeg(x, kWorldX), 0.5);
}

TEST(SketchPlaneAxis, AxisAlignedRectangleIsUnchanged) {
    const TopoDS_Face f = quad(gp_Pnt(0, 0, 0), gp_Pnt(30, 0, 0),
                               gp_Pnt(30, 10, 0), gp_Pnt(0, 10, 0));
    const gp_Dir x = sketchPlaneXDirection(f, kUp, kWorldX);
    // Either world axis in the plane is fine — a square grid looks identical
    // under a 90 degree turn. What must not happen is a diagonal.
    const double toX = axisAngleDeg(x, kWorldX);
    const double toY = axisAngleDeg(x, gp_Dir(0, 1, 0));
    EXPECT_TRUE(toX < 1.0 || toY < 1.0);
}

TEST(SketchPlaneAxis, DeliberatelyRotatedPartFollowsItsOwnEdge) {
    // A rectangle turned 30 degrees: no edge is near a world axis, so the
    // fallback keeps the grid on the part's own geometry rather than forcing
    // it to the world frame.
    const double a = 30.0 * M_PI / 180.0;
    auto rot = [&](double x, double y) {
        return gp_Pnt(x * std::cos(a) - y * std::sin(a),
                      x * std::sin(a) + y * std::cos(a), 0.0);
    };
    const TopoDS_Face f = quad(rot(0, 0), rot(30, 0), rot(30, 10), rot(0, 10));
    const gp_Dir x = sketchPlaneXDirection(f, kUp, kWorldX);

    const gp_Dir edge(std::cos(a), std::sin(a), 0.0);
    EXPECT_LT(axisAngleDeg(x, edge), 1.0);
    EXPECT_GT(axisAngleDeg(x, kWorldX), 25.0);   // definitely not world-forced
}

TEST(SketchPlaneAxis, CircularCapWithNoStraightEdgeTakesAWorldAxis) {
    // The case the old code already handled in its fallback: nothing to follow,
    // so the grid must not inherit an arbitrary parametric X.
    const TopoDS_Shape cyl =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), kUp), 5.0, 10.0).Shape();
    TopoDS_Face cap;
    for (TopExp_Explorer ex(cyl, TopAbs_FACE); ex.More(); ex.Next()) {
        BRepAdaptor_Surface sa(TopoDS::Face(ex.Current()));
        if (sa.GetType() != GeomAbs_Plane) continue;
        gp_Dir nn = sa.Plane().Axis().Direction();
        if (std::abs(gp_Vec(nn) * gp_Vec(kUp)) > 0.99) { cap = TopoDS::Face(ex.Current()); break; }
    }
    ASSERT_FALSE(cap.IsNull());

    // Hand it a deliberately skewed "surface X" — it must be ignored.
    const gp_Dir skewed(std::cos(0.7), std::sin(0.7), 0.0);
    const gp_Dir x = sketchPlaneXDirection(cap, kUp, skewed);
    const double toX = axisAngleDeg(x, kWorldX);
    const double toY = axisAngleDeg(x, gp_Dir(0, 1, 0));
    EXPECT_TRUE(toX < 1.0 || toY < 1.0);
}

TEST(SketchPlaneAxis, AVerticalFaceUsesAnAxisThatLiesInIt) {
    // Normal along world X: the returned direction must lie IN the plane, i.e.
    // be perpendicular to the normal, whichever axis it picks.
    const gp_Dir n(1, 0, 0);
    const TopoDS_Face f = quad(gp_Pnt(0, 0, 0), gp_Pnt(0, 20, 0),
                               gp_Pnt(0, 20, 8), gp_Pnt(0, 0, 8));
    const gp_Dir x = sketchPlaneXDirection(f, n, gp_Dir(0, 1, 0));
    EXPECT_NEAR(gp_Vec(x) * gp_Vec(n), 0.0, 1e-9);
}
