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
#include "modeling/SketchSolver.h"
#include "modeling/SketchTool.h"
#include "modeling/SvgImport.h"
#include "modeling/TextSketchOp.h"
#include "io/ProjectIO.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <unordered_set>
#include <memory>
#include <sstream>
#include <set>
#include <type_traits>

// SketchTool's Text / SVG stamp paths are not part of materializr_core (they
// pull in font rendering); stub them so this links.
namespace materializr {
int SvgImport::place(Sketch*, const SvgPaths&, glm::vec2, float, float) { return 0; }
int TextSketch::generate(Sketch*, const std::string&, const std::string&,
                         glm::vec2, float, float) { return 0; }
}

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

// The invariant is only as good as its adoption. These pin the two things that
// make adoption mechanical rather than remembered: a live sketch cannot be
// copy-assigned at all (it is a compile error, so the static_assert below is
// really documentation), and restoreFrom is what the production restore paths
// call — undo/redo, gizmo cancel, pattern preview/cancel, load, combine,
// combine undo, transactional rollback and draft recovery.
static_assert(!std::is_copy_assignable<materializr::Sketch>::value,
              "Sketch copy-assignment must stay deleted: assigning a live sketch "
              "wholesale is how a mirror image goes stale, and three review rounds "
              "each found another restore path that enumeration had missed.");
static_assert(std::is_copy_constructible<materializr::Sketch>::value,
              "Copy CONSTRUCTION stays available — a snapshot is a new object.");

// assignRaw is the deliberate escape hatch: bytes, no invariant. It exists for
// storing a snapshot into a member, and must NOT recompute (that is precisely
// what distinguishes it from restoreFrom).
TEST(SketchMirror, AssignRawCopiesWithoutReestablishingTheInvariant) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    Sketch stale = f.sk;                    // copy construction: consistent
    stale.movePoint(f.dst, {0.0f, 0.0f});   // now deliberately inconsistent

    Sketch snap;
    snap.assignRaw(stale);
    const auto* p = snap.getPoint(f.dst);
    ASSERT_NE(nullptr, p);
    EXPECT_NEAR(0.0f, p->pos.x, 1e-6) << "assignRaw must NOT recompute";

    Sketch fixed;
    fixed.restoreFrom(stale);
    EXPECT_NEAR(-3.0f, fixed.getPoint(f.dst)->pos.x, 1e-5) << "restoreFrom must";
}

// ─── Solver integration ──────────────────────────────────────────────────────

// The image must be current by the time solve() returns — including when the
// solve does NOT converge. Deriving from an unconverged source is correct;
// showing a stale image is not.
TEST(SketchMirrorSolver, SolveLeavesTheImageCurrent) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    materializr::SketchSolver solver;

    f.sk.movePoint(f.src, {7.0f, 1.0f});      // source moved, image now stale
    ASSERT_NEAR(-3.0f, posOf(f.sk, f.dst).x, 1e-5) << "precondition: still stale";
    solver.solve(f.sk);
    EXPECT_NEAR(-7.0f, posOf(f.sk, f.dst).x, 1e-5) << "solve must leave it current";
    EXPECT_NEAR( 1.0f, posOf(f.sk, f.dst).y, 1e-5);
}

// A derived point is not a free variable — it is computed. Counting it as free
// reports a fully mirrored sketch as permanently Under-constrained, which is the
// badge lying to the user.
TEST(SketchMirrorSolver, DerivedPointsDoNotCountTowardsDegreesOfFreedom) {
    materializr::SketchSolver solver;

    // Baseline: axis (2 pts) + one free point = 3 points = 6 DOF.
    Sketch plain;
    const int a = plain.addPoint({0.0f, -10.0f});
    const int b = plain.addPoint({0.0f,  10.0f});
    plain.addLine(a, b);
    plain.addPoint({3.0f, 4.0f});
    solver.solve(plain);
    const int baseline = solver.degreesOfFreedom();
    EXPECT_EQ(6, baseline);

    // Same sketch plus a mirrored image of that point: the image adds 2 stored
    // coordinates and subtracts them again, so the total is unchanged.
    Fixture f = makePointMirror({3.0f, 4.0f});
    solver.solve(f.sk);
    EXPECT_EQ(baseline, solver.degreesOfFreedom())
        << "a derived point adds no freedom";
}

// The oracle the review corrected: mirroring is NOT DOF-neutral overall,
// because materialising a free construction axis adds two points. Stating the
// wrong expectation here would have hidden a real off-by-four.
TEST(SketchMirrorSolver, MaterialisingAFreeAxisAddsFourDegreesOfFreedom) {
    materializr::SketchSolver solver;

    Sketch before;
    before.addPoint({3.0f, 4.0f});
    solver.solve(before);
    const int dofBefore = solver.degreesOfFreedom();
    EXPECT_EQ(2, dofBefore);

    Fixture f = makePointMirror({3.0f, 4.0f});   // adds axis (2 pts) + image
    solver.solve(f.sk);
    EXPECT_EQ(dofBefore + 4, solver.degreesOfFreedom())
        << "the axis's two endpoints are free; the image is not";
}

// A derived circle's radius is derived too, so it must not be counted.
TEST(SketchMirrorSolver, DerivedCircleRadiusDoesNotCountEither) {
    materializr::SketchSolver solver;
    Sketch sk;
    const int a = sk.addPoint({0.0f, -10.0f});
    const int b = sk.addPoint({0.0f,  10.0f});
    const int axis = sk.addLine(a, b);
    const int srcC = sk.addPoint({4.0f, 0.0f});
    const int srcCirc = sk.addCircle(srcC, 2.5);
    solver.solve(sk);
    const int baseline = solver.degreesOfFreedom();

    const int dstC = sk.addPoint({0.0f, 0.0f});
    const int dstCirc = sk.addCircle(dstC, 0.1);
    materializr::SketchMirror m;
    m.axisLineId = axis;
    m.points  = { {srcC, dstC} };
    m.circles = { {srcCirc, dstCirc} };
    sk.addMirror(m);
    sk.validateMirrors();
    solver.solve(sk);
    EXPECT_EQ(baseline, solver.degreesOfFreedom())
        << "derived centre (2) and derived radius (1) are all outputs";
}

// Ordering: recomputeMirrors() must run BEFORE refreshReferenceValues().
//
// A reference dimension re-measures itself from the geometry at the end of each
// solve. If the image is recomputed after that, a dimension attached to derived
// geometry reports the PREVIOUS solve's position — a label that silently lags
// reality by one edit. This test exists because a mutation swapping the two
// calls passed every other test in this file.
TEST(SketchMirrorSolver, ReferenceDimensionOnDerivedGeometryReadsThisSolve) {
    materializr::SketchSolver solver;
    Fixture f = makePointMirror({3.0f, 4.0f});
    ASSERT_NEAR(-3.0f, posOf(f.sk, f.dst).x, 1e-5);

    // A fixed anchor directly below the image, and a REFERENCE (measuring, not
    // driving) distance from it to the derived point.
    const int anchor = f.sk.addPoint({-3.0f, 0.0f});
    materializr::Constraint c{};
    c.type = materializr::ConstraintType::Distance;
    c.entityA = anchor;
    c.entityB = f.dst;
    c.isDriving = false;          // annotation only: the solver never enforces it
    c.value = 0.0;
    f.sk.addConstraint(c);

    solver.solve(f.sk);
    const auto valueNow = [&] {
        for (const auto& k : f.sk.getConstraints()) if (k.entityB == f.dst) return k.value;
        return 0.0;
    };
    EXPECT_NEAR(4.0, valueNow(), 1e-4) << "measures the image where it is now";

    // Move the SOURCE up by 5. The image follows to (-3, 9), so the reference
    // dimension must read 9 — not the 4 it would read from the stale image.
    f.sk.movePoint(f.src, {3.0f, 9.0f});
    solver.solve(f.sk);
    EXPECT_NEAR(9.0, valueNow(), 1e-4)
        << "a reference dimension on derived geometry lagged one solve behind";
}

// clear() must drop mirror groups along with the geometry. This is a corruption
// bug, not tidiness: clear() resets m_nextId to 1, so entities created after it
// get exactly the ids a surviving group still references — the group would claim
// brand-new unrelated geometry as derived, and recomputeMirrors() would then
// overwrite it with a reflection of something else entirely.
TEST(SketchMirror, ClearDropsMirrorGroupsSoRecycledIdsAreNotClaimed) {
    Fixture f = makePointMirror({3.0f, 4.0f});
    ASSERT_FALSE(f.sk.getMirrors().empty());

    f.sk.clear();
    EXPECT_TRUE(f.sk.getMirrors().empty()) << "groups must not outlive the geometry";

    // Rebuild from scratch — these ids collide with the cleared sketch's.
    const int p1 = f.sk.addPoint({7.0f, 7.0f});
    const int p2 = f.sk.addPoint({8.0f, 8.0f});
    f.sk.recomputeMirrors();
    EXPECT_FALSE(f.sk.isDerived(p1));
    EXPECT_FALSE(f.sk.isDerived(p2));
    EXPECT_NEAR(7.0f, posOf(f.sk, p1).x, 1e-6) << "new geometry must be untouched";
    EXPECT_NEAR(8.0f, posOf(f.sk, p2).x, 1e-6);

    // And the id sequence restarts cleanly, so a new group gets id 1.
    const int a = f.sk.addPoint({0.0f, -5.0f});
    const int b = f.sk.addPoint({0.0f,  5.0f});
    materializr::SketchMirror m;
    m.axisLineId = f.sk.addLine(a, b);
    m.points = { {p1, p2} };
    EXPECT_EQ(1, f.sk.addMirror(m)) << "m_nextMirrorId must reset too";
}

// --- commitMirror: where the user-visible bug actually gets fixed -----------
//
// The tool used to reflect the selection into independent copies. It now
// materialises a construction axis and registers a group, so the image is an
// output of the source from the moment it is created.

namespace {

// Half of a 10x10 square, open on the right at x=0: the single most common
// mirror workflow (draw half, mirror, extrude).
struct HalfProfile {
    materializr::Sketch sk;
    materializr::SketchTool tool;
    int bl = -1, br = -1, tr = -1, tl = -1;   // (-10,0) (0,0) (0,10) (-10,10)
    std::set<int> outPts, outLines;
};

std::unique_ptr<HalfProfile> makeHalfProfile() {
    auto h = std::make_unique<HalfProfile>();
    h->bl = h->sk.addPoint({-10.0f,  0.0f});
    h->br = h->sk.addPoint({  0.0f,  0.0f});
    h->tr = h->sk.addPoint({  0.0f, 10.0f});
    h->tl = h->sk.addPoint({-10.0f, 10.0f});
    h->sk.addLine(h->br, h->bl);
    h->sk.addLine(h->bl, h->tl);
    h->sk.addLine(h->tl, h->tr);
    h->tool.setSketch(&h->sk);
    return h;
}

void commitVerticalMirrorAtX0(HalfProfile& h) {
    ASSERT_TRUE(h.tool.beginMirror());
    h.tool.setMirrorAnchor({0.0f, 5.0f});
    h.tool.setMirrorAngle(static_cast<float>(M_PI) * 0.5f);   // vertical
    h.tool.commitMirror(h.outPts, h.outLines);
}

} // namespace

TEST(SketchMirrorCommit, BuildsOneGroupWithAConstructionAxis) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    ASSERT_EQ(1u, h->sk.getMirrors().size());
    const auto& mir = h->sk.getMirrors()[0];
    const auto* axis = [&]() -> const materializr::SketchLine* {
        for (const auto& l : h->sk.getLines()) if (l.id == mir.axisLineId) return &l;
        return nullptr;
    }();
    ASSERT_NE(nullptr, axis) << "the axis must be a real line in the sketch";
    EXPECT_TRUE(axis->isConstruction) << "an axis in a profile would break extrude";
    EXPECT_TRUE(h->sk.getPoint(axis->startPointId)->isConstruction);
    EXPECT_EQ(3u, mir.lines.size());
    for (const auto& [src, dst] : mir.lines) {
        EXPECT_FALSE(h->sk.isDerived(src));
        EXPECT_TRUE(h->sk.isDerived(dst)) << "every image must be flagged";
    }
    // The axis is not the user's new geometry; selecting it would make Delete
    // mean "break the link".
    EXPECT_EQ(0u, h->outLines.count(mir.axisLineId));
}

// buildWires joins segments by point ID, not by position. A source vertex ON
// the axis therefore has to be SHARED, not paired: a second point at the same
// coordinate leaves the profile an open chain that silently refuses to extrude.
TEST(SketchMirrorCommit, MirroredHalfProfileClosesIntoOneWire) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    EXPECT_FALSE(h->sk.isDerived(h->br)) << "a shared on-axis vertex is nobody's image";
    EXPECT_FALSE(h->sk.isDerived(h->tr));
    const auto wires = h->sk.buildWires();
    ASSERT_EQ(1u, wires.size()) << "the mirrored halves must close into ONE wire";

    // buildWires is NOT the path face picking and extrude use. Asserting only on
    // it passed while the app still showed two regions and would have extruded
    // two solids: buildRegionsUncached used the construction axis as a DIVIDER
    // and cut the face in half. Found by driving the real app, not by this file.
    const auto regions = h->sk.buildRegions();
    ASSERT_EQ(1u, regions.size()) << "the axis must not divide the face it mirrors about";
}

// The bug the user reported, through the real path: edit the source, solve,
// and the image has followed.
TEST(SketchMirrorCommit, EditingTheSourceMovesTheImageThroughSolve) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const int imageOfTl = [&] {
        for (const auto& [src, dst] : h->sk.getMirrors()[0].points)
            if (src == h->tl) return dst;
        return -1;
    }();
    ASSERT_NE(-1, imageOfTl);
    EXPECT_NEAR(10.0f, posOf(h->sk, imageOfTl).x, 1e-4);

    h->sk.movePoint(h->tl, {-4.0f, 12.0f});
    materializr::SketchSolver solver;
    solver.solve(h->sk);
    EXPECT_NEAR( 4.0f, posOf(h->sk, imageOfTl).x, 1e-4) << "the image must follow its source";
    EXPECT_NEAR(12.0f, posOf(h->sk, imageOfTl).y, 1e-4);
}

// Trap from PLAN.md: the oracle is NOT "same DOF as the original". The commit
// materialises a free axis whose two endpoints add four freedoms; the derived
// points subtract exactly what they add.
TEST(SketchMirrorCommit, DofIsTheOriginalPlusTheFreeAxisLessThePins) {
    auto h = makeHalfProfile();
    materializr::SketchSolver solver;
    solver.solve(h->sk);
    const int before = solver.degreesOfFreedom();
    commitVerticalMirrorAtX0(*h);
    solver.solve(h->sk);
    // +4 for the axis's two free endpoints, then -1 for each of the two shared
    // on-axis vertices pinned to it. The derived points subtract exactly what
    // they add, so they are absent from this sum.
    EXPECT_EQ(before + 4 - 2, solver.degreesOfFreedom());
}

// A driving row on an image is unsatisfiable — the recompute overwrites it
// every solve — while still consuming a freedom, so the sketch would read
// over-constrained forever. Refused at the door.
TEST(SketchMirrorCommit, DrivingConstraintOnDerivedGeometryIsRefused) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const int image = h->sk.getMirrors()[0].points.front().second;
    materializr::Constraint c{};
    c.type = materializr::ConstraintType::Fixed;
    c.entityA = image;
    EXPECT_EQ(-1, h->sk.addConstraint(c)) << "driving on an image must be refused";

    // A reference dimension only measures, so it is allowed through.
    materializr::Constraint ref{};
    ref.type = materializr::ConstraintType::Distance;
    ref.entityA = image;
    ref.entityB = h->br;
    ref.isDriving = false;
    EXPECT_NE(-1, h->sk.addConstraint(ref));
}

// The same rule for constraints that arrive by another route (a project file, a
// combine remap): validateMirrors drops them rather than carrying them.
TEST(SketchMirrorCommit, ValidateDemotesDrivingConstraintsThatBecameDerived) {
    auto h = makeHalfProfile();
    materializr::Constraint c{};
    c.type = materializr::ConstraintType::Fixed;
    c.entityA = h->sk.addPoint({50.0f, 50.0f});   // not derived yet: accepted
    const int cid = h->sk.addConstraint(c);
    ASSERT_NE(-1, cid);
    // Now make that very point an image of something.
    materializr::SketchMirror m;
    m.axisLineId = h->sk.getLines().front().id;
    m.points = { {h->tl, c.entityA} };
    h->sk.addMirror(m);
    h->sk.validateMirrors();
    // Demoted, not deleted: this path also runs from restoreFrom on every
    // undo/redo, where deleting would silently eat the user's dimensions. The
    // row stays and keeps measuring; it just stops driving.
    bool found = false;
    for (const auto& cn : h->sk.getConstraints())
        if (cn.id == cid) { found = true; EXPECT_FALSE(cn.isDriving); }
    EXPECT_TRUE(found) << "the dimension itself must survive";
}

// The image copies its source's construction flag, which only happens in the
// recompute — commitMirror's addLine() cannot know it. Without the recompute at
// the end of the commit, a mirrored construction line comes back as real
// profile geometry and silently changes what extrudes.
TEST(SketchMirrorCommit, MirroredConstructionGeometryStaysConstruction) {
    auto h = makeHalfProfile();
    const int scaffold = h->sk.addLine(h->bl, h->tr);
    h->sk.setConstruction(scaffold, true);
    commitVerticalMirrorAtX0(*h);
    const int image = [&] {
        for (const auto& [src, dst] : h->sk.getMirrors()[0].lines)
            if (src == scaffold) return dst;
        return -1;
    }();
    ASSERT_NE(-1, image);
    for (const auto& l : h->sk.getLines())
        if (l.id == image)
            EXPECT_TRUE(l.isConstruction) << "the image must inherit construction";
}

// A selection that lies entirely on the axis mirrors to itself: there is no
// relation to record. The commit must then leave the sketch exactly as it found
// it — an axis created before that check would survive as an orphan
// construction line belonging to no group, which nothing would ever clean up.
TEST(SketchMirrorCommit, NothingToDeriveLeavesNoOrphanAxis) {
    materializr::Sketch sk;
    materializr::SketchTool tool;
    const int a = sk.addPoint({0.0f,  0.0f});
    const int b = sk.addPoint({0.0f, 10.0f});
    sk.addLine(a, b);
    tool.setSketch(&sk);
    ASSERT_TRUE(tool.beginMirror());
    tool.setMirrorAnchor({0.0f, 5.0f});
    tool.setMirrorAngle(static_cast<float>(M_PI) * 0.5f);   // straight along it
    std::set<int> pts, lines;
    tool.commitMirror(pts, lines);
    EXPECT_TRUE(sk.getMirrors().empty());
    EXPECT_EQ(2u, sk.getPoints().size()) << "no axis endpoints should be left behind";
    EXPECT_EQ(1u, sk.getLines().size()) << "no orphan construction axis";
}

// A shared vertex is what holds the two halves together, so it must sit exactly
// on the axis and be kept there. Commit snaps it (the user placed the gizmo by
// hand, within a weld radius, not on the micron) and pins it.
TEST(SketchMirrorCommit, SharedOnAxisVertexIsSnappedAndPinned) {
    auto h = makeHalfProfile();
    h->sk.movePoint(h->br, {0.12f, 0.0f});          // off the axis, inside the weld radius
    commitVerticalMirrorAtX0(*h);
    EXPECT_NEAR(0.0f, posOf(h->sk, h->br).x, 1e-5) << "a shared vertex must be ON the axis";

    const int axisId = h->sk.getMirrors()[0].axisLineId;
    int pins = 0;
    for (const auto& c : h->sk.getConstraints())
        if (c.type == materializr::ConstraintType::DistancePointLine &&
            c.entityB == axisId && c.value == 0.0)
            ++pins;
    EXPECT_EQ(2, pins) << "both shared vertices must be pinned to the axis";

    // And the pin holds: nudging it off and solving brings it back.
    h->sk.movePoint(h->br, {3.0f, 0.0f});
    materializr::SketchSolver solver;
    solver.solve(h->sk);
    EXPECT_NEAR(0.0f, posOf(h->sk, h->br).x, 1e-2) << "the pin must return it to the axis";
}

// Mirroring an image would chain groups, which the recompute has no ordering
// guarantee for. Selecting "everything" after a first mirror is the ordinary
// way a user hits this.
TEST(SketchMirrorCommit, DerivedGeometryIsRefusedAsASource) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const std::size_t afterFirst = h->sk.getLines().size();

    materializr::SketchTool second;
    second.setSketch(&h->sk);
    ASSERT_TRUE(second.beginMirror());               // no selection: takes everything
    second.setMirrorAnchor({0.0f, 0.0f});
    second.setMirrorAngle(0.0f);                     // horizontal
    std::set<int> pts, lines;
    second.commitMirror(pts, lines);

    ASSERT_EQ(2u, h->sk.getMirrors().size());
    for (const auto& [src, dst] : h->sk.getMirrors()[1].lines)
        EXPECT_EQ(-1, h->sk.mirrorOwning(src))
            << "an image must never be recorded as a source";
    EXPECT_LT(h->sk.getLines().size(), afterFirst * 2)
        << "the derived half must not have been mirrored again";
}

// The construction filter buildWires gained is not mirror-specific: it changes
// profile building for every sketch. Pin the general behaviour so the change is
// visible if anyone reverts it.
TEST(SketchMirrorCommit, AConstructionLineNoLongerSplitsAProfile) {
    materializr::Sketch sk;
    const int a = sk.addPoint({0.0f, 0.0f});
    const int b = sk.addPoint({10.0f, 0.0f});
    const int c = sk.addPoint({10.0f, 10.0f});
    const int d = sk.addPoint({0.0f, 10.0f});
    sk.addLine(a, b); sk.addLine(b, c); sk.addLine(c, d); sk.addLine(d, a);
    ASSERT_EQ(1u, sk.buildWires().size());

    const int mid0 = sk.addPoint({5.0f, -2.0f});
    const int mid1 = sk.addPoint({5.0f, 12.0f});
    const int scaffold = sk.addLine(mid0, mid1);     // straight through the square
    sk.setConstruction(scaffold, true);
    EXPECT_EQ(1u, sk.buildWires().size()) << "scaffolding must not carve the profile in two";
}

// --- Round-trip -------------------------------------------------------------

TEST(SketchMirrorIO, GroupsSurviveASaveAndReload) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    ASSERT_EQ(1u, h->sk.getMirrors().size());
    const int imageOfTl = [&] {
        for (const auto& [src, dst] : h->sk.getMirrors()[0].points)
            if (src == h->tl) return dst;
        return -1;
    }();
    ASSERT_NE(-1, imageOfTl);

    std::ostringstream os;
    materializr::ProjectIO::writeSketchBody(os, h->sk);
    std::istringstream is(os.str());
    materializr::Sketch loaded;
    materializr::ProjectIO::parseSketchBody(is, loaded);

    ASSERT_EQ(1u, loaded.getMirrors().size()) << "the relation must survive the file";
    const auto& m = loaded.getMirrors()[0];
    EXPECT_EQ(h->sk.getMirrors()[0].axisLineId, m.axisLineId);
    EXPECT_EQ(h->sk.getMirrors()[0].points.size(), m.points.size());
    EXPECT_EQ(h->sk.getMirrors()[0].lines.size(), m.lines.size());
    EXPECT_TRUE(loaded.isDerived(imageOfTl));

    // And it is live, not just present: edit the source in the loaded copy.
    loaded.movePoint(h->tl, {-4.0f, 12.0f});
    materializr::SketchSolver solver;
    solver.solve(loaded);
    EXPECT_NEAR(4.0f, posOf(loaded, imageOfTl).x, 1e-4);
}

// Coordinates in the file are never trusted: load ends with a recompute, so a
// hand-edited or stale image is corrected rather than believed.
TEST(SketchMirrorIO, StaleImageCoordinatesAreCorrectedOnLoad) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const int image = h->sk.getMirrors()[0].points.front().second;
    const int source = h->sk.getMirrors()[0].points.front().first;

    std::ostringstream os;
    materializr::ProjectIO::writeSketchBody(os, h->sk);
    materializr::Sketch tampered;
    {
        std::istringstream is(os.str());
        materializr::ProjectIO::parseSketchBody(is, tampered);
    }
    tampered.movePoint(image, {999.0f, 999.0f});     // as if hand-edited in the file
    std::ostringstream os2;
    materializr::ProjectIO::writeSketchBody(os2, tampered);

    materializr::Sketch loaded;
    std::istringstream is2(os2.str());
    materializr::ProjectIO::parseSketchBody(is2, loaded);
    const glm::vec2 src = posOf(loaded, source);
    EXPECT_NEAR(-src.x, posOf(loaded, image).x, 1e-4) << "the image is recomputed, not read";
    EXPECT_NEAR( src.y, posOf(loaded, image).y, 1e-4);
}

// An old build's file has no MIRROR_COUNT at all. It must load as plain
// geometry, not fail — the degradation this feature promises.
TEST(SketchMirrorIO, AFileWithoutMirrorsLoadsAsPlainGeometry) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    std::ostringstream os;
    materializr::ProjectIO::writeSketchBody(os, h->sk);

    std::string text = os.str(), stripped;
    std::istringstream lines(text);
    for (std::string ln; std::getline(lines, ln); )
        if (ln.rfind("MIRROR_COUNT", 0) != 0 && ln.rfind("M ", 0) != 0 &&
            ln.rfind("MP ", 0) != 0 && ln.rfind("ME ", 0) != 0)
            stripped += ln + "\n";

    materializr::Sketch loaded;
    std::istringstream is(stripped);
    materializr::ProjectIO::parseSketchBody(is, loaded);
    EXPECT_TRUE(loaded.getMirrors().empty());
    EXPECT_EQ(h->sk.getPoints().size(), loaded.getPoints().size()) << "geometry must survive";
    for (const auto& p : loaded.getPoints())
        EXPECT_FALSE(p.derived) << "no group means nothing is derived";
}

// --- validation hardening: a group authorises locking and DELETING geometry,
// and it arrives from a file that may be corrupt. None of it is taken on trust.

namespace {
// A committed, valid mirror whose group can then be corrupted field by field.
std::unique_ptr<HalfProfile> committedMirror() {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    return h;
}
materializr::SketchMirror& groupOf(HalfProfile& h) {
    // The only mutable route is a rebuild: getMirrors() is const by design.
    return const_cast<materializr::SketchMirror&>(h.sk.getMirrors()[0]);
}
} // namespace

// The old check tested pair ids against ONE union of every entity id, so a
// line pair full of POINT ids passed and flagged those points derived.
TEST(SketchMirrorValidate, AnElementPairMustNameItsOwnEntityKind) {
    auto h = committedMirror();
    const int pA = h->tl, pB = h->bl;
    groupOf(*h).lines.push_back({pA, pB});          // two POINTS in the LINE pairs
    h->sk.validateMirrors();
    EXPECT_TRUE(h->sk.getMirrors().empty()) << "a mistyped pair must break the group";
    EXPECT_FALSE(h->sk.isDerived(pB)) << "and must not leave a point flagged derived";
}

// Two real lines whose endpoints do not correspond to the point mapping are not
// a reflection of anything; the group would lock geometry that never moves.
TEST(SketchMirrorValidate, ElementTopologyMustMatchThePointMapping) {
    Sketch sk;
    const int a = sk.addPoint({0.0f, -10.0f}), b = sk.addPoint({0.0f, 10.0f});
    const int axis = sk.addLine(a, b);
    const int p1 = sk.addPoint({ 4.0f, 1.0f}), p2 = sk.addPoint({ 6.0f, 8.0f});
    const int q1 = sk.addPoint({-4.0f, 1.0f}), q2 = sk.addPoint({-6.0f, 8.0f});
    const int src = sk.addLine(p1, p2);
    // The image's endpoints are REVERSED against the mapping. Geometrically the
    // same segment, so a laxer check waves it through — but recomputeMirrors
    // writes reflect(p1) into whichever point the pair says is p1's image, and
    // here that is q2. The mirror would tear itself apart on the next solve.
    const int bad = sk.addLine(q2, q1);
    materializr::SketchMirror m;
    m.axisLineId = axis;
    m.points = { {p1, q1}, {p2, q2} };     // every id distinct: no duplicate-owner shortcut
    m.lines  = { {src, bad} };
    sk.addMirror(m);
    sk.validateMirrors();
    EXPECT_TRUE(sk.getMirrors().empty()) << "mismatched topology must break the group";
}

// The arc swap is real: reflection reverses winding, so the image's start is
// the mapped source END. Validating without the swap rejects every valid arc.
TEST(SketchMirrorValidate, ArcTopologyIsCheckedWithTheWindingSwap) {
    Sketch sk;
    const int a = sk.addPoint({0.0f, -10.0f}), b = sk.addPoint({0.0f, 10.0f});
    const int axis = sk.addLine(a, b);
    const int ctr = sk.addPoint({5.0f, 0.0f});
    const int s0 = sk.addPoint({8.0f, 0.0f}), e0 = sk.addPoint({5.0f, 3.0f});
    const int arc0 = sk.addArc(ctr, s0, e0, 3.0);
    const int ctr1 = sk.addPoint({-5.0f, 0.0f});
    const int s1 = sk.addPoint({-8.0f, 0.0f}), e1 = sk.addPoint({-5.0f, 3.0f});
    const int arc1 = sk.addArc(ctr1, e1, s1, 3.0);     // start/end SWAPPED, as commitMirror does
    materializr::SketchMirror m;
    m.axisLineId = axis;
    m.points = { {ctr, ctr1}, {s0, s1}, {e0, e1} };
    m.arcs = { {arc0, arc1} };
    sk.addMirror(m);
    sk.validateMirrors();
    EXPECT_EQ(1u, sk.getMirrors().size()) << "a correctly swapped arc pair must survive";
}

// ...and the negative half, without which the check above is unpinned: an arc
// pair built WITHOUT the swap is not a reflection and must be rejected.
TEST(SketchMirrorValidate, AnArcPairBuiltWithoutTheSwapIsRejected) {
    Sketch sk;
    const int a = sk.addPoint({0.0f, -10.0f}), b = sk.addPoint({0.0f, 10.0f});
    const int axis = sk.addLine(a, b);
    const int ctr = sk.addPoint({5.0f, 0.0f});
    const int s0 = sk.addPoint({8.0f, 0.0f}), e0 = sk.addPoint({5.0f, 3.0f});
    const int arc0 = sk.addArc(ctr, s0, e0, 3.0);
    const int ctr1 = sk.addPoint({-5.0f, 0.0f});
    const int s1 = sk.addPoint({-8.0f, 0.0f}), e1 = sk.addPoint({-5.0f, 3.0f});
    const int arc1 = sk.addArc(ctr1, s1, e1, 3.0);    // NOT swapped
    materializr::SketchMirror m;
    m.axisLineId = axis;
    m.points = { {ctr, ctr1}, {s0, s1}, {e0, e1} };
    m.arcs = { {arc0, arc1} };
    sk.addMirror(m);
    sk.validateMirrors();
    EXPECT_TRUE(sk.getMirrors().empty()) << "an unswapped arc pair is not a reflection";
}

// sharedPoints authorises the identity mappings topology validation otherwise
// forbids, so an unvalidated list is a way to smuggle a corrupt group past it.
TEST(SketchMirrorValidate, ASharedPointMustActuallyLieOnTheAxis) {
    auto h = committedMirror();
    // A FRESH point, referenced by nothing and paired with nothing: the only
    // rule it can break is the on-axis one. Naming an existing paired source
    // here trips "a shared vertex may not also be paired" and leaves the
    // geometric check untested.
    const int stray = h->sk.addPoint({40.0f, 40.0f});
    groupOf(*h).sharedPoints.push_back(stray);
    h->sk.validateMirrors();
    EXPECT_TRUE(h->sk.getMirrors().empty()) << "an off-axis shared vertex must break the group";
}

// Ownership authorises DELETION. A bad claim is cleared, never honoured — but
// it must not destroy an otherwise sound relation either.
TEST(SketchMirrorValidate, AnUnprovableAxisClaimIsClearedNotObeyed) {
    auto h = committedMirror();
    auto& mir = groupOf(*h);
    ASSERT_TRUE(mir.axisGenerated);
    // Attach unrelated geometry to an axis endpoint: the axis is no longer
    // exclusively this group's, so the claim cannot be proven.
    const auto& lines = h->sk.getLines();
    int axisStart = -1;
    for (const auto& l : lines) if (l.id == mir.axisLineId) axisStart = l.startPointId;
    ASSERT_NE(-1, axisStart);
    h->sk.addLine(axisStart, h->sk.addPoint({40.0f, 40.0f}));
    h->sk.validateMirrors();
    ASSERT_EQ(1u, h->sk.getMirrors().size()) << "the relation itself is still sound";
    EXPECT_FALSE(h->sk.getMirrors()[0].axisGenerated) << "but it no longer owns the axis";
}

TEST(SketchMirrorValidate, APinClaimOnSomeoneElsesConstraintIsCleared) {
    auto h = committedMirror();
    // Shaped EXACTLY like a real pin — right type, zero value, right axis — and
    // differing only in naming a point that is not shared. Anything cruder is
    // caught by the type check and leaves entityA untested.
    materializr::Constraint c{};
    c.type = materializr::ConstraintType::DistancePointLine;
    c.entityA = h->tl;                                // a real point, but not on the axis
    c.entityB = h->sk.getMirrors()[0].axisLineId;
    c.value = 0.0;
    const int cid = h->sk.addConstraint(c);
    ASSERT_NE(-1, cid);
    groupOf(*h).pinConstraints.push_back(cid);
    h->sk.validateMirrors();
    ASSERT_EQ(1u, h->sk.getMirrors().size());
    for (int pid : h->sk.getMirrors()[0].pinConstraints)
        EXPECT_NE(cid, pid) << "a group must not claim a constraint it did not create";
}

// Files written by 5d56b20/0f2fe0a contain shared vertices and no field naming
// them. Without inference this plan would reject sketches the shipped build saves.
TEST(SketchMirrorValidate, LegacyGroupsWithoutSharedPointsAreMigrated) {
    auto h = committedMirror();
    auto& mir = groupOf(*h);
    ASSERT_FALSE(mir.sharedPoints.empty()) << "the fixture must have shared vertices";
    const std::size_t had = mir.sharedPoints.size();
    // Exactly what an old file loads as: no shared rows AND no ownership
    // columns. Clearing only the first would simulate a v2 record that misstated
    // itself, which is a corrupt file, not a legacy one — and must NOT migrate.
    mir.sharedPoints.clear();
    mir.ownershipDeclared = false;
    h->sk.validateMirrors();
    ASSERT_EQ(1u, h->sk.getMirrors().size()) << "a legacy group must survive, not be dropped";
    EXPECT_EQ(had, h->sk.getMirrors()[0].sharedPoints.size()) << "and its shared set restored";
}

// A source is checked against what the groups CLAIM, not against the `derived`
// flag — pass 2 writes that flag, so during validation it is stale, and on a
// fresh load it is simply false. These two shapes slipped through unnoticed.
TEST(SketchMirrorValidate, AChainOfGroupsIsRefused) {
    Sketch sk;
    const int a = sk.addPoint({0.0f, -10.0f}), b = sk.addPoint({0.0f, 10.0f});
    const int axis = sk.addLine(a, b);
    const int p = sk.addPoint({3.0f, 4.0f});
    const int q = sk.addPoint({-3.0f, 4.0f});     // image of p
    const int r = sk.addPoint({7.0f, 4.0f});      // would be the image OF THE IMAGE
    materializr::SketchMirror g1; g1.axisLineId = axis; g1.points = { {p, q} };
    sk.addMirror(g1);
    materializr::SketchMirror g2; g2.axisLineId = axis; g2.points = { {q, r} };
    sk.addMirror(g2);
    sk.validateMirrors();
    ASSERT_EQ(1u, sk.getMirrors().size()) << "the chained group must be dropped";
    EXPECT_EQ(p, sk.getMirrors()[0].points[0].first) << "and the sound one kept";
    EXPECT_FALSE(sk.isDerived(r)) << "the chained image must not stay flagged";
}

TEST(SketchMirrorValidate, ACycleBetweenGroupsIsRefused) {
    Sketch sk;
    const int a = sk.addPoint({0.0f, -10.0f}), b = sk.addPoint({0.0f, 10.0f});
    const int axis = sk.addLine(a, b);
    const int p = sk.addPoint({3.0f, 4.0f});
    const int q = sk.addPoint({-3.0f, 4.0f});
    // p is q's image and q is p's image: each group's source is the other's
    // output, so the recompute would have no ordering that converges.
    materializr::SketchMirror g1; g1.axisLineId = axis; g1.points = { {p, q} };
    sk.addMirror(g1);
    materializr::SketchMirror g2; g2.axisLineId = axis; g2.points = { {q, p} };
    sk.addMirror(g2);
    sk.validateMirrors();
    EXPECT_LE(sk.getMirrors().size(), 1u) << "a cycle must not survive intact";
    for (const auto& mir : sk.getMirrors())
        for (const auto& [src, dst] : mir.points)
            EXPECT_FALSE(sk.isDerived(src)) << "no surviving source may also be an image";
}

// --- serialisation v2: compatibility in BOTH directions, and recovery from a
// corrupt file. The grammar has changed twice; assertions replace assumption.

namespace {
// Round-trips a sketch through the writer, optionally mangling the text first.
materializr::Sketch reloadWith(const Sketch& src,
                               const std::function<std::string(std::string)>& mangle) {
    std::ostringstream os;
    materializr::ProjectIO::writeSketchBody(os, src);
    std::istringstream is(mangle ? mangle(os.str()) : os.str());
    materializr::Sketch out;
    materializr::ProjectIO::parseSketchBody(is, out);
    return out;
}
std::string dropRows(std::string text, const char* token) {
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(token, 0) != 0) out << line << "\n";
    return out.str();
}
} // namespace

TEST(SketchMirrorIO, OwnershipAndSharedVerticesRoundTrip) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& before = h->sk.getMirrors()[0];
    ASSERT_TRUE(before.axisGenerated);
    ASSERT_FALSE(before.sharedPoints.empty());
    ASSERT_FALSE(before.pinConstraints.empty());

    materializr::Sketch loaded = reloadWith(h->sk, nullptr);
    ASSERT_EQ(1u, loaded.getMirrors().size());
    const auto& after = loaded.getMirrors()[0];
    EXPECT_TRUE(after.axisGenerated) << "the axis claim must survive the file";
    EXPECT_EQ(before.sharedPoints.size(), after.sharedPoints.size());
    EXPECT_EQ(before.pinConstraints.size(), after.pinConstraints.size());
}

// An OLD file: no MS rows, no MC rows, no trailing M fields. It must still load
// as a working mirror, with the shared set and the pins inferred back.
TEST(SketchMirrorIO, AFileFromBeforeTheOwnershipFieldsStillLoads) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const std::size_t sharedHad = h->sk.getMirrors()[0].sharedPoints.size();

    materializr::Sketch loaded = reloadWith(h->sk, [](std::string t) {
        t = dropRows(std::move(t), "MC ");
        t = dropRows(std::move(t), "MS ");
        // Rewrite the M row back to its 4-field form, counts included.
        std::istringstream in(t); std::ostringstream out; std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("M ", 0) == 0) {
                std::istringstream ms(line); std::string tok;
                int id, axis, nPts, nElems;
                ms >> tok >> id >> axis >> nPts >> nElems;
                out << "M " << id << " " << axis << " " << nPts << " " << nElems << "\n";
            } else out << line << "\n";
        }
        return out.str();
    });

    ASSERT_EQ(1u, loaded.getMirrors().size()) << "a legacy file must not lose its relation";
    EXPECT_EQ(sharedHad, loaded.getMirrors()[0].sharedPoints.size())
        << "the shared set must be inferred back";
    EXPECT_FALSE(loaded.getMirrors()[0].pinConstraints.empty())
        << "and the pins reclaimed, or Break link would leave the vertex welded";
}

// The desync P8d names: an inflated count must not eat the record that follows.
// Driven by a count, the parser consumes the next line as a failed child row and
// that record is then never parsed — silent geometry loss from a corrupt file.
TEST(SketchMirrorIO, AnInflatedCountDoesNotSwallowTheNextRecord) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const std::size_t pointsBefore = h->sk.getPoints().size();

    materializr::Sketch loaded = reloadWith(h->sk, [](std::string t) {
        std::istringstream in(t); std::ostringstream out; std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("M ", 0) == 0) {
                std::istringstream ms(line); std::string tok;
                int id, axis, nPts, nElems, gen, nPins, nShared;
                ms >> tok >> id >> axis >> nPts >> nElems >> gen >> nPins >> nShared;
                out << "M " << id << " " << axis << " " << (nPts + 5) << " "
                    << nElems << " " << gen << " " << nPins << " " << nShared << "\n";
            } else out << line << "\n";
        }
        return out.str();
    });

    EXPECT_EQ(pointsBefore, loaded.getPoints().size())
        << "every point must still load — the corrupt mirror must not eat geometry";
    EXPECT_TRUE(loaded.getMirrors().empty())
        << "and the truncated relation itself must be dropped";
}

// A wrong OWNERSHIP count is not a broken relation: clear the claim, keep the
// group. Nothing is deleted on either path.
TEST(SketchMirrorIO, AWrongPinCountClearsTheClaimButKeepsTheGroup) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    materializr::Sketch loaded = reloadWith(h->sk, [](std::string t) {
        return dropRows(std::move(t), "MC ");   // rows gone, count still says otherwise
    });
    ASSERT_EQ(1u, loaded.getMirrors().size()) << "the relation is intact, so it survives";
    // And the claim must STAY cleared. An earlier version of this test asserted
    // only that the group survived, which passed while the legacy inference
    // quietly handed the deletion rights back — a corrupt record could still
    // authorise Delete mirror to remove constraints.
    EXPECT_TRUE(loaded.getMirrors()[0].pinConstraints.empty())
        << "a record whose ownership rows did not back its count owns nothing";
    EXPECT_FALSE(loaded.getMirrors()[0].axisGenerated)
        << "and that includes the axis claim, which names no rows of its own";
    EXPECT_EQ(2u, loaded.getConstraints().size()) << "and nothing was deleted";
}

// The desync only bites when a REAL record follows the mirror block, and the
// writer emits mirrors last — so a round-trip alone can never catch it. This
// fixture puts a point record after the group, which is exactly the shape a
// hand-edited or future-format file has.
TEST(SketchMirrorIO, ARecordAfterTheMirrorBlockIsNotSwallowed) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    ASSERT_EQ(2u, h->sk.getConstraints().size()) << "the pins are the payload we move";

    // The writer emits mirrors LAST, so a plain round-trip can never expose the
    // desync — the only line after the group is SKETCH_END, and losing that
    // changes nothing. Move the constraint block to AFTER the mirror block, so
    // a swallowed line costs something observable. That is the shape a
    // hand-edited or reordered file has.
    materializr::Sketch loaded = reloadWith(h->sk, [](std::string t) {
        std::istringstream in(t); std::vector<std::string> rows; std::string line;
        while (std::getline(in, line)) rows.push_back(line);
        std::vector<std::string> block, rest;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].rfind("CONSTRAINT_COUNT", 0) == 0) {
                block.push_back(rows[i]);
                while (i + 1 < rows.size() && rows[i + 1].rfind("K ", 0) == 0)
                    block.push_back(rows[++i]);
            } else rest.push_back(rows[i]);
        }
        std::size_t lastChild = 0;
        for (std::size_t i = 0; i < rest.size(); ++i) {
            const std::string& r = rest[i];
            if (r.rfind("MP ", 0) == 0 || r.rfind("ME ", 0) == 0 ||
                r.rfind("MC ", 0) == 0 || r.rfind("MS ", 0) == 0) lastChild = i;
        }
        std::ostringstream out;
        for (std::size_t i = 0; i < rest.size(); ++i) {
            out << rest[i] << "\n";
            if (i == lastChild) for (const auto& b : block) out << b << "\n";
        }
        return out.str();
    });

    EXPECT_EQ(2u, loaded.getConstraints().size())
        << "the record after the mirror block must still be parsed, not swallowed";
    EXPECT_EQ(1u, loaded.getMirrors().size()) << "and the group itself must survive";
}

// A DECLARED record whose MS rows disagree with its count. The question is not
// whether the group survives — it is whether anything the record failed to
// state correctly can still authorise a deletion.
TEST(SketchMirrorIO, AWrongSharedCountRestoresNoOwnership) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    ASSERT_FALSE(h->sk.getMirrors()[0].pinConstraints.empty());
    const std::size_t constraintsBefore = h->sk.getConstraints().size();

    materializr::Sketch loaded = reloadWith(h->sk, [](std::string t) {
        return dropRows(std::move(t), "MS ");   // rows gone, count still declares them
    });

    EXPECT_TRUE(loaded.getMirrors().empty() ||
                loaded.getMirrors()[0].pinConstraints.empty())
        << "a record that misstated its ownership must own nothing";
    EXPECT_EQ(constraintsBefore, loaded.getConstraints().size())
        << "and nothing may be deleted on this path";
    for (const auto& mir : loaded.getMirrors())
        EXPECT_FALSE(mir.axisGenerated)
            << "nor may it keep the right to delete the axis";
}

// Codex: an MS mismatch can still retain axis-deletion ownership.
//
// The obvious fixture — drop an MS row from the half-profile — is VACUOUS: the
// shared set is cleared, its identity mappings become unauthorised, and the
// group is dropped before any ownership question arises. The hole needs a group
// that SURVIVES the mismatch, which means one with no shared vertices at all:
// clearing an already-empty list changes nothing, topology is fine, and the
// record keeps the axis claim it just proved it cannot state correctly.
TEST(SketchMirrorIO, AnMSMismatchNeverKeepsTheAxisClaim) {
    auto h = makeHalfProfile();
    ASSERT_TRUE(h->tool.beginMirror());
    h->tool.setMirrorAnchor({-40.0f, 5.0f});          // clear of every source point
    h->tool.setMirrorAngle(static_cast<float>(M_PI) * 0.5f);
    h->tool.commitMirror(h->outPts, h->outLines);
    ASSERT_EQ(1u, h->sk.getMirrors().size());
    ASSERT_TRUE(h->sk.getMirrors()[0].sharedPoints.empty()) << "no vertex is on this axis";
    ASSERT_TRUE(h->sk.getMirrors()[0].axisGenerated);
    const std::size_t constraintsBefore = h->sk.getConstraints().size();

    // Declare shared rows the record does not have.
    materializr::Sketch loaded = reloadWith(h->sk, [](std::string t) {
        std::istringstream in(t); std::ostringstream out; std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("M ", 0) == 0) {
                std::istringstream ms(line); std::string tok;
                int id, axis, nPts, nElems, gen, nPins, nShared;
                ms >> tok >> id >> axis >> nPts >> nElems >> gen >> nPins >> nShared;
                out << "M " << id << " " << axis << " " << nPts << " " << nElems
                    << " " << gen << " " << nPins << " " << (nShared + 3) << "\n";
            } else out << line << "\n";
        }
        return out.str();
    });

    ASSERT_FALSE(loaded.getMirrors().empty())
        << "VACUOUS otherwise: this group must survive for ownership to matter";
    for (const auto& mir : loaded.getMirrors()) {
        EXPECT_FALSE(mir.axisGenerated)
            << "a record that misstated its ownership block may not delete the axis";
        EXPECT_TRUE(mir.pinConstraints.empty()) << "nor keep its pin claims";
    }
    EXPECT_EQ(constraintsBefore, loaded.getConstraints().size())
        << "and nothing may be deleted";
}

// Codex: a TRUNCATED ownership block can still retain axis-deletion rights.
// The counts only catch a disagreement between a number and its rows. A record
// that stops after axisGenerated declares no counts at all, so both default to
// zero, both "match" the zero rows present, and nothing is ever cleared.
TEST(SketchMirrorIO, ATruncatedOwnershipBlockKeepsNoDeletionRights) {
    auto h = makeHalfProfile();
    ASSERT_TRUE(h->tool.beginMirror());
    h->tool.setMirrorAnchor({-40.0f, 5.0f});          // no vertex lands on the axis
    h->tool.setMirrorAngle(static_cast<float>(M_PI) * 0.5f);
    h->tool.commitMirror(h->outPts, h->outLines);
    ASSERT_EQ(1u, h->sk.getMirrors().size());
    ASSERT_TRUE(h->sk.getMirrors()[0].axisGenerated);

    materializr::Sketch loaded = reloadWith(h->sk, [](std::string t) {
        t = dropRows(std::move(t), "MC ");
        t = dropRows(std::move(t), "MS ");
        std::istringstream in(t); std::ostringstream out; std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("M ", 0) == 0) {
                std::istringstream ms(line); std::string tok;
                int id, axis, nPts, nElems, gen;
                ms >> tok >> id >> axis >> nPts >> nElems >> gen;
                // Stops after axisGenerated: the ownership block is begun and
                // never finished.
                out << "M " << id << " " << axis << " " << nPts << " "
                    << nElems << " " << gen << "\n";
            } else out << line << "\n";
        }
        return out.str();
    });

    ASSERT_FALSE(loaded.getMirrors().empty())
        << "VACUOUS otherwise: this group must survive for ownership to matter";
    for (const auto& mir : loaded.getMirrors())
        EXPECT_FALSE(mir.axisGenerated)
            << "an unfinished ownership block may not carry a deletion right";
}

// --- deletion algebra ------------------------------------------------------
// Every fixture up to here mirrors LINES, whose endpoints are always referenced,
// so pruneOrphanPoints never touches them and a cascade test built on that
// helper passes green with the bug fully present. A mirrored STANDALONE point is
// referenced by no element BY DESIGN — which is exactly what the orphan sweep
// deletes. It runs from 8 call sites including the generic delete path.
TEST(SketchMirrorDelete, PruningDoesNotEatAMirroredStandalonePoint) {
    Sketch sk;
    materializr::SketchTool tool;
    tool.setSketch(&sk);
    const int src = sk.addPoint({5.0f, 3.0f});
    ASSERT_TRUE(tool.beginMirror());
    tool.setMirrorAnchor({0.0f, 0.0f});
    tool.setMirrorAngle(static_cast<float>(M_PI) * 0.5f);
    std::set<int> pts, lines;
    tool.commitMirror(pts, lines);

    ASSERT_EQ(1u, sk.getMirrors().size());
    ASSERT_FALSE(sk.getMirrors()[0].points.empty()) << "VACUOUS otherwise";
    const int image = sk.getMirrors()[0].points.front().second;

    sk.pruneOrphanPoints();

    EXPECT_NE(nullptr, sk.getPoint(src))   << "the source point must survive pruning";
    EXPECT_NE(nullptr, sk.getPoint(image)) << "and so must its image";
    EXPECT_EQ(1u, sk.getMirrors().size())  << "the relation must survive too";
}

// The teardown table: only explicit Delete mirror removes geometry.
TEST(SketchMirrorDelete, BreakLinkKeepsEveryPieceOfGeometryAndDropsThePins) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& mir = h->sk.getMirrors()[0];
    ASSERT_FALSE(mir.pinConstraints.empty()) << "VACUOUS otherwise";
    const int mirrorId = mir.id;
    const int axisId = mir.axisLineId;
    const std::size_t pts = h->sk.getPoints().size(), lns = h->sk.getLines().size();
    const int sharedPt = mir.sharedPoints.front();

    ASSERT_TRUE(h->sk.breakMirrorLink(mirrorId));

    EXPECT_EQ(pts, h->sk.getPoints().size()) << "Break link deletes no geometry";
    EXPECT_EQ(lns, h->sk.getLines().size()) << "the axis stays too";
    for (const auto& l : h->sk.getLines()) if (l.id == axisId) SUCCEED();
    for (const auto& c : h->sk.getConstraints())
        EXPECT_NE(materializr::ConstraintType::DistancePointLine, c.type)
            << "a pin left behind would weld the vertex to a meaningless line";
    // Released geometry must be free: still pinned, the vertex could not move.
    h->sk.movePoint(sharedPt, {3.0f, 3.0f});
    materializr::SketchSolver solver;
    solver.solve(h->sk);
    EXPECT_NEAR(3.0f, posOf(h->sk, sharedPt).x, 1e-3) << "the vertex must be free now";
}

TEST(SketchMirrorDelete, DeleteMirrorRemovesTheImageAndItsOwnAxisButNoSource) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& mir = h->sk.getMirrors()[0];
    ASSERT_TRUE(mir.axisGenerated) << "VACUOUS otherwise";
    const int mirrorId = mir.id, axisId = mir.axisLineId;
    const int imageLine = mir.lines.front().second;
    const int sourceLine = mir.lines.front().first;
    const int imagePoint = mir.points.front().second;
    const int sourcePoint = mir.points.front().first;

    ASSERT_TRUE(h->sk.deleteMirror(mirrorId));

    EXPECT_TRUE(h->sk.getMirrors().empty());
    for (const auto& l : h->sk.getLines()) {
        EXPECT_NE(imageLine, l.id) << "the image must go";
        EXPECT_NE(axisId, l.id)    << "and the axis it made";
    }
    EXPECT_EQ(nullptr, h->sk.getPoint(imagePoint)) << "and the image's points";
    EXPECT_NE(nullptr, h->sk.getPoint(sourcePoint)) << "but never a source point";
    bool srcAlive = false;
    for (const auto& l : h->sk.getLines()) if (l.id == sourceLine) srcAlive = true;
    EXPECT_TRUE(srcAlive) << "nor a source element";
    for (const auto& c : h->sk.getConstraints())
        EXPECT_NE(materializr::ConstraintType::DistancePointLine, c.type) << "pins go too";
}

// An unproven claim retains. Delete mirror must not remove a line the user drew.
TEST(SketchMirrorDelete, DeleteMirrorKeepsAnAxisItCannotProveItOwns) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const int axisId = h->sk.getMirrors()[0].axisLineId;
    int axisStart = -1;
    for (const auto& l : h->sk.getLines()) if (l.id == axisId) axisStart = l.startPointId;
    ASSERT_NE(-1, axisStart);
    h->sk.addLine(axisStart, h->sk.addPoint({40.0f, 40.0f}));   // provenance now unprovable
    h->sk.validateMirrors();
    ASSERT_FALSE(h->sk.getMirrors().empty());
    ASSERT_FALSE(h->sk.getMirrors()[0].axisGenerated) << "VACUOUS otherwise";

    ASSERT_TRUE(h->sk.deleteMirror(h->sk.getMirrors()[0].id));
    bool axisAlive = false;
    for (const auto& l : h->sk.getLines()) if (l.id == axisId) axisAlive = true;
    EXPECT_TRUE(axisAlive) << "an axis it cannot prove it owns must be retained";
}

// The endpoint guard is not redundant with validation. validateMirrors proves
// the axis is exclusively the group's AT THAT MOMENT; deleteMirror can be
// reached later with a stale claim, after geometry has attached to an endpoint.
// Re-checking at deletion time is what stops it taking a user's line with it.
TEST(SketchMirrorDelete, AxisEndpointsSurviveIfSomethingElseGrewOntoThem) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& mir = h->sk.getMirrors()[0];
    ASSERT_TRUE(mir.axisGenerated) << "VACUOUS otherwise";
    const int mirrorId = mir.id, axisId = mir.axisLineId;
    int axisStart = -1;
    for (const auto& l : h->sk.getLines()) if (l.id == axisId) axisStart = l.startPointId;
    ASSERT_NE(-1, axisStart);

    // Attach a user line to the endpoint and DO NOT re-validate: the claim is
    // now stale, exactly as it would be mid-edit.
    const int far = h->sk.addPoint({40.0f, 40.0f});
    const int userLine = h->sk.addLine(axisStart, far);

    ASSERT_TRUE(h->sk.deleteMirror(mirrorId));

    EXPECT_NE(nullptr, h->sk.getPoint(axisStart))
        << "an endpoint another element uses must not be deleted";
    bool userLineAlive = false;
    for (const auto& l : h->sk.getLines()) if (l.id == userLine) userLineAlive = true;
    EXPECT_TRUE(userLineAlive) << "and the user's line must still be whole";
    EXPECT_NE(nullptr, h->sk.getPoint(far));
}

// Deleting the image must not strand geometry the USER built on top of it. A
// line drawn to an image point, or a reference dimension measuring it, are both
// legal — reference dimensions on derived geometry are explicitly permitted —
// and neither may be left pointing at an id that no longer exists.
TEST(SketchMirrorDelete, DeleteMirrorLeavesNoDanglingUserGeometry) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& mir = h->sk.getMirrors()[0];
    const int mirrorId = mir.id;
    const int imagePoint = mir.points.front().second;

    // The user attaches their own line to the image, and measures it.
    const int far = h->sk.addPoint({40.0f, 40.0f});
    const int userLine = h->sk.addLine(imagePoint, far);
    materializr::Constraint ref{};
    ref.type = materializr::ConstraintType::Distance;
    ref.entityA = imagePoint;
    ref.entityB = far;
    ref.isDriving = false;                       // reference only: allowed on an image
    const int refId = h->sk.addConstraint(ref);
    ASSERT_NE(-1, refId) << "VACUOUS otherwise: a reference dimension must be accepted";

    ASSERT_TRUE(h->sk.deleteMirror(mirrorId));

    // The user's line is still whole, so its endpoint must still exist.
    bool userLineAlive = false;
    for (const auto& l : h->sk.getLines()) if (l.id == userLine) userLineAlive = true;
    ASSERT_TRUE(userLineAlive) << "the user's line must survive";
    EXPECT_NE(nullptr, h->sk.getPoint(imagePoint))
        << "a point the user's geometry still uses must not be deleted under it";

    // And nothing may reference an id that is gone.
    std::unordered_set<int> live;
    for (const auto& p : h->sk.getPoints())  live.insert(p.id);
    for (const auto& l : h->sk.getLines())   live.insert(l.id);
    for (const auto& c : h->sk.getCircles()) live.insert(c.id);
    for (const auto& a : h->sk.getArcs())    live.insert(a.id);
    for (const auto& s : h->sk.getSplines()) live.insert(s.id);
    for (const auto& c : h->sk.getConstraints()) {
        if (c.entityA >= 0) EXPECT_TRUE(live.count(c.entityA)) << "dangling entityA";
        if (c.entityB >= 0) EXPECT_TRUE(live.count(c.entityB)) << "dangling entityB";
    }
    for (const auto& l : h->sk.getLines()) {
        EXPECT_NE(nullptr, h->sk.getPoint(l.startPointId)) << "line with a missing start";
        EXPECT_NE(nullptr, h->sk.getPoint(l.endPointId))   << "line with a missing end";
    }
}

// The constraint sweep, exercised for real: a dimension between two image
// points that nothing else uses. Both points go, so the dimension must go with
// them. The previous fixture measured points the user's own line kept alive, so
// the sweep never ran and the rule was unpinned.
TEST(SketchMirrorDelete, ADimensionOnDeletedImageGeometryIsRemovedNotStranded) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& mir = h->sk.getMirrors()[0];
    ASSERT_GE(mir.points.size(), 2u) << "VACUOUS otherwise";
    const int mirrorId = mir.id;
    const int imgA = mir.points[0].second, imgB = mir.points[1].second;

    materializr::Constraint ref{};
    ref.type = materializr::ConstraintType::Distance;
    ref.entityA = imgA;
    ref.entityB = imgB;
    ref.isDriving = false;
    const int refId = h->sk.addConstraint(ref);
    ASSERT_NE(-1, refId) << "VACUOUS otherwise";

    ASSERT_TRUE(h->sk.deleteMirror(mirrorId));

    EXPECT_EQ(nullptr, h->sk.getPoint(imgA)) << "nothing else used it, so it goes";
    for (const auto& c : h->sk.getConstraints())
        EXPECT_NE(refId, c.id) << "its dimension must not outlive the geometry";
}

// And the converse: a dimension whose geometry was RETAINED must be retained.
// Sweeping with the pre-retention set would delete the user's dimension while
// the point it measures is still on screen.
TEST(SketchMirrorDelete, ADimensionOnARetainedImagePointSurvives) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& mir = h->sk.getMirrors()[0];
    const int mirrorId = mir.id;
    const int imagePoint = mir.points.front().second;

    const int far = h->sk.addPoint({40.0f, 40.0f});
    h->sk.addLine(imagePoint, far);              // keeps the image point alive
    materializr::Constraint ref{};
    ref.type = materializr::ConstraintType::Distance;
    ref.entityA = imagePoint;
    ref.entityB = far;
    ref.isDriving = false;
    const int refId = h->sk.addConstraint(ref);
    ASSERT_NE(-1, refId);

    ASSERT_TRUE(h->sk.deleteMirror(mirrorId));

    ASSERT_NE(nullptr, h->sk.getPoint(imagePoint)) << "VACUOUS otherwise";
    bool kept = false;
    for (const auto& c : h->sk.getConstraints()) if (c.id == refId) kept = true;
    EXPECT_TRUE(kept) << "a dimension whose geometry survived must survive too";
}

// --- the per-pair cascade --------------------------------------------------
// removeElement currently breaks the WHOLE group when either half of any pair
// goes: safe, but it means deleting one line of a ten-line mirror silently
// unlinks the other nine. Deleting a source should take its image with it and
// leave the rest of the relation standing.
TEST(SketchMirrorDelete, DeletingOneSourceElementRemovesOnlyItsImage) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const auto& mir = h->sk.getMirrors()[0];
    ASSERT_EQ(3u, mir.lines.size()) << "VACUOUS otherwise: need survivors to check";
    const int srcLine = mir.lines[0].first, imgLine = mir.lines[0].second;
    const int otherSrc = mir.lines[1].first, otherImg = mir.lines[1].second;

    h->sk.removeElement(srcLine);

    ASSERT_EQ(1u, h->sk.getMirrors().size()) << "the group must survive the loss of one pair";
    for (const auto& l : h->sk.getLines()) {
        EXPECT_NE(srcLine, l.id);
        EXPECT_NE(imgLine, l.id) << "the image must follow its source";
    }
    bool otherSrcAlive = false, otherImgAlive = false;
    for (const auto& l : h->sk.getLines()) {
        if (l.id == otherSrc) otherSrcAlive = true;
        if (l.id == otherImg) otherImgAlive = true;
    }
    EXPECT_TRUE(otherSrcAlive) << "the rest of the relation stands";
    EXPECT_TRUE(otherImgAlive);
    EXPECT_TRUE(h->sk.isDerived(otherImg)) << "and its survivors stay derived";
    for (const auto& [s, d] : h->sk.getMirrors()[0].lines)
        EXPECT_NE(srcLine, s) << "the dead pair must be gone from the group";
}

// Deleting an image directly is refused: the recompute would recreate it on the
// next solve, so allowing it is a lie. Break link and Delete mirror are the ways
// to get rid of one.
TEST(SketchMirrorDelete, DeletingADerivedElementDirectlyIsRefused) {
    auto h = makeHalfProfile();
    commitVerticalMirrorAtX0(*h);
    const int imgLine = h->sk.getMirrors()[0].lines[0].second;
    ASSERT_TRUE(h->sk.isDerived(imgLine)) << "VACUOUS otherwise";

    h->sk.removeElement(imgLine);

    bool stillThere = false;
    for (const auto& l : h->sk.getLines()) if (l.id == imgLine) stillThere = true;
    EXPECT_TRUE(stillThere) << "an image may not be deleted out from under its relation";
    EXPECT_EQ(1u, h->sk.getMirrors().size()) << "and the group is untouched";
}
