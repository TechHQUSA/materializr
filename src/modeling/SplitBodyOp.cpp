#include "SplitBodyOp.h"
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopTools_ListOfShape.hxx>
#include <imgui.h>

SplitBodyOp::SplitBodyOp() = default;

void SplitBodyOp::setBody(int id) {
    m_bodyId = id;
}

void SplitBodyOp::setSplitPlane(const gp_Pln& plane) {
    m_splitPlane = plane;
}

bool SplitBodyOp::execute(Document& doc) {
    if (m_bodyId < 0) {
        return false;
    }

    try {
        // Store previous shape for undo
        m_previousShape = doc.getBody(m_bodyId);

        // Create a large planar face from the split plane to act as the
        // splitting tool. The size should be large enough to fully
        // intersect the body.
        BRepBuilderAPI_MakeFace faceMaker(m_splitPlane, -1000.0, 1000.0, -1000.0, 1000.0);
        faceMaker.Build();
        if (!faceMaker.IsDone()) {
            return false;
        }
        TopoDS_Shape splittingFace = faceMaker.Shape();

        // Set up the splitter
        TopTools_ListOfShape arguments;
        arguments.Append(m_previousShape);

        TopTools_ListOfShape tools;
        tools.Append(splittingFace);

        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(arguments);
        splitter.SetTools(tools);
        splitter.Build();
        if (!splitter.IsDone()) {
            return false;
        }

        // Extract resulting solids
        std::vector<TopoDS_Shape> solids;
        for (TopExp_Explorer exp(splitter.Shape(), TopAbs_SOLID); exp.More(); exp.Next()) {
            solids.push_back(exp.Current());
        }

        if (solids.size() < 2) {
            // Split did not produce two bodies (plane may not intersect)
            return false;
        }

        // Update the original body with the first solid
        doc.updateBody(m_bodyId, solids[0]);

        // Add the second solid as a new body
        m_secondBodyId = doc.addBody(solids[1], "Split Body");

        return true;
    } catch (...) {
        return false;
    }
}

bool SplitBodyOp::undo(Document& doc) {
    try {
        // Remove the second body that was created
        if (m_secondBodyId >= 0) {
            doc.removeBody(m_secondBodyId);
            m_secondBodyId = -1;
        }

        // Restore the original body
        if (m_bodyId >= 0 && !m_previousShape.IsNull()) {
            doc.updateBody(m_bodyId, m_previousShape);
        }

        return true;
    } catch (...) {
        return false;
    }
}

std::string SplitBodyOp::description() const {
    return "Split Body " + std::to_string(m_bodyId) + " by plane";
}

void SplitBodyOp::renderProperties() {
    ImGui::Text("Split Body");
    ImGui::Separator();

    ImGui::Text("Body ID: %d", m_bodyId);

    // Display plane parameters (read-only for now, set programmatically)
    gp_Pnt loc = m_splitPlane.Location();
    gp_Dir dir = m_splitPlane.Axis().Direction();
    ImGui::Text("Plane origin: (%.1f, %.1f, %.1f)", loc.X(), loc.Y(), loc.Z());
    ImGui::Text("Plane normal: (%.2f, %.2f, %.2f)", dir.X(), dir.Y(), dir.Z());

    if (m_secondBodyId >= 0) {
        ImGui::Text("Second body ID: %d", m_secondBodyId);
    }
}

OperationDiff SplitBodyOp::captureDiff() const {
    OperationDiff d;
    if (m_bodyId >= 0 && !m_previousShape.IsNull())
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    if (m_secondBodyId >= 0) d.created.push_back(m_secondBodyId);
    return d;
}
