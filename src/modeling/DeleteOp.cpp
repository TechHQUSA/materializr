#include "DeleteOp.h"
#include <imgui.h>

DeleteOp::DeleteOp() = default;

void DeleteOp::setBodyId(int id) {
    m_bodyId = id;
}

bool DeleteOp::execute(Document& doc) {
    if (m_bodyId < 0) {
        return false;
    }

    try {
        // Save shape, name, and visibility for undo
        m_deletedShape = doc.getBody(m_bodyId);
        m_deletedName = doc.getBodyName(m_bodyId);
        m_wasVisible = doc.isBodyVisible(m_bodyId);

        // Remove the body from the document
        doc.removeBody(m_bodyId);

        return true;
    } catch (...) {
        return false;
    }
}

bool DeleteOp::undo(Document& doc) {
    if (m_deletedShape.IsNull()) {
        return false;
    }

    try {
        // Re-add the body with its original name
        int newId = doc.addBody(m_deletedShape, m_deletedName);
        doc.setBodyVisible(newId, m_wasVisible);

        // Update our stored body ID to reflect the re-added body
        m_bodyId = newId;

        return true;
    } catch (...) {
        return false;
    }
}

std::string DeleteOp::description() const {
    if (m_deletedName.empty()) {
        return "Delete body " + std::to_string(m_bodyId);
    }
    return "Delete \"" + m_deletedName + "\"";
}

void DeleteOp::renderProperties() {
    ImGui::Text("Delete");
    ImGui::Separator();

    ImGui::Text("Body ID: %d", m_bodyId);

    if (!m_deletedName.empty()) {
        ImGui::Text("Name: %s", m_deletedName.c_str());
    }
}

OperationDiff DeleteOp::captureDiff() const {
    OperationDiff d;
    if (m_bodyId >= 0 && !m_deletedShape.IsNull())
        d.deletedBefore.push_back({m_bodyId, m_deletedShape});
    return d;
}
