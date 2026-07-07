#pragma once

namespace materializr {

// Eink: flat, pure black/white/gray, no gradients or rounded corners — for
// e-paper Android tablets (Boox and similar). Pair with materializr::einkMode().
enum class Theme { Dark, Light, Eink };

class ThemeManager {
public:
    ThemeManager();

    void setTheme(Theme theme);
    Theme getTheme() const;
    void toggle();

    // Apply the current theme to ImGui
    void apply();

    // Render theme selector (for menu bar)
    // Returns true if theme changed
    bool renderSelector();

private:
    Theme m_theme = Theme::Dark;

    void applyDark();
    void applyLight();
    void applyEink();
};

} // namespace materializr
