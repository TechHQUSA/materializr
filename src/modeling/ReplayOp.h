#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <string>
#include <utility>
#include <vector>

// An operation reconstructed from a saved project. It can't re-run the original
// modelling action (the geometric inputs aren't reconstructable), but it stores
// the exact set of bodies before and after the step, so undo/redo reproduce the
// change and the history list survives a reload.
class ReplayOp : public Operation {
public:
    using BodyState = std::vector<std::pair<int, TopoDS_Shape>>;

    ReplayOp(std::string typeId, std::string name, std::string description,
             BodyState before, BodyState after);

    bool execute(Document& doc) override; // redo  -> restore the "after" state
    bool undo(Document& doc) override;     // undo  -> restore the "before" state
    std::string name() const override { return m_name; }
    std::string description() const override { return m_description; }
    void renderProperties() override;
    std::string typeId() const override { return m_typeId; }
    bool isReloaded() const override { return true; }

private:
    static void restore(Document& doc, const BodyState& state);

    std::string m_typeId, m_name, m_description;
    BodyState m_before, m_after;
};
