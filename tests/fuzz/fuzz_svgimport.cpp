// Fuzzer over SvgImport::load — nanosvg parsing plus this project's own
// preprocessing passes (<use> expansion, CSS inlining, <text> outline
// rendering via Font_BRepFont). File-path API, so each iteration's bytes go
// to a tmpfs temp file first.
#include "modeling/SvgImport.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_svg_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    materializr::SvgPaths out;
    materializr::SvgImport::load(path, out);

    unlink(path);
    return 0;
}
