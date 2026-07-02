#pragma once
#include "gl_common.h"
#include <glm/glm.hpp>
#include <TopoDS_Shape.hxx>
#include <TopLoc_Location.hxx>
#include <map>
#include <vector>

class SelectionManager;
class Document;

namespace materializr {

class SelectionHighlight {
public:
    SelectionHighlight();
    ~SelectionHighlight();

    bool initialize();

    void render(const SelectionManager& sel, const Document& doc,
                const glm::mat4& view, const glm::mat4& projection);

    // Width (in pixels) of highlighted edges and body outlines. Clamped to a
    // sane range; what the driver actually honours depends on its max line
    // width, but most support up to ~10.
    void setLineWidth(float w);

    // Drop every cached tessellation. Called on project load — the entries
    // pin their source shapes alive (see CacheEntry), so the outgoing
    // project's topology would otherwise stay resident forever.
    void clearCaches();

private:
    void renderFace(const TopoDS_Shape& face, const glm::mat4& vp, const glm::vec3& color);
    void renderEdge(const TopoDS_Shape& edge, const glm::mat4& vp, const glm::vec3& color);
    void renderBody(const TopoDS_Shape& body, const glm::mat4& vp, const glm::vec3& color);

    bool compileShader(unsigned int& shader, unsigned int type, const char* source);

    // Upload `verts` (xyz triplets, GL_LINES order) and draw them as quads of
    // `halfWidthPx` pixels using the geometry-shader line program. Used by both
    // edge and body highlighting so thickness is honoured in core-profile GL.
    void drawThickLines(const std::vector<float>& verts, const glm::mat4& vp,
                        const glm::vec3& color, float halfWidthPx);

    // Faces are a translucent triangle tint (no geometry shader).
    unsigned int m_program = 0;
    int m_locMVP = -1;
    int m_locColor = -1;

    // Lines: a separate program whose geometry shader expands each segment into
    // a screen-space quad, since glLineWidth > 1 is not honoured in core profile.
    unsigned int m_lineProgram = 0;
    int m_locLineMVP = -1;
    int m_locLineColor = -1;
    int m_locViewport = -1;
    int m_locHalfWidth = -1;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;

    // Highlighted-edge line width in pixels. Body outlines render slightly
    // thinner so a whole selected body stays distinguishable from single edges.
    float m_edgeLineWidth = 3.0f;

    // Highlight-tessellation caches. Without these, every frame the user has
    // something selected we re-walk the body's edges / the face's triangles /
    // the edge's curve (GCPnts / triangulation walks — 5-50ms per frame on a
    // complex part), so caching is the difference between smooth and 6-fps
    // orbits with a selection. Keyed on the sub-shape's TShape POINTER, with
    // three safety properties the original raw-pointer/vector maps lacked:
    //
    //  1. OWNERSHIP: each entry stores the TopoDS_Shape it was built from,
    //     pinning the TShape alive — so the key pointer can never be REUSED
    //     by a new allocation while the entry lives (a freed TShape's address
    //     could otherwise false-hit and render the OLD geometry for a NEW
    //     face).
    //  2. LOCATION REVALIDATION: the verts are baked in world coords; a
    //     location-only transform (multi-body gizmo move commit) keeps the
    //     TShape but moves the body, so a pointer-only key kept drawing the
    //     outline at the pre-move position ("wireframe lagging behind").
    //  3. BOUNDED SIZE: entries went stale on every topology rebuild (new
    //     TShape → new entry, old one orphaned forever) — hundreds of edits ×
    //     hundreds of KB per big-face entry leaked real memory over a long
    //     session. When a cache exceeds kCacheCap on insert it is cleared
    //     outright: only the CURRENT selection's entries get rebuilt next
    //     frame, so the flush is invisible. clearCaches() drops everything on
    //     project load (the pinned shapes belong to the outgoing project).
    struct CacheEntry {
        TopoDS_Shape shape;  // ownership pin (see above)
        TopLoc_Location loc; // pose the verts were sampled at
        std::vector<float> verts;
    };
    static constexpr size_t kCacheCap = 32;
    std::map<const void*, CacheEntry> m_bodyCache;
    std::map<const void*, CacheEntry> m_faceCache;
    std::map<const void*, CacheEntry> m_edgeCache;
};

} // namespace materializr
