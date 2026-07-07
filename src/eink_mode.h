#pragma once

// Runtime "eInk mode" flag, for e-paper Android tablets (Boox and similar).
// When ON: the render loop stops redrawing once idle (src/app/Application.cpp
// Application::run()) instead of holding a 15fps idle floor, and the theme
// switches to a flat high-contrast palette (see ThemeManager / TouchTheme.h) —
// e-ink panels ghost under continuous redraw and gain nothing from gradients.
//
// Unlike touchMode(), there is no per-platform default: this is manual opt-in
// only (Settings checkbox), off everywhere by default, since there's no
// reliable way to auto-detect e-ink hardware from a plain Android build.
//
// The value is fixed for a run: Application sets it from the saved setting at
// startup and everything reads it live via einkMode(). Changing it in Settings
// takes full effect on the next launch. Header-only (inline function-local
// static) — same pattern as touch_mode.h / ui_scale.h.
namespace materializr {

inline bool& einkModeRef() {
    static bool t = false;
    return t;
}
inline bool einkMode() { return einkModeRef(); }
inline void setEinkMode(bool on) { einkModeRef() = on; }

} // namespace materializr
