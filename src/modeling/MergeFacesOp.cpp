#include "MergeFacesOp.h"
#include "FaceLineage.h"
#include "UnifyTolerance.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools_History.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>

namespace {
int faceCount(const TopoDS_Shape& s) {
    if (s.IsNull()) return 0;
    TopTools_IndexedMapOfShape m;
    TopExp::MapShapes(s, TopAbs_FACE, m);
    return m.Extent();
}
} // namespace

MergeFacesOp::MergeFacesOp() = default;

void MergeFacesOp::setBody(int id) { m_bodyId = id; }

bool MergeFacesOp::execute(Document& doc) {
    if (m_bodyId < 0) return false;
    try {
        m_previousShape = doc.getBody(m_bodyId);
        if (m_previousShape.IsNull()) return false;
        m_facesBefore = faceCount(m_previousShape);

        // concatBSplines deliberately FALSE. With it on, unify re-fits spline
        // surfaces rather than just dropping redundant boundaries, and on a
        // part with curved faces that MOVES geometry — measured on the nacelle
        // as a 5.5e-6 relative volume change, which the guard below rejected.
        // A repair must not reshape the part.
        ShapeUpgrade_UnifySameDomain u(m_previousShape, /*edges=*/true,
                                       /*faces=*/true, /*concatBSplines=*/false);
        u.SetAngularTolerance(materializr::kUnifyAngularTol);
        u.Build();
        const TopoDS_Shape result = u.Shape();
        if (result.IsNull()) return false;

        m_facesAfter = faceCount(result);
        // Nothing to do. Refusing means History::pushOperation declines and no
        // step is added — running this on an already-clean body should not
        // litter the timeline.
        if (m_facesAfter >= m_facesBefore) return false;

        if (!BRepCheck_Analyzer(result).IsValid()) {
            std::fprintf(stderr, "[MergeFaces] result was invalid; rejecting.\n");
            return false;
        }
        // A merge must not change what the part IS: unify only drops redundant
        // internal boundaries, so a real volume change means it did something
        // else and the result is not trustworthy.
        //
        // 1e-4 RELATIVE, not tighter. BRepGProp integrates over the faces, so
        // its answer depends on how the boundary is subdivided — merging 189
        // faces into 183 moves the result by ~5e-6 relative on the nacelle with
        // nothing geometrically changed. That is the integrator's own
        // repeatability, not a reshape. A merge that actually moved material
        // would be orders of magnitude larger than this bound.
        GProp_GProps gA, gB;
        BRepGProp::VolumeProperties(m_previousShape, gA);
        BRepGProp::VolumeProperties(result, gB);
        const double volTol = 1e-4 * std::max(1.0, std::fabs(gA.Mass()));
        if (std::fabs(gA.Mass() - gB.Mass()) > volTol) {
            std::fprintf(stderr,
                         "[MergeFaces] volume changed %.6f -> %.6f; rejecting.\n",
                         gA.Mass(), gB.Mass());
            return false;
        }

        // Carry the body's face lineage across the merge BEFORE updateBody
        // clears it: a merged face inherits both parents' ids, so a downstream
        // fillet/chamfer that named one of them still resolves. Without this
        // the repair would silently drift every reference on the body.
        materializr::topo::FaceIdMap carried;
        bool haveMap = false;
        if (const auto* im = doc.bodyFaceIds(m_bodyId)) {
            carried = materializr::topo::carryThrough(*im, u.History(), result);
            haveMap = true;
        }

        doc.updateBody(m_bodyId, result);
        if (haveMap) doc.setBodyFaceIds(m_bodyId, std::move(carried));
        return true;
    } catch (...) {
        return false;
    }
}

bool MergeFacesOp::undo(Document& doc) {
    if (m_bodyId < 0 || m_previousShape.IsNull()) return false;
    try {
        doc.updateBody(m_bodyId, m_previousShape);
        return true;
    } catch (...) {
        return false;
    }
}

std::string MergeFacesOp::description() const {
    return "Merge faces (" + std::to_string(m_facesBefore) + " \xE2\x86\x92 " +
           std::to_string(m_facesAfter) + ")";
}

void MergeFacesOp::renderProperties() {
    ImGui::Text("Merge Faces");
    ImGui::Separator();
    ImGui::Text("Faces: %d -> %d", m_facesBefore, m_facesAfter);
    ImGui::Text("Body ID: %d", m_bodyId);
}

std::string MergeFacesOp::serializeParams() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "body=%d;before=%d;after=%d",
                  m_bodyId, m_facesBefore, m_facesAfter);
    return buf;
}

bool MergeFacesOp::deserializeParams(const std::string& blob) {
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        const std::string key = blob.substr(pos, eq - pos);
        const std::string val = blob.substr(eq + 1, end - eq - 1);
        if      (key == "body")   { m_bodyId = std::atoi(val.c_str()); any = true; }
        else if (key == "before") { m_facesBefore = std::atoi(val.c_str()); }
        else if (key == "after")  { m_facesAfter = std::atoi(val.c_str()); }
        pos = end + 1;
    }
    return any;
}

bool MergeFacesOp::rehydrateFromReload(const ReloadState& state, Document& /*doc*/) {
    if (m_bodyId < 0) return false;
    m_previousShape.Nullify();
    for (const auto& [id, shp] : state.modifiedBefore)
        if (id == m_bodyId) { m_previousShape = shp; break; }
    // The whole op is "unify this body", so the pre-state is all it needs to
    // re-execute — no sub-shape references to resolve.
    return !m_previousShape.IsNull();
}

OperationDiff MergeFacesOp::captureDiff() const {
    OperationDiff d;
    if (m_bodyId >= 0 && !m_previousShape.IsNull())
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    return d;
}
