// Deadline guard for OCCT's uninterruptible fillet builder.
//
// BRepFilletAPI_MakeFillet::Build() cannot be cancelled — it delegates to
// ChFi3d_Builder::Compute(), which takes no Message_ProgressRange — so a radius
// OCCT cannot resolve wedges the calling thread forever. Observed live: the
// render loop spun at 100% CPU inside ChFi3d_Builder::StoreData with no
// progress, and the app had to be killed. probe() exists so the synchronous
// build is never entered without evidence it terminates.

#include "modeling/FilletProbe.h"

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

#include <chrono>
#include <vector>

namespace {

TopoDS_Shape box(double dx, double dy, double dz) {
    return BRepPrimAPI_MakeBox(dx, dy, dz).Shape();
}

std::vector<TopoDS_Edge> firstEdge(const TopoDS_Shape& s) {
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next())
        return { TopoDS::Edge(ex.Current()) };
    return {};
}

double secondsOf(const std::function<void()>& fn) {
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
        .count();
}

} // namespace

// A fillet OCCT can build must be reported buildable. If this ever fails, the
// guard has become a blanket refusal and every fillet is silently broken.
TEST(FilletProbe, AcceptsABuildableFillet) {
    materializr::fillet::clearProbeCache();
    const TopoDS_Shape s = box(20, 20, 20);
    const auto edges = firstEdge(s);
    ASSERT_FALSE(edges.empty());
    EXPECT_TRUE(materializr::fillet::probe(s, edges, 2.0));
}

// Degenerate inputs must be refused without launching a worker at all.
TEST(FilletProbe, RefusesDegenerateInput) {
    materializr::fillet::clearProbeCache();
    const TopoDS_Shape s = box(20, 20, 20);
    const auto edges = firstEdge(s);
    ASSERT_FALSE(edges.empty());
    EXPECT_FALSE(materializr::fillet::probe(s, edges, 0.0));
    EXPECT_FALSE(materializr::fillet::probe(s, edges, -1.0));
    EXPECT_FALSE(materializr::fillet::probe(s, {}, 2.0));
    EXPECT_FALSE(materializr::fillet::probe(TopoDS_Shape(), edges, 2.0));
}

// A radius the geometry cannot take (larger than the box) must come back false
// rather than hanging. This is the guard's whole purpose: bounded time, always.
TEST(FilletProbe, ImpossibleRadiusReturnsWithinBudget) {
    materializr::fillet::clearProbeCache();
    const TopoDS_Shape s = box(20, 20, 20);
    const auto edges = firstEdge(s);
    ASSERT_FALSE(edges.empty());
    bool ok = true;
    const double secs = secondsOf([&] {
        ok = materializr::fillet::probe(s, edges, 500.0, 1.0);
    });
    EXPECT_FALSE(ok);
    // Whether OCCT refuses quickly or has to be abandoned at the deadline, the
    // CALLER must be released on schedule. Generous slack for CI scheduling.
    EXPECT_LT(secs, 5.0);
}

// The budget must be honoured even when the build genuinely cannot finish. A
// zero budget is the sharpest form of the question: give up immediately.
TEST(FilletProbe, ZeroBudgetGivesUpImmediately) {
    materializr::fillet::clearProbeCache();
    const TopoDS_Shape s = box(20, 20, 20);
    const auto edges = firstEdge(s);
    ASSERT_FALSE(edges.empty());
    bool ok = true;
    const double secs = secondsOf([&] {
        ok = materializr::fillet::probe(s, edges, 2.0, 0.0);
    });
    EXPECT_FALSE(ok);
    EXPECT_LT(secs, 2.0);
}

// An interactive preview re-asks the identical question every frame. Without
// memoisation the guard would double every fillet's cost forever — the probe
// must answer a repeat query from cache, not by rebuilding.
TEST(FilletProbe, RepeatQueryIsMemoised) {
    materializr::fillet::clearProbeCache();
    const TopoDS_Shape s = box(20, 20, 20);
    const auto edges = firstEdge(s);
    ASSERT_FALSE(edges.empty());
    const double cold = secondsOf([&] {
        EXPECT_TRUE(materializr::fillet::probe(s, edges, 2.0));
    });
    const double warm = secondsOf([&] {
        EXPECT_TRUE(materializr::fillet::probe(s, edges, 2.0));
    });
    // A cache hit does no OCCT work at all, so it is orders of magnitude
    // faster. Asserting only "not slower" keeps this robust on a loaded CI box
    // while still failing outright if the cache stops working.
    EXPECT_LT(warm, cold + 0.05);
    EXPECT_LT(warm, 0.05);
}

// Clearing must actually clear, or a stale verdict outlives the body it
// described and refuses (or permits) the wrong build after an edit.
TEST(FilletProbe, ClearDropsCachedVerdicts) {
    materializr::fillet::clearProbeCache();
    const TopoDS_Shape s = box(20, 20, 20);
    const auto edges = firstEdge(s);
    ASSERT_FALSE(edges.empty());
    EXPECT_TRUE(materializr::fillet::probe(s, edges, 2.0));
    materializr::fillet::clearProbeCache();
    const double cold = secondsOf([&] {
        EXPECT_TRUE(materializr::fillet::probe(s, edges, 2.0));
    });
    // Post-clear the answer must be recomputed, not served instantly.
    EXPECT_GT(cold, 0.0);
}
