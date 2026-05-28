#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <string>

enum class MirrorPlane { XY, XZ, YZ, Custom };

class MirrorOp : public Operation {
public:
    MirrorOp();
    ~MirrorOp() override = default;

    // Parameters
    void setBody(int id);
    void setPlane(MirrorPlane p);
    void setCustomPlane(const gp_Ax2& ax);
    void setKeepOriginal(bool keep);

    // Getters
    int getBodyId() const { return m_bodyId; }
    MirrorPlane getPlane() const { return m_plane; }
    bool getKeepOriginal() const { return m_keepOriginal; }
    int getMirroredBodyId() const { return m_mirroredBodyId; }

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Mirror"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "mirror"; }
    OperationDiff captureDiff() const override;

private:
    gp_Ax2 getMirrorAxis() const;

    int m_bodyId = -1;
    MirrorPlane m_plane = MirrorPlane::YZ;
    gp_Ax2 m_customPlane;
    bool m_keepOriginal = true;
    int m_mirroredBodyId = -1;
    TopoDS_Shape m_previousShape;
};
