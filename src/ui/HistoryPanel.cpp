#include "HistoryPanel.h"
#include "../core/History.h"
#include "../core/Document.h"
#include "../core/Operation.h"
#include <imgui.h>
#include <cstdio>

namespace materializr {

HistoryPanel::HistoryPanel() = default;

void HistoryPanel::setHistory(History* history) {
    m_history = history;
}

void HistoryPanel::setDocument(Document* doc) {
    m_document = doc;
}

bool HistoryPanel::render() {
    bool modified = false;

    ImGui::Begin("History");

    if (!m_history || !m_document) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No history available.");
        ImGui::End();
        return false;
    }

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Operation History");
    ImGui::Separator();

    int stepCount = m_history->stepCount();
    int currentStep = m_history->currentStep();
    int breakpoint = m_history->getBreakpoint();

    // If any step came from a reopened project, explain that those steps replay
    // saved geometry and can't have their parameters re-edited.
    bool anyReloaded = false;
    for (int i = 0; i < stepCount; ++i) {
        const Operation* op = m_history->getStep(i);
        if (op && op->isReloaded()) { anyReloaded = true; break; }
    }
    if (anyReloaded) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f),
            "Steps marked (reloaded) were restored from the saved project. "
            "Undo/redo work, but their parameters can't be edited.");
        ImGui::PopTextWrapPos();
        ImGui::Separator();
    }

    // Step list
    ImGui::BeginChild("StepList", ImVec2(0, -60), true);

    int deleteIndex = -1; // set by the context menu, applied after the loop

    for (int i = 0; i < stepCount; i++) {
        const Operation* op = m_history->getStep(i);
        if (!op) continue;

        // Draw breakpoint line before this step if breakpoint is set here
        if (breakpoint >= 0 && i == breakpoint + 1) {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::Separator();
            ImGui::PopStyleColor();
        }

        // Determine color based on state
        bool isAboveBreakpoint = (breakpoint >= 0 && i > breakpoint);
        bool isCurrentlyEditing = (i == m_editingStep);
        bool isDisabled = !op->isEnabled();
        bool isAboveCurrent = (i > currentStep);

        ImGui::PushID(i);

        // Style the selectable
        if (isCurrentlyEditing) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 1.0f, 0.3f));
        }

        if (isAboveBreakpoint || isAboveCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        } else if (isDisabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }

        // Format label: index + name + enabled / reloaded state
        char label[256];
        std::snprintf(label, sizeof(label), "%d. %s%s%s",
                      i + 1,
                      op->name().c_str(),
                      isDisabled ? " [disabled]" : "",
                      op->isReloaded() ? " (reloaded)" : "");

        bool selected = (i == m_editingStep);
        if (ImGui::Selectable(label, selected)) {
            m_editingStep = i;
            m_showProperties = true;
            m_deleteConflict = false;
        }

        // Pop text color
        if (isAboveBreakpoint || isAboveCurrent || isDisabled) {
            ImGui::PopStyleColor();
        }
        if (isCurrentlyEditing) {
            ImGui::PopStyleColor();
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("StepContextMenu")) {
            if (ImGui::MenuItem("Edit Parameters")) {
                m_editingStep = i;
                m_showProperties = true;
            }
            if (ImGui::MenuItem(op->isEnabled() ? "Disable" : "Enable")) {
                // We need to cast away const to modify - the operations() method
                // returns const ref, but we access through History which owns them
                const_cast<Operation*>(op)->setEnabled(!op->isEnabled());
                m_history->replayAll(*m_document);
                modified = true;
            }
            if (ImGui::MenuItem("Set Breakpoint Here")) {
                m_history->setBreakpoint(i);
                modified = true;
            }
            if (breakpoint == i && ImGui::MenuItem("Clear Breakpoint")) {
                m_history->setBreakpoint(-1);
                modified = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) {
                deleteIndex = i; // applied after the loop (mutates the list)
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    // Apply a queued delete now that we're done iterating the (about-to-change)
    // list. removeStep rebuilds in place and refuses (returns false) if a later
    // operation depends on the one being removed.
    if (deleteIndex >= 0) {
        if (m_history->removeStep(deleteIndex, *m_document)) {
            if (m_editingStep == deleteIndex) { m_editingStep = -1; m_showProperties = false; }
            else if (m_editingStep > deleteIndex) m_editingStep--;
            m_deleteConflict = false;
        } else {
            m_deleteConflict = true; // a dependent step blocked the removal
        }
        modified = true;
    }

    if (m_deleteConflict) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                           "Can't delete: a later operation depends on it.");
    }

    // Draw breakpoint line at the end if breakpoint is at last step
    if (breakpoint >= 0 && breakpoint == stepCount - 1) {
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::Separator();
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();

    // Properties sub-section
    if (m_showProperties && m_editingStep >= 0 && m_editingStep < stepCount) {
        const Operation* op = m_history->getStep(m_editingStep);
        if (op) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Properties: %s",
                               op->name().c_str());
            const_cast<Operation*>(op)->renderProperties();

            if (ImGui::Button("Apply Changes", ImVec2(-1, 0))) {
                m_history->editStep(m_editingStep, *m_document);
                modified = true;
            }
        }
    }

    // Bottom section: Undo/Redo + step counter
    ImGui::Separator();

    ImGui::BeginDisabled(!m_history->canUndo());
    if (ImGui::Button("Undo")) {
        m_history->undo(*m_document);
        modified = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!m_history->canRedo());
    if (ImGui::Button("Redo")) {
        m_history->redo(*m_document);
        modified = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Step counter
    char stepText[64];
    std::snprintf(stepText, sizeof(stepText), "Step %d/%d", currentStep + 1, stepCount);
    ImGui::Text("%s", stepText);

    ImGui::End();
    return modified;
}

} // namespace materializr
