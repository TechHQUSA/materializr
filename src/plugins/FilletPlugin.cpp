#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../plugin/InteractiveTool.h"
#include "../plugin/PluginRegistry.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/FilletOp.h"
#include <TopoDS.hxx>
#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace {

class FilletTool : public materializr::InteractiveTool {
public:
    void begin(materializr::PluginContext& ctx) override {
        const auto& sel = ctx.selection().getSelection();
        for (const auto& entry : sel) {
            if (entry.type == SelectionType::Edge && !entry.shape.IsNull()) {
                if (m_bodyId < 0) m_bodyId = entry.bodyId;
                if (entry.bodyId == m_bodyId)
                    m_edges.push_back(TopoDS::Edge(entry.shape));
            }
        }
        if (m_bodyId < 0 || m_edges.empty()) { m_done = true; return; }

        try {
            m_previousShape = ctx.document().getBody(m_bodyId);
        } catch (...) { m_done = true; return; }

        m_value = 1.0f;
        std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_value);
        m_inputFocus = true;
        updatePreview(ctx);
    }

    bool update(materializr::PluginContext&) override { return !m_done; }

    void commit(materializr::PluginContext& ctx) override {
        ctx.document().updateBody(m_bodyId, m_previousShape);

        auto op = std::make_unique<FilletOp>();
        op->setBody(m_bodyId);
        op->setEdges(m_edges);
        op->setRadius(static_cast<double>(m_value));
        ctx.history().pushOperation(std::move(op), ctx.document());

        ctx.selection().clear();
        ctx.markMeshesDirty();
        m_done = true;
    }

    void cancel(materializr::PluginContext& ctx) override {
        if (m_bodyId >= 0 && !m_previousShape.IsNull()) {
            ctx.document().updateBody(m_bodyId, m_previousShape);
        }
        ctx.markMeshesDirty();
        m_done = true;
    }

    bool handleInput(materializr::PluginContext& ctx, const materializr::ToolInputEvent& event) override {
        if (event.type == materializr::ToolInputEvent::KeyPress) {
            if (event.key == 525) { // ImGuiKey_Enter
                commit(ctx); return true;
            }
            if (event.key == 526) { // ImGuiKey_Escape
                cancel(ctx); return true;
            }
        }
        return false;
    }

    void renderOverlay(materializr::PluginContext& ctx) override {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.5f, 1.0f));
        ImGui::Text("FILLET - Type value or use slider. Enter to confirm, Escape to cancel.");
        ImGui::PopStyleColor();

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 260,
                                        ImGui::GetWindowPos().y + 50));
        ImGui::SetNextWindowSize(ImVec2(240, 0));
        ImGui::Begin("##FilletInput", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Radius (mm)");
        ImGui::Separator();

        if (m_inputFocus) {
            ImGui::SetKeyboardFocusHere();
            m_inputFocus = false;
        }

        if (ImGui::InputText("##val", m_inputBuf, sizeof(m_inputBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_value = static_cast<float>(std::atof(m_inputBuf));
            updatePreview(ctx);
            commit(ctx);
        } else {
            float parsed = static_cast<float>(std::atof(m_inputBuf));
            if (std::abs(parsed - m_value) > 0.01f && parsed > 0.01f) {
                m_value = parsed;
                updatePreview(ctx);
            }
        }

        ImGui::SameLine();
        ImGui::Text("mm");

        if (ImGui::SliderFloat("##fslider", &m_value, 0.1f, 20.0f, "%.1f mm")) {
            std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_value);
            updatePreview(ctx);
        }

        ImGui::Spacing();
        if (ImGui::Button("Confirm (Enter)", ImVec2(110, 0))) { commit(ctx); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)", ImVec2(110, 0))) { cancel(ctx); }

        ImGui::End();
    }

    std::string name() const override { return "Fillet"; }

private:
    void updatePreview(materializr::PluginContext& ctx) {
        if (m_bodyId < 0 || m_value < 0.01f) return;
        ctx.document().updateBody(m_bodyId, m_previousShape);
        try {
            auto op = std::make_unique<FilletOp>();
            op->setBody(m_bodyId);
            op->setEdges(m_edges);
            op->setRadius(static_cast<double>(m_value));
            if (!op->execute(ctx.document())) {
                ctx.document().updateBody(m_bodyId, m_previousShape);
            }
        } catch (...) {
            ctx.document().updateBody(m_bodyId, m_previousShape);
        }
        ctx.markMeshesDirty();
    }

    int m_bodyId = -1;
    std::vector<TopoDS_Edge> m_edges;
    float m_value = 1.0f;
    char m_inputBuf[32] = "1.0";
    bool m_inputFocus = true;
    TopoDS_Shape m_previousShape;
    bool m_done = false;
};

} // anonymous namespace

REGISTER_PLUGIN(Fillet, [](materializr::PluginContext& /*ctx*/) {
    // Fillet is now the Application's interactive edge op (drag handle + measurement
    // + live preview), which needs viewport input the plugin tool can't get. The
    // toolbar "Fillet" button lives in the app's edge tools.
})
