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

#include <algorithm>
#include <cmath>
#include <limits>

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
bool requireBodyId(Document& doc, const nlohmann::json& args, const char* key,
                   int& out, std::string& err) {
    double raw;
    if (!requireNumber(args, key, raw, err)) return false;
    if (!std::isfinite(raw) ||
        raw < static_cast<double>(std::numeric_limits<int>::min()) ||
        raw > static_cast<double>(std::numeric_limits<int>::max())) {
        err = std::string("'") + key + "' is out of range";
        return false;
    }
    if (raw != std::floor(raw)) {
        err = std::string("'") + key + "' must be a whole number, got " +
              std::to_string(raw);
        return false;
    }
    out = static_cast<int>(raw);
    for (int id : doc.getAllBodyIds()) if (id == out) return true;
    err = std::string("no body with id ") + std::to_string(out);
    return false;
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
    std::transform(plane.begin(), plane.end(), plane.begin(), ::tolower);
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
    return {false, "unknown tool '" + toolName + "'"};
}

} } // namespace materializr::ai
