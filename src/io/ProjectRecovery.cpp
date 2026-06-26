#include "ProjectRecovery.h"
#include "ProjectIO.h"
#include "../core/Document.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace materializr {

namespace {
// Base config directory (mirrors SketchRecovery.cpp / Settings.cpp).
std::string configBaseDir() {
#ifdef _WIN32
    if (const char* up = std::getenv("USERPROFILE"); up && *up)
        return std::string(up) + "\\materializr";
    return "materializr";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::string(xdg) + "/materializr";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.config/materializr";
    return ".materializr";
#endif
}
std::string metaPath() { return projectRecoveryPath() + ".meta"; }
} // namespace

std::string projectRecoveryPath() {
    return configBaseDir() + "/recovery/autosave.materializr";
}

bool writeProjectRecovery(const Document& doc, const ProjectHistory* history,
                          const std::string& projectPath, int bodyCount,
                          int stepCount) {
    const std::string path = projectRecoveryPath();
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);

    // Save to a temp file, then atomically rename — a crash mid-write must never
    // truncate the snapshot we'd restore from.
    const std::string tmp = path + ".tmp";
    auto res = ProjectIO::save(tmp, doc, history);
    if (!res.success) { std::filesystem::remove(tmp, ec); return false; }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { // cross-device or race: fall back to a direct overwrite
        std::filesystem::copy_file(
            tmp, path, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmp, ec);
        if (ec) return false;
    }

    // Plain-text sidecar meta: the project's identity + when + counts, so the
    // restore prompt can describe what it found without parsing the snapshot.
    std::ofstream os(metaPath(), std::ios::out | std::ios::trunc);
    if (os.is_open()) {
        os << "MZRECOVERY 1\n";
        os << "SAVEDAT " << static_cast<long long>(std::time(nullptr)) << "\n";
        os << "BODIES " << bodyCount << "\n";
        os << "STEPS " << stepCount << "\n";
        os << "PROJECT " << projectPath << "\n"; // rest-of-line; may be empty
    }
    return true;
}

bool hasProjectRecovery() {
    std::error_code ec;
    return std::filesystem::exists(projectRecoveryPath(), ec);
}

bool readProjectRecoveryMeta(ProjectRecoveryMeta& meta) {
    meta = ProjectRecoveryMeta{};
    if (!hasProjectRecovery()) return false;
    std::ifstream is(metaPath());
    if (is.is_open()) {
        std::string line;
        while (std::getline(is, line)) {
            std::istringstream s(line);
            std::string tok;
            s >> tok;
            if      (tok == "SAVEDAT") s >> meta.savedAtUnix;
            else if (tok == "BODIES")  s >> meta.bodyCount;
            else if (tok == "STEPS")   s >> meta.stepCount;
            else if (tok == "PROJECT") {
                std::string rest;
                std::getline(s, rest);
                if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                meta.projectPath = rest;
            }
        }
    }
    meta.valid = true; // the snapshot exists; the meta is best-effort
    return true;
}

void clearProjectRecovery() {
    std::error_code ec;
    std::filesystem::remove(projectRecoveryPath(), ec);
    std::filesystem::remove(projectRecoveryPath() + ".tmp", ec);
    std::filesystem::remove(metaPath(), ec);
}

} // namespace materializr
