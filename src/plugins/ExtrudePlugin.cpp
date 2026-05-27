#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../plugin/InteractiveTool.h"
#include "../plugin/PluginRegistry.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/ExtrudeOp.h"
#include <TopoDS.hxx>
#include <BRepGProp_Face.hxx>
#include <imgui.h>
#include <glm/glm.hpp>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace {

class ExtrudeTool : public materializr::InteractiveTool {
public:
    void begin(materializr::PluginContext& ctx) override {
        const auto& sel = ctx.selection().getSelection();
        for (const auto& entry : sel) {
            if (entry.type == SelectionType::Face && !entry.shape.IsNull()) {
                m_profile = entry.shape;
                break;
            }
        }
        if (m_profile.IsNull()) { m_done = true; return; }

        m_distance = 5.0f;
        std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_distance);
        m_inputFocus = true;

        if (m_profile.ShapeType() == TopAbs_FACE) {
            BRepGProp_Face prop(TopoDS::Face(m_profile));
            gp_Pnt center;
            gp_Vec norm;
            double u1, u2, v1, v2;
            prop.Bounds(u1, u2, v1, v2);
            prop.Normal((u1 + u2) * 0.5, (v1 + v2) * 0.5, center, norm);
            if (norm.Magnitude() > 1e-10) {
                m_normal = glm::normalize(glm::vec3(norm.X(), norm.Y(), norm.Z()));
            }
            m_origin = glm::vec3(center.X(), center.Y(), center.Z());
        }

        auto op = std::make_unique<ExtrudeOp>();
        op->setProfile(m_profile);
        op->setDistance(m_distance);
        op->setMode(ExtrudeMode::NewBody);
        if (ctx.history().pushOperation(std::move(op), ctx.document())) {
            auto ids = ctx.document().getAllBodyIds();
            m_previewBodyId = ids.back();
            ctx.markMeshesDirty();
        }
    }

    bool update(materializr::PluginContext&) override { return !m_done; }

    void commit(materializr::PluginContext& ctx) override {
        m_done = true;
        ctx.markMeshesDirty();
    }

    void cancel(materializr::PluginContext& ctx) override {
        if (m_previewBodyId >= 0) {
            ctx.document().removeBody(m_previewBodyId);
            ctx.history().undo(ctx.document());
        }
        m_done = true;
        ctx.markMeshesDirty();
    }

    bool handleInput(materializr::PluginContext& ctx, const materializr::ToolInputEvent& event) override {
        if (event.type == materializr::ToolInputEvent::KeyPress) {
            if (event.key == 525) { commit(ctx); return true; }
            if (event.key == 526) { cancel(ctx); return true; }
        }
        return false;
    }

    void renderOverlay(materializr::PluginContext& ctx) override {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::Text("EXTRUDE - Drag in viewport or type distance. Enter to confirm, Escape to cancel.");
        ImGui::PopStyleColor();

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 260,
                                        ImGui::GetWindowPos().y + 50));
        ImGui::SetNextWindowSize(ImVec2(240, 0));
        ImGui::Begin("##ExtrudeInput", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Extrude Distance (mm)");
        ImGui::Separator();

        if (m_inputFocus) {
            ImGui::SetKeyboardFocusHere();
            m_inputFocus = false;
        }

        if (ImGui::InputText("##dist", m_inputBuf, sizeof(m_inputBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_distance = static_cast<float>(std::atof(m_inputBuf));
            updatePreview(ctx);
            commit(ctx);
        } else {
            float parsed = static_cast<float>(std::atof(m_inputBuf));
            if (std::abs(parsed - m_distance) > 0.01f && std::abs(parsed) > 0.01f) {
                m_distance = parsed;
                updatePreview(ctx);
            }
        }

        ImGui::SameLine();
        ImGui::Text("mm");

        if (ImGui::SliderFloat("##slider", &m_distance, -50.0f, 50.0f, "%.1f mm")) {
            std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_distance);
            updatePreview(ctx);
        }

        ImGui::Spacing();
        if (ImGui::Button("Confirm (Enter)", ImVec2(110, 0))) { commit(ctx); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)", ImVec2(110, 0))) { cancel(ctx); }

        ImGui::End();
    }

    std::string name() const override { return "Extrude"; }

private:
    void updatePreview(materializr::PluginContext& ctx) {
        if (m_previewBodyId < 0) return;
        ctx.document().removeBody(m_previewBodyId);
        ctx.history().undo(ctx.document());

        auto op = std::make_unique<ExtrudeOp>();
        op->setProfile(m_profile);
        op->setDistance(static_cast<double>(m_distance));
        op->setMode(ExtrudeMode::NewBody);
        if (ctx.history().pushOperation(std::move(op), ctx.document())) {
            auto ids = ctx.document().getAllBodyIds();
            m_previewBodyId = ids.back();
            ctx.markMeshesDirty();
        }
    }

    TopoDS_Shape m_profile;
    float m_distance = 5.0f;
    glm::vec3 m_normal{0, 0, 1};
    glm::vec3 m_origin{0};
    int m_previewBodyId = -1;
    char m_inputBuf[32] = "5.0";
    bool m_inputFocus = true;
    bool m_done = false;
};

} // anonymous namespace

REGISTER_PLUGIN(Extrude, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Extrude", "Feature",
        materializr::SelectionContext::HasFaces, 400,
        nullptr,
        []() -> std::unique_ptr<materializr::InteractiveTool> {
            return std::make_unique<ExtrudeTool>();
        }});

    ctx.registerCommand({"Extrude Face", "",
        [](materializr::PluginContext& ctx) {
            materializr::PluginRegistry::instance().activateTool(
                std::make_unique<ExtrudeTool>(), ctx);
        }});
})
