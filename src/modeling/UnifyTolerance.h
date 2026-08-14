#pragma once

namespace materializr {

// Angular tolerance for every ShapeUpgrade_UnifySameDomain in the codebase.
//
// OCCT's default is Precision::Angular() = 1e-12, which is TIGHTER THAN ITS OWN
// BOOLEAN OUTPUT. The coplanar faces a cut or fuse leaves behind have normals
// agreeing to ~1e-9, not 1e-12, so unify silently declines to merge them and
// the split shows to the user as a seam line across an otherwise flat face —
// issue #81, hit on an imported-and-edited STEP part.
//
// Measured on that part (left nacelle.mzr, 189 faces, 10 coplanar-adjacent
// face pairs) with a probe that counts adjacent planar faces sharing a plane:
//
//     default            -> faces 189, coplanar pairs 10   (unify does nothing)
//     linear 1e-4 only   -> faces 189, coplanar pairs 10   (linear is NOT it)
//     angular 1e-9 only  -> faces 183, coplanar pairs  0
//     angular 1e-3 only  -> faces 160, coplanar pairs  0
//
// 1e-9 is deliberately the tightest value that still clears every seam: it
// merges only the six faces that were genuinely one, where 1e-3 merges 29. Do
// not loosen it to "fix" some other merge without measuring what else it
// starts merging — this tolerance decides which faces cease to exist, and
// downstream fillet/chamfer references are resolved against those faces.
constexpr double kUnifyAngularTol = 1.0e-9;

} // namespace materializr
