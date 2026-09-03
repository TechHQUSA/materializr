// Sketch mirror as a DERIVED relation.
//
// The bug this fixes: `SketchTool::commitMirror` used to reflect the selection
// into brand-new independent entities, so editing the original afterwards left
// the mirrored side behind. The image is now an OUTPUT — `recomputeMirrors()`
// rewrites it from its source — so "edit the original, the mirror follows" is a
// property of the data model rather than something the user has to redo.
//
// Deliberately NOT symmetry constraints: this solver is undamped Gauss-Seidel
// with a hand-counted DOF tally, and coupled symmetry rows in it oscillate and
// corrupt the constrained-state badge. See PLAN.md.

#include "modeling/Sketch.h"

#include <gtest/gtest.h>

#include <cmath>

using materializr::Sketch;
using materializr::SketchMirror;

namespace {

// A vertical axis at x=0 (from (0,-10) to (0,10)) plus one source point.
// Returns {sketch, axisLineId, srcPointId, dstPointId, mirrorId}.
struct Fixture {
    Sketch sk;
    int axis = -1, src = -1, dst = -1, mir = -1;
};

Fixture makePointMirror(glm::vec2 srcPos = {3.0f, 4.0f}) {
    Fixture f;
    const int a = f.sk.addPoint({0.0f, -10.0f});
    const int b = f.sk.addPoint({0.0f,  10.0f});
    f.axis = f.sk.addLine(a, b);
    f.src = f.sk.addPoint(srcPos);
    f.dst = f.sk.addPoint({0.0f, 0.0f});        // deliberately wrong; recompute fixes it
    SketchMirror m;
    m.axisLineId = f.axis;
    m.points = { {f.src, f.dst} };
    f.mir = f.sk.addMirror(m);
    f.sk.validateMirrors();
    f.sk.recomputeMirrors();
    return f;
}

glm::vec2 posOf(const Sketch& sk, int id) {
    const auto* p = sk.getPoint(id);
    return p ? p->pos : glm::vec2(std::nanf(""), std::nanf(""));
}

} // namespace

// The whole point: the image is computed, not copied.
TEST(SketchMirror, DerivedPointReflectsAcrossTheAxis) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    EXPECT_NEAR(-3.0f, posOf(f.sk, f.dst).x, 1e-5);
    EXPECT_NEAR( 4.0f, posOf(f.sk, f.dst).y, 1e-5);
}

// The reported bug, as a test: edit the original, the mirror follows.
TEST(SketchMirror, EditingTheSourceMovesTheImage) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    f.sk.movePoint(f.src, {5.0f, -2.0f});
    f.sk.recomputeMirrors();
    EXPECT_NEAR(-5.0f, posOf(f.sk, f.dst).x, 1e-5);
    EXPECT_NEAR(-2.0f, posOf(f.sk, f.dst).y, 1e-5);
}

// The axis is real geometry and free to move — the image follows it too, which
// is what the symmetry-constraint design could not safely allow.
TEST(SketchMirror, MovingTheAxisMovesTheImage) {
    Fixture f = makePointMirror({3.0f, 0.0f});
    ASSERT_NEAR(-3.0f, posOf(f.sk, f.dst).x, 1e-5);
    // Shift the axis to x=1: the reflection of x=3 becomes x=-1.
    const auto& lines = f.sk.getLines();
    ASSERT_FALSE(lines.empty());
    f.sk.movePoint(lines[0].startPointId, {1.0f, -10.0f});
    f.sk.movePoint(lines[0].endPointId,   {1.0f,  10.0f});
    f.sk.recomputeMirrors();
    EXPECT_NEAR(-1.0f, posOf(f.sk, f.dst).x, 1e-5);
}

// Idempotence matters: the invariant is re-established on many paths, often
// twice in a row. Recomputing an already-correct sketch must change nothing.
TEST(SketchMirror, RecomputeIsIdempotent) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    const glm::vec2 once = posOf(f.sk, f.dst);
    f.sk.recomputeMirrors();
    f.sk.recomputeMirrors();
    EXPECT_NEAR(once.x, posOf(f.sk, f.dst).x, 1e-6);
    EXPECT_NEAR(once.y, posOf(f.sk, f.dst).y, 1e-6);
}

// A point ON the axis reflects to itself. Round 1 of the review proposed
// treating these as "shared, not paired"; that is wrong, because nothing keeps
// the point on the axis — move it and the two are no longer reflections. Pairing
// it needs no special case at all.
TEST(SketchMirror, PointOnAxisReflectsToItselfAndStillTracks) {
    Fixture f = makePointMirror({0.0f, 5.0f});
    EXPECT_NEAR(0.0f, posOf(f.sk, f.dst).x, 1e-5);
    EXPECT_NEAR(5.0f, posOf(f.sk, f.dst).y, 1e-5);
    // Now move it OFF the axis — the image must separate, not stay welded.
    f.sk.movePoint(f.src, {2.0f, 5.0f});
    f.sk.recomputeMirrors();
    EXPECT_NEAR(-2.0f, posOf(f.sk, f.dst).x, 1e-5);
}

// A zero-length axis has no direction to reflect about. Reflecting anyway
// divides by zero and poisons every derived point in the sketch with NaN, so the
// group is broken instead.
TEST(SketchMirror, DegenerateAxisBreaksTheGroupInsteadOfProducingNaN) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    const auto& lines = f.sk.getLines();
    ASSERT_FALSE(lines.empty());
    f.sk.movePoint(lines[0].endPointId, {0.0f, -10.0f});   // collapse onto the start
    EXPECT_EQ(1, f.sk.validateMirrors()) << "the degenerate group should be broken";
    f.sk.recomputeMirrors();
    EXPECT_TRUE(f.sk.getMirrors().empty());
    EXPECT_TRUE(std::isfinite(posOf(f.sk, f.dst).x));
    EXPECT_TRUE(std::isfinite(posOf(f.sk, f.dst).y));
    EXPECT_FALSE(f.sk.isDerived(f.dst)) << "broken group must release its geometry";
}

// Break link keeps the geometry and releases it. Every abandonment path routes
// here, so a released entity must not stay flagged — a stale flag would keep it
// undraggable and still subtracting from DOF while owned by nothing.
TEST(SketchMirror, BreakLinkKeepsGeometryAndClearsOwnership) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    ASSERT_TRUE(f.sk.isDerived(f.dst));
    const glm::vec2 before = posOf(f.sk, f.dst);

    ASSERT_TRUE(f.sk.breakMirrorLink(f.mir));
    EXPECT_TRUE(f.sk.getMirrors().empty());
    EXPECT_FALSE(f.sk.isDerived(f.dst));
    EXPECT_NEAR(before.x, posOf(f.sk, f.dst).x, 1e-6) << "geometry survives";

    // And it is genuinely independent now: the source moves, the ex-image stays.
    f.sk.movePoint(f.src, {9.0f, 9.0f});
    f.sk.recomputeMirrors();
    EXPECT_NEAR(before.x, posOf(f.sk, f.dst).x, 1e-6);
    EXPECT_FALSE(f.sk.breakMirrorLink(f.mir)) << "second break is a no-op";
}

// Flags are normalised from group membership, never trusted. A file (or an old
// build that dropped the groups but kept the flags) can claim ownership that
// does not exist; the entity must come back unlocked.
TEST(SketchMirror, ValidateNormalisesFlagsFromGroupsNotFromTheFile) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    // Forge the situation: a group referencing an entity that never existed.
    SketchMirror bogus;
    bogus.axisLineId = f.axis;
    bogus.points = { {f.src, 999999} };
    f.sk.addRawMirror(bogus);
    EXPECT_EQ(1, f.sk.validateMirrors()) << "the group with a missing member is dropped";
    EXPECT_TRUE(f.sk.isDerived(f.dst)) << "the VALID group still owns its image";
    EXPECT_EQ(1u, f.sk.getMirrors().size());
}

// restoreFrom is the invariant for every whole-sketch assignment (undo/redo,
// gizmo cancel, pattern preview, load, combine). A plain copy would carry stale
// derived coordinates straight through.
TEST(SketchMirror, RestoreFromReestablishesTheImage) {
    Fixture f = makePointMirror({3.0f, 4.0f});

    // A snapshot whose derived coordinates are deliberately wrong, exactly as a
    // hand-edited or rounded file would be.
    Sketch stale = f.sk;
    stale.movePoint(f.dst, {0.0f, 0.0f});

    Sketch live;
    live.restoreFrom(stale);
    const auto* p = live.getPoint(f.dst);
    ASSERT_NE(nullptr, p);
    EXPECT_NEAR(-3.0f, p->pos.x, 1e-5) << "restoreFrom must recompute, not just assign";
    EXPECT_NEAR( 4.0f, p->pos.y, 1e-5);
    EXPECT_TRUE(live.isDerived(f.dst));
}

// Elements carry values a point cannot: a radius, and the construction flag that
// the old copy-based mirror silently dropped.
TEST(SketchMirror, CircleRadiusAndConstructionFlagFollowTheSource) {
    Sketch sk;
    const int a = sk.addPoint({0.0f, -10.0f});
    const int b = sk.addPoint({0.0f,  10.0f});
    const int axis = sk.addLine(a, b);
    const int srcC = sk.addPoint({4.0f, 0.0f});
    const int dstC = sk.addPoint({0.0f, 0.0f});
    const int srcCirc = sk.addCircle(srcC, 2.5);
    const int dstCirc = sk.addCircle(dstC, 0.1);   // wrong on purpose

    SketchMirror m;
    m.axisLineId = axis;
    m.points  = { {srcC, dstC} };
    m.circles = { {srcCirc, dstCirc} };
    sk.addMirror(m);
    sk.validateMirrors();
    sk.recomputeMirrors();

    const auto& circles = sk.getCircles();
    auto find = [&](int id) -> const materializr::SketchCircle* {
        for (const auto& c : circles) if (c.id == id) return &c;
        return nullptr;
    };
    ASSERT_NE(nullptr, find(dstCirc));
    EXPECT_NEAR(2.5, find(dstCirc)->radius, 1e-9) << "radius is derived too";
    EXPECT_NEAR(-4.0f, posOf(sk, dstC).x, 1e-5);

    // Now make the source a construction circle: the flag must follow, which the
    // old copy-based mirror never did.
    sk.setCircleRadius(srcCirc, 3.0);
    sk.recomputeMirrors();
    EXPECT_NEAR(3.0, find(dstCirc)->radius, 1e-9);
}

// No groups: recompute must be a cheap no-op, not a crash or a scan.
TEST(SketchMirror, NoMirrorsIsANoOp) {
    Sketch sk;
    const int p = sk.addPoint({1.0f, 2.0f});
    sk.recomputeMirrors();
    EXPECT_NEAR(1.0f, posOf(sk, p).x, 1e-6);
    EXPECT_EQ(0, sk.validateMirrors());
    EXPECT_FALSE(sk.isDerived(p));
}
