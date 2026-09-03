#include "FilletProbe.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <TopoDS.hxx>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>

namespace materializr {
namespace fillet {
namespace {

std::atomic<int> g_abandoned{0};

// One probe's result slot. Shared by value between the caller and the detached
// worker, so the worker can outlive the caller and still write somewhere valid
// — the whole reason this is a shared_ptr and not a stack local.
struct Slot {
    std::mutex              m;
    std::condition_variable cv;
    bool                    done = false;
    bool                    ok   = false;
};

// Memo key. The shape is identified by its TShape pointer: a fillet's inputs are
// the document's live body and its sub-edges, which keep identity between frames
// of one interactive drag — exactly the window where re-probing would hurt.
using Key = std::tuple<const void*, size_t, long long>;

Key makeKey(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges,
            double radius) {
    // Quantise the radius to 1e-6 mm so float noise in a dragged value doesn't
    // miss the cache on every frame, while still separating genuinely distinct
    // radii (OCCT's failures are isolated points, sometimes microns apart).
    const long long q = static_cast<long long>(radius * 1e6 + (radius < 0 ? -0.5 : 0.5));
    return Key{shape.TShape().get(), edges.size(), q};
}

std::mutex             g_cacheMu;
std::map<Key, bool>    g_cache;

} // namespace

void clearProbeCache() {
    std::lock_guard<std::mutex> lk(g_cacheMu);
    g_cache.clear();
}

int abandonedProbeCount() { return g_abandoned.load(); }

bool probe(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges,
           double radius, double budget) {
    if (shape.IsNull() || edges.empty() || radius <= 0.0) return false;

    const Key key = makeKey(shape, edges, radius);
    {
        std::lock_guard<std::mutex> lk(g_cacheMu);
        auto it = g_cache.find(key);
        if (it != g_cache.end()) return it->second;
    }

    // Deep copy for the worker. Mandatory, not defensive: OCCT lazily fills
    // BSplSLib caches inside Geom_BSplineSurface on first evaluation, so the
    // worker calling D1() on the SAME surface the render thread is meshing is a
    // straight data race. The copy also remaps the edges — sub-shapes of the
    // original are not sub-shapes of the copy, and Add() on a foreign edge does
    // not build what the caller asked for.
    TopoDS_Shape             copy;
    std::vector<TopoDS_Edge> copyEdges;
    try {
        BRepBuilderAPI_Copy copier(shape);
        if (!copier.IsDone()) return false;
        copy = copier.Shape();
        if (copy.IsNull()) return false;
        copyEdges.reserve(edges.size());
        for (const auto& e : edges) {
            const TopoDS_Shape m = copier.ModifiedShape(e);
            if (m.IsNull() || m.ShapeType() != TopAbs_EDGE) return false;
            copyEdges.push_back(TopoDS::Edge(m));
        }
    } catch (...) {
        return false;
    }

    auto slot = std::make_shared<Slot>();
    std::thread([slot, copy, copyEdges, radius]() {
        bool ok = false;
        try {
            BRepFilletAPI_MakeFillet mk(copy);
            for (const auto& e : copyEdges) mk.Add(radius, e);
            mk.Build();
            ok = mk.IsDone() && !mk.Shape().IsNull();
        } catch (...) {
            ok = false;
        }
        bool late = false;
        {
            std::lock_guard<std::mutex> lk(slot->m);
            late      = slot->done;   // the caller already gave up on us
            slot->ok  = ok;
            slot->done = true;
        }
        if (late) g_abandoned.fetch_sub(1);
        slot->cv.notify_all();
    }).detach();

    bool ok = false;
    {
        std::unique_lock<std::mutex> lk(slot->m);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::duration<double>(budget);
        if (slot->cv.wait_until(lk, deadline, [&] { return slot->done; })) {
            ok = slot->ok;
        } else {
            // Timed out. Mark the slot spent so the worker knows nobody is
            // listening, and leave it running — there is no way to stop it.
            slot->done = true;
            g_abandoned.fetch_add(1);
            std::fprintf(stderr,
                "[Fillet] probe exceeded %.1fs at R=%.4f — refusing the build "
                "(OCCT's blend cannot be interrupted; worker abandoned).\n",
                budget, radius);
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_cacheMu);
        g_cache[key] = ok;
    }
    return ok;
}

} // namespace fillet
} // namespace materializr
