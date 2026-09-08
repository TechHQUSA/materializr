#pragma once

#include <gp_Pln.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace materializr {

// Fill the cross-section of `shape` cut by `cuttingPlane`. Appends triangle
// positions (x,y,z per vertex) of the cap - the region where the plane passes
// through solid material - so a section-clipped solid does not read as a
// hollow shell. Keeps the -normal half-space, matching the viewport shader
// which discards the +normal side. Returns true if any cap was produced.
//
// Works from the shape's existing triangulation (the mesh the viewport draws),
// not from OCCT booleans: each triangle the plane crosses yields a segment,
// the segments are joined into loops, and the loops are filled as a planar
// face. A body with no triangulation produces no cap. Pure OCCT (no GL) so it
// is testable headless.
bool computeSectionCap(const TopoDS_Shape& shape, const gp_Pln& cuttingPlane,
                       std::vector<float>& outPositions);

} // namespace materializr
