#include "MeshWorker.h"

#include "core/MeshParams.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
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

void MeshWorker::request(int bodyId, const TopoDS_Shape& shape, float deflection,
                         float angularDeflection)
{
    Job job;
    job.bodyId = bodyId;
    job.tshape = shape.TShape().get();
    job.deflection = deflection;
    job.angularDeflection = angularDeflection;
    // Geometry copied, mesh not: the copy must not share surfaces or
    // triangulations with anything the main thread can still write.
    BRepBuilderAPI_Copy copier(shape, Standard_True, Standard_False);
    job.copy = copier.Shape();
    for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
        const TopoDS_Face& live = TopoDS::Face(fe.Current());
        job.faces.emplace_back(live, TopoDS::Face(copier.ModifiedShape(live)));
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    for (Job& waiting : m_queue) {
        if (waiting.bodyId == bodyId) {
            waiting = std::move(job);
            return;
        }
    }
    m_queue.push_back(std::move(job));
    m_wake.notify_one();
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
    int landed = 0;
    for (const auto& [face, tri] : r.faces) {
        if (tri.IsNull()) continue;
        builder.UpdateFace(face, tri);
        ++landed;
    }
    return landed;
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
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
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
            for (const auto& [live, copy] : job.faces) {
                TopLoc_Location loc;
                Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(copy, loc);
                if (tri.IsNull()) ++r.unmeshedFaces;
                r.faces.emplace_back(live, tri);
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
