// MeshWorker meshes a private copy of a body on a worker thread and hands back
// one triangulation per live face; land() moves them onto the live faces.
// These tests check the result is the mesh a direct run would have produced.
#include "viewport/MeshWorker.h"
#include "core/MeshParams.h"

#include <gtest/gtest.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <chrono>
#include <thread>
#include <vector>

using materializr::MeshWorker;

namespace {

constexpr float kDefl = 0.1f, kAng = 0.3f;

// Per-face triangle counts, in explorer order (-1 for an unmeshed face).
std::vector<int> triangleCounts(const TopoDS_Shape& s) {
    std::vector<int> v;
    for (TopExp_Explorer e(s, TopAbs_FACE); e.More(); e.Next()) {
        TopLoc_Location l;
        Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(TopoDS::Face(e.Current()), l);
        v.push_back(t.IsNull() ? -1 : t->NbTriangles());
    }
    return v;
}

// Per-face sum of node coordinates, in explorer order: the same mesh landed
// on the wrong face, or in the wrong frame, changes this.
std::vector<double> nodeSums(const TopoDS_Shape& s) {
    std::vector<double> v;
    for (TopExp_Explorer e(s, TopAbs_FACE); e.More(); e.Next()) {
        TopLoc_Location l;
        Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(TopoDS::Face(e.Current()), l);
        double sum = 0.0;
        if (!t.IsNull())
            for (int i = 1; i <= t->NbNodes(); ++i) {
                const gp_Pnt p = t->Node(i);
                sum += p.X() + p.Y() + p.Z();
            }
        v.push_back(sum);
    }
    return v;
}

// A self-intersecting planar wire: the mesher leaves this face untriangulated,
// every time. Stands in for the fused-tori class of body.
TopoDS_Face bowtieFace() {
    BRepBuilderAPI_MakePolygon p(gp_Pnt(0, 0, 0), gp_Pnt(10, 10, 0), gp_Pnt(10, 0, 0), gp_Pnt(0, 10, 0), true);
    return BRepBuilderAPI_MakeFace(p.Wire(), true).Face();
}

TopoDS_Shape holePlate(int nx = 8, int ny = 8) {
    TopoDS_Shape plate = BRepPrimAPI_MakeBox(nx * 15.0, ny * 15.0, 10.0).Shape();
    TopoDS_Compound holes;
    BRep_Builder bb;
    bb.MakeCompound(holes);
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            bb.Add(holes, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(7.5 + i * 15.0, 7.5 + j * 15.0, -1.0), gp_Dir(0, 0, 1)), 3.0, 12.0).Shape());
    return BRepAlgoAPI_Cut(plate, holes).Shape();
}

std::vector<MeshWorker::Result> waitAll(MeshWorker& w) {
    std::vector<MeshWorker::Result> all;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& r : w.collect()) all.push_back(std::move(r));
        if (w.pending() == 0 && !all.empty()) {
            for (auto& r : w.collect()) all.push_back(std::move(r)); // anything finished between the two calls
            return all;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return all;
}

} // namespace

TEST(MeshWorker, LandsTheMeshADirectRunWouldProduce) {
    TopoDS_Shape shape = holePlate();
    // Reference: the same body meshed directly (Delabella is deterministic).
    TopoDS_Shape reference = BRepBuilderAPI_Copy(shape, Standard_True, Standard_False).Shape();
    BRepMesh_IncrementalMesh direct(reference, materializr::meshParams(kDefl, kAng, true));
    const std::vector<int> expected = triangleCounts(reference);

    MeshWorker worker;
    worker.request(7, shape, kDefl, kAng);
    std::vector<MeshWorker::Result> results = waitAll(worker);
    ASSERT_EQ(results.size(), 1u);
    const MeshWorker::Result& r = results[0];
    EXPECT_EQ(r.bodyId, 7);
    EXPECT_EQ(r.tshape, shape.TShape().get());
    EXPECT_EQ(r.unmeshedFaces, 0);
    EXPECT_GT(r.millis, 0.0);
    // Nothing touched the live shape before land().
    for (int n : triangleCounts(shape)) EXPECT_EQ(n, -1);
    EXPECT_EQ(MeshWorker::land(r), static_cast<int>(expected.size()));
    EXPECT_EQ(triangleCounts(shape), expected);
    const std::vector<double> sums = nodeSums(shape), want = nodeSums(reference);
    ASSERT_EQ(sums.size(), want.size());
    for (size_t i = 0; i < sums.size(); ++i) EXPECT_NEAR(sums[i], want[i], 1e-6) << "face " << i;
}

TEST(MeshWorker, ReportsFacesTheMesherCouldNotTriangulate) {
    // The caller uses the count to keep such a body on the synchronous path
    // (tessellate() Cleans and re-meshes it in the frame anyway).
    TopoDS_Compound c;
    BRep_Builder bb;
    bb.MakeCompound(c);
    bb.Add(c, BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape());
    bb.Add(c, bowtieFace());
    MeshWorker worker;
    worker.request(2, c, kDefl, kAng);
    std::vector<MeshWorker::Result> results = waitAll(worker);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].unmeshedFaces, 1);
    EXPECT_EQ(results[0].faces.size(), 7u);
    EXPECT_EQ(MeshWorker::land(results[0]), 6); // the box faces still land
}

TEST(MeshWorker, LandsOnAMovedBody) {
    // The live shape carries a Location; triangulations live on the TShape in
    // local coordinates, so the result must not depend on where the body is.
    TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    gp_Trsf t;
    t.SetTranslation(gp_Vec(100.0, 0.0, 50.0));
    TopoDS_Shape moved = box.Moved(TopLoc_Location(t));
    MeshWorker worker;
    worker.request(3, moved, kDefl, kAng);
    std::vector<MeshWorker::Result> results = waitAll(worker);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(MeshWorker::land(results[0]), 6);
    for (int n : triangleCounts(box)) EXPECT_EQ(n, 2); // the unmoved handle sees the same TShape
}

TEST(MeshWorker, NewestRequestForABodyWins) {
    // Keep the worker busy on body 1 (a 300-hole plate, about 50 ms) while
    // three requests for body 2 arrive within a millisecond: only the last
    // one should be meshed.
    TopoDS_Shape a = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    TopoDS_Shape b = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
    TopoDS_Shape c = BRepPrimAPI_MakeBox(30.0, 30.0, 30.0).Shape();
    MeshWorker worker;
    worker.request(1, holePlate(20, 15), kDefl, kAng);
    worker.request(2, a, kDefl, kAng);
    worker.request(2, b, kDefl, kAng);
    worker.request(2, c, kDefl, kAng);
    EXPECT_EQ(worker.pending(), 2u);
    std::vector<MeshWorker::Result> results = waitAll(worker);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].bodyId, 1);
    EXPECT_EQ(results[1].bodyId, 2);
    EXPECT_EQ(results[1].tshape, c.TShape().get());
    EXPECT_EQ(worker.pending(), 0u);
}
