#include "ThreadOp.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <Geom_CylindricalSurface.hxx>
#include <Geom2d_Line.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Dir2d.hxx>
#include <imgui.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

ThreadOp::ThreadOp() = default;

void ThreadOp::setAxis(const gp_Ax2& axis) {
    m_axis = axis;
    m_axOX = axis.Location().X();
    m_axOY = axis.Location().Y();
    m_axOZ = axis.Location().Z();
    m_axDX = axis.Direction().X();
    m_axDY = axis.Direction().Y();
    m_axDZ = axis.Direction().Z();
    m_axXX = axis.XDirection().X();
    m_axXY = axis.XDirection().Y();
    m_axXZ = axis.XDirection().Z();
}

bool ThreadOp::execute(Document& doc) {
    if (m_bodyId < 0 || m_pitch <= 0.05 || m_depth <= 0.0 ||
        m_length <= 0.0 || m_radius <= m_depth) {
        return false;
    }
    // Runaway guard: a 0.1 mm pitch over a long rod would sweep thousands of
    // turns and lock the app for minutes. 300 turns is far beyond any sane
    // model at this app's scale.
    double turns = m_length / m_pitch;
    if (turns > 300.0) return false;

    try {
        m_previousShape = doc.getBody(m_bodyId);
        if (m_previousShape.IsNull()) return false;

        // Rebuild the axis from the serialisable components (identical for
        // fresh and reloaded ops).
        gp_Pnt loc(m_axOX, m_axOY, m_axOZ);
        gp_Dir zd(m_axDX, m_axDY, m_axDZ);
        gp_Dir xd(m_axXX, m_axXY, m_axXZ);
        gp_Ax3 ax3(loc, zd, xd);

        // ---- Helix spine: a straight 2D line on the cylindrical surface.
        // U is the angular coordinate, V the axial one, so slope pitch/2π in
        // (U,V) IS the helix; handedness flips the U direction.
        Handle(Geom_CylindricalSurface) cylSurf =
            new Geom_CylindricalSurface(ax3, m_radius);
        gp_Dir2d slope(m_rightHanded ? 2.0 * M_PI : -2.0 * M_PI, m_pitch);
        Handle(Geom2d_Line) line2d = new Geom2d_Line(gp_Pnt2d(0.0, 0.0), slope);
        // Parametric length of `turns` revolutions along that 2D line.
        double uLen = std::sqrt(4.0 * M_PI * M_PI + m_pitch * m_pitch) * turns;
        TopoDS_Edge helixEdge =
            BRepBuilderAPI_MakeEdge(line2d, cylSurf, 0.0, uLen).Edge();
        BRepLib::BuildCurves3d(helixEdge); // pipe shell needs the 3D curve
        TopoDS_Wire spine = BRepBuilderAPI_MakeWire(helixEdge).Wire();

        // ---- V-groove profile at the helix start (U=0, V=0 → the point
        // loc + radius·X̂). The triangle lives in the radial/axial plane:
        // base slightly on the MATERIAL-FREE side of the surface so the cut
        // detaches cleanly, apex `depth` into the material. Hole: material
        // is outside the surface → apex outward. Boss: apex inward.
        double pad   = 0.05 * m_pitch + 1e-3;
        double baseR = m_isHole ? (m_radius - pad) : (m_radius + pad);
        double apexR = m_isHole ? (m_radius + m_depth) : (m_radius - m_depth);
        double halfW = 0.3 * m_pitch; // groove half-width at the surface

        auto pt = [&](double rad, double dz) {
            return gp_Pnt(loc.X() + zd.X() * dz + xd.X() * rad,
                          loc.Y() + zd.Y() * dz + xd.Y() * rad,
                          loc.Z() + zd.Z() * dz + xd.Z() * rad);
        };
        BRepBuilderAPI_MakePolygon tri;
        tri.Add(pt(baseR, -halfW));
        tri.Add(pt(baseR, halfW));
        tri.Add(pt(apexR, 0.0));
        tri.Close();
        TopoDS_Wire profile = tri.Wire();

        // ---- Sweep the profile along the helix. Binormal mode keeps the
        // triangle's axial edge parallel to the cylinder axis the whole way
        // round (Frenet would corkscrew it).
        BRepOffsetAPI_MakePipeShell pipe(spine);
        pipe.SetMode(zd);
        pipe.Add(profile, Standard_False, Standard_False);
        pipe.Build();
        if (!pipe.IsDone()) return false;
        if (!pipe.MakeSolid()) return false;

        BRepAlgoAPI_Cut cut(m_previousShape, pipe.Shape());
        cut.Build();
        if (!cut.IsDone()) return false;

        doc.updateBody(m_bodyId, cut.Shape());
        return true;
    } catch (...) {
        return false;
    }
}

bool ThreadOp::undo(Document& doc) {
    if (m_bodyId < 0 || m_previousShape.IsNull()) return false;
    try {
        doc.updateBody(m_bodyId, m_previousShape);
        return true;
    } catch (...) {
        return false;
    }
}

std::string ThreadOp::description() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s thread Ø%.1f, pitch %.2f mm%s",
                  m_isHole ? "Internal" : "External",
                  m_radius * 2.0, m_pitch, m_rightHanded ? "" : " (LH)");
    return buf;
}

void ThreadOp::renderProperties() {
    ImGui::Text("%s Thread", m_isHole ? "Internal" : "External");
    ImGui::Separator();
    ImGui::InputDouble("Pitch (mm)", &m_pitch, 0.1, 0.5, "%.2f");
    if (m_pitch < 0.1) m_pitch = 0.1;
    ImGui::InputDouble("Depth (mm)", &m_depth, 0.05, 0.2, "%.2f");
    if (m_depth < 0.05) m_depth = 0.05;
    bool rh = m_rightHanded;
    if (ImGui::Checkbox("Right-handed", &rh)) m_rightHanded = rh;
    ImGui::Text("Diameter: %.2f mm   Length: %.2f mm", m_radius * 2.0, m_length);
}

OperationDiff ThreadOp::captureDiff() const {
    OperationDiff d;
    if (m_bodyId >= 0 && !m_previousShape.IsNull())
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    return d;
}

std::string ThreadOp::serializeParams() const {
    char buf[420];
    std::snprintf(buf, sizeof(buf),
        "body=%d;radius=%.6f;length=%.6f;pitch=%.6f;depth=%.6f;hole=%d;rh=%d;"
        "ox=%.9g;oy=%.9g;oz=%.9g;dx=%.9g;dy=%.9g;dz=%.9g;"
        "xx=%.9g;xy=%.9g;xz=%.9g",
        m_bodyId, m_radius, m_length, m_pitch, m_depth,
        m_isHole ? 1 : 0, m_rightHanded ? 1 : 0,
        m_axOX, m_axOY, m_axOZ, m_axDX, m_axDY, m_axDZ,
        m_axXX, m_axXY, m_axXZ);
    return buf;
}

bool ThreadOp::deserializeParams(const std::string& blob) {
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        double d = std::atof(val.c_str());
        int    i = std::atoi(val.c_str());
        if      (key == "body")   { m_bodyId = i; any = true; }
        else if (key == "radius") { m_radius = d; any = true; }
        else if (key == "length") { m_length = d; any = true; }
        else if (key == "pitch")  { m_pitch = d; any = true; }
        else if (key == "depth")  { m_depth = d; any = true; }
        else if (key == "hole")   { m_isHole = (i != 0); any = true; }
        else if (key == "rh")     { m_rightHanded = (i != 0); any = true; }
        else if (key == "ox") { m_axOX = d; any = true; }
        else if (key == "oy") { m_axOY = d; any = true; }
        else if (key == "oz") { m_axOZ = d; any = true; }
        else if (key == "dx") { m_axDX = d; any = true; }
        else if (key == "dy") { m_axDY = d; any = true; }
        else if (key == "dz") { m_axDZ = d; any = true; }
        else if (key == "xx") { m_axXX = d; any = true; }
        else if (key == "xy") { m_axXY = d; any = true; }
        else if (key == "xz") { m_axXZ = d; any = true; }
        pos = end + 1;
    }
    return any;
}

bool ThreadOp::rehydrateFromReload(const ReloadState& state, Document& /*doc*/) {
    if (m_bodyId < 0) return false;
    m_previousShape.Nullify();
    for (const auto& [id, shp] : state.modifiedBefore)
        if (id == m_bodyId) { m_previousShape = shp; break; }
    return !m_previousShape.IsNull();
}
