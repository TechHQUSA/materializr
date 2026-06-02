#include "PlaneTransformOp.h"
#include "../core/Document.h"
#include <imgui.h>

bool PlaneTransformOp::execute(Document& doc) {
    // setPlane with an id that isn't present is a safe no-op (it just finds
    // nothing), so this stays robust if a plane was removed after the op was
    // recorded or its id shifted across a full replay.
    for (const auto& e : m_entries) {
        doc.setPlane(e.planeId, e.after);
    }
    return true;
}

bool PlaneTransformOp::undo(Document& doc) {
    for (const auto& e : m_entries) {
        doc.setPlane(e.planeId, e.before);
    }
    return true;
}

std::string PlaneTransformOp::description() const {
    if (m_entries.size() == 1) {
        return m_label + " (plane " + std::to_string(m_entries.front().planeId) + ")";
    }
    return m_label + " (" + std::to_string(m_entries.size()) + " planes)";
}

void PlaneTransformOp::renderProperties() {
    ImGui::TextUnformatted(m_label.c_str());
    ImGui::Text("Planes affected: %d", static_cast<int>(m_entries.size()));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "Construction-plane transform (undo/redo only).");
}
