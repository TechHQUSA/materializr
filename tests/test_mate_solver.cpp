// A Fasten mate places bodyB relative to bodyA. Solving is a topological walk
// from the grounded body; it is not iterative and has no residual.

#include "core/Document.h"
#include "modeling/Mate.h"
#include "modeling/MateSolver.h"
#include "modeling/FaceAnchor.h"

#include <gtest/gtest.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax3.hxx>
#include <cmath>

using materializr::Mate;
using materializr::MateType;
using materializr::MateSolver;

namespace {

// Minimum corner of a body's bounding box - enough to prove a placement moved.
gp_Pnt minCorner(const Document& doc, int bodyId) {
    Bnd_Box box;
    BRepBndLib::Add(doc.getBody(bodyId), box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return gp_Pnt(xmin, ymin, zmin);
}

Mate fasten(int a, int b, double offset) {
    Mate m{};
    m.type = MateType::Fasten;
    m.bodyA = a;
    m.bodyB = b;
    m.offset = offset;
    return m;
}

} // namespace

TEST(MateSolver, FastenPlacesBodyBAtTheOffsetFromBodyA) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    MateSolver solver;
    auto r = solver.solve(doc);
    ASSERT_TRUE(r.ok) << r.error;

    EXPECT_NEAR(minCorner(doc, b).X(), 25.0, 1e-6);
    // The grounded body must not have moved.
    EXPECT_NEAR(minCorner(doc, a).X(), 0.0, 1e-6);
}

TEST(MateSolver, SolvingTwiceIsIdempotent) {
    // The arc-radius refresh removed in August 2026 failed exactly this: it
    // re-derived a value from geometry it had just written, so every solve
    // moved the model again. A placement solve must be a function of the base
    // geometry, never of the last solve's output.
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    MateSolver solver;
    ASSERT_TRUE(solver.solve(doc).ok);
    gp_Pnt first = minCorner(doc, b);
    ASSERT_TRUE(solver.solve(doc).ok);
    gp_Pnt second = minCorner(doc, b);

    EXPECT_NEAR(first.X(), second.X(), 1e-9);
    EXPECT_NEAR(first.Y(), second.Y(), 1e-9);
    EXPECT_NEAR(first.Z(), second.Z(), 1e-9);
}

TEST(MateSolver, ChainedFastenComposesThroughTheMiddleBody) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    int c = doc.addBody(BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape(), "C");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));
    doc.addMate(fasten(b, c, 10.0));

    MateSolver solver;
    ASSERT_TRUE(solver.solve(doc).ok);
    EXPECT_NEAR(minCorner(doc, c).X(), 35.0, 1e-6);
}

TEST(MateSolver, SuppressedMateDoesNotPlace) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    Mate m = fasten(a, b, 25.0);
    m.suppressed = true;
    doc.addMate(m);

    MateSolver solver;
    ASSERT_TRUE(solver.solve(doc).ok);
    EXPECT_NEAR(minCorner(doc, b).X(), 0.0, 1e-6);
}

TEST(MateSolver, TwoMatesOnOneBodyIsAnError) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    int c = doc.addBody(BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape(), "C");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));
    doc.addMate(fasten(c, b, 5.0));   // b placed twice

    MateSolver solver;
    auto r = solver.solve(doc);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(MateSolver, GroundedBodyPlacedByAMateIsAnError) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));
    doc.addMate(fasten(b, a, 25.0));

    MateSolver solver;
    auto r = solver.solve(doc);
    EXPECT_FALSE(r.ok);
}

TEST(MateSolver, UngroundedCycleIsAnError) {
    // The previous test is really "grounded body also placed by a mate". This
    // one has no grounded body at all, so the walk has to detect the loop
    // itself rather than tripping the grounding check.
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    int c = doc.addBody(BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape(), "C");
    // No setGroundedBody: a -> b -> c -> a is a pure loop.
    doc.addMate(fasten(a, b, 5.0));
    doc.addMate(fasten(b, c, 5.0));
    doc.addMate(fasten(c, a, 5.0));

    MateSolver solver;
    auto r = solver.solve(doc);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("cycle"), std::string::npos) << r.error;
}

#include "modeling/Sketch.h"

TEST(MateSolver, SketchAnchoredToAMatedBodyTravelsWithIt) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);

    // A sketch anchored to body B. getSourceBody() is the link TransformOp
    // follows when it carries sketches along with a moved body.
    auto sk = std::make_shared<materializr::Sketch>();
    sk->setPlane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
    sk->setSourceBody(b);
    int sketchId = doc.addSketch(sk, "S");

    doc.addMate(fasten(a, b, 25.0));

    MateSolver solver;
    ASSERT_TRUE(solver.solve(doc).ok);

    gp_Pln moved = doc.getSketch(sketchId)->getPlane();
    EXPECT_NEAR(moved.Location().X(), 25.0, 1e-6)
        << "sketch stayed behind while its body moved";
}

#include "modeling/TransformOp.h"

TEST(MateSolver, TransformOnAMateOwnedBodyIsRefused) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    // Moving B directly would be undone by the next solve, so it is refused
    // rather than silently reverted.
    TransformOp op;
    op.setBodyId(b);
    op.setTranslation(1.0, 0.0, 0.0);
    EXPECT_FALSE(op.execute(doc));

    // A body no mate places is unaffected.
    TransformOp opA;
    opA.setBodyId(a);
    opA.setTranslation(1.0, 0.0, 0.0);
    EXPECT_TRUE(opA.execute(doc));
}

// ---- Frame alignment: the shared core of Concentric and Planar ----
//
// Driven directly rather than through anchors, so the geometry maths is
// pinned independently of face resolution. What differs between the mate
// types is only which frame the anchor yields, never how two frames are
// brought together.

TEST(MateAlign, BringsFrameBOntoFrameAAtZeroOffset) {
    Mate m{};
    m.type = MateType::Planar;
    gp_Ax3 fa(gp_Pnt(10, 0, 0), gp_Dir(0, 0, 1));
    gp_Ax3 fb(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));

    gp_Trsf t = MateSolver::alignTrsf(m, fa, fb);
    gp_Pnt origin(0, 0, 0);
    origin.Transform(t);
    EXPECT_NEAR(origin.X(), 10.0, 1e-9);
    EXPECT_NEAR(origin.Y(), 0.0, 1e-9);
    EXPECT_NEAR(origin.Z(), 0.0, 1e-9);
}

TEST(MateAlign, OffsetRunsAlongFrameANormal) {
    Mate m{};
    m.type = MateType::Planar;
    m.offset = 4.0;
    gp_Ax3 fa(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    gp_Ax3 fb(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));

    gp_Trsf t = MateSolver::alignTrsf(m, fa, fb);
    gp_Pnt p(0, 0, 0);
    p.Transform(t);
    EXPECT_NEAR(p.Z(), 4.0, 1e-9) << "offset must run along A's normal";
}

TEST(MateAlign, RotatesAFrameOntoADifferentAxis) {
    // A cylinder lying along X mated to one along Z: the placement has to
    // rotate, not just translate. This is the case a translation-only Fasten
    // cannot express and Concentric exists for.
    Mate m{};
    m.type = MateType::Concentric;
    gp_Ax3 fa(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    gp_Ax3 fb(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));

    gp_Trsf t = MateSolver::alignTrsf(m, fa, fb);
    // A point one unit along B's axis must land one unit along A's.
    gp_Pnt p(1, 0, 0);
    p.Transform(t);
    EXPECT_NEAR(p.X(), 0.0, 1e-9);
    EXPECT_NEAR(p.Y(), 0.0, 1e-9);
    EXPECT_NEAR(p.Z(), 1.0, 1e-9);
}

TEST(MateAlign, FlippedReversesTheMatingDirection) {
    Mate m{};
    m.type = MateType::Planar;
    gp_Ax3 fa(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    gp_Ax3 fb(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));

    gp_Pnt probe(0, 0, 1);
    gp_Pnt unflipped = probe, flipped = probe;
    unflipped.Transform(MateSolver::alignTrsf(m, fa, fb));
    m.flipped = true;
    flipped.Transform(MateSolver::alignTrsf(m, fa, fb));

    EXPECT_NEAR(unflipped.Z(), 1.0, 1e-9);
    EXPECT_NEAR(flipped.Z(), -1.0, 1e-9)
        << "flipped must reach the other of the two alignments";
}

TEST(MateAlign, AngleRollsAboutFrameANormal) {
    Mate m{};
    m.type = MateType::Planar;
    m.angle = M_PI / 2.0;
    gp_Ax3 fa(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    gp_Ax3 fb(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));

    gp_Pnt p(1, 0, 0);
    p.Transform(MateSolver::alignTrsf(m, fa, fb));
    EXPECT_NEAR(p.X(), 0.0, 1e-6);
    EXPECT_NEAR(p.Y(), 1.0, 1e-6) << "a quarter turn about A's normal";
}

TEST(MateAlign, IsIdempotentUnderRepeatedComposition) {
    // Same property the body solve has: computing the transform twice from the
    // same frames must give the same answer.
    Mate m{};
    m.type = MateType::Concentric;
    m.offset = 3.0;
    m.angle = 0.4;
    gp_Ax3 fa(gp_Pnt(1, 2, 3), gp_Dir(0, 1, 0));
    gp_Ax3 fb(gp_Pnt(-4, 0, 2), gp_Dir(1, 0, 1));

    gp_Trsf t1 = MateSolver::alignTrsf(m, fa, fb);
    gp_Trsf t2 = MateSolver::alignTrsf(m, fa, fb);
    gp_Pnt p1(5, 6, 7), p2(5, 6, 7);
    p1.Transform(t1);
    p2.Transform(t2);
    EXPECT_NEAR(p1.X(), p2.X(), 1e-12);
    EXPECT_NEAR(p1.Y(), p2.Y(), 1e-12);
    EXPECT_NEAR(p1.Z(), p2.Z(), 1e-12);
}

TEST(MateSolver, MateWithUnresolvableAnchorsIsMarkedBrokenNotFatal) {
    // A regeneration can rename the faces a mate referenced. The solve must
    // survive it: mark the mate broken, leave that body where it is, and keep
    // placing every other mate in the document.
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    int c = doc.addBody(BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape(), "C");
    doc.setGroundedBody(a);

    // A Planar mate carrying anchors that cannot resolve (no sketch backs
    // them), plus a healthy anchorless Fasten elsewhere.
    Mate bad{};
    bad.type = MateType::Planar;
    bad.bodyA = a;
    bad.bodyB = b;
    FaceAnchor::Anchor anch;
    anch.kind = FaceAnchor::Anchor::Wall;
    anch.sketchId = 999;      // no such sketch
    anch.elemId = 999;
    bad.anchorsA.push_back(anch);
    bad.anchorsB.push_back(anch);
    int badId = doc.addMate(bad);

    doc.addMate(fasten(a, c, 12.0));

    MateSolver solver;
    auto r = solver.solve(doc);

    EXPECT_TRUE(r.ok) << "one lost reference must not strand the whole assembly";
    ASSERT_EQ(r.brokenMateIds.size(), 1u);
    EXPECT_EQ(r.brokenMateIds[0], badId);

    // The broken mate's body stayed put; the healthy one still placed.
    EXPECT_NEAR(minCorner(doc, b).X(), 0.0, 1e-6);
    EXPECT_NEAR(minCorner(doc, c).X(), 12.0, 1e-6);

    for (const auto& m : doc.getMates())
        if (m.id == badId) EXPECT_TRUE(m.broken);
}

#include "core/History.h"

TEST(MateSolver, HistoryPushRePlacesAMatedBody) {
    // The point of the whole feature: change the model and the mated body
    // follows, without anyone calling the solver by hand.
    Document doc;
    History hist;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    // Any op pushed through history triggers the re-place.
    auto op = std::make_unique<TransformOp>();
    op->setBodyId(a);
    op->setTranslation(0.0, 0.0, 0.0);   // a no-op move on the grounded body
    ASSERT_TRUE(hist.pushOperation(std::move(op), doc));

    EXPECT_NEAR(minCorner(doc, b).X(), 25.0, 1e-6)
        << "mated body was not placed after a history push";
}

TEST(MateSolver, RepeatedHistoryPushesDoNotCompound) {
    // The failure mode this design is most exposed to: each push re-solving
    // against geometry the previous solve already moved. The solver is held
    // across calls precisely so the base stays the unmated geometry.
    Document doc;
    History hist;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    for (int i = 0; i < 4; ++i) {
        auto op = std::make_unique<TransformOp>();
        op->setBodyId(a);
        op->setTranslation(0.0, 0.0, 0.0);
        ASSERT_TRUE(hist.pushOperation(std::move(op), doc));
        EXPECT_NEAR(minCorner(doc, b).X(), 25.0, 1e-6)
            << "placement compounded on push " << i;
    }
}

// Non-commuting frames: frameA rotated AND translated, frameB off-origin.
// Every other alignment fixture here is a commuting configuration, where the
// coordinate-change and the displacement happen to agree - which is why a full
// green suite hid alignTrsf using the wrong one of the two.
TEST(MateAlign, NonCommutingFramesBringOriginOntoOrigin) {
    Mate m{};
    m.type = MateType::Planar;
    gp_Ax3 fa(gp_Pnt(10, 0, 0), gp_Dir(1, 0, 0));
    gp_Ax3 fb(gp_Pnt(0, 5, 0), gp_Dir(0, 0, 1));

    gp_Pnt got = fb.Location();
    got.Transform(MateSolver::alignTrsf(m, fa, fb));
    EXPECT_NEAR(got.X(), 10.0, 1e-6);
    EXPECT_NEAR(got.Y(), 0.0, 1e-6);
    EXPECT_NEAR(got.Z(), 0.0, 1e-6);
}

// The feature's headline claim, with a REAL translation. The earlier version
// of this test moved the reference by (0,0,0), which proved only that the
// solve ran.
TEST(MateSolver, MovingTheReferenceCarriesTheMatedBody) {
    Document doc;
    History hist;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    MateSolver s;
    ASSERT_TRUE(s.solve(doc).ok);
    ASSERT_NEAR(minCorner(doc, b).X(), 25.0, 1e-6);

    auto op = std::make_unique<TransformOp>();
    op->setBodyId(a);
    op->setTranslation(100.0, 0.0, 0.0);
    ASSERT_TRUE(hist.pushOperation(std::move(op), doc));

    EXPECT_NEAR(minCorner(doc, a).X(), 100.0, 1e-6);
    EXPECT_NEAR(minCorner(doc, b).X(), 125.0, 1e-6)
        << "mated body did not follow its reference";
}

// A mate to a body that has been deleted must not throw out of solve(), which
// runs inside pushOperation AFTER the op is committed.
TEST(MateSolver, MateToADeletedBodyDoesNotThrow) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));
    doc.removeBody(b);

    MateSolver s;
    EXPECT_NO_THROW({ auto r = s.solve(doc); (void)r; });
}

// Loaded ids must not collide with newly created ones.
TEST(MateStoreIds, RawMateAdvancesTheCounter) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    Mate loaded = fasten(a, b, 1.0);
    loaded.id = 7;                       // as if it came from a file
    doc.addRawMate(loaded);

    int fresh = doc.addMate(fasten(b, a, 2.0));
    EXPECT_GT(fresh, 7) << "a new mate collided with a loaded id";
}

// Editing a mated body's GEOMETRY must survive the next solve. The base cache
// is captured once; if nothing invalidates it, the solve transforms the stale
// pre-edit shape and writes it back, destroying the edit while the history
// step that made it still exists - model and geometry permanently disagree.
TEST(MateSolver, EditingAMatedBodyIsNotRevertedByTheNextSolve) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    MateSolver s;
    ASSERT_TRUE(s.solve(doc).ok);

    // Grow B, as a feature edit would.
    doc.updateBody(b, BRepPrimAPI_MakeBox(9.0, 4.0, 4.0).Shape());
    ASSERT_TRUE(s.solve(doc).ok);

    Bnd_Box box;
    BRepBndLib::Add(doc.getBody(b), box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    EXPECT_NEAR(x1 - x0, 9.0, 1e-6)
        << "the solve reverted the body to its stale pre-edit base";
}

// A deleted body's base must not outlive it: putBody re-issues the same id on
// undo, so a stale base would be applied to different geometry.
TEST(MateSolver, RemovingABodyDropsItsMateBase) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    MateSolver s;
    ASSERT_TRUE(s.solve(doc).ok);
    ASSERT_TRUE(doc.hasMateBase(b));

    doc.removeBody(b);
    EXPECT_FALSE(doc.hasMateBase(b)) << "stale base outlived its body";
}

// A body behind a BROKEN mate must not be placed relative to a position its
// reference never took. A healthy B->C after a broken A->B previously moved C
// to where B is not.
TEST(MateSolver, BodyBehindABrokenMateIsNotMovedToAPhantomPosition) {
    // Solve HEALTHY first, so B is genuinely placed and its base and its live
    // shape differ - that gap is what makes a stale base dangerous. Then break
    // A->B and re-solve. C must not move: it is mated to B, and B has not
    // moved. The earlier version of this test asserted on B (which is pinned
    // at the origin under every implementation and so could never fail) and
    // never looked at C at all.
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    int c = doc.addBody(BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape(), "C");
    doc.setGroundedBody(a);

    int abId = doc.addMate(fasten(a, b, 25.0));
    doc.addMate(fasten(b, c, 10.0));

    MateSolver s;
    ASSERT_TRUE(s.solve(doc).ok);
    ASSERT_NEAR(minCorner(doc, b).X(), 25.0, 1e-6);
    const double cHealthy = minCorner(doc, c).X();
    ASSERT_NEAR(cHealthy, 35.0, 1e-6);

    // Break A->B by pointing it at anchors nothing can resolve.
    FaceAnchor::Anchor anch;
    anch.kind = FaceAnchor::Anchor::Wall;
    anch.sketchId = 999;
    anch.elemId = 999;
    for (auto& m : doc.getMutableMates()) {
        if (m.id != abId) continue;
        m.type = MateType::Planar;
        m.anchorsA.push_back(anch);
        m.anchorsB.push_back(anch);
    }

    auto r = s.solve(doc);
    EXPECT_TRUE(r.ok) << "one lost reference must not strand the assembly";
    EXPECT_NEAR(minCorner(doc, b).X(), 25.0, 1e-6)
        << "the broken mate's body should stay where it was";
    EXPECT_NEAR(minCorner(doc, c).X(), cHealthy, 1e-6)
        << "C measured from a position B does not occupy";
}

TEST(MateSolver, TransformOnAMatedBodyStillReplays) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    TransformOp op;
    op.setBodyId(b);
    op.setTranslation(1.0, 0.0, 0.0);

    EXPECT_FALSE(op.execute(doc)) << "an interactive move must be refused";

    doc.setReplaying(true);
    EXPECT_TRUE(op.execute(doc)) << "a replayed step must be allowed through";
    doc.setReplaying(false);
}

// redo() must run through the SAME replay guard undo/editStep/replayAll
// already use. Without it, redoing a TransformOp on a body that got mated
// after the op first executed hits TransformOp::execute's interactive
// refusal (it is not a replay as far as doc.isReplaying() can tell), and
// History gets stuck: m_failedReplayAt is set and nothing ever clears it,
// so Ctrl+Y can never advance past that step again.
TEST(MateSolver, RedoOfATransformSurvivesTheBodyBeingMatedAfterward) {
    Document doc;
    History hist;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");

    // B moves while it is still an ordinary, unmated body - this push must
    // succeed as a ordinary interactive step.
    auto op = std::make_unique<TransformOp>();
    op->setBodyId(b);
    op->setTranslation(1.0, 0.0, 0.0);
    ASSERT_TRUE(hist.pushOperation(std::move(op), doc));
    ASSERT_EQ(hist.currentStep(), 0);

    // Now B is mate-placed. TransformOp::execute refuses B outside a replay
    // from this point on.
    doc.setGroundedBody(a);
    doc.addMate(fasten(a, b, 25.0));

    ASSERT_TRUE(hist.undo(doc));
    ASSERT_TRUE(hist.canRedo());
    EXPECT_TRUE(hist.redo(doc))
        << "redo must replay the step (doc.isReplaying() must be true during "
           "it), not attempt it as an interactive move that mate placement "
           "then refuses";
    EXPECT_EQ(hist.currentStep(), 0) << "the tip must actually advance";
}

// The solver's error must reach somewhere the UI can read it.
TEST(MateSolver, SolveErrorIsRecordedOnTheDocument) {
    Document doc;
    History hist;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    int c = doc.addBody(BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape(), "C");
    doc.addMate(fasten(a, b, 25.0));
    doc.addMate(fasten(b, a, 5.0));      // grounded body also placed

    // Drive the push through a body NO mate places - transforming a
    // mate-placed body is refused, so the solve would never run.
    auto op = std::make_unique<TransformOp>();
    op->setBodyId(c);
    op->setTranslation(0.0, 0.0, 0.0);
    ASSERT_TRUE(hist.pushOperation(std::move(op), doc));

    EXPECT_FALSE(doc.mateSolveError().empty())
        << "a solver error vanished instead of reaching the UI";
}

// The panel builds a fresh MateSolver on every edit. Body bases live on the
// Document so they survive that, but the sketch-plane bases did not - so each
// new solver captured the ALREADY-PLACED plane as its base and applied the
// full placement again. The body holds still while its sketch walks away.
TEST(MateSolver, SketchPlaneDoesNotDriftAcrossSolverInstances) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);

    auto sk = std::make_shared<materializr::Sketch>();
    sk->setPlane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
    sk->setSourceBody(b);
    int sid = doc.addSketch(sk, "S");

    doc.addMate(fasten(a, b, 25.0));

    double planeX = 0.0;
    for (int i = 0; i < 3; ++i) {
        MateSolver fresh;                       // as the panel does, per edit
        ASSERT_TRUE(fresh.solve(doc).ok);
        planeX = doc.getSketch(sid)->getPlane().Location().X();
        EXPECT_NEAR(doc.getSketch(sid)->getPlane().Location().X(), 25.0, 1e-6)
            << "sketch plane drifted on solve " << i << " (x=" << planeX << ")";
    }
}

// Deleting a mate must not leave a base behind: the body stays where the solve
// put it, so a base describing the UNPLACED shape makes any later mate that
// uses this body as its reference measure from a position it does not occupy.
TEST(MateSolver, DeletingAMateDoesNotPoisonTheNextOne) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    int c = doc.addBody(BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape(), "C");
    doc.setGroundedBody(a);

    int id = doc.addMate(fasten(a, b, 25.0));
    MateSolver s;
    ASSERT_TRUE(s.solve(doc).ok);
    ASSERT_NEAR(minCorner(doc, b).X(), 25.0, 1e-6);

    doc.removeMate(id);
    EXPECT_FALSE(doc.hasMateBase(b)) << "base outlived its mate";

    // C mated to B with offset 0 must hold where it is.
    const double cBefore = minCorner(doc, c).X();
    doc.addMate(fasten(b, c, 0.0));
    ASSERT_TRUE(s.solve(doc).ok);
    EXPECT_NEAR(minCorner(doc, c).X(), cBefore + 25.0, 1e-6)
        << "C was placed relative to B's unplaced phantom position";
}

// A suppressed mate leaves its body placed, so its base must go the same way a
// deleted mate's does.
TEST(MateSolver, SuppressingAMateDropsItsBase) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);
    int id = doc.addMate(fasten(a, b, 25.0));

    MateSolver s;
    ASSERT_TRUE(s.solve(doc).ok);
    ASSERT_TRUE(doc.hasMateBase(b));

    for (auto& m : doc.getMutableMates()) if (m.id == id) m.suppressed = true;
    ASSERT_TRUE(s.solve(doc).ok);
    EXPECT_FALSE(doc.hasMateBase(b)) << "suppressed mate kept a stale base";
}
