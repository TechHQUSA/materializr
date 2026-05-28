#include "ReplayOp.h"
#include <imgui.h>
#include <set>

ReplayOp::ReplayOp(std::string typeId, std::string name, std::string description,
                   BodyState before, BodyState after)
    : m_typeId(std::move(typeId))
    , m_name(std::move(name))
    , m_description(std::move(description))
    , m_before(std::move(before))
    , m_after(std::move(after)) {}

void ReplayOp::restore(Document& doc, const BodyState& state) {
    std::set<int> wanted;
    for (const auto& [id, shape] : state) {
        doc.putBody(id, shape);
        wanted.insert(id);
    }
    // Drop any body that shouldn't exist in this state (e.g. one this step
    // created, when undoing).
    for (int id : doc.getAllBodyIds()) {
        if (!wanted.count(id)) doc.removeBody(id);
    }
}

bool ReplayOp::execute(Document& doc) {
    restore(doc, m_after);
    return true;
}

bool ReplayOp::undo(Document& doc) {
    restore(doc, m_before);
    return true;
}

void ReplayOp::renderProperties() {
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", m_description.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Loaded from a saved project. Undo/redo work, but the "
                       "parameters of a reloaded step can't be edited.");
}
