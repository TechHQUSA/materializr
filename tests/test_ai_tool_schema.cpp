#include "ai/AiToolSchema.h"

#include <gtest/gtest.h>
#include <algorithm>

using namespace materializr::ai;

TEST(AiToolSchema, AllToolsContainsExactlySeventeenTools) {
    const auto& tools = allTools();
    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.name);
    std::vector<std::string> expected = {
        "add_box", "add_cylinder", "add_sphere", "add_cone", "add_torus",
        "move_body", "rotate_body", "scale_body", "boolean_op",
        "copy_body", "delete_body", "separate_body", "align_body", "mirror_body",
        "pattern_body", "construction_axis", "construction_plane"};
    EXPECT_EQ(names, expected);
    EXPECT_EQ(tools.size(), 17u);
}

TEST(AiToolSchema, AllNineOriginalToolNamesAreStillPresent) {
    const auto& tools = allTools();
    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.name);
    std::vector<std::string> original = {
        "add_box", "add_cylinder", "add_sphere", "add_cone", "add_torus",
        "move_body", "rotate_body", "scale_body", "boolean_op"};
    for (const auto& n : original) {
        EXPECT_NE(std::find(names.begin(), names.end(), n), names.end()) << "missing: " << n;
    }
}

namespace {
const ToolDef* findTool(const std::vector<ToolDef>& tools, const std::string& name) {
    for (const auto& t : tools) if (t.name == name) return &t;
    return nullptr;
}
void expectParamSplit(const ToolDef& t, const std::vector<std::string>& required,
                      const std::vector<std::string>& optional) {
    for (const auto& name : required) {
        bool found = false;
        for (const auto& p : t.params) if (p.name == name) { found = true; EXPECT_TRUE(p.required) << name; }
        EXPECT_TRUE(found) << "missing required param: " << name;
    }
    for (const auto& name : optional) {
        bool found = false;
        for (const auto& p : t.params) if (p.name == name) { found = true; EXPECT_FALSE(p.required) << name; }
        EXPECT_TRUE(found) << "missing optional param: " << name;
    }
}
} // namespace

TEST(AiToolSchema, MirrorBodyParamShapeMatchesTask1_2) {
    const auto* t = findTool(allTools(), "mirror_body");
    ASSERT_NE(t, nullptr);
    expectParamSplit(*t, {"body_id", "plane"}, {"keep_original"});
}

TEST(AiToolSchema, PatternBodyParamShapeMatchesBrief) {
    const auto* t = findTool(allTools(), "pattern_body");
    ASSERT_NE(t, nullptr);
    expectParamSplit(*t, {"body_id", "type", "count"},
                     {"spacing_x", "spacing_y", "spacing_z", "axis_x", "axis_y", "axis_z",
                      "origin_x", "origin_y", "origin_z", "total_angle_degrees"});
}

TEST(AiToolSchema, ConstructionAxisParamShapeMatchesBrief) {
    const auto* t = findTool(allTools(), "construction_axis");
    ASSERT_NE(t, nullptr);
    expectParamSplit(*t, {"type"}, {"p1_x", "p1_y", "p1_z", "p2_x", "p2_y", "p2_z", "name"});
}

TEST(AiToolSchema, ConstructionPlaneParamShapeMatchesBrief) {
    const auto* t = findTool(allTools(), "construction_plane");
    ASSERT_NE(t, nullptr);
    expectParamSplit(*t, {"type"}, {"offset", "name"});
}

TEST(AiToolSchema, EveryToolHasANonEmptyDescription) {
    for (const auto& t : allTools())
        EXPECT_FALSE(t.description.empty()) << "tool: " << t.name;
}

TEST(AiToolSchema, AnthropicJsonHasOneEntryPerToolWithInputSchema) {
    nlohmann::json j = toolsToAnthropicJson(allTools());
    ASSERT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), allTools().size());
    for (const auto& entry : j) {
        EXPECT_TRUE(entry.contains("name"));
        EXPECT_TRUE(entry.contains("input_schema"));
        EXPECT_EQ(entry["input_schema"]["type"], "object");
    }
}

TEST(AiToolSchema, OpenAiJsonWrapsEachToolInAFunctionEnvelope) {
    nlohmann::json j = toolsToOpenAiJson(allTools());
    ASSERT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), allTools().size());
    for (const auto& entry : j) {
        EXPECT_EQ(entry["type"], "function");
        EXPECT_TRUE(entry["function"].contains("name"));
        EXPECT_TRUE(entry["function"].contains("parameters"));
        EXPECT_EQ(entry["function"]["parameters"]["type"], "object");
    }
}

TEST(AiToolSchema, BooleanOpModeParamIsRequired) {
    for (const auto& t : allTools()) {
        if (t.name != "boolean_op") continue;
        bool found = false;
        for (const auto& p : t.params)
            if (p.name == "mode") { found = true; EXPECT_TRUE(p.required); }
        EXPECT_TRUE(found);
        return;
    }
    FAIL() << "boolean_op tool not found";
}
