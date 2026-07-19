// Fuzzer over BrepIO::import — OCCT's native BREP reader (BRepTools::Read) run
// on untrusted bytes, the exact same crafted-file crash surface as StepIO/
// IgesIO but for the .brep exchange path (FreeCAD and anything OCCT-based).
// Like those two, BrepIO.cpp is recompiled into this target WITH
// OCC_CONVERT_SIGNALS (materializr_core doesn't carry it) so the harness
// actually exercises the OCC_CATCH_SIGNALS signal-conversion path — otherwise
// a kernel SIGSEGV on a malformed file would abort instead of being caught.
// File-path API, so each iteration's bytes go to a tmpfs temp file first.
//
// ASAN_OPTIONS=allow_user_segv_handler=1:handle_segv=1 is needed at run
// time so OCCT's SIGSEGV handler is the one that fires, not ASan's — same note
// as the StepIO/IgesIO harnesses.
#include "io/BrepIO.h"
#include "core/Document.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_brep_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    Document doc;
    materializr::BrepIO::import(path, doc);

    unlink(path);
    return 0;
}
