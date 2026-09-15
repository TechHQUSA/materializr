#include "ai/AiToolDispatcher.h"
#include "core/Document.h"
#include "core/History.h"
#include "plugin/PluginContext.h"
#include "modeling/Sketch.h"

#include <gtest/gtest.h>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <memory>

using namespace materializr::ai;
using materializr::PluginContext;

namespace {
// A PluginContext with just enough bound to run executeTool: Document +
// History. The other _bind() parameters aren't touched by any tool.
PluginContext makeCtx(Document& doc, History& hist) {
    PluginContext ctx;
    ctx._bind(&doc, &hist, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    return ctx;
}
double bboxSizeX(Document& doc, int bodyId) {
    Bnd_Box box;
    BRepBndLib::Add(doc.getBody(bodyId), box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    return x1 - x0;
}
// PrimitiveOp's worldPnt maps user Z (up) -> world Y and user Y (depth) ->
// world Z, so a box's world-space Y extent is its HEIGHT and Z extent its DEPTH.
void bboxWorldYZ(Document& doc, int bodyId, double& sizeY, double& sizeZ) {
    Bnd_Box box;
    BRepBndLib::Add(doc.getBody(bodyId), box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    sizeY = y1 - y0;
    sizeZ = z1 - z0;
}
void bboxWorldOrigin(Document& doc, int bodyId, double& x, double& y, double& z) {
    Bnd_Box box;
    BRepBndLib::Add(doc.getBody(bodyId), box);
    double x1, y1, z1;
    box.Get(x, y, z, x1, y1, z1);
}
Bnd_Box bboxForBody(Document& doc, int bodyId) {
    Bnd_Box box;
    BRepBndLib::Add(doc.getBody(bodyId), box);
    return box;
}
int addTestBox(PluginContext& ctx, Document& doc) {
    ToolResult r = executeTool(ctx, "add_box", {{"width", 10.0}, {"height", 10.0}, {"depth", 10.0}});
    (void)r;
    return doc.getAllBodyIds().back();
}
double volumeOf(Document& doc, int bodyId) {
    GProp_GProps g;
    BRepGProp::VolumeProperties(doc.getBody(bodyId), g);
    return g.Mass();
}
void addRect(materializr::Sketch& sk, float x0, float y0, float x1, float y1) {
    int a = sk.addPoint({x0, y0}), b = sk.addPoint({x1, y0});
    int c = sk.addPoint({x1, y1}), d = sk.addPoint({x0, y1});
    sk.addLine(a, b); sk.addLine(b, c); sk.addLine(c, d); sk.addLine(d, a);
}
// A sketch with two disjoint rectangular regions: A is 10x10 (100 mm^2) at
// the origin, B is 6x4 (24 mm^2) well clear of A - same shapes/pattern as
// tests/test_extrude_regions.cpp, reused here for region_indices coverage.
int addTwoRegionSketch(Document& doc) {
    auto sk = std::make_shared<materializr::Sketch>();
    sk->setPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0), gp_Dir(1, 0, 0))));
    addRect(*sk, 0, 0, 10, 10);   // region 0: 100 mm^2
    addRect(*sk, 20, 0, 26, 4);   // region 1: 24 mm^2
    return doc.addSketch(sk, "Test Sketch");
}
} // namespace

TEST(AiToolDispatcher, AddBoxCreatesABodyWithTheGivenDimensions) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"width", 20.0}, {"height", 15.0}, {"depth", 10.0}};
    ToolResult r = executeTool(ctx, "add_box", args);

    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    int id = doc.getAllBodyIds().front();
    EXPECT_NEAR(bboxSizeX(doc, id), 20.0, 1e-6);
}

TEST(AiToolDispatcher, AddBoxDefaultsPositionToTheOrigin) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"width", 10.0}, {"height", 10.0}, {"depth", 10.0}};
    ToolResult r = executeTool(ctx, "add_box", args);
    ASSERT_TRUE(r.ok) << r.message;

    Bnd_Box box;
    BRepBndLib::Add(doc.getBody(doc.getAllBodyIds().front()), box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    EXPECT_NEAR(x0, 0.0, 1e-6);
    EXPECT_NEAR(y0, 0.0, 1e-6);
    EXPECT_NEAR(z0, 0.0, 1e-6);
}

TEST(AiToolDispatcher, RejectsANonPositiveBoxDimensionWithoutTouchingTheDocument) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"width", -5.0}, {"height", 10.0}, {"depth", 10.0}};
    ToolResult r = executeTool(ctx, "add_box", args);

    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.message.empty());
    EXPECT_TRUE(doc.getAllBodyIds().empty())
        << "a rejected tool call must not create a body";
}

TEST(AiToolDispatcher, MoveBodyRejectsAnUnknownBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"body_id", 999}, {"dx", 1.0}, {"dy", 0.0}, {"dz", 0.0}};
    ToolResult r = executeTool(ctx, "move_body", args);
    EXPECT_FALSE(r.ok);
}

TEST(AiToolDispatcher, BooleanOpRejectsAnUnknownMode) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    ToolResult a = executeTool(ctx, "add_box", {{"width", 10.0}, {"height", 10.0}, {"depth", 10.0}});
    ToolResult b = executeTool(ctx, "add_box", {{"width", 5.0}, {"height", 5.0}, {"depth", 5.0}});
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    auto ids = doc.getAllBodyIds();
    ASSERT_EQ(ids.size(), 2u);

    nlohmann::json args = {{"target_body_id", ids[0]}, {"tool_body_id", ids[1]}, {"mode", "explode"}};
    ToolResult r = executeTool(ctx, "boolean_op", args);
    EXPECT_FALSE(r.ok);
}

TEST(AiToolDispatcher, BooleanOpUnionMergesTwoBodiesIntoOne) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    ToolResult a = executeTool(ctx, "add_box", {{"width", 10.0}, {"height", 10.0}, {"depth", 10.0}});
    ToolResult b = executeTool(ctx, "add_box",
        {{"width", 10.0}, {"height", 10.0}, {"depth", 10.0}, {"x", 5.0}});
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    auto ids = doc.getAllBodyIds();
    ASSERT_EQ(ids.size(), 2u);

    ToolResult r = executeTool(ctx, "boolean_op",
        {{"target_body_id", ids[0]}, {"tool_body_id", ids[1]}, {"mode", "union"}});
    EXPECT_TRUE(r.ok) << r.message;
    EXPECT_EQ(doc.getAllBodyIds().size(), 1u)
        << "the tool body must be consumed by a union";
}

TEST(AiToolDispatcher, UnknownToolNameIsRejected) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult r = executeTool(ctx, "delete_universe", {});
    EXPECT_FALSE(r.ok);
}

TEST(AiToolDispatcher, AddBoxWithDifferentHeightAndDepthLandsOnTheCorrectWorldAxes) {
    // Regression for the height/depth swap: setBoxExtents(x,y,z) is W/D/H, so
    // the dispatcher must call it as (w, d, h), not (w, h, d).
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"width", 5.0}, {"height", 20.0}, {"depth", 30.0}};
    ToolResult r = executeTool(ctx, "add_box", args);
    ASSERT_TRUE(r.ok) << r.message;

    double sizeY, sizeZ;
    bboxWorldYZ(doc, doc.getAllBodyIds().front(), sizeY, sizeZ);
    EXPECT_NEAR(sizeY, 20.0, 1e-6) << "world Y must be the requested height";
    EXPECT_NEAR(sizeZ, 30.0, 1e-6) << "world Z must be the requested depth";
}

TEST(AiToolDispatcher, MoveBodyRoundTripsBackToTheOriginalPosition) {
    // add_sphere and move_body must agree on which axis is "up" vs "depth".
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json addArgs = {{"radius", 2.0}, {"x", 1.0}, {"y", 10.0}, {"z", 3.0}};
    ToolResult added = executeTool(ctx, "add_sphere", addArgs);
    ASSERT_TRUE(added.ok) << added.message;
    int id = doc.getAllBodyIds().front();

    double ox0, oy0, oz0;
    bboxWorldOrigin(doc, id, ox0, oy0, oz0);

    nlohmann::json moveArgs = {{"body_id", id}, {"dx", 4.0}, {"dy", -6.0}, {"dz", 2.0}};
    ToolResult moved = executeTool(ctx, "move_body", moveArgs);
    ASSERT_TRUE(moved.ok) << moved.message;

    // Sanity-check the intermediate position uses the same user->world
    // convention as add_sphere: world = (ox+dx, oz+dz, oy+dy).
    double oxm, oym, ozm;
    bboxWorldOrigin(doc, id, oxm, oym, ozm);
    EXPECT_NEAR(oxm, ox0 + 4.0, 1e-6);
    EXPECT_NEAR(oym, oy0 + 2.0, 1e-6);
    EXPECT_NEAR(ozm, oz0 - 6.0, 1e-6);

    nlohmann::json undoArgs = {{"body_id", id}, {"dx", -4.0}, {"dy", 6.0}, {"dz", -2.0}};
    ToolResult undone = executeTool(ctx, "move_body", undoArgs);
    ASSERT_TRUE(undone.ok) << undone.message;

    double ox1, oy1, oz1;
    bboxWorldOrigin(doc, id, ox1, oy1, oz1);
    EXPECT_NEAR(ox1, ox0, 1e-6);
    EXPECT_NEAR(oy1, oy0, 1e-6);
    EXPECT_NEAR(oz1, oz0, 1e-6);
}

TEST(AiToolDispatcher, MoveBodyRejectsAFractionalBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ASSERT_TRUE(executeTool(ctx, "add_box",
        {{"width", 1.0}, {"height", 1.0}, {"depth", 1.0}}).ok);

    nlohmann::json args = {{"body_id", 1.9}, {"dx", 0.0}, {"dy", 0.0}, {"dz", 0.0}};
    ToolResult r = executeTool(ctx, "move_body", args);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("whole number"), std::string::npos) << r.message;
}

TEST(AiToolDispatcher, MoveBodyRejectsAnOutOfRangeBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"body_id", 1e20}, {"dx", 0.0}, {"dy", 0.0}, {"dz", 0.0}};
    ToolResult r = executeTool(ctx, "move_body", args);
    EXPECT_FALSE(r.ok);
}

TEST(AiToolDispatcher, RotateBodyAboutUpAxisMatchesTheHandedTransformOpRotation) {
    // rotate_body swaps axis_y/axis_z (op->setRotation(ax, az, ay, -angle))
    // to match the user->world axis convention, and negates the angle to
    // undo the reflection that swap introduces (see rotateBody's comment).
    // With axis_z=1 the world rotation axis is (0,1,0); TransformOp applies
    // gp_Trsf::SetRotation(axis, angleRad) which for axis Y implements the
    // standard right-hand rotation matrix x'=x*cos(t)+z*sin(t),
    // z'=-x*sin(t)+z*cos(t). Here t = -angle_degrees (in radians), so a
    // world point (x,y,z) maps to (-z,y,x) for a 90 degree request.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json addArgs = {{"width", 2.0}, {"height", 2.0}, {"depth", 2.0}, {"x", 10.0}};
    ASSERT_TRUE(executeTool(ctx, "add_box", addArgs).ok);
    int id = doc.getAllBodyIds().front();

    nlohmann::json rotateArgs = {{"body_id", id}, {"axis_x", 0.0}, {"axis_y", 0.0},
                                 {"axis_z", 1.0}, {"angle_degrees", 90.0}};
    ToolResult r = executeTool(ctx, "rotate_body", rotateArgs);
    ASSERT_TRUE(r.ok) << r.message;

    double x, y, z;
    bboxWorldOrigin(doc, id, x, y, z);
    EXPECT_NEAR(x, -2.0, 1e-6);
    EXPECT_NEAR(y, 0.0, 1e-6);
    EXPECT_NEAR(z, 10.0, 1e-6);
}

TEST(AiToolDispatcher, AddBoxRejectsANonNumericOptionalPosition) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"width", 5.0}, {"height", 5.0}, {"depth", 5.0}, {"x", "100"}};
    ToolResult r = executeTool(ctx, "add_box", args);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(doc.getAllBodyIds().empty())
        << "a malformed optional argument must not silently default";
}

TEST(AiToolDispatcher, CopyBodyCreatesANewBody) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);

    nlohmann::json args = {{"body_id", bodyId}, {"dx", 5.0}, {"dy", 0.0}, {"dz", 0.0}};
    ToolResult result = executeTool(ctx, "copy_body", args);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(doc.getAllBodyIds().size(), 2u);
}

TEST(AiToolDispatcher, CopyBodyRejectsAnUnknownBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {{"body_id", 9999}};
    ToolResult result = executeTool(ctx, "copy_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, DeleteBodyRemovesIt) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);

    ToolResult result = executeTool(ctx, "delete_body", {{"body_id", bodyId}});
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(doc.getAllBodyIds().size(), 0u);
}

TEST(AiToolDispatcher, DeleteBodyRejectsAnUnknownBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    ToolResult result = executeTool(ctx, "delete_body", {{"body_id", 9999}});
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, SeparateBodyRejectsAnUnknownBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    ToolResult result = executeTool(ctx, "separate_body", {{"body_id", 9999}});
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, SeparateBodySplitsATwoShellBodyIntoTwoBodies) {
    // Union two disjoint boxes so the resulting body has two disconnected
    // solid shells for separate_body to split, not just an id-rejection path.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    ToolResult a = executeTool(ctx, "add_box", {{"width", 5.0}, {"height", 5.0}, {"depth", 5.0}});
    ToolResult b = executeTool(ctx, "add_box",
        {{"width", 5.0}, {"height", 5.0}, {"depth", 5.0}, {"x", 100.0}});
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    auto ids = doc.getAllBodyIds();
    ASSERT_EQ(ids.size(), 2u);

    ToolResult unioned = executeTool(ctx, "boolean_op",
        {{"target_body_id", ids[0]}, {"tool_body_id", ids[1]}, {"mode", "union"}});
    ASSERT_TRUE(unioned.ok) << unioned.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    int bodyId = doc.getAllBodyIds().front();

    ToolResult result = executeTool(ctx, "separate_body", {{"body_id", bodyId}});
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(doc.getAllBodyIds().size(), 2u);
}

TEST(AiToolDispatcher, AlignBodyMovesTheSourcePointToTheTargetPoint) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc); // box created at the world origin

    Bnd_Box before = bboxForBody(doc, bodyId);
    nlohmann::json args = {
        {"body_id", bodyId},
        {"source_x", 0.0}, {"source_y", 0.0}, {"source_z", 0.0},
        {"target_x", 10.0}, {"target_y", 3.0}, {"target_z", 7.0}
    };
    ToolResult result = executeTool(ctx, "align_body", args);
    EXPECT_TRUE(result.ok) << result.message;
    Bnd_Box after = bboxForBody(doc, bodyId);
    // Deliberately asymmetric (10, 3, 7) offset: a wrong y/z remap in align_body
    // would shift the bbox by (10, 7, 3) instead, which this assertion catches
    // and a symmetric or on-axis-only offset would not.
    double bx0, by0, bz0, bx1, by1, bz1, ax0, ay0, az0, ax1, ay1, az1;
    before.Get(bx0, by0, bz0, bx1, by1, bz1);
    after.Get(ax0, ay0, az0, ax1, ay1, az1);
    EXPECT_NEAR(ax0 - bx0, 10.0, 1e-6);
    EXPECT_NEAR(ay0 - by0, 7.0, 1e-6);  // world Y == user-space Z (up)
    EXPECT_NEAR(az0 - bz0, 3.0, 1e-6);  // world Z == user-space Y (depth)
}

TEST(AiToolDispatcher, AlignBodyRejectsAnUnknownBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    nlohmann::json args = {
        {"body_id", 9999},
        {"source_x", 0.0}, {"source_y", 0.0}, {"source_z", 0.0},
        {"target_x", 1.0}, {"target_y", 1.0}, {"target_z", 1.0}
    };
    ToolResult result = executeTool(ctx, "align_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, AlignBodyRejectsNonFiniteCoordinates) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);

    // Not non-finite on its own, but paired with a source at the opposite
    // extreme the subtraction overflows to infinity.
    nlohmann::json args = {
        {"body_id", bodyId},
        {"source_x", -1e308}, {"source_y", 0.0}, {"source_z", 0.0},
        {"target_x", 1e308}, {"target_y", 0.0}, {"target_z", 0.0}
    };
    ToolResult result = executeTool(ctx, "align_body", args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(doc.getAllBodyIds().size(), 1u); // no mutation happened
}

TEST(AiToolDispatcher, MirrorBodyAcrossXyPlaneMirrorsTheHeightAxis) {
    // xy in user-space maps to world XZ (see the ruling above): mirroring
    // across it must flip user-space Z (height, world Y), not Y (depth,
    // world Z). Place the test box off-origin on both axes so a wrong
    // mapping (flipping depth instead of height) is distinguishable.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    ToolResult moved = executeTool(ctx, "move_body", {{"body_id", bodyId}, {"dx", 5.0}, {"dy", 3.0}, {"dz", 4.0}});
    ASSERT_TRUE(moved.ok) << moved.message;
    Bnd_Box before = bboxForBody(doc, bodyId);

    nlohmann::json args = {{"body_id", bodyId}, {"plane", "xy"}};
    ToolResult result = executeTool(ctx, "mirror_body", args);
    EXPECT_TRUE(result.ok) << result.message;
    auto ids = doc.getAllBodyIds();
    ASSERT_EQ(ids.size(), 2u); // keep_original defaults true
    int newBodyId = (ids[0] == bodyId) ? ids[1] : ids[0];

    Bnd_Box after = bboxForBody(doc, newBodyId);
    double bx0, by0, bz0, bx1, by1, bz1, ax0, ay0, az0, ax1, ay1, az1;
    before.Get(bx0, by0, bz0, bx1, by1, bz1);
    after.Get(ax0, ay0, az0, ax1, ay1, az1);
    // World X and world Z (user-space depth) stay the same; world Y
    // (user-space height) is negated.
    EXPECT_NEAR(ax0, bx0, 1e-6);
    EXPECT_NEAR(ax1, bx1, 1e-6);
    EXPECT_NEAR(az0, bz0, 1e-6);
    EXPECT_NEAR(az1, bz1, 1e-6);
    EXPECT_NEAR(ay0, -by1, 1e-6);
    EXPECT_NEAR(ay1, -by0, 1e-6);
}

TEST(AiToolDispatcher, MirrorBodyRejectsAnInvalidPlane) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);

    nlohmann::json args = {{"body_id", bodyId}, {"plane", "diagonal"}};
    ToolResult result = executeTool(ctx, "mirror_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, MirrorBodyWithKeepOriginalFalseReplacesTheBody) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);

    nlohmann::json args = {{"body_id", bodyId}, {"plane", "xy"}, {"keep_original", "false"}};
    ToolResult result = executeTool(ctx, "mirror_body", args);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(doc.getAllBodyIds().size(), 1u);
    // getMirroredBodyId() is -1 when keep_original is false - the message
    // must report the retained bodyId, not that sentinel.
    EXPECT_EQ(result.message.find("-1"), std::string::npos);
    EXPECT_NE(result.message.find(std::to_string(bodyId)), std::string::npos);
}

TEST(AiToolDispatcher, PatternBodyLinearCreatesTheRightCount) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "linear"}, {"count", 3}, {"spacing_x", 10.0}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(doc.getAllBodyIds().size(), 3u);
}

TEST(AiToolDispatcher, PatternBodyRadialCreatesTheRightCount) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "radial"}, {"count", 4}, {"total_angle_degrees", 360.0}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(doc.getAllBodyIds().size(), 4u);
}

TEST(AiToolDispatcher, PatternBodyRadialMatchesRotateBodysHandedness) {
    // count=2, total_angle_degrees=180 places the second instance at
    // angle/count = 90 degrees (see the spacing-convention ruling above) -
    // NOT 180. Compare against a single rotate_body call of 90 degrees on an
    // identical box at an off-axis position (x=10, away from the Z axis) so
    // a wrong handedness or a wrong spacing convention both produce a
    // detectable mismatch.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int a = addTestBox(ctx, doc);
    ASSERT_TRUE(executeTool(ctx, "move_body", {{"body_id", a}, {"dx", 10.0}, {"dy", 0.0}, {"dz", 0.0}}).ok);
    nlohmann::json patternArgs = {{"body_id", a}, {"type", "radial"}, {"count", 2},
                                   {"total_angle_degrees", 180.0},
                                   {"axis_x", 0.0}, {"axis_y", 0.0}, {"axis_z", 1.0},
                                   {"origin_x", 0.0}, {"origin_y", 0.0}, {"origin_z", 0.0}};
    ToolResult patternResult = executeTool(ctx, "pattern_body", patternArgs);
    ASSERT_TRUE(patternResult.ok) << patternResult.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 2u);
    auto patternIds = doc.getAllBodyIds();
    int patternedId = (patternIds[0] == a) ? patternIds[1] : patternIds[0];
    Bnd_Box patternedBox = bboxForBody(doc, patternedId);

    Document doc2;
    History hist2;
    PluginContext ctx2 = makeCtx(doc2, hist2);
    int b = addTestBox(ctx2, doc2);
    ASSERT_TRUE(executeTool(ctx2, "move_body", {{"body_id", b}, {"dx", 10.0}, {"dy", 0.0}, {"dz", 0.0}}).ok);
    nlohmann::json rotateArgs = {{"body_id", b}, {"angle_degrees", 90.0}, {"axis_x", 0.0},
                                  {"axis_y", 0.0}, {"axis_z", 1.0}};
    ToolResult rotateResult = executeTool(ctx2, "rotate_body", rotateArgs);
    ASSERT_TRUE(rotateResult.ok) << rotateResult.message;
    Bnd_Box rotatedBox = bboxForBody(doc2, b);

    double px0, py0, pz0, px1, py1, pz1, rx0, ry0, rz0, rx1, ry1, rz1;
    patternedBox.Get(px0, py0, pz0, px1, py1, pz1);
    rotatedBox.Get(rx0, ry0, rz0, rx1, ry1, rz1);
    EXPECT_NEAR(px0, rx0, 1e-6);
    EXPECT_NEAR(py0, ry0, 1e-6);
    EXPECT_NEAR(pz0, rz0, 1e-6);
    EXPECT_NEAR(px1, rx1, 1e-6);
    EXPECT_NEAR(py1, ry1, 1e-6);
    EXPECT_NEAR(pz1, rz1, 1e-6);
}

TEST(AiToolDispatcher, PatternBodyRadialWithNonzeroOriginRotatesAboutThatPoint) {
    // count=2, total_angle_degrees=180 -> the second instance sits at 90
    // degrees (angle/count, per the spacing convention above) about
    // origin=(5, 3, 0), rotating about the up axis. Origin has unequal
    // nonzero x/y so a dropped or mis-signed origin term is detectable.
    //
    // add_box places the box's CORNER at the given origin, not its center
    // (PrimitiveOp.cpp) - do NOT assume a fixed box position and hand-compute
    // the expected result from box dimensions. Instead: measure the box's
    // actual bbox CENTER before the pattern call, apply the standard
    // rotate-about-a-point formula to that measured center, and assert the
    // new instance's bbox center matches the computed value. For a standard
    // +90-degree rotation about (ox, oy) in the user X/(user-depth Y) plane:
    // x' = ox - (y - oy), y' = oy + (x - ox), z' = z (unchanged, axis is up).
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int a = addTestBox(ctx, doc);
    ASSERT_TRUE(executeTool(ctx, "move_body", {{"body_id", a}, {"dx", 15.0}, {"dy", 0.0}, {"dz", 0.0}}).ok);
    Bnd_Box beforeBox = bboxForBody(doc, a);
    double bx0, by0, bz0, bx1, by1, bz1;
    beforeBox.Get(bx0, by0, bz0, bx1, by1, bz1);
    // World Y == user height (Z), world Z == user depth (Y) (see the
    // bboxWorldYZ comment above).
    double cxUser = (bx0 + bx1) / 2.0;
    double cyUser = (bz0 + bz1) / 2.0;
    double czUser = (by0 + by1) / 2.0;
    double ox = 5.0, oy = 3.0;
    double expectedXUser = ox - (cyUser - oy);
    double expectedYUser = oy + (cxUser - ox);

    nlohmann::json patternArgs = {{"body_id", a}, {"type", "radial"}, {"count", 2},
                                   {"total_angle_degrees", 180.0},
                                   {"axis_x", 0.0}, {"axis_y", 0.0}, {"axis_z", 1.0},
                                   {"origin_x", ox}, {"origin_y", oy}, {"origin_z", 0.0}};
    ToolResult result = executeTool(ctx, "pattern_body", patternArgs);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 2u);
    auto ids = doc.getAllBodyIds();
    int newBodyId = (ids[0] == a) ? ids[1] : ids[0];
    Bnd_Box afterBox = bboxForBody(doc, newBodyId);
    double ax0, ay0, az0, ax1, ay1, az1;
    afterBox.Get(ax0, ay0, az0, ax1, ay1, az1);
    double actualXUser = (ax0 + ax1) / 2.0;
    double actualYUser = (az0 + az1) / 2.0;
    double actualZUser = (ay0 + ay1) / 2.0;
    EXPECT_NEAR(actualXUser, expectedXUser, 1e-6);
    EXPECT_NEAR(actualYUser, expectedYUser, 1e-6);
    EXPECT_NEAR(actualZUser, czUser, 1e-6);
}

TEST(AiToolDispatcher, PatternBodyRejectsOversizedLinearSpacing) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "linear"}, {"count", 3},
                            {"spacing_x", 1e300}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(doc.getAllBodyIds().size(), 1u);
}

TEST(AiToolDispatcher, PatternBodyRejectsOversizedRadialOrigin) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "radial"}, {"count", 3},
                            {"origin_x", 1e300}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(doc.getAllBodyIds().size(), 1u);
}

TEST(AiToolDispatcher, PatternBodyRejectsOversizedRadialAngle) {
    // A finite angle can still overflow PatternOp's internal degrees-to-
    // radians-per-instance conversion even though it passes a plain
    // std::isfinite check - this must be caught by the magnitude cap, not
    // just the finiteness check.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "radial"}, {"count", 3},
                            {"total_angle_degrees", 1.7e308}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(doc.getAllBodyIds().size(), 1u);
}

TEST(AiToolDispatcher, PatternBodyRejectsAnInvalidType) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "spiral"}, {"count", 3}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, PatternBodyRejectsANonIntegerCount) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "linear"}, {"count", 2.5}, {"spacing_x", 10.0}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, PatternBodyRejectsACountBelowTwo) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "linear"}, {"count", 1}, {"spacing_x", 10.0}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, PatternBodyRejectsACountAboveFiveHundred) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "linear"}, {"count", 501}, {"spacing_x", 10.0}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, PatternBodyRejectsAZeroRadialAxis) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int bodyId = addTestBox(ctx, doc);
    nlohmann::json args = {{"body_id", bodyId}, {"type", "radial"}, {"count", 4},
                            {"axis_x", 0.0}, {"axis_y", 0.0}, {"axis_z", 0.0}};
    ToolResult result = executeTool(ctx, "pattern_body", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, ConstructionAxisWorldXSucceeds) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult result = executeTool(ctx, "construction_axis", {{"type", "x"}});
    EXPECT_TRUE(result.ok) << result.message;
}

TEST(AiToolDispatcher, ConstructionAxisTwoPointsSucceeds) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    nlohmann::json args = {{"type", "two_points"},
        {"p1_x", 0.0}, {"p1_y", 0.0}, {"p1_z", 0.0},
        {"p2_x", 10.0}, {"p2_y", 0.0}, {"p2_z", 0.0}};
    ToolResult result = executeTool(ctx, "construction_axis", args);
    EXPECT_TRUE(result.ok) << result.message;
}

TEST(AiToolDispatcher, ConstructionAxisWorldYSucceeds) {
    // Baseline only (see ConstructionAxisWorldYUsesWorldZDirection below for
    // the actual remap proof) - "y" must at least be accepted.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult result = executeTool(ctx, "construction_axis", {{"type", "y"}});
    EXPECT_TRUE(result.ok) << result.message;
}

TEST(AiToolDispatcher, ConstructionAxisWorldYUsesWorldZDirection) {
    // User-space "y" (depth) must map to AxisCreationType::WorldZ, i.e. the
    // literal world direction (0,0,1) - NOT world Y (0,1,0). A swapped
    // "y"->WorldY mapping would produce a detectably different direction.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult result = executeTool(ctx, "construction_axis", {{"type", "y"}});
    ASSERT_TRUE(result.ok) << result.message;
    int axisId = doc.getAllAxisIds().empty() ? -1 : doc.getAllAxisIds().front();
    ASSERT_NE(axisId, -1);
    const AxisEntry* axis = doc.getAxis(axisId);
    ASSERT_NE(axis, nullptr);
    EXPECT_NEAR(axis->direction.X(), 0.0, 1e-9);
    EXPECT_NEAR(axis->direction.Y(), 0.0, 1e-9);
    EXPECT_NEAR(axis->direction.Z(), 1.0, 1e-9);
}

TEST(AiToolDispatcher, ConstructionAxisTwoPointsAsymmetricSwapsYAndZ) {
    // p1=(0,0,0), p2=(0, 3, 7): the dispatcher must build gp_Pnt(x, z, y) for
    // each point (world Y = user height/Z, world Z = user depth/Y). With an
    // asymmetric second point (y != z, both nonzero) a swapped p1z/p1y or
    // p2z/p2y bug produces a detectably different axis direction than the
    // correct world vector (0, 7, 3) (normalized).
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    nlohmann::json args = {{"type", "two_points"},
        {"p1_x", 0.0}, {"p1_y", 0.0}, {"p1_z", 0.0},
        {"p2_x", 0.0}, {"p2_y", 3.0}, {"p2_z", 7.0}};
    ToolResult result = executeTool(ctx, "construction_axis", args);
    ASSERT_TRUE(result.ok) << result.message;
    int axisId = doc.getAllAxisIds().empty() ? -1 : doc.getAllAxisIds().front();
    ASSERT_NE(axisId, -1);
    const AxisEntry* axis = doc.getAxis(axisId);
    ASSERT_NE(axis, nullptr);
    double mag = std::sqrt(7.0 * 7.0 + 3.0 * 3.0);
    EXPECT_NEAR(axis->direction.X(), 0.0, 1e-9);
    EXPECT_NEAR(axis->direction.Y(), 7.0 / mag, 1e-9);
    EXPECT_NEAR(axis->direction.Z(), 3.0 / mag, 1e-9);
}

TEST(AiToolDispatcher, ConstructionAxisRejectsCoincidentPoints) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    nlohmann::json args = {{"type", "two_points"},
        {"p1_x", 5.0}, {"p1_y", 5.0}, {"p1_z", 5.0},
        {"p2_x", 5.0}, {"p2_y", 5.0}, {"p2_z", 5.0}};
    ToolResult result = executeTool(ctx, "construction_axis", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, ConstructionAxisRejectsAnInvalidType) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult result = executeTool(ctx, "construction_axis", {{"type", "diagonal"}});
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, ConstructionAxisRejectsANonStringName) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    nlohmann::json args = {{"type", "x"}, {"name", 42}};
    ToolResult result = executeTool(ctx, "construction_axis", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, ConstructionPlaneXySucceeds) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult result = executeTool(ctx, "construction_plane", {{"type", "xy"}, {"offset", 5.0}});
    EXPECT_TRUE(result.ok) << result.message;
}

TEST(AiToolDispatcher, ConstructionPlaneXzSucceeds) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult result = executeTool(ctx, "construction_plane", {{"type", "xz"}, {"offset", 5.0}});
    EXPECT_TRUE(result.ok) << result.message;
}

TEST(AiToolDispatcher, ConstructionPlaneRejectsAnInvalidType) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult result = executeTool(ctx, "construction_plane", {{"type", "diagonal"}});
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, ConstructionPlaneRejectsANonStringName) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    nlohmann::json args = {{"type", "xy"}, {"name", 42}};
    ToolResult result = executeTool(ctx, "construction_plane", args);
    EXPECT_FALSE(result.ok);
}

TEST(AiToolDispatcher, ExtrudeSketchWithNoRegionIndicesExtrudesTheWholeProfile) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch", {{"sketch_id", sid}, {"distance", 5.0}});
    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    // Whole profile = both regions combined: (100 + 24) * 5.
    EXPECT_NEAR(volumeOf(doc, doc.getAllBodyIds().front()), 124.0 * 5.0, 1e-6);
}

TEST(AiToolDispatcher, ExtrudeSketchSingleRegionMatchesThatRegionsVolume) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0}}, {"distance", 5.0}});
    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    EXPECT_NEAR(volumeOf(doc, doc.getAllBodyIds().front()), 100.0 * 5.0, 1e-6);
}

TEST(AiToolDispatcher, ExtrudeSketchMultiRegionTotalVolumeIsTheSumOfBothPrisms) {
    // Ordinary new_body extrude of a compound profile produces N separate
    // prism solids, not one fused solid - assert the SUM, not a single-
    // solid shape (see PLAN.md's compound-semantics correction).
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0, 1}}, {"distance", 5.0}});
    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    EXPECT_NEAR(volumeOf(doc, doc.getAllBodyIds().front()), (100.0 + 24.0) * 5.0, 1e-6);
}

TEST(AiToolDispatcher, ExtrudeSketchNegativeDistanceSweepsTheOppositeDirection) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0}}, {"distance", -5.0}});
    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    EXPECT_NEAR(volumeOf(doc, doc.getAllBodyIds().front()), 100.0 * 5.0, 1e-6)
        << "sign reverses direction, not the resulting volume";
}

TEST(AiToolDispatcher, ExtrudeSketchSymmetricIgnoresSignAndUsesAbsoluteDistanceAsTotalThickness) {
    Document doc;
    History histPos, histNeg;
    PluginContext ctxPos = makeCtx(doc, histPos);
    int sid = addTwoRegionSketch(doc);

    Document doc2;
    PluginContext ctxNeg = makeCtx(doc2, histNeg);
    int sid2 = addTwoRegionSketch(doc2);

    ToolResult rPos = executeTool(ctxPos, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0}}, {"distance", 6.0}, {"symmetric", "true"}});
    ToolResult rNeg = executeTool(ctxNeg, "extrude_sketch",
        {{"sketch_id", sid2}, {"region_indices", {0}}, {"distance", -6.0}, {"symmetric", "true"}});
    ASSERT_TRUE(rPos.ok) << rPos.message;
    ASSERT_TRUE(rNeg.ok) << rNeg.message;
    // Total thickness is abs(distance) = 6, split 3 each way - same result
    // regardless of sign.
    EXPECT_NEAR(volumeOf(doc, doc.getAllBodyIds().front()), 100.0 * 6.0, 1e-6);
    EXPECT_NEAR(volumeOf(doc2, doc2.getAllBodyIds().front()), 100.0 * 6.0, 1e-6);
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsAnOutOfRangeRegionIndex) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);
    int before = hist.stepCount();

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {5}}, {"distance", 5.0}});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("5"), std::string::npos) << r.message;
    EXPECT_EQ(hist.stepCount(), before);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsANonIntegerRegionIndex) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0.5}}, {"distance", 5.0}});
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsAnOutOfIntRangeRegionIndex) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {1e100}}, {"distance", 5.0}});
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsANonArrayRegionIndices) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", 0}, {"distance", 5.0}});
    EXPECT_FALSE(r.ok);
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsDuplicateRegionIndices) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0, 0}}, {"distance", 5.0}});
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsAnUnknownSketchId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);

    ToolResult r = executeTool(ctx, "extrude_sketch", {{"sketch_id", 999}, {"distance", 5.0}});
    EXPECT_FALSE(r.ok);
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsZeroDistance) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch", {{"sketch_id", sid}, {"distance", 0.0}});
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsNonFiniteDistance) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    nlohmann::json nanArgs = {{"sketch_id", sid}, {"distance", std::nan("")}};
    nlohmann::json infArgs = {{"sketch_id", sid}, {"distance", std::numeric_limits<double>::infinity()}};
    EXPECT_FALSE(executeTool(ctx, "extrude_sketch", nanArgs).ok);
    EXPECT_FALSE(executeTool(ctx, "extrude_sketch", infArgs).ok);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchModeSubtractRequiresTargetBodyId) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0}}, {"distance", 5.0}, {"mode", "subtract"}});
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchModeSubtractCutsTheTargetBody) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    // Target: a 30x30x30 box straddling the sketch plane (y in [-15,15] world,
    // matching the sketch plane through the origin) so the small region-0
    // prism cuts into it rather than missing it entirely.
    ToolResult box = executeTool(ctx, "add_box",
        {{"width", 30.0}, {"height", 30.0}, {"depth", 30.0}, {"x", -10.0}, {"y", -15.0}, {"z", -15.0}});
    ASSERT_TRUE(box.ok) << box.message;
    int targetId = doc.getAllBodyIds().front();
    double before = volumeOf(doc, targetId);

    int sid = addTwoRegionSketch(doc);
    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0}}, {"distance", 5.0},
         {"mode", "subtract"}, {"target_body_id", targetId}});
    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    EXPECT_LT(volumeOf(doc, doc.getAllBodyIds().front()), before)
        << "subtract must reduce the target body's volume";
}

TEST(AiToolDispatcher, ExtrudeSketchRejectsAnEmptySketchWithNoValidProfile) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    auto sk = std::make_shared<materializr::Sketch>();
    sk->setPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0), gp_Dir(1, 0, 0))));
    int sid = doc.addSketch(sk, "Empty Sketch"); // no geometry at all

    ToolResult r = executeTool(ctx, "extrude_sketch", {{"sketch_id", sid}, {"distance", 5.0}});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("no valid profile"), std::string::npos) << r.message;
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}

TEST(AiToolDispatcher, ExtrudeSketchSubtractConsumingTheEntireTargetFailsAndLeavesItUnchanged) {
    // Exercises the checked pushOperation() path itself: ExtrudeOp::execute()
    // structurally succeeds the boolean build but its own commitGuard rejects
    // a near-zero-mass result (the cut consumed the whole target) and
    // returns false - the dispatcher must surface that as a failure and
    // leave the target body untouched, not silently report success.
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    ToolResult box = executeTool(ctx, "add_box", {{"width", 5.0}, {"height", 5.0}, {"depth", 5.0}});
    ASSERT_TRUE(box.ok) << box.message;
    int targetId = doc.getAllBodyIds().front();
    double before = volumeOf(doc, targetId);
    int stepsBefore = hist.stepCount();

    // A sketch region far larger than the 5x5x5 box, swept far enough to
    // fully engulf it in every axis.
    auto sk = std::make_shared<materializr::Sketch>();
    sk->setPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0), gp_Dir(1, 0, 0))));
    addRect(*sk, -10, -10, 10, 10);
    int sid = doc.addSketch(sk, "Engulfing Sketch");

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"distance", 20.0}, {"mode", "subtract"}, {"target_body_id", targetId}});
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(hist.stepCount(), stepsBefore)
        << "a failed extrude must not append a history step";
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    EXPECT_NEAR(volumeOf(doc, targetId), before, 1e-6)
        << "the target body's geometry must be unchanged after a failed subtract";
}

TEST(AiToolDispatcher, ExtrudeSketchUndoRemovesTheNewBodyAndRedoRestoresIt) {
    Document doc;
    History hist;
    PluginContext ctx = makeCtx(doc, hist);
    int sid = addTwoRegionSketch(doc);

    ToolResult r = executeTool(ctx, "extrude_sketch",
        {{"sketch_id", sid}, {"region_indices", {0}}, {"distance", 5.0}});
    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);

    ASSERT_TRUE(hist.undo(doc));
    EXPECT_TRUE(doc.getAllBodyIds().empty()) << "undo must remove the extruded body";

    ASSERT_TRUE(hist.redo(doc));
    ASSERT_EQ(doc.getAllBodyIds().size(), 1u);
    EXPECT_NEAR(volumeOf(doc, doc.getAllBodyIds().front()), 100.0 * 5.0, 1e-6);
}
