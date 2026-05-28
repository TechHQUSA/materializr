#pragma once
#include <string>

namespace materializr {

// User-facing application preferences that persist between launches. Defaults
// here are the out-of-the-box behaviour and are also the fallback whenever a
// key is missing or unreadable in the settings file.
struct AppSettings {
    int  theme              = 0;    // 0 = Dark, 1 = Light
    int  orbitButton        = 2;    // ImGuiMouseButton: 0=Left, 1=Right, 2=Middle
    int  panButton          = 1;
    bool levelOrbit         = true; // turntable (level) vs free trackball orbit
    bool autosaveEnabled    = false;
    int  autosaveIntervalSec = 120;
};

// Reads/writes AppSettings as a simple `key = value` text file. The reader is
// intentionally tolerant: unknown keys are ignored and missing/garbled keys
// fall back to defaults, so a settings file written by a newer or older build
// never prevents the app from starting. The writer always emits the full set
// of currently-known keys, so new settings are added to the file automatically.
namespace SettingsIO {
    // Default location: $XDG_CONFIG_HOME/materializr/settings.cfg
    // (or ~/.config/materializr/settings.cfg).
    std::string defaultPath();

    AppSettings load(const std::string& path);
    bool        save(const std::string& path, const AppSettings& s);
}

} // namespace materializr
