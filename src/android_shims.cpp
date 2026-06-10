// Android implementations of the two desktop-only subsystems excluded from the
// Android build: portable-file-dialogs (FileDialogs) and the libcurl update
// checker (UpdateChecker). Entirely compiled out on desktop.
//
// FileDialogs is currently a minimal stub: it tracks the "last directory" and
// reports no active dialog, so the app builds and runs. Real open/save needs
// the Storage Access Framework (ACTION_OPEN_DOCUMENT / ACTION_CREATE_DOCUMENT)
// bridged through JNI — see android/README.md, Task "SAF file I/O".
#if defined(__ANDROID__)

#include "io/FileDialogs.h"
#include "ui/UpdateChecker.h"

#include <cctype>
#include <string>
#include <vector>

namespace materializr {

// ── FileDialogs (stub; SAF wiring pending) ───────────────────────────────────
namespace {
std::string& lastDirStorage() {
    static std::string dir;
    return dir;
}
} // namespace

void FileDialogs::openFile(const std::string& /*title*/,
                           const std::vector<FileFilter>& /*filters*/,
                           std::function<void(const std::string&)> callback) {
    // TODO(SAF): launch ACTION_OPEN_DOCUMENT via JNI and deliver the chosen URI.
    if (callback) callback(std::string()); // empty == cancelled
}

void FileDialogs::saveFile(const std::string& /*title*/,
                           const std::string& /*defaultName*/,
                           const std::vector<FileFilter>& /*filters*/,
                           std::function<void(const std::string&)> callback) {
    // TODO(SAF): launch ACTION_CREATE_DOCUMENT via JNI and deliver the chosen URI.
    if (callback) callback(std::string());
}

void FileDialogs::render() { /* no native dialog to draw yet */ }
bool FileDialogs::isOpen() { return false; }
void FileDialogs::setLastDir(const std::string& dir) { lastDirStorage() = dir; }
const std::string& FileDialogs::getLastDir() { return lastDirStorage(); }

// ── UpdateChecker (no network check on mobile; keep version compare) ──────────
UpdateChecker::Result UpdateChecker::check(const std::string& /*owner*/,
                                           const std::string& /*repo*/) {
    Result r;
    r.ok = false;
    r.current = MATERIALIZR_VERSION;
    r.errorMessage = "Update checks are disabled on Android.";
    return r;
}

int UpdateChecker::compareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s) {
        std::vector<int> parts;
        int cur = 0; bool any = false;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) { cur = cur * 10 + (c - '0'); any = true; }
            else if (c == '.') { parts.push_back(any ? cur : 0); cur = 0; any = false; }
            // ignore a leading 'v' and any other non-digit separators
        }
        parts.push_back(any ? cur : 0);
        return parts;
    };
    std::vector<int> va = parse(a), vb = parse(b);
    std::size_t n = va.size() > vb.size() ? va.size() : vb.size();
    for (std::size_t i = 0; i < n; ++i) {
        int x = i < va.size() ? va[i] : 0;
        int y = i < vb.size() ? vb[i] : 0;
        if (x < y) return -1;
        if (x > y) return 1;
    }
    return 0;
}

} // namespace materializr

#endif // __ANDROID__
