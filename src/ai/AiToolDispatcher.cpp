#include "AiToolDispatcher.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../modeling/PrimitiveOp.h"
#include "../modeling/TransformOp.h"
#include "../modeling/BooleanOp.h"
#include "../modeling/CopyOp.h"
#include "../modeling/DeleteOp.h"
#include "../modeling/SeparateBodyOp.h"
#include "../modeling/MirrorOp.h"
#include "../modeling/PatternOp.h"
#include "../modeling/ConstructionAxisOp.h"
#include "../modeling/ConstructionPlaneOp.h"
#include "../modeling/ExtrudeOp.h"
#include "../modeling/Sketch.h"

#include <gp_Pnt.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>

#include <cmath>
#include <limits>
#include <set>

namespace materializr { namespace ai {

namespace {

// nlohmann::json::value(key, default) already does exactly what an optional
// numeric arg needs; this wraps the REQUIRED case so a missing key is a
// deliberate error rather than silently defaulting to 0.
bool requireNumber(const nlohmann::json& args, const char* key, double& out,
                   std::string& err) {
    if (!args.contains(key) || !args[key].is_number()) {
        err = std::string("missing or non-numeric required argument '") + key + "'";
        return false;
    }
    out = args[key].get<double>();
    return true;
}
bool requirePositive(const nlohmann::json& args, const char* key, double& out,
                     std::string& err) {
    if (!requireNumber(args, key, out, err)) return false;
    if (out <= 0.0) {
        err = std::string("'") + key + "' must be positive, got " + std::to_string(out);
        return false;
    }
    return true;
}
// Shared by requireBodyId/requireSketchId/validateRegionIndex: finite ->
// representable as int -> integral, in that order (reject before cast - a
// value like 1e100 is finite and would satisfy floor(v)==v, but is not a
// safe int conversion). `label` names the value in error messages (e.g.
// "'body_id'" or "a 'region_indices' value").
bool parseWholeNumber(double raw, int& out, const std::string& label, std::string& err) {
    if (!std::isfinite(raw) ||
        raw < static_cast<double>(std::numeric_limits<int>::min()) ||
        raw > static_cast<double>(std::numeric_limits<int>::max())) {
        err = label + " is out of range";
        return false;
    }
    if (raw != std::floor(raw)) {
        err = label + " must be a whole number, got " + std::to_string(raw);
        return false;
    }
    out = static_cast<int>(raw);
    return true;
}
bool requireBodyId(Document& doc, const nlohmann::json& args, const char* key,
                   int& out, std::string& err) {
    double raw;
    if (!requireNumber(args, key, raw, err)) return false;
    if (!parseWholeNumber(raw, out, std::string("'") + key + "'", err)) return false;
    for (int id : doc.getAllBodyIds()) if (id == out) return true;
    err = std::string("no body with id ") + std::to_string(out);
    return false;
}
bool requireSketchId(Document& doc, const nlohmann::json& args, const char* key,
                     int& out, std::string& err) {
    double raw;
    if (!requireNumber(args, key, raw, err)) return false;
    if (!parseWholeNumber(raw, out, std::string("'") + key + "'", err)) return false;
    for (int id : doc.getAllSketchIds()) if (id == out) return true;
    err = std::string("no sketch with id ") + std::to_string(out);
    return false;
}
// Validates one region_indices element: finite -> representable as int ->
// integral -> non-negative, in that order.
bool validateRegionIndex(const nlohmann::json& v, std::string& err, int& out) {
    if (!v.is_number()) {
        err = "'region_indices' must contain only numbers";
        return false;
    }
    if (!parseWholeNumber(v.get<double>(), out, "a 'region_indices' value", err)) return false;
    if (out < 0) {
        err = "'region_indices' must not contain negative values";
        return false;
    }
    return true;
}
// Distinguishes "absent" (use fallback) from "present but wrong type" (reject) -
// optNumber's single-return-value shape couldn't tell those apart, silently
// defaulting a malformed call instead of rejecting it.
bool optionalNumber(const nlohmann::json& args, const char* key, double& out,
                    double fallback, std::string& err) {
    if (!args.contains(key)) { out = fallback; return true; }
    if (!args[key].is_number()) {
        err = std::string("'") + key + "' must be a number";
        return false;
    }
    out = args[key].get<double>();
    return true;
}
bool requireFiniteNumber(const nlohmann::json& args, const char* key, double& out, std::string& err) {
    if (!requireNumber(args, key, out, err)) return false;
    if (!std::isfinite(out)) {
        err = std::string(key) + " must be a finite number";
        return false;
    }
    return true;
}

bool optionalFiniteNumber(const nlohmann::json& args, const char* key, double& out, double fallback, std::string& err) {
    if (!optionalNumber(args, key, out, fallback, err)) return false;
    if (!std::isfinite(out)) {
        err = std::string(key) + " must be a finite number";
        return false;
    }
    return true;
}

ToolResult addPrimitive(PluginContext& ctx, PrimitiveOp::Kind kind,
                        const nlohmann::json& args) {
    std::string err;
    auto op = std::make_unique<PrimitiveOp>();
    op->setKind(kind);
    switch (kind) {
        case PrimitiveOp::Kind::Box: {
            double w, h, d;
            if (!requirePositive(args, "width", w, err) ||
                !requirePositive(args, "height", h, err) ||
                !requirePositive(args, "depth", d, err))
                return {false, err};
            // PrimitiveOp::setBoxExtents(x,y,z) takes W(x)/D(y)/H(z) - depth in the
            // middle slot, height last - so the call order is (w, d, h), not (w, h, d).
            op->setBoxExtents(w, d, h);
            break;
        }
        case PrimitiveOp::Kind::Cylinder: {
            double r, h;
            if (!requirePositive(args, "radius", r, err) ||
                !requirePositive(args, "height", h, err))
                return {false, err};
            op->setRadius(r);
            op->setHeight(h);
            break;
        }
        case PrimitiveOp::Kind::Sphere: {
            double r;
            if (!requirePositive(args, "radius", r, err)) return {false, err};
            op->setRadius(r);
            break;
        }
        case PrimitiveOp::Kind::Cone: {
            double br, tr, h;
            if (!requirePositive(args, "bottom_radius", br, err) ||
                !requireNumber(args, "top_radius", tr, err) ||
                !requirePositive(args, "height", h, err))
                return {false, err};
            if (tr < 0.0) return {false, "'top_radius' must not be negative"};
            op->setRadius(br);
            op->setTopRadius(tr);
            op->setHeight(h);
            break;
        }
        case PrimitiveOp::Kind::Torus: {
            double major, minor;
            if (!requirePositive(args, "major_radius", major, err) ||
                !requirePositive(args, "minor_radius", minor, err))
                return {false, err};
            if (major <= minor)
                return {false, "'major_radius' must be greater than 'minor_radius'"};
            op->setRadius(major);
            op->setMinorRadius(minor);
            break;
        }
    }
    double x, y, z;
    if (!optionalNumber(args, "x", x, 0.0, err) ||
        !optionalNumber(args, "y", y, 0.0, err) ||
        !optionalNumber(args, "z", z, 0.0, err))
        return {false, err};
    op->setOrigin(x, y, z);
    if (!ctx.history().pushOperation(std::move(op), ctx.document()))
        return {false, "the operation failed to execute"};
    // pushOperation only touches the Document - without this, the new body
    // never reaches the renderer (same class of bug as BooleanPlugin's
    // partial-subtract gap: nothing marks the viewport dirty on its own).
    ctx.markMeshesDirty();
    // PrimitiveOp appends the new body, so its id is the last one - see Document::addBody.
    int newId = ctx.document().getAllBodyIds().back();
    return {true, "Created body " + std::to_string(newId)};
}

ToolResult moveBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    double dx, dy, dz;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err) ||
        !requireNumber(args, "dx", dx, err) ||
        !requireNumber(args, "dy", dy, err) ||
        !requireNumber(args, "dz", dz, err))
        return {false, err};
    auto op = std::make_unique<TransformOp>();
    op->setBodyId(bodyId);
    op->setType(TransformType::Translate);
    // TransformOp::setTranslation passes its args straight through as raw world
    // coordinates, unlike PrimitiveOp::setOrigin, which remaps user (x,y,z) to
    // world (x,z,y) - see PrimitiveOp.cpp's worldPnt() comment (user Z "up" ->
    // world Y, user Y "depth" -> world Z). Swap dy/dz here so move_body uses the
    // SAME user-space convention as add_* - do not "fix" this back to (dx,dy,dz).
    op->setTranslation(dx, dz, dy);
    if (!ctx.history().pushOperation(std::move(op), ctx.document()))
        return {false, "the operation failed to execute"};
    ctx.markMeshesDirty();
    return {true, "Moved body " + std::to_string(bodyId)};
}

ToolResult rotateBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    double ax, ay, az, angle;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err) ||
        !requireNumber(args, "axis_x", ax, err) ||
        !requireNumber(args, "axis_y", ay, err) ||
        !requireNumber(args, "axis_z", az, err) ||
        !requireNumber(args, "angle_degrees", angle, err))
        return {false, err};
    if (ax == 0.0 && ay == 0.0 && az == 0.0)
        return {false, "the rotation axis must not be the zero vector"};
    auto op = std::make_unique<TransformOp>();
    op->setBodyId(bodyId);
    op->setType(TransformType::Rotate);
    // Same user-to-world axis swap as moveBody's setTranslation (see its comment)
    // applied to the rotation axis, so an AI-issued axis of (0,0,1) ("rotate
    // around up") means world Y, consistent with add_*'s origin convention.
    // The (x,y,z)->(x,z,y) map has determinant -1 (a reflection, not a pure
    // rotation), so swapping only the axis flips the rotation's handedness;
    // negating the angle restores the sense the user intended.
    op->setRotation(ax, az, ay, -angle);
    if (!ctx.history().pushOperation(std::move(op), ctx.document()))
        return {false, "the operation failed to execute"};
    ctx.markMeshesDirty();
    return {true, "Rotated body " + std::to_string(bodyId)};
}

ToolResult scaleBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    double factor;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err) ||
        !requirePositive(args, "factor", factor, err))
        return {false, err};
    auto op = std::make_unique<TransformOp>();
    op->setBodyId(bodyId);
    op->setType(TransformType::Scale);
    op->setScale(factor);
    if (!ctx.history().pushOperation(std::move(op), ctx.document()))
        return {false, "the operation failed to execute"};
    ctx.markMeshesDirty();
    return {true, "Scaled body " + std::to_string(bodyId)};
}

ToolResult booleanOp(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int targetId, toolId;
    if (!requireBodyId(ctx.document(), args, "target_body_id", targetId, err) ||
        !requireBodyId(ctx.document(), args, "tool_body_id", toolId, err))
        return {false, err};
    if (targetId == toolId)
        return {false, "target_body_id and tool_body_id must be different bodies"};
    if (!args.contains("mode") || !args["mode"].is_string())
        return {false, "missing required argument 'mode'"};
    std::string modeStr = args["mode"].get<std::string>();
    BooleanMode mode;
    if (modeStr == "union") mode = BooleanMode::Union;
    else if (modeStr == "subtract") mode = BooleanMode::Subtract;
    else if (modeStr == "intersect") mode = BooleanMode::Intersect;
    else return {false, "'mode' must be one of: union, subtract, intersect"};

    auto op = std::make_unique<BooleanOp>();
    op->setTargetBodyId(targetId);
    op->setToolBodyId(toolId);
    op->setMode(mode);
    if (!ctx.history().pushOperation(std::move(op), ctx.document()))
        return {false, "the operation failed to execute"};
    ctx.markMeshesDirty();
    return {true, "Combined bodies " + std::to_string(targetId) + " and " +
                  std::to_string(toolId) + " (" + modeStr + ")"};
}

ToolResult copyBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err)) return {false, err};
    double dx = 0, dy = 0, dz = 0;
    if (!optionalFiniteNumber(args, "dx", dx, 0.0, err)) return {false, err};
    if (!optionalFiniteNumber(args, "dy", dy, 0.0, err)) return {false, err};
    if (!optionalFiniteNumber(args, "dz", dz, 0.0, err)) return {false, err};
    auto op = std::make_unique<CopyOp>();
    op->setSourceBodyId(bodyId);
    op->setOffset(dx, dz, dy);
    CopyOp* raw = op.get();
    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    return {true, "Copied body " + std::to_string(bodyId) + " to new body " +
                  std::to_string(raw->getCreatedBodyId()) + "."};
}

ToolResult deleteBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err)) return {false, err};
    auto op = std::make_unique<DeleteOp>();
    op->setBodyId(bodyId);
    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    return {true, "Deleted body " + std::to_string(bodyId) + "."};
}

ToolResult separateBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err)) return {false, err};
    auto op = std::make_unique<SeparateBodyOp>();
    op->setBody(bodyId);
    SeparateBodyOp* raw = op.get();
    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    // getNewBodyIds() returns only the NEW bodies split off, not counting the
    // original body id that survives - include both counts explicitly.
    std::string idList;
    for (int id : raw->getNewBodyIds()) idList += std::to_string(id) + " ";
    return {true, "Separated body " + std::to_string(bodyId) + " into " +
                  std::to_string(raw->getNewBodyIds().size() + 1) + " bodies total: original id " +
                  std::to_string(bodyId) + ", new ids " + idList + "."};
}

ToolResult alignBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err)) return {false, err};
    double sx, sy, sz, tx, ty, tz;
    if (!requireFiniteNumber(args, "source_x", sx, err)) return {false, err};
    if (!requireFiniteNumber(args, "source_y", sy, err)) return {false, err};
    if (!requireFiniteNumber(args, "source_z", sz, err)) return {false, err};
    if (!requireFiniteNumber(args, "target_x", tx, err)) return {false, err};
    if (!requireFiniteNumber(args, "target_y", ty, err)) return {false, err};
    if (!requireFiniteNumber(args, "target_z", tz, err)) return {false, err};
    double dx = tx - sx, dy = ty - sy, dz = tz - sz;
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) {
        return {false, "the distance between source and target is too large to represent"};
    }
    // Implemented as a TransformOp translation by (target - source), the same
    // verified path move_body already uses, rather than AlignOp directly - see
    // the Codex round-1 ruling above (AlignOp drops face lineage and has no
    // verified call-site in this branch).
    auto op = std::make_unique<TransformOp>();
    op->setBodyId(bodyId);
    op->setType(TransformType::Translate);
    op->setTranslation(dx, dz, dy);
    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    return {true, "Aligned body " + std::to_string(bodyId) + "."};
}

ToolResult mirrorBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err)) return {false, err};
    if (!args.contains("plane") || !args["plane"].is_string()) {
        return {false, "plane is required and must be a string"};
    }
    std::string plane = args["plane"].get<std::string>();
    // User-space xy (world X/Z) -> MirrorPlane::XZ; user-space xz (world X/Y)
    // -> MirrorPlane::XY. See the Codex ruling above - do not map these two
    // straight through by name, that mirrors the wrong axis.
    MirrorPlane mp;
    if (plane == "xy") mp = MirrorPlane::XZ;
    else if (plane == "xz") mp = MirrorPlane::XY;
    else if (plane == "yz") mp = MirrorPlane::YZ;
    else return {false, "plane must be \"xy\", \"xz\", or \"yz\""};
    bool keep = true;
    if (args.contains("keep_original")) {
        if (!args["keep_original"].is_string()) return {false, "keep_original must be a string"};
        std::string k = args["keep_original"].get<std::string>();
        if (k == "false") keep = false;
        else if (k != "true") return {false, "keep_original must be \"true\" or \"false\""};
    }
    auto op = std::make_unique<MirrorOp>();
    op->setBody(bodyId);
    op->setPlane(mp);
    op->setKeepOriginal(keep);
    MirrorOp* raw = op.get();
    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    // getMirroredBodyId() is -1 when keep_original is false (MirrorOp.cpp
    // updates the original body in place instead of creating a new one) -
    // report the retained bodyId in that case, not the sentinel.
    if (keep) {
        return {true, "Mirrored body " + std::to_string(bodyId) + " across the " + plane +
                      " plane. New body id " + std::to_string(raw->getMirroredBodyId()) + "."};
    }
    return {true, "Mirrored body " + std::to_string(bodyId) + " across the " + plane +
                  " plane in place (original replaced, body id " + std::to_string(bodyId) + " unchanged)."};
}

ToolResult patternBody(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int bodyId;
    if (!requireBodyId(ctx.document(), args, "body_id", bodyId, err)) return {false, err};
    if (!args.contains("type") || !args["type"].is_string()) {
        return {false, "type is required and must be a string"};
    }
    std::string type = args["type"].get<std::string>();
    double countRaw;
    if (!requirePositive(args, "count", countRaw, err)) return {false, err};
    if (!std::isfinite(countRaw) || countRaw != std::floor(countRaw)) {
        return {false, "count must be a finite whole number"};
    }
    if (countRaw < 2.0 || countRaw > 500.0) {
        return {false, "count must be between 2 and 500"};
    }
    int count = static_cast<int>(countRaw);

    auto op = std::make_unique<PatternOp>();
    op->setBody(bodyId);
    op->setCount(count);
    PatternOp* rawPattern = op.get();

    // PatternOp multiplies spacing/angle by instance index internally
    // (count up to 500) - a per-step value near the double range limit
    // would overflow partway through the array. Cap per-step inputs so
    // count * value stays comfortably finite (1e15 leaves enormous
    // headroom below overflow even at count=500 while accepting every
    // realistic model dimension).
    constexpr double kMaxPerStepMagnitude = 1e15;

    if (type == "linear") {
        double sx = 0, sy = 0, sz = 0;
        if (!optionalFiniteNumber(args, "spacing_x", sx, 0.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "spacing_y", sy, 0.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "spacing_z", sz, 0.0, err)) return {false, err};
        if (std::fabs(sx) > kMaxPerStepMagnitude || std::fabs(sy) > kMaxPerStepMagnitude ||
            std::fabs(sz) > kMaxPerStepMagnitude) {
            return {false, "spacing values are too large"};
        }
        op->setType(PatternType::Linear);
        op->setLinearSpacing(sx, sz, sy);
    } else if (type == "radial") {
        double ax = 0, ay = 0, az = 1, ox = 0, oy = 0, oz = 0, angle = 360;
        if (!optionalFiniteNumber(args, "axis_x", ax, 0.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "axis_y", ay, 0.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "axis_z", az, 1.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "origin_x", ox, 0.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "origin_y", oy, 0.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "origin_z", oz, 0.0, err)) return {false, err};
        if (!optionalFiniteNumber(args, "total_angle_degrees", angle, 360.0, err)) return {false, err};
        double axisLen = std::sqrt(ax * ax + ay * ay + az * az);
        if (!std::isfinite(axisLen) || axisLen < 1e-9) {
            return {false, "the radial axis must not be the zero vector or too large to represent"};
        }
        if (std::fabs(ox) > kMaxPerStepMagnitude || std::fabs(oy) > kMaxPerStepMagnitude ||
            std::fabs(oz) > kMaxPerStepMagnitude || std::fabs(angle) > kMaxPerStepMagnitude) {
            // PatternOp.cpp converts (angle / count) to radians per instance -
            // a finite but huge angle (e.g. 1.7e308) overflows that
            // multiplication even though it passed the plain isfinite check.
            return {false, "origin or angle values are too large"};
        }
        op->setType(PatternType::Radial);
        op->setRadialAxis(ax, az, ay);
        op->setRadialOrigin(ox, oz, oy);
        // Negate, matching rotateBody's existing -angle: the user-to-world
        // coordinate swap has determinant -1, reversing rotation handedness.
        op->setTotalAngle(-angle);
    } else {
        return {false, "type must be \"linear\" or \"radial\""};
    }

    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    std::string newIds;
    for (int id : rawPattern->getCreatedBodyIds()) newIds += std::to_string(id) + " ";
    return {true, "Created a " + type + " pattern of body " + std::to_string(bodyId) +
                  " with " + std::to_string(count) + " instances. New body ids: " + newIds};
}

ToolResult constructionAxis(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    if (!args.contains("type") || !args["type"].is_string()) {
        return {false, "type is required and must be a string"};
    }
    std::string type = args["type"].get<std::string>();
    auto op = std::make_unique<ConstructionAxisOp>();
    if (type == "x") {
        op->setType(AxisCreationType::WorldX);
    } else if (type == "y") {
        // User-space Y (depth) maps to world Z - confirmed against
        // Application_InteractiveOps.cpp:2157.
        op->setType(AxisCreationType::WorldZ);
    } else if (type == "z") {
        // User-space Z (up) maps to world Y - confirmed against
        // Application_InteractiveOps.cpp:2157.
        op->setType(AxisCreationType::WorldY);
    } else if (type == "two_points") {
        double p1x, p1y, p1z, p2x, p2y, p2z;
        if (!requireFiniteNumber(args, "p1_x", p1x, err)) return {false, err};
        if (!requireFiniteNumber(args, "p1_y", p1y, err)) return {false, err};
        if (!requireFiniteNumber(args, "p1_z", p1z, err)) return {false, err};
        if (!requireFiniteNumber(args, "p2_x", p2x, err)) return {false, err};
        if (!requireFiniteNumber(args, "p2_y", p2y, err)) return {false, err};
        if (!requireFiniteNumber(args, "p2_z", p2z, err)) return {false, err};
        double dist = std::sqrt((p2x - p1x) * (p2x - p1x) + (p2y - p1y) * (p2y - p1y) +
                                 (p2z - p1z) * (p2z - p1z));
        if (!std::isfinite(dist) || dist < 1e-6) {
            // ConstructionAxisOp itself silently falls back to World X for
            // near-coincident points (< 1e-9) - reject explicitly instead of
            // letting the model get an axis it never asked for.
            return {false, "p1 and p2 must not be the same point"};
        }
        op->setType(AxisCreationType::TwoPoints);
        op->setPoints(gp_Pnt(p1x, p1z, p1y), gp_Pnt(p2x, p2z, p2y));
    } else {
        return {false, "type must be \"x\", \"y\", \"z\", or \"two_points\""};
    }
    if (args.contains("name")) {
        if (!args["name"].is_string()) return {false, "name must be a string"};
        op->setName(args["name"].get<std::string>());
    }
    ConstructionAxisOp* raw = op.get();
    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    return {true, "Created construction axis " + std::to_string(raw->getCreatedAxisId()) + "."};
}

ToolResult constructionPlane(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    if (!args.contains("type") || !args["type"].is_string()) {
        return {false, "type is required and must be a string"};
    }
    std::string type = args["type"].get<std::string>();
    PlaneCreationType pt;
    if (type == "xy") pt = PlaneCreationType::XY;
    else if (type == "xz") pt = PlaneCreationType::XZ;
    else if (type == "yz") pt = PlaneCreationType::YZ;
    else return {false, "type must be \"xy\", \"xz\", or \"yz\""};
    double offset = 0;
    if (!optionalFiniteNumber(args, "offset", offset, 0.0, err)) return {false, err};
    auto op = std::make_unique<ConstructionPlaneOp>();
    op->setType(pt);
    op->setOffset(offset);
    if (args.contains("name")) {
        if (!args["name"].is_string()) return {false, "name must be a string"};
        op->setName(args["name"].get<std::string>());
    }
    if (!ctx.history().pushOperation(std::move(op), ctx.document())) {
        return {false, "the operation failed to execute"};
    }
    ctx.markMeshesDirty();
    return {true, "Created a " + type + " construction plane."};
}

ToolResult extrudeSketch(PluginContext& ctx, const nlohmann::json& args) {
    std::string err;
    int sketchId;
    if (!requireSketchId(ctx.document(), args, "sketch_id", sketchId, err))
        return {false, err};
    auto sketch = ctx.document().getSketch(sketchId);
    // requireSketchId already confirmed sketchId is in getAllSketchIds(),
    // which getSketch() reads from the same store - this should never be
    // null, but don't dereference blindly.
    if (!sketch) return {false, "no sketch with id " + std::to_string(sketchId)};

    if (args.contains("region_indices") && !args["region_indices"].is_array())
        return {false, "'region_indices' must be an array"};

    // Cheap argument checks first, real OCCT geometry work (region
    // resolution below) last - matches every sibling handler's order
    // (booleanOp validates both ids and mode before touching the kernel;
    // patternBody validates count before building the op).
    double distance;
    if (!requireFiniteNumber(args, "distance", distance, err)) return {false, err};
    if (distance == 0.0) return {false, "'distance' must not be zero"};

    bool symmetric = false;
    if (args.contains("symmetric")) {
        if (!args["symmetric"].is_string()) return {false, "'symmetric' must be a string"};
        std::string s = args["symmetric"].get<std::string>();
        if (s == "true") symmetric = true;
        else if (s != "false") return {false, "'symmetric' must be \"true\" or \"false\""};
    }

    ExtrudeMode mode = ExtrudeMode::NewBody;
    std::string modeStr = "new_body";
    if (args.contains("mode")) {
        if (!args["mode"].is_string()) return {false, "'mode' must be a string"};
        modeStr = args["mode"].get<std::string>();
        if (modeStr == "new_body") mode = ExtrudeMode::NewBody;
        else if (modeStr == "union") mode = ExtrudeMode::Union;
        else if (modeStr == "subtract") mode = ExtrudeMode::Subtract;
        else if (modeStr == "intersect") mode = ExtrudeMode::Intersect;
        else return {false, "'mode' must be one of: new_body, union, subtract, intersect"};
    }

    int targetBodyId = -1;
    if (mode != ExtrudeMode::NewBody) {
        if (!requireBodyId(ctx.document(), args, "target_body_id", targetBodyId, err))
            return {false, err};
    }

    bool haveIndices = args.contains("region_indices") && !args["region_indices"].empty();
    TopoDS_Shape profile;
    if (haveIndices) {
        const auto& arr = args["region_indices"];
        // A valid array can never usefully hold more entries than the
        // sketch has regions (duplicates are rejected below) - bounding
        // against the real region count, not an arbitrary constant, means
        // an oversized array fails on its very first element rather than
        // being fully validated/deduped before any bounds check runs.
        auto regions = sketch->buildRegions();
        if (arr.size() > regions.size())
            return {false, "'region_indices' has more entries (" + std::to_string(arr.size()) +
                          ") than the sketch has regions (" + std::to_string(regions.size()) + ")"};
        std::set<int> seen;
        std::vector<TopoDS_Face> faces;
        for (const auto& v : arr) {
            int idx;
            if (!validateRegionIndex(v, err, idx)) return {false, err};
            if (!seen.insert(idx).second)
                return {false, "'region_indices' contains a duplicate index: " +
                              std::to_string(idx)};
            if (idx >= static_cast<int>(regions.size())) {
                return {false, "region_index " + std::to_string(idx) +
                              " out of range (sketch has " + std::to_string(regions.size()) +
                              " regions)"};
            }
            faces.push_back(regions[idx].face);
        }
        if (faces.size() == 1) {
            profile = faces.front();
        } else {
            // One extrude producing N independent solids - MakePrism sweeps
            // each face of a compound separately, it does not fuse them
            // (Symmetric mode is the exception: it fuses the whole
            // profile's up/down sweep regardless of region count - see
            // ExtrudeOp::execute()). Same shape as the interactive multi-
            // region extrude in Application.cpp.
            TopoDS_Compound comp;
            BRep_Builder bb;
            bb.MakeCompound(comp);
            for (const auto& f : faces) bb.Add(comp, f);
            profile = comp;
        }
    } else {
        profile = sketch->buildProfileShape();
    }
    if (profile.IsNull()) {
        return {false, "sketch " + std::to_string(sketchId) + " has no valid profile to extrude"};
    }

    auto op = std::make_unique<ExtrudeOp>();
    op->setProfile(profile);
    op->setSketchSource(sketchId);
    op->setDistance(distance);
    op->setDirection(symmetric ? ExtrudeDirection::Symmetric : ExtrudeDirection::Normal);
    op->setMode(mode);
    op->setTargetBody(targetBodyId);
    ExtrudeOp* raw = op.get();
    if (!ctx.history().pushOperation(std::move(op), ctx.document()))
        return {false, "the operation failed to execute"};
    ctx.markMeshesDirty();
    if (mode == ExtrudeMode::NewBody) {
        return {true, "Extruded sketch " + std::to_string(sketchId) + " into new body " +
                      std::to_string(raw->createdBodyId()) + "."};
    }
    return {true, "Extruded sketch " + std::to_string(sketchId) + " and combined it into body " +
                  std::to_string(targetBodyId) + " (" + modeStr + ")."};
}

} // namespace

ToolResult executeTool(PluginContext& ctx, const std::string& toolName,
                       const nlohmann::json& args) {
    if (toolName == "add_box") return addPrimitive(ctx, PrimitiveOp::Kind::Box, args);
    if (toolName == "add_cylinder") return addPrimitive(ctx, PrimitiveOp::Kind::Cylinder, args);
    if (toolName == "add_sphere") return addPrimitive(ctx, PrimitiveOp::Kind::Sphere, args);
    if (toolName == "add_cone") return addPrimitive(ctx, PrimitiveOp::Kind::Cone, args);
    if (toolName == "add_torus") return addPrimitive(ctx, PrimitiveOp::Kind::Torus, args);
    if (toolName == "move_body") return moveBody(ctx, args);
    if (toolName == "rotate_body") return rotateBody(ctx, args);
    if (toolName == "scale_body") return scaleBody(ctx, args);
    if (toolName == "boolean_op") return booleanOp(ctx, args);
    if (toolName == "copy_body") return copyBody(ctx, args);
    if (toolName == "delete_body") return deleteBody(ctx, args);
    if (toolName == "separate_body") return separateBody(ctx, args);
    if (toolName == "align_body") return alignBody(ctx, args);
    if (toolName == "mirror_body") return mirrorBody(ctx, args);
    if (toolName == "pattern_body") return patternBody(ctx, args);
    if (toolName == "construction_axis") return constructionAxis(ctx, args);
    if (toolName == "construction_plane") return constructionPlane(ctx, args);
    if (toolName == "extrude_sketch") return extrudeSketch(ctx, args);
    return {false, "unknown tool '" + toolName + "'"};
}

} } // namespace materializr::ai
