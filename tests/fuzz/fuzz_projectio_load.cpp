// Coverage-guided fuzzer over ProjectIO::load — the full-file path (gzip
// v3 + legacy plain-text v2 grammar). ProjectIO::load takes a file path (not
// a buffer) and does its own ifstream open + gzip-sniff + inflate internally,
// so each iteration's bytes are persisted to a tmpfs file first.
#include "io/ProjectIO.h"
#include "core/Document.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_pio_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    Document doc;
    materializr::ProjectHistory hist;
    // Return value / errorMessage deliberately ignored: any input is valid to
    // *try* — ASan/UBSan gate on crashes, not on parse success.
    materializr::ProjectIO::load(path, doc, &hist);

    unlink(path);
    return 0;
}
