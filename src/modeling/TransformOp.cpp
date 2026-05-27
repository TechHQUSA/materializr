#include "TransformOp.h"
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <gp_Trsf.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Mat.hxx>
#include <gp_XYZ.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <imgui.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

TransformOp::TransformOp() = default;

void TransformOp::setBodyId(int id) {
    m_bodyId = id;
}

void TransformOp::setType(TransformType type) {
    m_type = type;
}

void TransformOp::setTranslation(double dx, double dy, double dz) {
    m_dx = dx;
    m_dy = dy;
    m_dz = dz;
}

void TransformOp::setRotation(double ax, double ay, double az, double angleDeg) {
    m_ax = ax;
    m_ay = ay;
    m_az = az;
    m_angle = angleDeg;
}

void TransformOp::setScale(double factor) {
    m_scale = factor;
    m_nonUniform = false;
}

void TransformOp::setScaleXYZ(double sx, double sy, double sz) {
    m_sx = sx;
    m_sy = sy;
    m_sz = sz;
    m_nonUniform = true;
}

void TransformOp::setCenter(double cx, double cy, double cz) {
    m_cx = cx;
    m_cy = cy;
    m_cz = cz;
}

bool TransformOp::execute(Document& doc) {
    if (m_bodyId < 0) {
        return false;
    }

    try {
        // Store previous shape for undo
        m_previousShape = doc.getBody(m_bodyId);

        gp_Pnt center(m_cx, m_cy, m_cz);

        // Non-uniform scale needs a general transform (gp_Trsf can't); scale each
        // axis about the centre with gp_GTrsf (translation keeps the centre fixed).
        if (m_type == TransformType::Scale && m_nonUniform) {
            gp_GTrsf gt;
            gt.SetVectorialPart(gp_Mat(m_sx, 0, 0, 0, m_sy, 0, 0, 0, m_sz));
            gt.SetTranslationPart(gp_XYZ(m_cx - m_sx * m_cx,
                                         m_cy - m_sy * m_cy,
                                         m_cz - m_sz * m_cz));
            BRepBuilderAPI_GTransform gtf(m_previousShape, gt, true);
            gtf.Build();
            if (!gtf.IsDone()) return false;
            doc.updateBody(m_bodyId, gtf.Shape());
            return true;
        }

        gp_Trsf trsf;
        switch (m_type) {
            case TransformType::Translate: {
                trsf.SetTranslation(gp_Vec(m_dx, m_dy, m_dz));
                break;
            }
            case TransformType::Rotate: {
                double angleRad = m_angle * M_PI / 180.0;
                gp_Ax1 axis(center, gp_Dir(m_ax, m_ay, m_az));
                trsf.SetRotation(axis, angleRad);
                break;
            }
            case TransformType::Scale: {
                trsf.SetScale(center, m_scale);
                break;
            }
        }

        BRepBuilderAPI_Transform transform(m_previousShape, trsf, true);
        transform.Build();
        if (!transform.IsDone()) {
            return false;
        }

        doc.updateBody(m_bodyId, transform.Shape());
        return true;
    } catch (...) {
        return false;
    }
}

bool TransformOp::undo(Document& doc) {
    if (m_bodyId < 0 || m_previousShape.IsNull()) {
        return false;
    }

    try {
        doc.updateBody(m_bodyId, m_previousShape);
        return true;
    } catch (...) {
        return false;
    }
}

std::string TransformOp::description() const {
    switch (m_type) {
        case TransformType::Translate:
            return "Translate (" + std::to_string(m_dx) + ", " +
                   std::to_string(m_dy) + ", " + std::to_string(m_dz) + ")";
        case TransformType::Rotate:
            return "Rotate " + std::to_string(m_angle) + " deg around (" +
                   std::to_string(m_ax) + ", " + std::to_string(m_ay) + ", " +
                   std::to_string(m_az) + ")";
        case TransformType::Scale:
            return "Scale by " + std::to_string(m_scale);
    }
    return "Transform";
}

void TransformOp::renderProperties() {
    ImGui::Text("Transform");
    ImGui::Separator();

    const char* typeItems[] = { "Translate", "Rotate", "Scale" };
    int typeIndex = static_cast<int>(m_type);
    if (ImGui::Combo("Type", &typeIndex, typeItems, 3)) {
        m_type = static_cast<TransformType>(typeIndex);
    }

    ImGui::InputInt("Body ID", &m_bodyId);

    switch (m_type) {
        case TransformType::Translate:
            ImGui::InputDouble("X", &m_dx, 0.1, 1.0, "%.3f");
            ImGui::InputDouble("Y", &m_dy, 0.1, 1.0, "%.3f");
            ImGui::InputDouble("Z", &m_dz, 0.1, 1.0, "%.3f");
            break;
        case TransformType::Rotate:
            ImGui::InputDouble("Axis X", &m_ax, 0.1, 1.0, "%.3f");
            ImGui::InputDouble("Axis Y", &m_ay, 0.1, 1.0, "%.3f");
            ImGui::InputDouble("Axis Z", &m_az, 0.1, 1.0, "%.3f");
            ImGui::InputDouble("Angle (deg)", &m_angle, 1.0, 15.0, "%.1f");
            break;
        case TransformType::Scale:
            ImGui::InputDouble("Scale Factor", &m_scale, 0.1, 0.5, "%.3f");
            break;
    }
}
