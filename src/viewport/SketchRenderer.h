#pragma once

#include "gl_common.h"
#include <glm/glm.hpp>
#include <vector>
#include <set>

namespace materializr {

class Sketch;
class SketchTool;
class SketchSolver;

class SketchRenderer {
public:
    SketchRenderer();
    ~SketchRenderer();

    bool initialize();

    // Width (px) for committed sketch geometry — user setting (Sketch line
    // width). Point markers scale with it too. See uploadAndDraw / drawLines.
    void setLineWidth(float w) { m_lineWidth = w; }

    void render(const Sketch* sketch, const SketchTool* tool,
                const glm::mat4& view, const glm::mat4& projection,
                const SketchSolver* solver = nullptr);

    // Highlight a single region of a sketch (outline only, in given color).
    void renderRegionBoundary(const Sketch* sketch, int regionIndex,
                              const glm::vec3& color, float lineWidth,
                              const glm::mat4& view, const glm::mat4& projection);

    // Translucent fill of a region's face (triangulated). Drawn under the
    // boundary so selected/hovered regions read as surfaces, not outlines.
    void renderRegionFill(const Sketch* sketch, int regionIndex,
                          const glm::vec3& color, float alpha,
                          const glm::mat4& view, const glm::mat4& projection);

    // Highlight every primitive in a sketch (lines, circles, arcs, splines,
    // polygon edges) in a single colour at the given line width — used when
    // the whole sketch is in the selection, including open profiles that
    // have no closed region for renderRegionBoundary to outline.
    void renderSketchHighlight(const Sketch* sketch,
                               const glm::vec3& color, float lineWidth,
                               const glm::mat4& view, const glm::mat4& projection);

    // Highlight only specific elements (by id) of a sketch — used to show which
    // line / circle / arc a selected history step edits, even when that sketch
    // isn't the one being actively drawn.
    void renderElementsHighlight(const Sketch* sketch,
                                 const std::set<int>& lineIds,
                                 const std::set<int>& circleIds,
                                 const std::set<int>& arcIds,
                                 const glm::vec3& color, float lineWidth,
                                 const glm::mat4& view, const glm::mat4& projection);

    // Draw a face-local measurement grid covering the active sketch face.
    // `faceExtent` is the half-width of the grid (in sketch units) around the sketch origin.
    void renderFaceGrid(const Sketch* sketch, float faceExtent, float gridStep,
                        const glm::mat4& view, const glm::mat4& projection);

private:
    bool compileShader(unsigned int& shader, unsigned int type, const char* source);
    bool linkProgram();

    void drawLines(const Sketch* sketch, const glm::mat4& vp);
    void drawCircles(const Sketch* sketch, const glm::mat4& vp);
    void drawArcs(const Sketch* sketch, const glm::mat4& vp);
    void drawPoints(const Sketch* sketch, const glm::mat4& vp);
    void drawSplines(const Sketch* sketch, const glm::mat4& vp);
    void drawPolygons(const Sketch* sketch, const glm::mat4& vp);
    void drawPreview(const Sketch* sketch, const SketchTool* tool, const glm::mat4& vp);
    void drawSvgGhost(const Sketch* sketch, const SketchTool* tool, const glm::mat4& vp);
    void drawTrimHover(const Sketch* sketch, const SketchTool* tool, const glm::mat4& vp);
    void drawMidpointDots(const Sketch* sketch, const glm::mat4& vp);
    void drawConstraints(const Sketch* sketch, const SketchSolver* solver, const glm::mat4& vp);

    void uploadAndDraw(const std::vector<float>& verts, GLenum mode, const glm::vec3& color,
                       const glm::mat4& vp, float lineWidth = 2.0f);

    // Convert sketch 2D coordinate to 3D world position using the sketch's plane
    glm::vec3 toWorld(const Sketch* sketch, glm::vec2 pt2d) const;

    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    float m_lineWidth = 2.5f; // committed-geometry width (Sketch line width setting)
    int m_locMVP = -1;
    int m_locColor = -1;
    int m_locAlpha = -1;
    int m_locPointSize = -1;
};

} // namespace materializr
