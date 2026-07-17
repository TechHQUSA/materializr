// The ONE translation unit that owns the stb_image implementation.
#include "ImageDecode.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO            // we only ever decode from memory
#define STBI_NO_HDR              // no float/HDR path — photos only
#define STBI_NO_LINEAR
#include "../third_party/stb_image.h"

#include <cstring>

namespace materializr {

bool decodeImage(const uint8_t* bytes, size_t len, DecodedImage& out) {
    if (!bytes || len == 0) return false;
    int w = 0, h = 0, comp = 0;
    // Force 4 channels: the GL upload path is plain RGBA8 either way, and a
    // constant format keeps the renderer simple.
    stbi_uc* px = stbi_load_from_memory(bytes, static_cast<int>(len),
                                        &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return true;
}

bool probeImageSize(const uint8_t* bytes, size_t len, int& wOut, int& hOut) {
    if (!bytes || len == 0) return false;
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(bytes, static_cast<int>(len), &w, &h, &comp))
        return false;
    if (w <= 0 || h <= 0) return false;
    wOut = w;
    hOut = h;
    return true;
}

} // namespace materializr
