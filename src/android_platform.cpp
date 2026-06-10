#include "android_platform.h"

#if defined(__ANDROID__)

#include <SDL.h>
#include <android/log.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <system_error>
#include <unistd.h>

namespace materializr {
namespace {
namespace fs = std::filesystem;

void logi(const std::string& m) {
    __android_log_print(ANDROID_LOG_INFO, "Materializr", "%s", m.c_str());
}

// Copy a file bundled in the APK's assets/ (read via SDL's asset-aware RWops)
// to an absolute path in writable storage. Idempotent — skips if dest exists.
bool extractAsset(const std::string& assetPath, const std::string& destPath) {
    std::error_code ec;
    if (fs::exists(destPath, ec)) return true;

    SDL_RWops* in = SDL_RWFromFile(assetPath.c_str(), "rb"); // reads from APK assets
    if (!in) { logi("missing asset: " + assetPath); return false; }
    Sint64 size = SDL_RWsize(in);
    std::vector<char> buf(size > 0 ? static_cast<size_t>(size) : 0);
    Sint64 rd = (size > 0) ? SDL_RWread(in, buf.data(), 1, static_cast<size_t>(size)) : 0;
    SDL_RWclose(in);

    fs::create_directories(fs::path(destPath).parent_path(), ec);
    std::FILE* out = std::fopen(destPath.c_str(), "wb");
    if (!out) return false;
    if (rd > 0) std::fwrite(buf.data(), 1, static_cast<size_t>(rd), out);
    std::fclose(out);
    return true;
}

} // namespace

void androidInitRuntime() {
    const char* internalC = SDL_AndroidGetInternalStoragePath();
    const std::string internal = internalC ? internalC : ".";

    // (1) Settings: SettingsIO::defaultPath() uses $HOME/.config/materializr.
    setenv("HOME", internal.c_str(), 1);

    // (2) Fonts: chdir here and extract the TTFs so resolveBundledFont()'s
    //     cwd-relative "assets/fonts/<name>" candidate resolves.
    if (chdir(internal.c_str()) != 0) logi("chdir to internal storage failed");
    const char* fonts[] = {
        "JetBrainsMono-Regular.ttf", "DejaVuSans.ttf", "DejaVuSerif.ttf"
    };
    for (const char* f : fonts) {
        extractAsset(std::string("fonts/") + f, internal + "/assets/fonts/" + f);
    }

    // (3) OpenCASCADE resources: extract every file listed in the bundled
    //     manifest, then point the CSF_* env vars at the extracted tree.
    const std::string resRoot = internal + "/occt-resources";
    if (SDL_RWops* list = SDL_RWFromFile("occt-resources.list", "rb")) {
        Sint64 sz = SDL_RWsize(list);
        std::string text(sz > 0 ? static_cast<size_t>(sz) : 0, '\0');
        if (sz > 0) SDL_RWread(list, text.data(), 1, static_cast<size_t>(sz));
        SDL_RWclose(list);
        size_t pos = 0, extracted = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string rel = text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
            pos = (nl == std::string::npos) ? text.size() : nl + 1;
            while (!rel.empty() && (rel.back() == '\r' || rel.back() == ' ')) rel.pop_back();
            if (rel.empty()) continue;
            if (extractAsset("occt-resources/" + rel, resRoot + "/" + rel)) ++extracted;
        }
        logi("extracted " + std::to_string(extracted) + " OCCT resource files");
    }

    auto setres = [&](const char* var, const std::string& sub) {
        setenv(var, (resRoot + "/" + sub).c_str(), 1);
    };
    setres("CSF_StandardDefaults",     "StdResource");
    setres("CSF_StandardLiteDefaults", "StdResource");
    setres("CSF_XCAFDefaults",         "StdResource");
    setres("CSF_PluginDefaults",       "StdResource");
    setres("CSF_TObjMessage",          "TObj");
    setres("CSF_XmlOcafResource",      "XmlOcafResource");
    setres("CSF_XSMessage",            "XSMessage");
    setres("CSF_SHMessage",            "SHMessage");
    setres("CSF_XSTEPDefaults",        "XSTEPResource");
    setres("CSF_STEPDefaults",         "XSTEPResource");
    setres("CSF_IGESDefaults",         "XSTEPResource");
    setres("CSF_MIGRATION_TYPES",      "StdResource/MigrationSheet.txt");

    logi("Materializr Android runtime initialized (home=" + internal + ")");
}

} // namespace materializr

#else  // desktop: nothing to do

namespace materializr { void androidInitRuntime() {} }

#endif
