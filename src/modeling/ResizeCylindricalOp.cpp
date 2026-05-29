#include "ResizeCylindricalOp.h"

#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>

#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace {
// MakeCone with equal R1/R2 has degenerate edge cases — use MakeCylinder
// where the radii match exactly, MakeCone where they differ.
// `BRepPrimAPI_Make*` primitives report IsDone() == false until Build() is
// explicitly called; the lazy Shape() accessor builds on demand but checking
// IsDone() pre-build looks like a failure. Build() first, then check.
TopoDS_Shape makeRevolveSolid(const gp_Ax2& axis, double r1, double r2, double h, bool* ok) {
    *ok = false;
    try {
        if (std::abs(r1 - r2) < 1e-7) {
            BRepPrimAPI_MakeCylinder mk(axis, r1, h);
            mk.Build();
            if (!mk.IsDone()) return TopoDS_Shape();
            *ok = true;
            return mk.Shape();
        }
        BRepPrimAPI_MakeCone mk(axis, r1, r2, h);
        mk.Build();
        if (!mk.IsDone()) return TopoDS_Shape();
        *ok = true;
        return mk.Shape();
    } catch (...) {
        return TopoDS_Shape();
    }
}
}

void ResizeCylindricalOp::setBody(int bodyId)         { m_bodyId = bodyId; }
void ResizeCylindricalOp::setAxis(const gp_Ax2& axis) { m_axis = axis; }
void ResizeCylindricalOp::setHeight(double h)         { m_height = h; }
void ResizeCylindricalOp::setOldRadii(double bottomR, double topR) {
    m_oldBottomR = bottomR; m_oldTopR = topR;
}
void ResizeCylindricalOp::setNewRadii(double bottomR, double topR) {
    m_newBottomR = bottomR; m_newTopR = topR;
}
void ResizeCylindricalOp::setIsHole(bool h)           { m_isHole = h; }

bool ResizeCylindricalOp::execute(Document& doc) {
    if (m_bodyId < 0 || m_height <= 0.0) {
        std::fprintf(stderr, "[Resize] bad params: bodyId=%d h=%.3f\n",
                     m_bodyId, m_height);
        return false;
    }
    if (m_newBottomR <= 0.0 || m_newTopR <= 0.0) {
        std::fprintf(stderr, "[Resize] non-positive new radii: bot=%.3f top=%.3f\n",
                     m_newBottomR, m_newTopR);
        return false;
    }

    try {
        m_previousShape = doc.getBody(m_bodyId);
    } catch (...) { return false; }

    // No-op: leave the body alone.
    if (std::abs(m_newBottomR - m_oldBottomR) < 1e-5 &&
        std::abs(m_newTopR    - m_oldTopR)    < 1e-5) return true;

    try {
        // We DON'T build the full new/old cylinder and swap them — that would
        // destroy other features sharing the cylinder's volume (e.g. a tube
        // becomes a solid cylinder because OLD is a R=0…outerR cylinder that
        // includes the tube's inner hole). Instead, build only the RING /
        // FRUSTUM SHELL of material that's actually changing (the volumetric
        // difference between OLD and NEW lateral surfaces), then fuse or cut
        // that into the body.
        //
        // No axial padding — caps coincide with the body's caps, which OCCT
        // generally handles by merging or leaving as internal faces (visually
        // invisible). The 0.1mm axial padding I tried earlier created annular
        // stubs sticking out past the body's caps.

        // Direction of OLD's lateral pad: shift OLD AWAY from where the
        // change-volume will live, so its surface doesn't coincide with the
        // body's existing cylindrical face. growing → OLD shifts inward (–).
        // shrinking → OLD shifts outward (+).
        bool growing = (m_newBottomR > m_oldBottomR) || (m_newTopR > m_oldTopR);
        const double kRadialPad = (growing ? -1.0 : +1.0) * 0.01;
        double paddedOldBot = std::max(1e-4, m_oldBottomR + kRadialPad);
        double paddedOldTop = std::max(1e-4, m_oldTopR    + kRadialPad);

        // The "outer" cone contains the "inner" cone geometrically — pick by
        // grow direction. For face edits both ends move together; for edge
        // edits only one end moves and the other stays at oldR, but the pad
        // still makes outer ≥ inner everywhere.
        double outerBot, outerTop, innerBot, innerTop;
        if (growing) {
            outerBot = m_newBottomR;  outerTop = m_newTopR;
            innerBot = paddedOldBot;  innerTop = paddedOldTop;
        } else {
            outerBot = paddedOldBot;  outerTop = paddedOldTop;
            innerBot = m_newBottomR;  innerTop = m_newTopR;
        }

        bool ok = false;
        TopoDS_Shape outerSolid = makeRevolveSolid(m_axis, outerBot, outerTop, m_height, &ok);
        if (!ok || outerSolid.IsNull()) {
            std::fprintf(stderr, "[Resize] outer revolve failed (R1=%.3f R2=%.3f H=%.3f)\n",
                         outerBot, outerTop, m_height);
            return false;
        }
        TopoDS_Shape innerSolid = makeRevolveSolid(m_axis, innerBot, innerTop, m_height, &ok);
        if (!ok || innerSolid.IsNull()) {
            std::fprintf(stderr, "[Resize] inner revolve failed (R1=%.3f R2=%.3f H=%.3f)\n",
                         innerBot, innerTop, m_height);
            return false;
        }

        // Ring / frustum shell = outer − inner.
        BRepAlgoAPI_Cut ringMaker(outerSolid, innerSolid);
        ringMaker.Build();
        if (!ringMaker.IsDone()) {
            std::fprintf(stderr, "[Resize] ring cut failed\n");
            return false;
        }
        TopoDS_Shape ring = ringMaker.Shape();
        if (ring.IsNull()) {
            std::fprintf(stderr, "[Resize] ring null\n");
            return false;
        }

        // Add material when (hole shrinking) OR (solid growing). Cut otherwise.
        bool addMaterial = (m_isHole != growing);
        std::fprintf(stderr,
            "[Resize] isHole=%d growing=%d add=%d old(%.3f→%.3f) padded(%.3f→%.3f) "
            "new(%.3f→%.3f) H=%.3f\n",
            m_isHole ? 1 : 0, growing ? 1 : 0, addMaterial ? 1 : 0,
            m_oldBottomR, m_oldTopR, paddedOldBot, paddedOldTop,
            m_newBottomR, m_newTopR, m_height);

        TopoDS_Shape result;
        if (addMaterial) {
            BRepAlgoAPI_Fuse fuse(m_previousShape, ring);
            fuse.Build();
            if (!fuse.IsDone()) {
                std::fprintf(stderr, "[Resize] body fuse failed\n");
                return false;
            }
            result = fuse.Shape();
        } else {
            BRepAlgoAPI_Cut cut(m_previousShape, ring);
            cut.Build();
            if (!cut.IsDone()) {
                std::fprintf(stderr, "[Resize] body cut failed\n");
                return false;
            }
            result = cut.Shape();
        }
        if (result.IsNull()) {
            std::fprintf(stderr, "[Resize] result null\n");
            return false;
        }

        // Merge the ring's caps with the body's adjacent caps so the top and
        // bottom faces are single uniform faces again — without this OCCT
        // leaves them as separate adjacent planar faces that share an edge
        // (a visible hairline seam across the cap face). UnifySameDomain
        // walks the topology and merges any pair of adjacent same-surface
        // faces.
        try {
            ShapeUpgrade_UnifySameDomain unify(result,
                                               /*unifyEdges=*/Standard_True,
                                               /*unifyFaces=*/Standard_True,
                                               /*concatBSplines=*/Standard_False);
            unify.Build();
            TopoDS_Shape clean = unify.Shape();
            if (!clean.IsNull()) result = clean;
        } catch (...) {
            // Non-fatal — fall back to the un-unified result.
            std::fprintf(stderr, "[Resize] unify pass threw, using raw result\n");
        }

        doc.updateBody(m_bodyId, result);
        return true;
    } catch (...) {
        std::fprintf(stderr, "[Resize] exception during boolean\n");
        return false;
    }
}

bool ResizeCylindricalOp::undo(Document& doc) {
    if (m_bodyId < 0 || m_previousShape.IsNull()) return false;
    try { doc.updateBody(m_bodyId, m_previousShape); return true; }
    catch (...) { return false; }
}

void ResizeCylindricalOp::renderProperties() {
    if (std::abs(m_newTopR - m_newBottomR) < 1e-5) {
        ImGui::Text("%s diameter: %.2f mm",
                    m_isHole ? "Hole" : "Outer", m_newTopR * 2.0);
    } else {
        ImGui::Text("%s — cone", m_isHole ? "Hole" : "Outer");
        ImGui::Text("  bottom diameter: %.2f mm", m_newBottomR * 2.0);
        ImGui::Text("  top    diameter: %.2f mm", m_newTopR    * 2.0);
    }
    ImGui::Text("Length: %.2f mm", m_height);
    ImGui::TextDisabled("Re-edit by clicking a circular edge or the face.");
}

std::string ResizeCylindricalOp::description() const {
    char buf[160];
    if (std::abs(m_newTopR - m_newBottomR) < 1e-5)
        std::snprintf(buf, sizeof(buf),
                      "Resize %s D %.2f → %.2f mm",
                      m_isHole ? "hole" : "outer",
                      m_oldTopR * 2.0, m_newTopR * 2.0);
    else
        std::snprintf(buf, sizeof(buf),
                      "Shape %s: %.2f / %.2f mm (bottom / top)",
                      m_isHole ? "hole" : "outer",
                      m_newBottomR * 2.0, m_newTopR * 2.0);
    return buf;
}
