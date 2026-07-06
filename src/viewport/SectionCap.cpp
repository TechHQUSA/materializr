#include "SectionCap.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRep_Tool.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

namespace materializr {

bool computeSectionCap(const TopoDS_Shape& shape, const gp_Pln& cuttingPlane,
                       std::vector<float>& outPositions) {
    if (shape.IsNull()) return false;

    const size_t startSize = outPositions.size();

    try {
        // ShapeRenderer clips away the +normal half (discard dot>0), so the
        // solid it keeps is the -normal half-space. Intersect the body with
        // that half-space and fill the new planar faces sitting on the cut
        // plane, otherwise a solid body reads as a hollow shell.
        const gp_Dir n = cuttingPlane.Axis().Direction();
        const gp_Pnt loc = cuttingPlane.Location();

        // Cheap reject: only proceed when the plane strictly cuts the body
        // (material on BOTH sides). This skips the Common+mesh for bodies the
        // plane never touches (avoids per-frame cost while dragging the offset
        // slider) and, since it demands material on the removed side too, stops
        // a plane that merely grazes a boundary face from drawing a spurious
        // cap over that untouched face.
        Bnd_Box bbox;
        BRepBndLib::Add(shape, bbox);
        if (bbox.IsVoid()) return false;
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const gp_Pnt corners[8] = {
            gp_Pnt(xmin, ymin, zmin), gp_Pnt(xmax, ymin, zmin),
            gp_Pnt(xmin, ymax, zmin), gp_Pnt(xmax, ymax, zmin),
            gp_Pnt(xmin, ymin, zmax), gp_Pnt(xmax, ymin, zmax),
            gp_Pnt(xmin, ymax, zmax), gp_Pnt(xmax, ymax, zmax)};
        double dLo = 1e300, dHi = -1e300;
        for (const gp_Pnt& c : corners) {
            const double d = gp_Vec(loc, c).Dot(gp_Vec(n));
            if (d < dLo) dLo = d;
            if (d > dHi) dHi = d;
        }
        const double straddleEps = 1e-6;
        if (!(dLo < -straddleEps && dHi > straddleEps)) return false;

        const double L = 1.0e5; // plane extent; must bisect any body
        TopoDS_Face planeFace =
            BRepBuilderAPI_MakeFace(cuttingPlane, -L, L, -L, L).Face();
        gp_Pnt refPnt = loc.Translated(gp_Vec(n) * -1.0); // kept (solid) side
        TopoDS_Shape halfSpace =
            BRepPrimAPI_MakeHalfSpace(planeFace, refPnt).Solid();

        BRepAlgoAPI_Common common(shape, halfSpace);
        common.Build();
        if (!common.IsDone()) return false;
        const TopoDS_Shape& capped = common.Shape();
        if (capped.IsNull()) return false;

        BRepMesh_IncrementalMesh mesher(capped, 0.1);
        mesher.Perform();

        const double planeTol = 1.0e-3;
        for (TopExp_Explorer fexp(capped, TopAbs_FACE); fexp.More(); fexp.Next()) {
            const TopoDS_Face& face = TopoDS::Face(fexp.Current());
            BRepAdaptor_Surface surf(face);
            if (surf.GetType() != GeomAbs_Plane) continue;
            gp_Pln fp = surf.Plane();
            // Cap faces lie ON the cut plane: parallel normal + point on plane.
            if (!fp.Axis().Direction().IsParallel(n, 0.01)) continue;
            if (cuttingPlane.Distance(fp.Location()) > planeTol) continue;

            TopLoc_Location tloc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, tloc);
            if (tri.IsNull()) continue;
            const gp_Trsf& trsf = tloc.Transformation();

            for (int t = 1; t <= tri->NbTriangles(); ++t) {
                int a = 0, b = 0, c = 0;
                tri->Triangle(t).Get(a, b, c);
                const gp_Pnt verts[3] = {
                    tri->Node(a).Transformed(trsf),
                    tri->Node(b).Transformed(trsf),
                    tri->Node(c).Transformed(trsf)};
                for (const gp_Pnt& p : verts) {
                    outPositions.push_back(static_cast<float>(p.X()));
                    outPositions.push_back(static_cast<float>(p.Y()));
                    outPositions.push_back(static_cast<float>(p.Z()));
                }
            }
        }
    } catch (...) {
        // Never hand back a half-built cap: roll back any partial append so a
        // failure renders as "no cap" rather than broken geometry.
        outPositions.resize(startSize);
        return false;
    }

    return outPositions.size() > startSize;
}

} // namespace materializr
