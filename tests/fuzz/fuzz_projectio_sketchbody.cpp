// Fast, in-memory (no temp file) fuzzer targeting ProjectIO::parseSketchBody
// specifically — the count/length-prefix-token-heavy nested grammar (SPLINE/
// POLYGON element loops etc.) inside the anonymous-namespace
// parseSketchBodyImpl, reached through this public wrapper. Kept as a
// separate harness from fuzz_projectio_load (which fuzzes the whole gzip'd
// file) so the mutator can hammer this specific inner grammar directly at
// much higher exec/sec, instead of relying on it to first get past the
// gzip/header framing on every iteration.
#include "io/ProjectIO.h"
#include "modeling/Sketch.h"

#include <cstdint>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    std::istringstream is(std::string(reinterpret_cast<const char*>(Data), Size));
    materializr::Sketch sk;
    materializr::ProjectIO::parseSketchBody(is, sk, "SKETCH_END");
    return 0;
}
