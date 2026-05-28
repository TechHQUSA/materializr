#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <string>

class DeleteOp : public Operation {
public:
    DeleteOp();
    ~DeleteOp() override = default;

    void setBodyId(int id);

    // Getters
    int getBodyId() const { return m_bodyId; }

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Delete"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "delete"; }
    OperationDiff captureDiff() const override;

private:
    int m_bodyId = -1;
    TopoDS_Shape m_deletedShape;
    std::string m_deletedName;
    bool m_wasVisible = true;
};
