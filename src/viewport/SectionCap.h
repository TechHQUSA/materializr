#pragma once
#include <gp_Pln.hxx>
#include <TopoDS_Shape.hxx>
#include <vector>

namespace materializr {

// Fill the cross-section of `shape` cut by `cuttingPlane`. Appends triangle
// positions (x,y,z per vertex) of the planar cap faces - the region where the
// plane passes through solid material - so a section-clipped solid does not
// read as a hollow shell. Keeps the -normal half-space, matching the viewport
// shader which discards the +normal side. Returns true if any cap was produced.
//
// Pure OCCT (no GL) so it is testable headless.
bool computeSectionCap(const TopoDS_Shape& shape, const gp_Pln& cuttingPlane,
                       std::vector<float>& outPositions);

} // namespace materializr
