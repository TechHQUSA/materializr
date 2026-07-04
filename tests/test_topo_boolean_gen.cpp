// BooleanOp publishes a two-input GenerationLedger, so the "gen" strategy can
// name a boolean SEAM EDGE by the two faces that made it (each face named by
// sketchface against its own input body — edit-stable). THE deferred dream
// case: seam sub-shapes were unnameable by every geometric scheme, which is
// why fillets on seams bake to ReplayOps today. This proves the naming layer
// now reaches them and survives an upstream sketch edit.

#include "modeling/TopoName.h"
#include "modeling/GenerationLedger.h"
#include "modeling/BooleanOp.h"
#include "core/Document.h"
#include "modeling/Sketch.h"
#include "modeling/ExtrudeOp.h"

#include <gtest/gtest.h>
#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <cmath>
#include <memory>

using materializr::Sketch;
using namespace materializr;

namespace {

std::shared_ptr<Sketch> makeRect(double x0, double y0, double x1, double y1,
                                 int pid[4]) {
    auto sk = std::make_shared<Sketch>();
    sk->setPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))));
    pid[0] = sk->addPoint({(float)x0, (float)y0});
    pid[1] = sk->addPoint({(float)x1, (float)y0});
    pid[2] = sk->addPoint({(float)x1, (float)y1});
    pid[3] = sk->addPoint({(float)x0, (float)y1});
    for (int i = 0; i < 4; ++i) sk->addLine(pid[i], pid[(i + 1) % 4]);
    return sk;
}

gp_Pnt edgeMid(const TopoDS_Edge& e) {
    BRepAdaptor_Curve c(e);
    return c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
}

TopoDS_Edge edgeNear(const TopoDS_Shape& body, const gp_Pnt& p, double tol) {
    for (TopExp_Explorer ex(body, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge e = TopoDS::Edge(ex.Current());
        if (edgeMid(e).Distance(p) < tol) return e;
    }
    return {};
}

} // namespace

TEST(TopoBooleanGen, SeamEdgeNameSurvivesSketchEdit) {
    Document doc;
    // Body A: 20x10 slab, z 0..10. Body B: 15..25 x 3..7 post, z 0..15 —
    // sticks out of A's top, so B's walls cut SEAM edges into A's top face.
    int pa[4], pb[4];
    auto skA = makeRect(0, 0, 20, 10, pa);
    auto skB = makeRect(15, 3, 25, 7, pb);
    int sidA = doc.addSketch(skA), sidB = doc.addSketch(skB);
    ExtrudeOp extA; extA.setSketchSource(sidA); extA.setDistance(10.0);
    ASSERT_TRUE(extA.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(extA.execute(doc));
    int bodyA = doc.getAllBodyIds().front();
    ExtrudeOp extB; extB.setSketchSource(sidB); extB.setDistance(15.0);
    ASSERT_TRUE(extB.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(extB.execute(doc));
    int bodyB = -1;
    for (int id : doc.getAllBodyIds()) if (id != bodyA) bodyB = id;
    ASSERT_GE(bodyB, 0);

    BooleanOp fuse;
    fuse.setTargetBodyId(bodyA);
    fuse.setToolBodyId(bodyB);
    fuse.setMode(BooleanMode::Union);
    ASSERT_TRUE(fuse.execute(doc));
    ASSERT_GT(fuse.generationLedger().generated.Extent() +
              fuse.generationLedger().modified.Extent(), 0)
        << "boolean must publish its two-input lineage";

    // The seam edge where B's x=15 wall crosses A's top (z=10): mid (15,5,10).
    TopoDS_Edge seam = edgeNear(doc.getBody(bodyA), gp_Pnt(15, 5, 10), 0.5);
    ASSERT_FALSE(seam.IsNull()) << "fused body must have the seam edge";

    topo::Context ctx;
    ctx.doc = &doc; ctx.shape = doc.getBody(bodyA); ctx.type = TopAbs_EDGE;
    ctx.gen = &fuse.generationLedger();
    topo::Ref ref = topo::mint(seam, ctx);
    std::string genPayload;
    for (const auto& n : ref.names)
        if (n.scheme == "gen") { genPayload = n.payload; break; }
    ASSERT_FALSE(genPayload.empty())
        << "the seam edge must be nameable by generation lineage";

    // UPSTREAM EDIT: slide B two mm left (x 15..25 -> 13..23). Replay the
    // chain the way the cascade does: undo the boolean, rebuild B, re-fuse.
    ASSERT_TRUE(fuse.undo(doc));
    skB->movePoint(pb[0], {13.0f, 3.0f});
    skB->movePoint(pb[1], {23.0f, 3.0f});
    skB->movePoint(pb[2], {23.0f, 7.0f});
    skB->movePoint(pb[3], {13.0f, 7.0f});
    ASSERT_TRUE(extB.rebuildProfileFromSketch(doc));
    ASSERT_TRUE(extB.execute(doc));
    ASSERT_TRUE(fuse.execute(doc)) << "re-fuse on the moved post";

    // Resolve the ORIGINAL gen payload against the NEW ledger — directly via
    // the gen strategy, so no other scheme can mask the result. It must land
    // the MOVED seam edge at (13, 5, 10).
    const topo::Strategy* gen = topo::Registry::instance().forScheme("gen");
    ASSERT_NE(gen, nullptr);
    topo::Context rc;
    rc.doc = &doc; rc.shape = doc.getBody(bodyA); rc.type = TopAbs_EDGE;
    rc.gen = &fuse.generationLedger();
    rc.crossRebuild = true;
    TopoDS_Shape out = gen->resolve(genPayload, rc);
    ASSERT_FALSE(out.IsNull())
        << "seam-edge lineage must resolve on the rebuilt boolean";
    ASSERT_EQ(out.ShapeType(), TopAbs_EDGE);
    gp_Pnt mid = edgeMid(TopoDS::Edge(out));
    EXPECT_NEAR(mid.X(), 13.0, 1e-6) << "the MOVED seam (x=13), not the old x=15";
    EXPECT_NEAR(mid.Z(), 10.0, 1e-6) << "still the top-face seam";
}
