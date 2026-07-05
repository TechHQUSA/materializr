// Story A.2 (interaction-grammar port): tapping a face created by a push/pull
// must trace back to its history step, exactly like fillet/chamfer faces
// already do — Operation::ownsFace() is the hook the viewport's click path
// uses to open the owning step's editor. PushPullOp did not implement it.

#include "core/Document.h"
#include "modeling/PushPullOp.h"

#include <gtest/gtest.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepGProp_Face.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>

namespace {

// The planar face whose center sits at the given z (within tolerance).
TopoDS_Face planarFaceAtZ(const TopoDS_Shape& s, double z, double nzMin = 0.9) {
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
        TopoDS_Face f = TopoDS::Face(ex.Current());
        Handle(Geom_Surface) surf = BRep_Tool::Surface(f);
        if (surf.IsNull() || !surf->IsKind(STANDARD_TYPE(Geom_Plane))) continue;
        BRepGProp_Face g(f); double u1,u2,v1,v2; g.Bounds(u1,u2,v1,v2);
        gp_Pnt c; gp_Vec n; g.Normal(0.5*(u1+u2),0.5*(v1+v2),c,n);
        if (std::abs(n.Z()) >= nzMin && std::abs(c.Z() - z) < 1e-6) return f;
    }
    return TopoDS_Face();
}

} // namespace

TEST(PushPullOwnsFace, ExtrudedFacesTraceBackToTheirStep) {
    Document doc;
    TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(0,0,0), 20, 20, 10).Shape();
    int id = doc.addBody(box, "Box");

    TopoDS_Face top = planarFaceAtZ(doc.getBody(id), 10.0);
    ASSERT_FALSE(top.IsNull());

    PushPullOp op;
    std::vector<PushPullOp::Target> targets(1);
    targets[0].profile = top;
    targets[0].sourceBodyId = id;
    op.setTargets(std::move(targets));
    op.setDistance(5.0); // extrude the top up to z=15
    ASSERT_TRUE(op.execute(doc));

    // The NEW top cap (z=15) was created by this pull — must trace back.
    TopoDS_Face newTop = planarFaceAtZ(doc.getBody(id), 15.0);
    ASSERT_FALSE(newTop.IsNull());
    EXPECT_TRUE(op.ownsFace(newTop))
        << "the pull's new cap face must be owned by its step";

    // The untouched bottom face (z=0) is NOT this op's product.
    TopoDS_Face bottom = planarFaceAtZ(doc.getBody(id), 0.0);
    ASSERT_FALSE(bottom.IsNull());
    EXPECT_FALSE(op.ownsFace(bottom))
        << "pre-existing faces must not be claimed by the pull";
}
