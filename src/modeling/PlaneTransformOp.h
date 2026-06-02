#pragma once
#include "../core/Operation.h"
#include <gp_Pln.hxx>
#include <string>
#include <vector>

// Undoable construction-plane transform (move / rotate / hinge). Plane edits
// are applied LIVE to the Document (by the gizmo drag and the Rotate-About-
// Axis popup) — this op just records before/after gp_Pln snapshots so undo /
// redo can swap between them. Pushed via History::pushExecuted (the effect is
// already in the document by the time we record it).
//
// Crucially it owns NO bodies: captureDiff() is empty and execute()/undo()
// touch only Document::setPlane. That sidesteps the bodyId=-1 crash that made
// plane drags skip history in v0.6.0 (a body TransformOp with id -1 hit
// getBody(-1) on replay). Holds a batch of entries so a multi-plane gizmo
// drag commits as a single undo step.
class PlaneTransformOp : public Operation {
public:
    struct Entry {
        int    planeId;
        gp_Pln before;
        gp_Pln after;
    };

    PlaneTransformOp(std::string label, std::vector<Entry> entries)
        : m_label(std::move(label)), m_entries(std::move(entries)) {}

    bool execute(Document& doc) override;   // apply the "after" poses (redo)
    bool undo(Document& doc) override;      // restore the "before" poses

    std::string name() const override { return m_label; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "plane_transform"; }

private:
    std::string        m_label;
    std::vector<Entry> m_entries;
};
