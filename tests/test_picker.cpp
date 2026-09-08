// Hover picking runs Picker::pick on every rendered frame the cursor rests on
// a body. It used to call BRepMesh_IncrementalMesh(shape, 0.1) each time "to
// make sure the shape is tessellated" - but the mesher rebuilds its whole data
// model per call even when it changes nothing (9 ms on a 54-face part, 66 ms
// on a 1683-face part), and at Low quality (0.5 mm) it actually RE-meshed the
// body finer than the renderer asked for. These tests pin the fix: the picker
// reuses the renderer's triangulation and snaps the hit onto the exact surface.

#include "core/Document.h"
#include "viewport/Camera.h"
#include "viewport/Picker.h"

#include <gtest/gtest.h>

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <vector>

using materializr::Camera;
using materializr::Picker;
using materializr::PickResult;

namespace {

constexpr float kW = 800.0f;
constexpr float kH = 600.0f;

// Mesh the way the renderer does at Low quality (0.5 mm linear deflection).
void meshLikeRendererLow(const TopoDS_Shape& s) {
    BRepMesh_IncrementalMesh m(s, 0.5, Standard_False, 0.5, Standard_True);
    m.Perform();
}

// One entry per face: the Poly_Triangulation it carries (null if none).
std::vector<const void*> triangulations(const TopoDS_Shape& s) {
    std::vector<const void*> v;
    for (TopExp_Explorer e(s, TopAbs_FACE); e.More(); e.Next()) {
        TopLoc_Location l;
        v.push_back(BRep_Tool::Triangulation(TopoDS::Face(e.Current()), l).get());
    }
    return v;
}

Camera framing(const TopoDS_Shape& s) {
    Bnd_Box bb;
    BRepBndLib::Add(s, bb);
    double x0, y0, z0, x1, y1, z1;
    bb.Get(x0, y0, z0, x1, y1, z1);
    Camera cam;
    cam.setAspect(kW / kH);
    cam.zoomToFit(glm::vec3(x0, y0, z0), glm::vec3(x1, y1, z1));
    return cam;
}

PickResult pickCentre(const TopoDS_Shape& s, Document& doc) {
    Camera cam = framing(s);
    Picker picker;
    return picker.pick(kW * 0.5f, kH * 0.5f, kW, kH, cam, doc);
}

} // namespace

TEST(Picker, ReusesTheRenderersTriangulation) {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 10.0).Shape();
    meshLikeRendererLow(box);
    const auto before = triangulations(box);
    ASSERT_FALSE(before.empty());
    for (const void* t : before) ASSERT_NE(t, nullptr);

    Document doc;
    const int id = doc.addBody(box, "plate");
    PickResult r = pickCentre(box, doc);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.bodyId, id);

    // The pick must not have touched the mesh: same triangulation objects.
    EXPECT_EQ(triangulations(box), before);
}

TEST(Picker, DoesNotMeshABodyTheRendererNeverMeshed) {
    // A body the renderer could not tessellate is not drawn, so the picker
    // must neither hit it nor try to mesh it (that retry would run the
    // mesher every hovered frame).
    TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 10.0).Shape();
    for (const void* t : triangulations(box)) ASSERT_EQ(t, nullptr);

    Document doc;
    doc.addBody(box, "plate");
    PickResult r = pickCentre(box, doc);
    EXPECT_FALSE(r.hit);
    for (const void* t : triangulations(box)) EXPECT_EQ(t, nullptr);
}

TEST(Picker, HitPointIsSnappedToTheExactSurface) {
    // A sphere: every hit lands on a curved face, where a 0.5 mm chord mesh
    // puts the ray/triangle intersection visibly off the true surface. The
    // measure tool consumes hitPoint, so it must lie ON the surface.
    TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(10.0).Shape();
    meshLikeRendererLow(sphere);

    Document doc;
    doc.addBody(sphere, "ball");
    PickResult r = pickCentre(sphere, doc);
    ASSERT_TRUE(r.hit);
    EXPECT_NEAR(glm::length(r.hitPoint), 10.0f, 1e-3f);
}
