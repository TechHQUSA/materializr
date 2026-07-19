// Fuzzer over StlIO::import — OCCT's STL reader (RWStl::ReadFile) on untrusted
// bytes, covering both the ASCII and binary STL grammars plus the downstream
// triangulation-to-solid sewing. Same crafted-file crash surface as the other
// mesh/exchange readers; StlIO.cpp is recompiled here WITH OCC_CONVERT_SIGNALS
// (materializr_core doesn't carry it) so OCC_CATCH_SIGNALS in the importer
// actually converts a kernel fault into the catch rather than aborting.
// File-path API → tmpfs temp file per iteration.
//
// Run with ASAN_OPTIONS=allow_user_segv_handler=1:handle_segv=1 so OCCT's
// signal handler wins, not ASan's — same note as the StepIO/IgesIO/BrepIO
// harnesses.
#include "io/StlIO.h"
#include "core/Document.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_stl_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    Document doc;
    materializr::StlIO::import(path, doc);

    unlink(path);
    return 0;
}
