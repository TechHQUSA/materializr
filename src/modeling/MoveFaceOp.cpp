#include "MoveFaceOp.h"
#include "SubShapeIndex.h"
#include "Sketch.h"
#include <gp_Trsf.hxx>
#include <gp_Pln.hxx>

#include <imgui.h>
#include <cstdio>
#include <cstring>

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp_Face.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <Standard_ErrorHandler.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>

bool MoveFaceOp::execute(Document& doc) {
    if (m_bodyId < 0 || m_face.IsNull() || m_move.Magnitude() < 1e-6) return false;

    try {
        OCC_CATCH_SIGNALS // turn an OCCT kernel fault here into a catch below
        m_previousShape = doc.getBody(m_bodyId);
        if (m_previousShape.IsNull()) return false;

        // Outward face normal N and a point P0 on the face (BRepGProp_Face's
        // Normal is orientation-corrected, so it points out of the body).
        BRepGProp_Face prop(m_face);
        double u1, u2, v1, v2;
        prop.Bounds(u1, u2, v1, v2);
        gp_Pnt c; gp_Vec nv;
        prop.Normal((u1 + u2) * 0.5, (v1 + v2) * 0.5, c, nv);
        if (nv.Magnitude() < 1e-9) return false;
        gp_Vec N = nv.Normalized();
        gp_Vec P0(c.X(), c.Y(), c.Z());

        // Keep the move strictly in the face's plane (slide, never push/pull).
        gp_Vec V = m_move - N * m_move.Dot(N);
        if (V.Magnitude() < 1e-6) return false; // nothing to slide

        // LOFT REBUILD (replaces the old whole-body gp_GTrsf shear, which made
        // OCCT NURBS-convert the entire body and segfault on freeform/boolean
        // geometry). A prism is bounded by the selected face and an OPPOSITE
        // PARALLEL face; lofting a capped solid between that base wire and the
        // MOVED top wire rebuilds the sheared prism with ruled walls — purely
        // local geometry, no whole-body convert. Bodies that aren't a clean
        // single-profile prism (no matching opposite face, or the face has
        // holes) simply refuse here instead of crashing.
        if (m_face.NbChildren() > 0) {
            // OuterWire-only loft can't carry inner (hole) loops yet.
            TopoDS_Wire ow = BRepTools::OuterWire(m_face);
            int nwires = 0;
            for (TopExp_Explorer wx(m_face, TopAbs_WIRE); wx.More(); wx.Next()) ++nwires;
            if (nwires > 1) {
                std::fprintf(stderr, "[MoveFace] refused: face has holes (not yet supported)\n");
                return false;
            }
        }

        // Find the opposite parallel planar face (the prism's far cap).
        TopoDS_Face baseFace;
        double bestDist = -1e300;
        for (TopExp_Explorer fx(m_previousShape, TopAbs_FACE); fx.More(); fx.Next()) {
            TopoDS_Face f = TopoDS::Face(fx.Current());
            if (f.IsSame(m_face)) continue;
            Handle(Geom_Surface) s = BRep_Tool::Surface(f);
            if (s.IsNull() || !s->IsKind(STANDARD_TYPE(Geom_Plane))) continue;
            BRepGProp_Face fp(f);
            double uu1, uu2, vv1, vv2; fp.Bounds(uu1, uu2, vv1, vv2);
            gp_Pnt fc; gp_Vec fn; fp.Normal((uu1 + uu2) * 0.5, (vv1 + vv2) * 0.5, fc, fn);
            if (fn.Magnitude() < 1e-9) continue;
            if (fn.Normalized().Dot(N) > -0.999) continue; // not antiparallel
            double dist = -(gp_Vec(fc.X(), fc.Y(), fc.Z()) - P0).Dot(N);
            if (dist > bestDist) { bestDist = dist; baseFace = f; }
        }
        if (baseFace.IsNull()) {
            std::fprintf(stderr, "[MoveFace] refused: no opposite face to loft from\n");
            return false;
        }

        TopoDS_Wire wBase = BRepTools::OuterWire(baseFace);
        TopoDS_Wire wTop  = BRepTools::OuterWire(m_face);
        if (wBase.IsNull() || wTop.IsNull()) return false;

        // Move the top wire by V; reverse the base wire so both run the same way
        // viewed from +N (the two caps' outer wires are oppositely oriented).
        gp_Trsf slideT; slideT.SetTranslation(V);
        TopoDS_Wire wTopMoved =
            TopoDS::Wire(BRepBuilderAPI_Transform(wTop, slideT, Standard_True).Shape());
        wBase.Reverse();

        BRepOffsetAPI_ThruSections loft(Standard_True /*solid*/, Standard_True /*ruled*/);
        loft.AddWire(wBase);
        loft.AddWire(wTopMoved);
        loft.Build();
        if (!loft.IsDone()) {
            std::fprintf(stderr, "[MoveFace] loft failed — refusing\n");
            return false;
        }
        TopoDS_Shape result = loft.Shape();
        if (result.IsNull() || !BRepCheck_Analyzer(result).IsValid()) {
            std::fprintf(stderr, "[MoveFace] lofted result invalid — refusing\n");
            return false;
        }
        int nsolids = 0;
        for (TopExp_Explorer sx(result, TopAbs_SOLID); sx.More(); sx.Next()) ++nsolids;
        if (nsolids < 1) return false;

        m_resultShape = result;
        doc.updateBody(m_bodyId, result);

        // Slide on-face sketches by the same in-plane move (translate their
        // plane), so they stay glued to the moved face instead of floating.
        m_appliedMove = V;
        gp_Trsf slide;
        slide.SetTranslation(V);
        for (int sid : m_sketchIds) {
            if (auto sk = doc.getSketch(sid)) {
                gp_Pln pln = sk->getPlane();
                pln.Transform(slide);
                sk->setPlane(pln);
                // The cached host face is used to build the sketch's regions —
                // move it too (copy=true forces a fresh TShape so the region
                // cache, keyed on it, invalidates), or its stale geometry
                // highlights at the OLD position when the region is clicked.
                TopoDS_Face sf = sk->getSourceFace();
                if (!sf.IsNull()) {
                    TopoDS_Shape mv = BRepBuilderAPI_Transform(sf, slide, Standard_True).Shape();
                    if (!mv.IsNull() && mv.ShapeType() == TopAbs_FACE)
                        sk->setSourceFace(TopoDS::Face(mv));
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool MoveFaceOp::undo(Document& doc) {
    if (m_bodyId < 0 || m_previousShape.IsNull()) return false;
    try {
        doc.updateBody(m_bodyId, m_previousShape);
        // Slide the on-face sketches back to where they were.
        gp_Trsf back;
        back.SetTranslation(m_appliedMove.Reversed());
        for (int sid : m_sketchIds) {
            if (auto sk = doc.getSketch(sid)) {
                gp_Pln pln = sk->getPlane();
                pln.Transform(back);
                sk->setPlane(pln);
                TopoDS_Face sf = sk->getSourceFace();
                if (!sf.IsNull()) {
                    TopoDS_Shape mv = BRepBuilderAPI_Transform(sf, back, Standard_True).Shape();
                    if (!mv.IsNull() && mv.ShapeType() == TopAbs_FACE)
                        sk->setSourceFace(TopoDS::Face(mv));
                }
            }
        }
        return true;
    } catch (...) { return false; }
}

std::string MoveFaceOp::description() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Move Face by (%.2f, %.2f, %.2f)",
                  m_move.X(), m_move.Y(), m_move.Z());
    return buf;
}

void MoveFaceOp::renderProperties() {
    ImGui::Text("Move Face");
    ImGui::Separator();
    ImGui::Text("Body ID: %d", m_bodyId);
    ImGui::Text("Move: (%.2f, %.2f, %.2f) mm", m_move.X(), m_move.Y(), m_move.Z());
}

OperationDiff MoveFaceOp::captureDiff() const {
    OperationDiff d;
    if (m_bodyId >= 0 && !m_previousShape.IsNull())
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    return d;
}

std::string MoveFaceOp::serializeParams() const {
    std::string blob;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "body=%d;mx=%.6f;my=%.6f;mz=%.6f",
                  m_bodyId, m_move.X(), m_move.Y(), m_move.Z());
    blob += buf;
    if (!m_previousShape.IsNull() && !m_face.IsNull()) {
        std::vector<TopoDS_Shape> faces{m_face};
        std::string idx = SubShapeIndex::serialize(m_previousShape, faces, TopAbs_FACE);
        if (!idx.empty()) blob += ";face=" + idx;
    }
    return blob;
}

bool MoveFaceOp::deserializeParams(const std::string& blob) {
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if      (key == "body") { m_bodyId = std::atoi(val.c_str()); any = true; }
        else if (key == "mx")   { m_move.SetX(std::atof(val.c_str())); any = true; }
        else if (key == "my")   { m_move.SetY(std::atof(val.c_str())); any = true; }
        else if (key == "mz")   { m_move.SetZ(std::atof(val.c_str())); any = true; }
        else if (key == "face") { m_faceIndices = SubShapeIndex::parse(val); any = true; }
        pos = end + 1;
    }
    return any;
}

bool MoveFaceOp::rehydrateFromReload(const ReloadState& state, Document& /*doc*/) {
    if (m_bodyId < 0 || m_faceIndices.empty()) return false;
    m_previousShape.Nullify();
    m_resultShape.Nullify();
    for (const auto& [id, shp] : state.modifiedBefore)
        if (id == m_bodyId) { m_previousShape = shp; break; }
    for (const auto& [id, shp] : state.modifiedAfter)
        if (id == m_bodyId) { m_resultShape = shp; break; }
    if (m_previousShape.IsNull()) return false;

    std::vector<TopoDS_Shape> resolved;
    if (!SubShapeIndex::resolveAll(m_previousShape, m_faceIndices, TopAbs_FACE, resolved) ||
        resolved.empty()) {
        return false;
    }
    m_face = TopoDS::Face(resolved.front());
    return true;
}
