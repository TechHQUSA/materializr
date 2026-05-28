#include "PushPullOp.h"

#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp_Face.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <imgui.h>
#include <cmath>
#include <unordered_set>

PushPullOp::PushPullOp() = default;

void PushPullOp::setTargets(std::vector<Target> targets) {
    m_targets = std::move(targets);
}

void PushPullOp::setDistance(double d) {
    m_distance = d;
}

bool PushPullOp::execute(Document& doc) {
    m_previousBodies.clear();
    m_createdBodyIds.clear();
    if (m_targets.empty() || std::abs(m_distance) < 1e-6) return false;

    std::unordered_set<int> savedBodies;

    for (const auto& tgt : m_targets) {
        if (tgt.profile.IsNull()) continue;

        // Compute face outward normal
        gp_Vec faceNormal(0, 0, 1);
        try {
            BRepGProp_Face prop(tgt.profile);
            double u1, u2, v1, v2;
            prop.Bounds(u1, u2, v1, v2);
            gp_Pnt center;
            gp_Vec n;
            prop.Normal((u1 + u2) * 0.5, (v1 + v2) * 0.5, center, n);
            if (n.Magnitude() > 1e-10) faceNormal = n.Normalized();
        } catch (...) {}

        // Sign of distance determines prism direction & boolean mode
        double dir = m_distance >= 0.0 ? 1.0 : -1.0;
        gp_Vec prismVec = faceNormal * (std::abs(m_distance) * dir);

        TopoDS_Shape prism;
        try {
            BRepPrimAPI_MakePrism mk(tgt.profile, prismVec);
            mk.Build();
            if (!mk.IsDone()) continue;
            prism = mk.Shape();
        } catch (...) { continue; }

        if (tgt.sourceBodyId >= 0) {
            // Save original (once per body)
            if (!savedBodies.count(tgt.sourceBodyId)) {
                try {
                    m_previousBodies.emplace_back(tgt.sourceBodyId,
                                                  doc.getBody(tgt.sourceBodyId));
                    savedBodies.insert(tgt.sourceBodyId);
                } catch (...) { continue; }
            }

            TopoDS_Shape current = doc.getBody(tgt.sourceBodyId);
            TopoDS_Shape result;
            try {
                if (m_distance > 0) {
                    BRepAlgoAPI_Fuse fuse(current, prism);
                    fuse.Build();
                    if (!fuse.IsDone()) continue;
                    result = fuse.Shape();
                } else {
                    BRepAlgoAPI_Cut cut(current, prism);
                    cut.Build();
                    if (!cut.IsDone()) continue;
                    result = cut.Shape();
                }
                try {
                    ShapeUpgrade_UnifySameDomain unifier(result, true, true, true);
                    unifier.Build();
                    TopoDS_Shape unified = unifier.Shape();
                    if (!unified.IsNull()) result = unified;
                } catch (...) {}
                doc.updateBody(tgt.sourceBodyId, result);
            } catch (...) { continue; }
        } else {
            // Free-floating: create a new body
            int id = doc.addBody(prism, m_distance > 0 ? "Push" : "Pull");
            m_createdBodyIds.push_back(id);
        }
    }

    return !m_previousBodies.empty() || !m_createdBodyIds.empty();
}

bool PushPullOp::undo(Document& doc) {
    try {
        // Remove created bodies first
        for (int id : m_createdBodyIds) {
            doc.removeBody(id);
        }
        m_createdBodyIds.clear();
        // Restore mutated bodies
        for (const auto& [id, shape] : m_previousBodies) {
            doc.updateBody(id, shape);
        }
        m_previousBodies.clear();
        return true;
    } catch (...) {
        return false;
    }
}

std::string PushPullOp::description() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Push/Pull %.2f mm (%zu region%s)",
                  m_distance, m_targets.size(), m_targets.size() == 1 ? "" : "s");
    return buf;
}

void PushPullOp::renderProperties() {
    ImGui::Text("Push/Pull");
    ImGui::Separator();
    ImGui::InputDouble("Distance", &m_distance, 0.1, 1.0, "%.3f");
    ImGui::Text("Regions: %zu", m_targets.size());
}

OperationDiff PushPullOp::captureDiff() const {
    OperationDiff d;
    for (const auto& [id, shape] : m_previousBodies)
        if (id >= 0 && !shape.IsNull()) d.modifiedBefore.push_back({id, shape});
    for (int id : m_createdBodyIds)
        if (id >= 0) d.created.push_back(id);
    return d;
}
