// Fuzzer over IgesIO::import — same signal-conversion validation as
// fuzz_stepio.cpp (see that file's header comment for the required
// ASAN_OPTIONS=allow_user_segv_handler=1:handle_segv=1 run flag). Kept as a
// separate target/corpus from fuzz_stepio: IGES and STEP are different
// grammars entirely, and a shared corpus would waste mutator cycles turning
// one format's tokens into the other's garbage.
#include "io/IgesIO.h"
#include "core/Document.h"

#include <OSD.hxx>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    OSD::SetSignal(Standard_False);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_igs_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    Document doc;
    materializr::IgesIO::import(path, doc);

    unlink(path);
    return 0;
}
