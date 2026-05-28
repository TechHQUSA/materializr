#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <string>

enum class ExtrudeMode { NewBody, Union, Subtract, Intersect };
enum class ExtrudeDirection { Normal, Symmetric, Custom };

class ExtrudeOp : public Operation {
public:
    ExtrudeOp();
    ~ExtrudeOp() override = default;

    // Parameters
    void setProfile(const TopoDS_Shape& wire); // closed wire/face to extrude
    void setDistance(double distance);
    void setDirection(ExtrudeDirection dir);
    void setMode(ExtrudeMode mode);
    void setTargetBody(int bodyId); // for boolean modes
    void setDraftAngle(double degrees);

    // Getters
    double getDistance() const { return m_distance; }
    ExtrudeDirection getDirection() const { return m_direction; }
    ExtrudeMode getMode() const { return m_mode; }
    int getTargetBody() const { return m_targetBodyId; }
    double getDraftAngle() const { return m_draftAngle; }

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Extrude"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "extrude"; }
    OperationDiff captureDiff() const override;

private:
    TopoDS_Shape m_profile;
    double m_distance = 10.0;
    ExtrudeDirection m_direction = ExtrudeDirection::Normal;
    ExtrudeMode m_mode = ExtrudeMode::NewBody;
    int m_targetBodyId = -1;
    double m_draftAngle = 0.0;

    // For undo
    int m_createdBodyId = -1;
    TopoDS_Shape m_previousTargetShape; // for boolean undo
};
