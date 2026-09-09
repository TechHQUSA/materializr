#include "MeshWorker.h"

#include "core/MeshParams.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>

#include <chrono>

namespace materializr {

MeshWorker::MeshWorker() : m_thread([this] { run(); }) {}

MeshWorker::~MeshWorker()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

bool MeshWorker::request(int bodyId, const TopoDS_Shape& shape, float deflection,
                         float angularDeflection)
{
    Job job;
    job.bodyId = bodyId;
    job.tshape = shape.TShape().get();
    job.deflection = deflection;
    job.angularDeflection = angularDeflection;
    try {
        // Geometry copied, mesh not: the copy must not share surfaces or
        // triangulations with anything the main thread can still write.
        BRepBuilderAPI_Copy copier(shape, Standard_True, Standard_False);
        job.copy = copier.Shape();
        for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
            Job::Face f;
            f.live = TopoDS::Face(fe.Current());
            f.copy = TopoDS::Face(copier.ModifiedShape(f.live));
            for (TopExp_Explorer ee(f.live, TopAbs_EDGE); ee.More(); ee.Next()) {
                const TopoDS_Edge& e = TopoDS::Edge(ee.Current());
                f.edges.emplace_back(e, TopoDS::Edge(copier.ModifiedShape(e)));
            }
            job.faces.push_back(std::move(f));
        }
    } catch (...) {
        return false; // OCCT refused the copy: not a worker's problem to have
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    for (Job& waiting : m_queue) {
        if (waiting.bodyId == bodyId) {
            waiting = std::move(job);
            return true;
        }
    }
    m_queue.push_back(std::move(job));
    m_wake.notify_one();
    return true;
}

std::vector<MeshWorker::Result> MeshWorker::collect()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Result> out;
    out.swap(m_done);
    return out;
}

int MeshWorker::land(const Result& r)
{
    BRep_Builder builder;
    // Drop every old triangulation and edge polygon first, in one pass: an
    // edge shared by two faces carries a polygon per face, and cleaning face
    // B after landing face A would take A's fresh polygon with it.
    for (const auto& f : r.faces) BRepTools::Clean(f.live);
    int landed = 0;
    for (const auto& f : r.faces) {
        if (f.tri.IsNull()) continue;
        builder.UpdateFace(f.live, f.tri);
        TopLoc_Location loc;
        BRep_Tool::Triangulation(f.live, loc);
        for (const auto& [edge, poly] : f.edges)
            if (!poly.IsNull()) builder.UpdateEdge(edge, poly, f.tri, loc);
        ++landed;
    }
    return landed;
}

void MeshWorker::pause()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = true;
}

void MeshWorker::resume()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused = false;
    }
    m_wake.notify_all();
}

size_t MeshWorker::pending() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size() + m_running;
}

void MeshWorker::run()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || (!m_paused && !m_queue.empty()); });
            if (m_stop) return;
            job = std::move(m_queue.front());
            m_queue.pop_front();
            ++m_running;
        }
        Result r;
        r.bodyId = job.bodyId;
        r.tshape = job.tshape;
        r.deflection = job.deflection;
        r.angularDeflection = job.angularDeflection;
        try {
            const auto t0 = std::chrono::steady_clock::now();
            BRepMesh_IncrementalMesh mesher(
                job.copy, materializr::meshParams(job.deflection, job.angularDeflection, true));
            r.millis = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t0).count();
            for (const auto& jf : job.faces) {
                Result::Face rf;
                rf.live = jf.live;
                TopLoc_Location loc;
                rf.tri = BRep_Tool::Triangulation(jf.copy, loc);
                if (rf.tri.IsNull()) {
                    ++r.unmeshedFaces;
                } else {
                    for (const auto& [liveEdge, copyEdge] : jf.edges)
                        rf.edges.emplace_back(
                            liveEdge, BRep_Tool::PolygonOnTriangulation(copyEdge, rf.tri, loc));
                }
                r.faces.push_back(std::move(rf));
            }
        } catch (...) {
            r.faces.clear();
            r.unmeshedFaces = static_cast<int>(job.faces.size());
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done.push_back(std::move(r));
        --m_running;
    }
}

} // namespace materializr
