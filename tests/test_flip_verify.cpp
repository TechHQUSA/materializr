// Verification harness for two reviewer claims that contradict the earlier
// "no flip reproduced" conclusion. These are throwaway checks, not shipped
// coverage.

#include "modeling/Sketch.h"
#include "modeling/SketchSolver.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cstdio>

using materializr::Constraint;
using materializr::ConstraintType;
using materializr::Sketch;
using materializr::SketchSolver;

// CLAIM (BMAD I1): a VERTICAL point pair under a Distance dim driven
// 10 -> 0 -> 10 rotates 90 degrees, because the degenerate guard invents
// +x unconditionally. Predicted settle: (-5,5)/(5,5).
TEST(FlipVerify, PointPointDistanceThroughZero) {
    Sketch sk;
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({0.0f, 10.0f});

    Constraint d{};
    d.type = ConstraintType::Distance;
    d.entityA = a;
    d.entityB = b;
    d.value = 10.0;
    int id = sk.addConstraint(d);

    SketchSolver solver;
    ASSERT_TRUE(solver.solve(sk, 500, 1e-4));

    for (double v : {0.0, 10.0}) {
        for (auto& c : sk.getMutableConstraints())
            if (c.id == id) c.value = v;
        solver.solve(sk, 500, 1e-4);
        printf("  after value=%g : a=(%.4f,%.4f) b=(%.4f,%.4f)\n", v,
               sk.getPoint(a)->pos.x, sk.getPoint(a)->pos.y,
               sk.getPoint(b)->pos.x, sk.getPoint(b)->pos.y);
    }

    glm::vec2 pa = sk.getPoint(a)->pos, pb = sk.getPoint(b)->pos;
    glm::vec2 axis = pb - pa;
    // The pair started vertical. If the claim holds, it is now horizontal.
    EXPECT_GT(std::abs(axis.y), std::abs(axis.x))
        << "pair rotated off its original vertical axis: axis=("
        << axis.x << "," << axis.y << ")";
}

// CLAIM (BMAD I2): the DPL through-zero test only passes because the point was
// placed on the side the arbitrary normal happens to favour. Mirror it to
// (5,-4) and it should flip to the positive side.
TEST(FlipVerify, PointLineThroughZeroNegativeSide) {
    Sketch sk;
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({10.0f, 0.0f});
    int ln = sk.addLine(a, b);
    int p = sk.addPoint({5.0f, -4.0f}); // MIRROR of the shipped test

    Constraint d{};
    d.type = ConstraintType::DistancePointLine;
    d.entityA = p;
    d.entityB = ln;
    d.value = 4.0;
    int id = sk.addConstraint(d);

    SketchSolver solver;
    ASSERT_TRUE(solver.solve(sk, 500, 1e-4));
    ASSERT_LT(sk.getPoint(p)->pos.y, 0.0f) << "should start on the negative side";

    for (double v : {0.0, 6.0}) {
        for (auto& c : sk.getMutableConstraints())
            if (c.id == id) c.value = v;
        solver.solve(sk, 500, 1e-4);
        printf("  after value=%g : p=(%.4f,%.4f)\n", v,
               sk.getPoint(p)->pos.x, sk.getPoint(p)->pos.y);
    }

    EXPECT_LT(sk.getPoint(p)->pos.y, 0.0f)
        << "point crossed to the far side of the line";
}

// CLAIM (CodeRabbit I2): a non-positive radius now drags arc endpoints to the
// centre, destroying geometry that previously survived.
TEST(FlipVerify, NonPositiveArcRadiusDestroysGeometry) {
    Sketch sk;
    int c = sk.addPoint({0.0f, 0.0f});
    int s = sk.addPoint({5.0f, 0.0f});
    int e = sk.addPoint({0.0f, 5.0f});
    int arc = sk.addArc(c, s, e, 5.0);

    Constraint r{};
    r.type = ConstraintType::Radius;
    r.entityA = arc;
    r.value = 5.0;
    int id = sk.addConstraint(r);

    SketchSolver solver;
    ASSERT_TRUE(solver.solve(sk, 500, 1e-4));

    for (auto& cc : sk.getMutableConstraints())
        if (cc.id == id) cc.value = -3.0;
    solver.solve(sk, 500, 1e-4);

    float ds = glm::distance(sk.getPoint(s)->pos, sk.getPoint(c)->pos);
    float de = glm::distance(sk.getPoint(e)->pos, sk.getPoint(c)->pos);
    printf("  after radius=-3 : |s-c|=%.6f |e-c|=%.6f\n", ds, de);
    EXPECT_GT(ds, 1e-3f) << "start endpoint collapsed onto the centre";
    EXPECT_GT(de, 1e-3f) << "end endpoint collapsed onto the centre";
}
