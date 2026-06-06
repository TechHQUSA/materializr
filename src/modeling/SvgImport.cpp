#include "SvgImport.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg.h"

namespace materializr {

namespace {

// Sample one cubic segment, point count driven by its control-polygon
// length relative to the whole image (same idea as the glyph sampler's
// deflection: dense enough that the chords read as the curve).
void sampleCubic(std::vector<glm::vec2>& out, const float* p, float ref) {
    float clen = 0.0f;
    for (int i = 0; i < 3; ++i)
        clen += std::hypot(p[(i + 1) * 2] - p[i * 2],
                           p[(i + 1) * 2 + 1] - p[i * 2 + 1]);
    int n = std::max(1, static_cast<int>(std::ceil(clen / (0.005f * ref))));
    n = std::min(n, 64);
    for (int i = 1; i <= n; ++i) {
        float t = static_cast<float>(i) / n, u = 1.0f - t;
        out.push_back(glm::vec2(
            u*u*u*p[0] + 3*u*u*t*p[2] + 3*u*t*t*p[4] + t*t*t*p[6],
            u*u*u*p[1] + 3*u*u*t*p[3] + 3*u*t*t*p[5] + t*t*t*p[7]));
    }
}

} // namespace

bool SvgImport::load(const std::string& path, SvgPaths& out) {
    out = SvgPaths();
    NSVGimage* img = nsvgParseFromFile(path.c_str(), "mm", 96.0f);
    if (!img) {
        std::fprintf(stderr, "[SVG] cannot parse '%s'\n", path.c_str());
        return false;
    }
    const float ref = std::max(1.0f, std::max(img->width, img->height));

    bool haveBB = false;
    for (NSVGshape* sh = img->shapes; sh; sh = sh->next) {
        for (NSVGpath* p = sh->paths; p; p = p->next) {
            if (p->npts < 4) continue;
            std::vector<glm::vec2> pts;
            pts.push_back(glm::vec2(p->pts[0], p->pts[1]));
            for (int i = 0; i < p->npts - 1; i += 3)
                sampleCubic(pts, &p->pts[i * 2], ref);
            // Collapse consecutive duplicates — SVG paths routinely carry
            // degenerate (zero-length) cubics at joints, and a zero-length
            // sketch line would sink the whole wire in buildWires.
            {
                std::vector<glm::vec2> ded;
                ded.reserve(pts.size());
                for (const auto& q : pts)
                    if (ded.empty() ||
                        glm::length(q - ded.back()) > 1e-5f * ref)
                        ded.push_back(q);
                pts.swap(ded);
            }
            // drop the closing duplicate; closure is an explicit line later
            if (pts.size() > 2 &&
                glm::length(pts.front() - pts.back()) < 1e-4f * ref)
                pts.pop_back();
            if (pts.size() < (p->closed ? 3u : 2u)) continue;
            for (const auto& q : pts) {
                if (!haveBB) {
                    out.bbMin = out.bbMax = q;
                    haveBB = true;
                } else {
                    out.bbMin = glm::min(out.bbMin, q);
                    out.bbMax = glm::max(out.bbMax, q);
                }
            }
            out.loops.push_back(std::move(pts));
            out.closed.push_back(p->closed != 0);
        }
    }
    nsvgDelete(img);

    if (out.empty()) {
        std::fprintf(stderr, "[SVG] '%s' holds no usable paths\n",
                     path.c_str());
        return false;
    }
    std::fprintf(stderr, "[SVG] '%s': %zu paths, %.1f x %.1f units\n",
                 path.c_str(), out.loops.size(), out.size().x, out.size().y);
    return true;
}

int SvgImport::place(Sketch* sketch, const SvgPaths& svg, glm::vec2 pos,
                     float widthMm, float angleDeg) {
    if (!sketch || svg.empty() || widthMm <= 0.01f) return 0;
    glm::vec2 size = svg.size();
    float rawW = (size.x > 1e-6f) ? size.x : size.y;
    if (rawW <= 1e-6f) return 0;
    const float scale = widthMm / rawW;
    const glm::vec2 center = 0.5f * (svg.bbMin + svg.bbMax);
    const float a = glm::radians(angleDeg);
    const float ca = std::cos(a), sa = std::sin(a);
    auto map = [&](glm::vec2 p) {
        glm::vec2 l = (p - center) * scale;
        l.y = -l.y; // SVG is Y-down; sketches are Y-up
        return pos + glm::vec2(l.x * ca - l.y * sa, l.x * sa + l.y * ca);
    };

    int placed = 0;
    for (size_t li = 0; li < svg.loops.size(); ++li) {
        const auto& loop = svg.loops[li];
        // ids only — SketchPoint* would dangle across reallocations
        std::vector<int> ids;
        ids.reserve(loop.size());
        for (const auto& p : loop)
            ids.push_back(sketch->addPoint(map(p), /*fromText=*/true));
        for (size_t i = 0; i + 1 < ids.size(); ++i)
            sketch->addLine(ids[i], ids[i + 1], /*fromText=*/true);
        if (svg.closed[li] && ids.size() >= 3)
            sketch->addLine(ids.back(), ids.front(), /*fromText=*/true);
        placed++;
    }
    std::fprintf(stderr, "[SVG] placed %d loops at %.1f mm wide\n", placed,
                 widthMm);
    return placed;
}

} // namespace materializr
