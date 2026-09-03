#pragma once
// Deadline guard for OCCT's fillet builder.
//
// BRepFilletAPI_MakeFillet::Build() takes a Message_ProgressRange, but it hands
// off to ChFi3d_Builder::Compute(), which takes NO progress argument (see
// ChFi3d_Builder.hxx). Nothing inside the blend algorithm ever polls for a user
// break, so a pathological radius cannot be cancelled: the call simply never
// returns. Observed on FOB.mzr — ChFi3d_Builder::StoreData spinning at 100% CPU
// with no progress for minutes, wedging the render loop that called it.
//
// Since the build cannot be interrupted, it must not be entered blind. probe()
// runs the SAME build on a DETACHED worker against a deep copy and waits only
// `budget`. A caller that gets true may then run the real build synchronously,
// knowing this shape + radius converges. A caller that gets false must refuse.
//
// The worker is detached, not a std::async future, on purpose: a std::future's
// destructor BLOCKS until the task finishes, so a wedged probe parked in a
// member vector would hang the app on quit — the same freeze, moved to exit.
// A detached thread leaks (it keeps burning a core until OCCT returns, if ever)
// but never blocks anyone. That leak is the honest cost of an uninterruptible
// third-party algorithm; the cure is a helper process we can kill, which is a
// much larger change.

#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <vector>

namespace materializr {
namespace fillet {

// Default wall-clock budget for one probe. Generous enough for a legitimately
// heavy blend on a large body, far below the point a user calls it frozen.
inline constexpr double kDefaultProbeSeconds = 2.5;

// True when a fillet of `radius` on `edges` of `shape` completes inside
// `budget`. False on timeout, throw, or a build that fails outright.
//
// Results are memoised per (shape, edges, radius): an interactive preview
// re-runs the identical query every frame, and probing twice per frame would
// itself be the performance bug this exists to prevent.
bool probe(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges,
           double radius, double budget = kDefaultProbeSeconds);

// Drop memoised verdicts. Call when the body changes underneath a cached entry.
void clearProbeCache();

// How many probes are still running past their budget (detached, unkillable).
// Surfaced so the UI can warn rather than silently burning cores.
int abandonedProbeCount();

} // namespace fillet
} // namespace materializr
