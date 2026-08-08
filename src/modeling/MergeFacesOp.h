#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <string>
#include <vector>

// Merge a body's coplanar faces back into single faces — the repair half of
// issue #81.
//
// An imported STEP part usually arrives with flat surfaces already split into
// several coplanar pieces, and editing it splits more. The pieces are
// geometrically one surface, but they stay separate faces, so the user sees a
// seam line across an otherwise flat face and anything that treats a face as a
// unit misbehaves: Unfold sees several faces where there is one panel, and
// Sketch-on-face binds to whichever sliver was picked.
//
// The ops themselves now prevent NEW seams (see UnifyTolerance.h — the cause
// was OCCT's default angular tolerance being tighter than its own boolean
// output). This op is for geometry that is ALREADY split: parts imported
// before that fix, and STEP files that arrived split in the first place.
//
// Deliberately whole-body rather than face-selection: the merge is a topology
// rewrite, and OCCT decides which faces are same-domain — asking the user to
// pick faces would imply a control that doesn't exist. execute() refuses (and
// so adds no history step) when nothing merges, so running it on a clean body
// is a no-op the user is told about rather than an empty step in the timeline.
class MergeFacesOp : public Operation {
public:
    MergeFacesOp();
    ~MergeFacesOp() override = default;

    void setBody(int id);
    int getBodyId() const { return m_bodyId; }
    int getFacesBefore() const { return m_facesBefore; }
    int getFacesAfter() const { return m_facesAfter; }

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Merge Faces"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "mergefaces"; }
    OperationDiff captureDiff() const override;
    std::vector<int> plannedBodyIds() const override { return {m_bodyId}; }
    std::string serializeParams() const override;
    bool deserializeParams(const std::string& blob) override;
    bool rehydrateFromReload(const ReloadState& state, Document& doc) override;

private:
    int m_bodyId = -1;
    TopoDS_Shape m_previousShape;
    // For the History label. Persisted so a reloaded step still reads
    // "Merge Faces (189 -> 183)" instead of losing its own description.
    int m_facesBefore = 0;
    int m_facesAfter = 0;
};
