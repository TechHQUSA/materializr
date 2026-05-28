#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <string>

enum class TransformType { Translate, Rotate, Scale };

class TransformOp : public Operation {
public:
    TransformOp();
    ~TransformOp() override = default;

    // Parameters
    void setBodyId(int id);
    void setType(TransformType type);
    void setTranslation(double dx, double dy, double dz);
    void setRotation(double ax, double ay, double az, double angleDeg);
    void setScale(double factor);                       // uniform
    void setScaleXYZ(double sx, double sy, double sz);  // per-axis (non-uniform)
    // Centre for Rotate/Scale (default world origin). Gizmo transforms use the
    // body's bounding-box centre so it rotates/scales in place.
    void setCenter(double cx, double cy, double cz);

    // Getters
    int getBodyId() const { return m_bodyId; }
    TransformType getType() const { return m_type; }

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Transform"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "transform"; }
    OperationDiff captureDiff() const override;

private:
    int m_bodyId = -1;
    TransformType m_type = TransformType::Translate;
    double m_dx = 0, m_dy = 0, m_dz = 0;
    double m_ax = 0, m_ay = 1, m_az = 0, m_angle = 0;
    double m_scale = 1.0;
    double m_sx = 1.0, m_sy = 1.0, m_sz = 1.0; // per-axis scale
    bool m_nonUniform = false;
    double m_cx = 0, m_cy = 0, m_cz = 0;       // centre for rotate/scale
    TopoDS_Shape m_previousShape;
};
