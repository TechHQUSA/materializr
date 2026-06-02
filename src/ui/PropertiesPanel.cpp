#include "PropertiesPanel.h"
#include "../core/History.h"
#include "../core/Document.h"
#include "../core/SelectionManager.h"
#include "../core/Operation.h"
#include "../modeling/Sketch.h"
#include "../modeling/SketchSolver.h"
#include "../modeling/SketchEditOp.h"
#include "../modeling/SketchConstraints.h"
#include "../modeling/TransformOp.h"
#include "../core/EventBus.h"
#include "../core/Events.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

namespace materializr {

PropertiesPanel::PropertiesPanel() = default;

void PropertiesPanel::setHistory(History* history) {
    m_history = history;
}

void PropertiesPanel::setDocument(Document* doc) {
    m_document = doc;
}

void PropertiesPanel::setSelectionManager(const SelectionManager* sel) {
    m_selection = sel;
}

void PropertiesPanel::setEditingStep(int step) {
    m_editingStep = step;
}

int PropertiesPanel::getEditingStep() const {
    return m_editingStep;
}

bool PropertiesPanel::render() {
    bool modified = false;

    ImGui::Begin("Properties");

    // Case 1: Editing a history operation
    if (m_history && m_editingStep >= 0 && m_editingStep < m_history->stepCount()) {
        const Operation* op = m_history->getStep(m_editingStep);
        if (op) {
            // Operation header
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", op->name().c_str());
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", op->description().c_str());
            ImGui::Spacing();

            // Render the operation's parameter controls
            const_cast<Operation*>(op)->renderProperties();

            ImGui::Spacing();
            ImGui::Separator();

            if (ImGui::Button("Apply Changes", ImVec2(-1, 0))) {
                if (m_document) {
                    m_history->editStep(m_editingStep, *m_document);
                    modified = true;
                }
            }

            ImGui::Spacing();

            // Enabled/disabled toggle
            bool enabled = op->isEnabled();
            if (ImGui::Checkbox("Enabled", &enabled)) {
                const_cast<Operation*>(op)->setEnabled(enabled);
                if (m_document) {
                    m_history->replayAll(*m_document);
                    modified = true;
                }
            }

            // Step info
            ImGui::Spacing();
            ImGui::Separator();
            char stepInfo[64];
            std::snprintf(stepInfo, sizeof(stepInfo), "Step %d of %d",
                          m_editingStep + 1, m_history->stepCount());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", stepInfo);

            // Clear selection button
            if (ImGui::Button("Deselect", ImVec2(-1, 0))) {
                m_editingStep = -1;
            }
        }
    }
    // Case 2: A body is selected (but no operation being edited)
    else if (m_selection && m_selection->hasSelection() && m_document &&
             m_selection->primaryType() == SelectionType::Body) {
        const auto& sel = m_selection->getSelection();
        int bodyId = sel[0].bodyId;

        // Header
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Body Properties");
        ImGui::Separator();

        // Body name (editable)
        std::string bodyName = m_document->getBodyName(bodyId);
        static char nameBuffer[128];
        std::strncpy(nameBuffer, bodyName.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::Text("Name:");
        ImGui::SameLine();
        if (ImGui::InputText("##BodyName", nameBuffer, sizeof(nameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_document->setBodyName(bodyId, nameBuffer);
        }

        // Body ID
        char idText[32];
        std::snprintf(idText, sizeof(idText), "ID: %d", bodyId);
        ImGui::Text("%s", idText);

        // Visibility toggle
        bool visible = m_document->isBodyVisible(bodyId);
        if (ImGui::Checkbox("Visible", &visible)) {
            m_document->setBodyVisible(bodyId, visible);
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Bounding box dimensions
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Dimensions");

        const TopoDS_Shape& shape = m_document->getBody(bodyId);
        if (!shape.IsNull()) {
            Bnd_Box bbox;
            // AddOptimal w/ useTriangulation=false, useShapeTolerance=false:
            // evaluates analytic surfaces (so a Ø80 cylinder reads exactly
            // 80.000, not 80.007 from a tessellation chord) and skips the
            // per-face Tolerance() safety padding (which would inflate the
            // 20 mm height to 20.010). User-facing dim readouts only — leave
            // the conservative BRepBndLib::Add elsewhere alone since hit
            // boxes / framing want the safety margin.
            BRepBndLib::AddOptimal(shape, bbox, Standard_False, Standard_False);

            if (!bbox.IsVoid()) {
                Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
                bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);

                // World axes (Y-up internally) -> user axes (Z-up): user X is
                // world X, user Y is world Z, user Z is world Y. The display
                // and edit code below talks user-axis order; m_userToWorld
                // maps the loop index to the actual world axis the value
                // describes.
                const int m_userToWorld[3] = {0, 2, 1};
                const double worldExtents[3] = {xmax - xmin, ymax - ymin, zmax - zmin};
                const double worldMins[3] = {xmin, ymin, zmin};
                const double userExtents[3] = {
                    worldExtents[m_userToWorld[0]],
                    worldExtents[m_userToWorld[1]],
                    worldExtents[m_userToWorld[2]],
                };

                char dimText[128];
                std::snprintf(dimText, sizeof(dimText), "%.2f x %.2f x %.2f",
                              userExtents[0], userExtents[1], userExtents[2]);
                ImGui::Text("Size: %s", dimText);

                ImGui::Spacing();

                // Editable per-axis bbox extents. Typing a new value and
                // pressing Enter (or clicking out) scales the body so that
                // axis's extent matches the typed value, anchored at the
                // body's bbox-min corner — body grows in +axis direction
                // only so the result is predictable. Non-uniform scale via
                // TransformOp keeps each axis independent.
                const char* axisLabels[3] = {"X", "Y", "Z"};
                const ImVec4 axisColors[3] = {
                    ImVec4(1.00f, 0.35f, 0.35f, 1.0f),
                    ImVec4(0.35f, 1.00f, 0.35f, 1.0f),
                    ImVec4(0.40f, 0.55f, 1.00f, 1.0f),
                };

                for (int i = 0; i < 3; ++i) {
                    auto& edit = m_bodyDimEdit[i];
                    // Refresh the buffer from current bbox whenever the
                    // user isn't actively editing — covers external updates
                    // (undo/redo, other panels) AND first-time display.
                    if (!edit.focused || edit.bodyId != bodyId) {
                        std::snprintf(edit.buf, sizeof(edit.buf), "%.3f", userExtents[i]);
                    }

                    ImGui::PushID(i);
                    ImGui::TextColored(axisColors[i], "%s", axisLabels[i]);
                    ImGui::SameLine(40);
                    ImGui::SetNextItemWidth(110);
                    ImGui::InputText("##bodydim", edit.buf, sizeof(edit.buf),
                                     ImGuiInputTextFlags_CharsDecimal |
                                     ImGuiInputTextFlags_AutoSelectAll);
                    bool justActivated   = ImGui::IsItemActivated();
                    bool justDeactivated = ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SameLine(); ImGui::Text("mm");

                    if (justActivated) {
                        edit.focused = true;
                        edit.bodyId = bodyId;
                        edit.initialExtent = userExtents[i];
                    }
                    if (justDeactivated) {
                        double newExtent = std::atof(edit.buf);
                        if (newExtent > 0 &&
                            edit.initialExtent > 1e-6 &&
                            std::abs(newExtent - edit.initialExtent) > 1e-4) {
                            double ratio = newExtent / edit.initialExtent;
                            // Apply the scale to the WORLD axis that backs
                            // this user-axis slot.
                            int worldAxis = m_userToWorld[i];
                            auto op = std::make_unique<TransformOp>();
                            op->setBodyId(bodyId);
                            op->setType(TransformType::Scale);
                            double sx = 1, sy = 1, sz = 1;
                            if (worldAxis == 0)      sx = ratio;
                            else if (worldAxis == 1) sy = ratio;
                            else                     sz = ratio;
                            op->setScaleXYZ(sx, sy, sz);
                            op->setCenter(worldMins[0], worldMins[1], worldMins[2]);
                            m_history->pushOperation(std::move(op), *m_document);
                            modified = true;
                        }
                        edit.focused = false;
                    }
                    ImGui::PopID();
                }
                ImGui::TextDisabled("Press Enter or click out to commit.");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Empty shape");
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No shape data");
        }

        // If multiple bodies selected, show count
        if (m_selection->selectedBodyCount() > 1) {
            ImGui::Spacing();
            ImGui::Separator();
            char multiText[64];
            std::snprintf(multiText, sizeof(multiText), "%d bodies selected",
                          m_selection->selectedBodyCount());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", multiText);
        }
    }
    // Case 3: Other selection types
    else if (m_selection && m_selection->hasSelection()) {
        const char* typeName = "Object";
        int count = static_cast<int>(m_selection->getSelection().size());

        // A SketchRegion entry is the user pointing at its parent sketch, so
        // we treat the two interchangeably for the constraint editor below.
        bool sketchLike = false;
        int  parentSketchId = -1;
        switch (m_selection->primaryType()) {
            case SelectionType::Face:
                typeName = "Face";
                count = m_selection->selectedFaceCount();
                break;
            case SelectionType::Edge:
                typeName = "Edge";
                count = m_selection->selectedEdgeCount();
                break;
            case SelectionType::Vertex:
                typeName = "Vertex";
                break;
            case SelectionType::Sketch:
            case SelectionType::SketchRegion:
                typeName = (m_selection->primaryType() == SelectionType::SketchRegion)
                            ? "Region" : "Sketch";
                count = (m_selection->primaryType() == SelectionType::SketchRegion)
                            ? m_selection->selectedSketchRegionCount()
                            : m_selection->selectedSketchCount();
                sketchLike = true;
                for (const auto& e : m_selection->getSelection()) {
                    if ((e.type == SelectionType::Sketch ||
                         e.type == SelectionType::SketchRegion) && e.sketchId >= 0) {
                        parentSketchId = e.sketchId; break;
                    }
                }
                break;
            case SelectionType::Plane:
                typeName = "Plane";
                break;
            default:
                break;
        }

        char selText[128];
        std::snprintf(selText, sizeof(selText), "%d %s(s) selected", count, typeName);
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", selText);
        ImGui::Separator();

        if (sketchLike && m_document && m_history && parentSketchId >= 0) {
            renderSketchConstraintsPanel(parentSketchId, modified);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "Sub-shape properties not yet available.");
        }
    }
    // Case 4: Nothing selected
    else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "Select an object or operation");
    }

    ImGui::End();
    return modified;
}

// Edits the live sketch's constraints in place. Differs from
// SketchEditOp::renderProperties (which works on the m_after snapshot of
// the step you clicked in History): that path doesn't survive a project
// save/load because reloaded steps become parameterless ReplayOps. This
// panel reads/writes the current sketch directly, so the workflow works
// across sessions.
//
// Commit policy: text edits commit on Enter or focus-out (the
// IsItemDeactivatedAfterEdit signal). On commit we snapshot the pre-edit
// sketch, apply the value, run the solver, and push a SketchEditOp
// covering both states — so the change is undoable AND shows up as a
// proper step in history.
void PropertiesPanel::renderSketchConstraintsPanel(int sketchId, bool& modified) {
    auto sk = m_document->getSketch(sketchId);
    if (!sk) return;
    // One-shot diagnostic: log when the panel first opens on a sketch so we
    // can confirm the constraint editor is being reached. Suppress repeat
    // logs for the same sketch on subsequent frames.
    static int s_lastLoggedSketchId = -1;
    if (sketchId != s_lastLoggedSketchId) {
        std::fprintf(stderr, "[Cascade] PropertiesPanel opened on sketchId=%d "
                             "(constraints=%zu",
                     sketchId, sk->getConstraints().size());
        for (const auto& c : sk->getConstraints()) {
            const char* tn = "?";
            switch (c.type) {
                case ConstraintType::Coincident:    tn = "Coincident";    break;
                case ConstraintType::Horizontal:    tn = "Horizontal";    break;
                case ConstraintType::Vertical:      tn = "Vertical";      break;
                case ConstraintType::Distance:      tn = "Distance";      break;
                case ConstraintType::Radius:        tn = "Radius";        break;
                case ConstraintType::Parallel:      tn = "Parallel";      break;
                case ConstraintType::Perpendicular: tn = "Perpendicular"; break;
                case ConstraintType::Fixed:         tn = "Fixed";         break;
                case ConstraintType::Tangent:       tn = "Tangent";       break;
                case ConstraintType::Equal:         tn = "Equal";         break;
                case ConstraintType::Concentric:    tn = "Concentric";    break;
                case ConstraintType::Angle:         tn = "Angle";         break;
            }
            std::fprintf(stderr, " %s=%.2f", tn, c.value);
        }
        std::fprintf(stderr, ")\n");
        s_lastLoggedSketchId = sketchId;
    }

    // Switching to a different sketch: throw away buffered edits from the
    // previous one so we don't show stale text in the inputs.
    if (m_constraintPanelSketchId != sketchId) {
        m_constraintEdits.clear();
        m_constraintPanelSketchId = sketchId;
    }

    auto& cs = sk->getMutableConstraints();
    if (cs.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "No constraints on this sketch.");
        ImGui::TextWrapped("Add one by right-clicking a sketch element in "
                           "sketch-edit mode and picking \"Add Constraint\".");
        return;
    }

    ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Constraints");
    ImGui::Separator();

    // Friendly type names for the non-editable bullet rows.
    auto typeName = [](ConstraintType t) -> const char* {
        switch (t) {
            case ConstraintType::Coincident:    return "Coincident";
            case ConstraintType::Horizontal:    return "Horizontal";
            case ConstraintType::Vertical:      return "Vertical";
            case ConstraintType::Parallel:      return "Parallel";
            case ConstraintType::Perpendicular: return "Perpendicular";
            case ConstraintType::Fixed:         return "Fix Position";
            case ConstraintType::Tangent:       return "Tangent";
            case ConstraintType::Equal:         return "Equal length";
            case ConstraintType::Concentric:    return "Concentric";
            default:                            return "Constraint";
        }
    };

    // Helper: commit a value change.
    //  - mutate the live constraint
    //  - re-solve so dependent geometry follows
    //  - push a SketchEditOp(before, after) covering the change
    auto commitEdit = [&](Constraint& c, double newValue, ConstraintEdit& edit) {
        if (!edit.beforeSnap) edit.beforeSnap = std::make_shared<Sketch>(*sk);
        c.value = newValue;
        SketchSolver solver;
        solver.solve(*sk);
        auto after = std::make_shared<Sketch>(*sk);
        auto op = std::make_unique<SketchEditOp>(sk, edit.beforeSnap, after);
        m_history->pushExecuted(std::move(op));
        edit.beforeSnap.reset();
        edit.focused = false;
        modified = true;
        // Cascade trigger: Application listens for this and re-executes any
        // ExtrudeOp downstream of `sketchId` so the body follows the new
        // constraint value. No-op when nobody's subscribed.
        if (m_eventBus) {
            std::fprintf(stderr, "[Cascade] PropertiesPanel publish SketchEdited sketchId=%d\n", sketchId);
            m_eventBus->publish(SketchEditedEvent{sketchId});
        } else {
            std::fprintf(stderr, "[Cascade] PropertiesPanel has no event bus\n");
        }
    };

    int anyDim = 0;
    for (size_t i = 0; i < cs.size(); ++i) {
        Constraint& c = cs[i];
        ImGui::PushID(static_cast<int>(c.id));

        // Render dimensional ones (Distance, Radius/Diameter, Angle) inline.
        // Non-dimensional ones get a single muted bullet — there's nothing to
        // tune, but listing them confirms what's actually applied.
        bool isDim = (c.type == ConstraintType::Distance ||
                      c.type == ConstraintType::Radius   ||
                      c.type == ConstraintType::Angle);
        if (isDim) {
            ++anyDim;
            auto& edit = m_constraintEdits[c.id];

            // Display value: Radius shown as diameter (matches sketch popup).
            double shown = (c.type == ConstraintType::Radius) ? (c.value * 2.0)
                          : (c.type == ConstraintType::Angle)
                                ? (c.value * 180.0 / M_PI)
                                : c.value;

            // Refill the buffer when the user is NOT actively editing this
            // field, so external changes (solver runs, undo/redo) propagate
            // into the visible text. While focused we leave the buffer alone
            // so we don't trample the user's keystrokes.
            const char* unit = (c.type == ConstraintType::Angle) ? "\xC2\xB0" : "mm";
            const char* label =
                c.type == ConstraintType::Distance ? "Distance"
              : c.type == ConstraintType::Radius   ? "\xC3\x98 (diameter)"
                                                   : "Angle";
            if (!edit.focused) {
                std::snprintf(edit.buf, sizeof(edit.buf), "%.3f", shown);
            }

            ImGui::TextUnformatted(label);
            ImGui::SameLine(120);
            ImGui::SetNextItemWidth(110);
            ImGui::InputText("##val", edit.buf, sizeof(edit.buf),
                             ImGuiInputTextFlags_CharsDecimal |
                             ImGuiInputTextFlags_AutoSelectAll |
                             ImGuiInputTextFlags_EnterReturnsTrue);
            bool justActivated   = ImGui::IsItemActivated();
            bool justDeactivated = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine(); ImGui::Text("%s", unit);

            // Snapshot the pre-edit sketch the moment the user starts typing
            // so we can use it as the "before" of the eventual SketchEditOp.
            if (justActivated) {
                edit.focused = true;
                edit.beforeSnap = std::make_shared<Sketch>(*sk);
            }
            // Commit on Enter / focus-out (whichever fires first).
            if (justDeactivated) {
                double typed = std::atof(edit.buf);
                double newRaw = (c.type == ConstraintType::Radius)
                                    ? typed * 0.5
                              : (c.type == ConstraintType::Angle)
                                    ? typed * M_PI / 180.0
                                    : typed;
                if (std::abs(newRaw - c.value) > 1e-6) {
                    commitEdit(c, newRaw, edit);
                } else {
                    edit.focused = false;
                    edit.beforeSnap.reset();
                }
            }
        } else {
            ImGui::TextDisabled("\xE2\x80\xA2 %s", typeName(c.type));
        }
        ImGui::PopID();
    }

    if (!anyDim) {
        ImGui::Spacing();
        ImGui::TextWrapped("This sketch has no dimensional constraints — only "
                           "Horizontal / Parallel / etc., which have nothing "
                           "to tune.");
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("Press Enter or click elsewhere to commit a value.");
    }
}

} // namespace materializr
