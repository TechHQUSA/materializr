// Fuzzer over StepIO::import, validating that OCC_CATCH_SIGNALS (StepIO.cpp)
// actually converts an OCCT kernel SIGSEGV on a crafted STEP file into the
// catch block rather than the process aborting outright.
//
// IMPORTANT — run with:
//   ASAN_OPTIONS=allow_user_segv_handler=1:handle_segv=1 ./fuzz_stepio ...
// ASan installs its own SIGSEGV handler at process startup; without
// allow_user_segv_handler=1 it refuses to let OSD::SetSignal's later
// sigaction() call win the race, and ASan's handler fires first instead of
// OCCT's. That's still a legitimate "found a fault" result, but it means
// this harness never actually exercises the specific thing it exists to
// validate. Set the env var, always.
#include "io/StepIO.h"
#include "core/Document.h"

#include <OSD.hxx>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    OSD::SetSignal(Standard_False); // same call main.cpp makes at startup
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_stp_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    Document doc;
    materializr::StepIO::import(path, doc);

    unlink(path);
    return 0;
}
