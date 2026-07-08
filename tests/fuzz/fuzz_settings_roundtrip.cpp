// Fuzzer chaining Settings load -> save -> importJson -> exportJson (both
// the text .cfg and JSON formats). Cheapest harness to build — Settings.cpp
// has zero OCCT/SDL/ImGui dependency. Proves the *parser* survives
// adversarial round-tripping; it does NOT prove the known downstream OOB-
// index bug (unclamped orbitButton/panButton/theme ints eventually feeding
// an ImGui array index in Application_Viewport.cpp) is fixed — that needs a
// dedicated test_settings_clamping gtest, out of scope for this harness.
#include "io/Settings.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_cfg_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    materializr::AppSettings s = materializr::SettingsIO::load(path);
    materializr::SettingsIO::save(path, s); // load -> apply -> re-save

    bool ok = false;
    materializr::AppSettings s2 = materializr::SettingsIO::importJson(path, &ok);
    materializr::SettingsIO::exportJson(path, s2);

    unlink(path);
    return 0;
}
