#include "SvgExport.h"
#include "../modeling/Sketch.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace materializr {

namespace {
struct Poly {
    std::vector<glm::vec2> pts;
    bool closed = false;
};
} // namespace

SvgExportResult SvgExport::exportSketch(const std::string& filePath, const Sketch& sketch) {
    SvgExportResult result;
    std::vector<Poly> polys;

    // Lines (exact). Construction geometry is reference-only — never cut.
    for (const auto& l : sketch.getLines()) {
        if (l.isConstruction) continue;
        const SketchPoint* a = sketch.getPoint(l.startPointId);
        const SketchPoint* b = sketch.getPoint(l.endPointId);
        if (!a || !b) continue;
        polys.push_back({{a->pos, b->pos}, false});
    }

    // Circles → closed polyline.
    for (const auto& c : sketch.getCircles()) {
        if (c.isConstruction) continue;
        const SketchPoint* ctr = sketch.getPoint(c.centerPointId);
        if (!ctr) continue;
        const int N = 72;
        Poly p;
        p.closed = true;
        for (int i = 0; i < N; ++i) {
            float a = 2.0f * float(M_PI) * float(i) / float(N);
            p.pts.push_back(ctr->pos + glm::vec2(std::cos(a), std::sin(a)) * float(c.radius));
        }
        polys.push_back(std::move(p));
    }

    // Arcs → open polyline. Matches SketchRenderer::drawArcs exactly: CCW from
    // start to end, with endAngle += 2π when it wraps below startAngle.
    for (const auto& ar : sketch.getArcs()) {
        if (ar.isConstruction) continue;
        const SketchPoint* ctr = sketch.getPoint(ar.centerPointId);
        const SketchPoint* s = sketch.getPoint(ar.startPointId);
        const SketchPoint* e = sketch.getPoint(ar.endPointId);
        if (!ctr || !s || !e) continue;
        float a0 = std::atan2(s->pos.y - ctr->pos.y, s->pos.x - ctr->pos.x);
        float a1 = std::atan2(e->pos.y - ctr->pos.y, e->pos.x - ctr->pos.x);
        if (a1 < a0) a1 += 2.0f * float(M_PI);
        const int N = 48;
        Poly p;
        p.closed = false;
        for (int i = 0; i <= N; ++i) {
            float a = a0 + (a1 - a0) * float(i) / float(N);
            p.pts.push_back(ctr->pos + glm::vec2(std::cos(a), std::sin(a)) * float(ar.radius));
        }
        polys.push_back(std::move(p));
    }

    // Splines → sampled open polyline (the same interpolation the extrudable
    // profile uses, so the cut matches what's drawn).
    for (const auto& sp : sketch.getSplines()) {
        if (sp.isConstruction) continue;
        std::vector<glm::vec2> pts = sketch.sampleSpline2D(sp, 64);
        if (pts.size() >= 2) polys.push_back({std::move(pts), false});
    }

    if (polys.empty()) {
        result.errorMessage = "Sketch has no exportable (non-construction) geometry.";
        return result;
    }

    // Bounding box in sketch-plane millimeters.
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const auto& pl : polys)
        for (const auto& q : pl.pts) {
            minX = std::min(minX, q.x);
            minY = std::min(minY, q.y);
            maxX = std::max(maxX, q.x);
            maxY = std::max(maxY, q.y);
        }
    const float margin = 1.0f; // 1 mm border so edge geometry isn't flush to the page
    minX -= margin;
    minY -= margin;
    maxX += margin;
    maxY += margin;
    float w = maxX - minX, h = maxY - minY;
    if (w <= 0.0f || h <= 0.0f) {
        result.errorMessage = "Sketch geometry is degenerate (zero extent).";
        return result;
    }

    FILE* f = std::fopen(filePath.c_str(), "w");
    if (!f) {
        result.errorMessage = "Could not open file for writing: " + filePath;
        return result;
    }

    // 1 SVG user unit = 1 mm (width/height in mm + matching viewBox), so the
    // file imports at true size in LightBurn/Inkscape/CAM. Y is flipped (CAD is
    // Y-up, SVG is Y-down) so the part isn't mirrored top-to-bottom.
    std::fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    std::fprintf(f,
                 "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                 "width=\"%.4fmm\" height=\"%.4fmm\" viewBox=\"0 0 %.4f %.4f\">\n",
                 w, h, w, h);

    for (const auto& pl : polys) {
        std::fputs("  <path d=\"", f);
        for (size_t i = 0; i < pl.pts.size(); ++i) {
            float x = pl.pts[i].x - minX;
            float y = maxY - pl.pts[i].y; // flip Y
            std::fprintf(f, "%c%.4f %.4f ", i == 0 ? 'M' : 'L', x, y);
        }
        if (pl.closed) std::fputs("Z", f);
        std::fputs("\" fill=\"none\" stroke=\"#000000\" stroke-width=\"0.1\"/>\n", f);
    }

    std::fputs("</svg>\n", f);
    std::fclose(f);

    result.success = true;
    result.curveCount = static_cast<int>(polys.size());
    return result;
}

} // namespace materializr
