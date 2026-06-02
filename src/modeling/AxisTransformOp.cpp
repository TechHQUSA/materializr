#include "AxisTransformOp.h"
#include "../core/Document.h"
#include <imgui.h>

bool AxisTransformOp::execute(Document& doc) {
    for (const auto& e : m_entries) {
        doc.setAxis(e.axisId, e.afterOrigin, e.afterDir);
    }
    return true;
}

bool AxisTransformOp::undo(Document& doc) {
    for (const auto& e : m_entries) {
        doc.setAxis(e.axisId, e.beforeOrigin, e.beforeDir);
    }
    return true;
}

std::string AxisTransformOp::description() const {
    if (m_entries.size() == 1) {
        return m_label + " (axis " + std::to_string(m_entries.front().axisId) + ")";
    }
    return m_label + " (" + std::to_string(m_entries.size()) + " axes)";
}

void AxisTransformOp::renderProperties() {
    ImGui::TextUnformatted(m_label.c_str());
    ImGui::Text("Axes affected: %d", static_cast<int>(m_entries.size()));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "Construction-axis transform (undo/redo only).");
}
