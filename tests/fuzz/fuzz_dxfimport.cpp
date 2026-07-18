// Fuzzer over DxfImport::importFile — the DXF profile parser added in the
// STEP/IGES/DXF exchange work. It walks an untrusted group-code/value stream
// and feeds the values straight through std::atoi/std::atof (LINE, CIRCLE,
// ARC, LWPOLYLINE/POLYLINE bulge segments, SPLINE de-Boor sampling, ELLIPSE),
// so it's exactly the allocate/parse-from-untrusted-tokens surface the other
// harnesses target — but neither the June audits nor the original fuzz set
// covered it. File-path API, so each iteration's bytes go to a tmpfs temp
// file first (same pattern as fuzz_svgimport).
#include "io/DxfImport.h"
#include "modeling/Sketch.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    char path[] = "/dev/shm/mzr_fuzz_dxf_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    ssize_t written = write(fd, Data, Size);
    close(fd);
    if (written != static_cast<ssize_t>(Size)) { unlink(path); return 0; }

    materializr::Sketch sk;
    materializr::DxfImport::importFile(path, sk);

    unlink(path);
    return 0;
}
