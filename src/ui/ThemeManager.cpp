#include "ThemeManager.h"
#include <imgui.h>

namespace materializr {

ThemeManager::ThemeManager() = default;

void ThemeManager::setTheme(Theme theme) {
    m_theme = theme;
    apply();
}

Theme ThemeManager::getTheme() const {
    return m_theme;
}

void ThemeManager::toggle() {
    if (m_theme == Theme::Dark) {
        setTheme(Theme::Light);
    } else {
        setTheme(Theme::Dark);
    }
}

void ThemeManager::apply() {
    switch (m_theme) {
        case Theme::Light: applyLight(); break;
        case Theme::Eink:  applyEink();  break;
        case Theme::Dark:
        default:           applyDark();  break;
    }
}

bool ThemeManager::renderSelector() {
    int current = static_cast<int>(m_theme);
    const char* items[] = { "Dark", "Light", "eInk (High Contrast)" };

    if (ImGui::Combo("Theme", &current, items, 3)) {
        setTheme(static_cast<Theme>(current));
        return true;
    }

    return false;
}

void ThemeManager::applyDark() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    // Reset the eInk theme's hard borders (theme switches are live).
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg]         = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_ChildBg]          = ImVec4(0.12f, 0.12f, 0.14f, 0.0f);
    colors[ImGuiCol_PopupBg]          = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
    colors[ImGuiCol_Border]           = ImVec4(0.25f, 0.25f, 0.28f, 0.50f);
    colors[ImGuiCol_BorderShadow]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.12f, 0.12f, 0.16f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.10f, 0.50f);

    colors[ImGuiCol_MenuBarBg]        = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);

    colors[ImGuiCol_FrameBg]          = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.24f, 0.24f, 0.30f, 1.0f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.28f, 0.28f, 0.36f, 1.0f);

    colors[ImGuiCol_Button]           = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.30f, 0.30f, 0.38f, 1.0f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.35f, 0.35f, 0.45f, 1.0f);

    colors[ImGuiCol_Header]           = ImVec4(0.22f, 0.28f, 0.38f, 1.0f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.28f, 0.35f, 0.48f, 1.0f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.30f, 0.38f, 0.52f, 1.0f);

    colors[ImGuiCol_Tab]              = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
    colors[ImGuiCol_TabHovered]       = ImVec4(0.28f, 0.35f, 0.48f, 1.0f);
    colors[ImGuiCol_TabSelected]      = ImVec4(0.22f, 0.28f, 0.38f, 1.0f);
    colors[ImGuiCol_TabDimmed]         = ImVec4(0.11f, 0.11f, 0.14f, 1.0f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.17f, 0.19f, 0.26f, 1.0f);

    colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.10f, 0.10f, 0.12f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]    = ImVec4(0.30f, 0.30f, 0.34f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.38f, 0.42f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.45f, 0.45f, 0.50f, 1.0f);

    colors[ImGuiCol_Separator]        = ImVec4(0.25f, 0.25f, 0.28f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.55f, 0.85f, 0.78f);
    colors[ImGuiCol_SeparatorActive]  = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);

    colors[ImGuiCol_ResizeGrip]       = ImVec4(0.35f, 0.55f, 0.85f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]= ImVec4(0.35f, 0.55f, 0.85f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.35f, 0.55f, 0.85f, 0.95f);

    colors[ImGuiCol_CheckMark]        = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.0f);

    colors[ImGuiCol_Text]             = ImVec4(0.92f, 0.92f, 0.94f, 1.0f);
    colors[ImGuiCol_TextDisabled]     = ImVec4(0.50f, 0.50f, 0.52f, 1.0f);

    colors[ImGuiCol_DockingPreview]   = ImVec4(0.35f, 0.55f, 0.85f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]   = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
}

void ThemeManager::applyLight() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    // Reset the eInk theme's hard borders (theme switches are live).
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg]         = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
    colors[ImGuiCol_ChildBg]          = ImVec4(0.94f, 0.94f, 0.94f, 0.0f);
    colors[ImGuiCol_PopupBg]          = ImVec4(0.98f, 0.98f, 0.98f, 0.94f);
    colors[ImGuiCol_Border]           = ImVec4(0.70f, 0.70f, 0.72f, 0.50f);
    colors[ImGuiCol_BorderShadow]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_TitleBg]          = ImVec4(0.82f, 0.82f, 0.84f, 1.0f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.76f, 0.76f, 0.80f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.82f, 0.82f, 0.84f, 0.50f);

    colors[ImGuiCol_MenuBarBg]        = ImVec4(0.88f, 0.88f, 0.90f, 1.0f);

    colors[ImGuiCol_FrameBg]          = ImVec4(0.86f, 0.86f, 0.88f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.78f, 0.78f, 0.82f, 1.0f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.72f, 0.72f, 0.78f, 1.0f);

    colors[ImGuiCol_Button]           = ImVec4(0.78f, 0.78f, 0.82f, 1.0f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.68f, 0.68f, 0.75f, 1.0f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.60f, 0.60f, 0.68f, 1.0f);

    colors[ImGuiCol_Header]           = ImVec4(0.72f, 0.78f, 0.88f, 1.0f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.62f, 0.70f, 0.82f, 1.0f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.55f, 0.65f, 0.78f, 1.0f);

    colors[ImGuiCol_Tab]              = ImVec4(0.82f, 0.82f, 0.86f, 1.0f);
    colors[ImGuiCol_TabHovered]       = ImVec4(0.62f, 0.70f, 0.82f, 1.0f);
    colors[ImGuiCol_TabSelected]      = ImVec4(0.72f, 0.78f, 0.88f, 1.0f);
    // Unfocused (dimmed) dock tabs — without these, docked panel tab labels
    // (Properties/History/Tools) kept the dark default and went unreadable
    // against the black light-theme text until hovered.
    colors[ImGuiCol_TabDimmed]         = ImVec4(0.84f, 0.84f, 0.88f, 1.0f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.78f, 0.82f, 0.90f, 1.0f);

    colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.90f, 0.90f, 0.92f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]    = ImVec4(0.68f, 0.68f, 0.72f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.58f, 0.58f, 0.62f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.50f, 0.55f, 1.0f);

    colors[ImGuiCol_Separator]        = ImVec4(0.70f, 0.70f, 0.72f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.20f, 0.45f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive]  = ImVec4(0.20f, 0.45f, 0.75f, 1.0f);

    colors[ImGuiCol_ResizeGrip]       = ImVec4(0.20f, 0.45f, 0.75f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]= ImVec4(0.20f, 0.45f, 0.75f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.20f, 0.45f, 0.75f, 0.95f);

    colors[ImGuiCol_CheckMark]        = ImVec4(0.20f, 0.45f, 0.75f, 1.0f);
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.20f, 0.45f, 0.75f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.15f, 0.35f, 0.65f, 1.0f);

    colors[ImGuiCol_Text]             = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    colors[ImGuiCol_TextDisabled]     = ImVec4(0.45f, 0.45f, 0.48f, 1.0f);

    colors[ImGuiCol_DockingPreview]   = ImVec4(0.20f, 0.45f, 0.75f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]   = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
}

void ThemeManager::applyEink() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Flat: no rounding, no soft gradients — e-ink can't render anti-aliased
    // curves or subtle shading cleanly, and rounded corners just add ghosting-
    // prone edges.
    style.WindowRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    const ImVec4 white  (1.00f, 1.00f, 1.00f, 1.0f);
    const ImVec4 black  (0.00f, 0.00f, 0.00f, 1.0f);
    const ImVec4 ltGray (0.85f, 0.85f, 0.85f, 1.0f);
    const ImVec4 midGray(0.65f, 0.65f, 0.65f, 1.0f);
    const ImVec4 dkGray (0.35f, 0.35f, 0.35f, 1.0f);

    colors[ImGuiCol_WindowBg]         = white;
    colors[ImGuiCol_ChildBg]          = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
    colors[ImGuiCol_PopupBg]          = white;
    colors[ImGuiCol_Border]           = black;
    colors[ImGuiCol_BorderShadow]     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_TitleBg]          = ltGray;
    colors[ImGuiCol_TitleBgActive]    = ltGray;
    colors[ImGuiCol_TitleBgCollapsed] = ltGray;

    colors[ImGuiCol_MenuBarBg]        = ltGray;

    colors[ImGuiCol_FrameBg]          = white;
    colors[ImGuiCol_FrameBgHovered]   = ltGray;
    colors[ImGuiCol_FrameBgActive]    = midGray;

    colors[ImGuiCol_Button]           = ltGray;
    colors[ImGuiCol_ButtonHovered]    = midGray;
    colors[ImGuiCol_ButtonActive]     = dkGray;

    colors[ImGuiCol_Header]           = ltGray;
    colors[ImGuiCol_HeaderHovered]    = midGray;
    colors[ImGuiCol_HeaderActive]     = dkGray;

    colors[ImGuiCol_Tab]              = ltGray;
    colors[ImGuiCol_TabHovered]       = midGray;
    colors[ImGuiCol_TabSelected]      = midGray;
    colors[ImGuiCol_TabDimmed]        = ltGray;
    colors[ImGuiCol_TabDimmedSelected]= midGray;

    colors[ImGuiCol_ScrollbarBg]      = white;
    colors[ImGuiCol_ScrollbarGrab]    = midGray;
    colors[ImGuiCol_ScrollbarGrabHovered] = dkGray;
    colors[ImGuiCol_ScrollbarGrabActive]  = black;

    colors[ImGuiCol_Separator]        = black;
    colors[ImGuiCol_SeparatorHovered] = dkGray;
    colors[ImGuiCol_SeparatorActive]  = black;

    colors[ImGuiCol_ResizeGrip]       = midGray;
    colors[ImGuiCol_ResizeGripHovered]= dkGray;
    colors[ImGuiCol_ResizeGripActive] = black;

    colors[ImGuiCol_CheckMark]        = black;
    colors[ImGuiCol_SliderGrab]       = dkGray;
    colors[ImGuiCol_SliderGrabActive] = black;

    colors[ImGuiCol_Text]             = black;
    colors[ImGuiCol_TextDisabled]     = midGray;

    colors[ImGuiCol_DockingPreview]   = midGray;
    colors[ImGuiCol_DockingEmptyBg]   = white;
}

} // namespace materializr
