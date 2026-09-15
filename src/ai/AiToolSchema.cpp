#include "AiToolSchema.h"

namespace materializr { namespace ai {

namespace {
ToolParam num(const char* name, const char* desc, bool required = true) {
    return {name, ToolParamType::Number, required, desc};
}
ToolParam str(const char* name, const char* desc, bool required = true) {
    return {name, ToolParamType::String, required, desc};
}
ToolParam intArray(const char* name, const char* desc, bool required = true) {
    return {name, ToolParamType::IntegerArray, required, desc};
}
// x, y, z default to the world origin - see AiToolDispatcher (Task 5) for
// where the default is actually applied when the model omits them.
std::vector<ToolParam> withOrigin(std::vector<ToolParam> params) {
    params.push_back(num("x", "World X position in mm (default 0).", false));
    params.push_back(num("y", "World Y position in mm (default 0).", false));
    params.push_back(num("z", "World Z position in mm (default 0).", false));
    return params;
}
} // namespace

const std::vector<ToolDef>& allTools() {
    static const std::vector<ToolDef> kTools = {
        {"add_box", "Create a rectangular box body.",
         withOrigin({num("width", "Size along X in mm."),
                     num("height", "Size along the up axis (Z) in mm."),
                     num("depth", "Size along the horizontal depth axis (Y) in mm.")})},
        {"add_cylinder", "Create a cylindrical body.",
         withOrigin({num("radius", "Radius in mm."),
                     num("height", "Height in mm.")})},
        {"add_sphere", "Create a spherical body.",
         withOrigin({num("radius", "Radius in mm.")})},
        {"add_cone", "Create a conical body (top_radius 0 for a sharp point).",
         withOrigin({num("bottom_radius", "Base radius in mm."),
                     num("top_radius", "Top radius in mm; 0 for a point."),
                     num("height", "Height in mm.")})},
        {"add_torus", "Create a torus (ring) body.",
         withOrigin({num("major_radius", "Distance from centre to tube centre, in mm."),
                     num("minor_radius", "Tube radius in mm.")})},
        {"move_body", "Translate an existing body.",
         {num("body_id", "The id of the body to move."),
          num("dx", "Move along X in mm."),
          num("dy", "Move along Y in mm."),
          num("dz", "Move along Z in mm.")}},
        {"rotate_body", "Rotate an existing body about an axis through the world origin.",
         {num("body_id", "The id of the body to rotate."),
          num("axis_x", "Rotation axis X component."),
          num("axis_y", "Rotation axis Y component."),
          num("axis_z", "Rotation axis Z component."),
          num("angle_degrees", "Rotation angle in degrees.")}},
        {"scale_body", "Uniformly scale an existing body.",
         {num("body_id", "The id of the body to scale."),
          num("factor", "Scale factor, e.g. 2.0 doubles the size.")}},
        {"boolean_op", "Combine two bodies with a boolean operation.",
         {num("target_body_id", "The body kept after the operation."),
          num("tool_body_id", "The body combined into the target."),
          str("mode", "One of: union, subtract, intersect.")}},
        ToolDef{"copy_body", "Duplicate an existing body, optionally offset by (dx, dy, dz).",
            {num("body_id", "The id of the body to copy."),
             num("dx", "Offset along X in mm.", false),
             num("dy", "Offset along Y (user-space depth) in mm.", false),
             num("dz", "Offset along Z (user-space height/up) in mm.", false)}},
        ToolDef{"delete_body", "Permanently remove a body from the document.",
            {num("body_id", "The id of the body to delete.")}},
        ToolDef{"separate_body", "Split a body with multiple disconnected solid shells into separate bodies, one per shell.",
            {num("body_id", "The id of the body to separate.")}},
        ToolDef{"align_body", "Move a body so a chosen point on it lands on a chosen target point in space.",
            {num("body_id", "The id of the body to align."),
             num("source_x", "X of the point on the body, in mm."),
             num("source_y", "Depth (user-space Y) of the point on the body, in mm."),
             num("source_z", "Height (user-space Z, up) of the point on the body, in mm."),
             num("target_x", "X of the destination point, in mm."),
             num("target_y", "Depth (user-space Y) of the destination point, in mm."),
             num("target_z", "Height (user-space Z, up) of the destination point, in mm.")}},
        ToolDef{"mirror_body", "Create a mirrored copy of a body across a standard plane.",
            {num("body_id", "The id of the body to mirror."),
             str("plane", "Which standard plane to mirror across: \"xy\", \"xz\", or \"yz\"."),
             str("keep_original", "\"true\" to keep the original body, \"false\" to replace it. Defaults to \"true\".", false)}},
        ToolDef{"pattern_body", "Create a linear or radial array of copies of a body. For a radial pattern, instances are spaced evenly at total_angle_degrees / count, not across (count - 1) steps - a 180 degree total with 2 instances places the second at 90 degrees, not 180.",
            {num("body_id", "The id of the body to pattern."),
             str("type", "\"linear\" or \"radial\"."),
             num("count", "How many total instances, including the original. Must be a whole number from 2 to 500."),
             num("spacing_x", "Linear pattern: spacing along X per step, in mm. Ignored for radial.", false),
             num("spacing_y", "Linear pattern: spacing along user-space depth (Y) per step, in mm. Ignored for radial.", false),
             num("spacing_z", "Linear pattern: spacing along user-space height (Z, up) per step, in mm. Ignored for radial.", false),
             num("axis_x", "Radial pattern: X component of the rotation axis. Ignored for linear. Defaults to 0.", false),
             num("axis_y", "Radial pattern: user-space depth (Y) component of the rotation axis. Ignored for linear. Defaults to 0.", false),
             num("axis_z", "Radial pattern: user-space height (Z, up) component of the rotation axis. Ignored for linear. Defaults to 1 (straight up).", false),
             num("origin_x", "Radial pattern: X of the rotation center. Ignored for linear. Defaults to 0.", false),
             num("origin_y", "Radial pattern: user-space depth (Y) of the rotation center. Ignored for linear. Defaults to 0.", false),
             num("origin_z", "Radial pattern: user-space height (Z, up) of the rotation center. Ignored for linear. Defaults to 0.", false),
             num("total_angle_degrees", "Radial pattern: total angle spanned by the array, in degrees. Ignored for linear. Defaults to 360.", false)}},
        ToolDef{"construction_axis", "Create a construction (reference) axis, expressed in user-space (Z-up) coordinates.",
            {str("type", "\"x\", \"y\", \"z\" for a standard axis, or \"two_points\" for a custom axis through two points."),
             num("p1_x", "two_points only: X of the first point.", false),
             num("p1_y", "two_points only: user-space depth (Y) of the first point.", false),
             num("p1_z", "two_points only: user-space height (Z, up) of the first point.", false),
             num("p2_x", "two_points only: X of the second point.", false),
             num("p2_y", "two_points only: user-space depth (Y) of the second point.", false),
             num("p2_z", "two_points only: user-space height (Z, up) of the second point.", false),
             str("name", "Optional name for the new axis.", false)}},
        ToolDef{"construction_plane", "Create a construction (reference) plane on a standard plane, expressed in user-space (Z-up) coordinates, optionally offset.",
            {str("type", "\"xy\", \"xz\", or \"yz\" (user-space)."),
             num("offset", "Offset distance from the plane along its normal, in mm. Defaults to 0.", false),
             str("name", "Optional name for the new plane.", false)}},
        ToolDef{"extrude_sketch", "Extrude one or more regions of an existing sketch (or its whole profile) into a solid body.",
            {num("sketch_id", "The id of the sketch to extrude."),
             intArray("region_indices", "Which closed regions of the sketch to extrude, by index into its region list. Omit or pass an empty list to extrude the whole sketch profile.", false),
             num("distance", "Sweep distance in mm. Must be nonzero. The sign picks a direction (positive = along the profile's face normal, negative = the opposite way) UNLESS symmetric is true, in which case only the magnitude matters: the total thickness is abs(distance), split evenly on both sides of the sketch plane."),
             str("symmetric", "\"true\" to extrude equally in both directions (total thickness abs(distance)), \"false\" for a one-sided extrude. Defaults to \"false\".", false),
             str("mode", "One of: new_body, union, subtract, intersect. Defaults to new_body.", false),
             num("target_body_id", "Required unless mode is new_body: the existing body to combine the extrusion with.", false)}},
        ToolDef{"describe_scene", "List every body, sketch, construction axis, and construction plane currently in the document (id, name, visibility, and a cheap geometric summary), to discover ids for other tools. Read-only. Each category is paginated at 100 items; pass the matching after_*_id from a truncated result to continue.",
            {num("after_body_id", "Only list bodies with an id greater than this, to continue a truncated body listing. Omit to start from the beginning.", false),
             num("after_sketch_id", "Same as after_body_id, for sketches.", false),
             num("after_axis_id", "Same as after_body_id, for construction axes.", false),
             num("after_plane_id", "Same as after_body_id, for construction planes.", false)}},
    };
    return kTools;
}

namespace {
// Only for the two scalar types - IntegerArray needs the array/items shape
// below, which doesn't fit a bare "type" string.
const char* typeName(ToolParamType t) {
    return t == ToolParamType::String ? "string" : "number";
}
// Shared by both formatters: the JSON Schema "object" body every provider
// wraps identically (properties + required list), only the outer envelope
// differs (Anthropic's input_schema vs OpenAI's function.parameters).
nlohmann::json paramsToJsonSchema(const std::vector<ToolParam>& params) {
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();
    for (const auto& p : params) {
        if (p.type == ToolParamType::IntegerArray) {
            properties[p.name] = {{"type", "array"},
                                   {"items", {{"type", "integer"}}},
                                   {"description", p.description}};
        } else {
            properties[p.name] = {{"type", typeName(p.type)}, {"description", p.description}};
        }
        if (p.required) required.push_back(p.name);
    }
    return {{"type", "object"}, {"properties", properties}, {"required", required}};
}
} // namespace

nlohmann::json toolsToAnthropicJson(const std::vector<ToolDef>& tools) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& t : tools)
        out.push_back({{"name", t.name},
                       {"description", t.description},
                       {"input_schema", paramsToJsonSchema(t.params)}});
    return out;
}

nlohmann::json toolsToOpenAiJson(const std::vector<ToolDef>& tools) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& t : tools)
        out.push_back({{"type", "function"},
                       {"function", {{"name", t.name},
                                     {"description", t.description},
                                     {"parameters", paramsToJsonSchema(t.params)}}}});
    return out;
}

} } // namespace materializr::ai
