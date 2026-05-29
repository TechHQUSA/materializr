# Changelog

All notable changes to Materializr are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow SemVer.

## [0.1.1] — 2026-05-28

A "polish + missing-feature" release. Most work is in interaction: gizmos for
operations that were previously click-twice or hidden, live measurement readouts
during drags, and a measure tool. A handful of geometry and persistence bugs
were tracked down along the way, including one data-loss case in undo replay.

### Added

- **Measure tool** (Object / Edge / Line modes) reachable from the no-selection
  and sketch toolbars. Object measures the combined bbox of every selected body
  (clicking a face counts as picking its body); Edge sums the lengths of every
  selected edge; Line is a two-click point-to-point distance drawn as a purple
  line in the viewport that lives in 3D — orbiting moves it correctly.
- **Sketch Move/Rotate gizmo** rendered on the selection centroid in Select
  mode. Axis arrows (red X, green Y in the sketch plane) and a centre dot for
  free move snap to the active grid step; the rotate ring snaps to 15° while
  dragging. After a rotate drag, a popup lets you type an exact angle. The
  standalone sketch "Rotate" toolbar button has been retired in favour of the
  gizmo ring.
- **Sketch box-select** — click and drag from empty space in Select mode draws
  a rectangle and selects every sketch element whose screen-space projection
  lands inside. Ctrl preserves existing selection.
- **Double-click a sketch line** to select its entire connected chain (lines
  sharing endpoints, transitively). Double-clicking empty space still selects
  every element.
- **Push/Pull face arrow** — selecting a face and choosing Push/Pull now draws
  an arrow along the face normal, with a popup to type the distance and a live
  mm readout while dragging the arrow.
- **Fillet / Chamfer drag handle** on the picked edge — drag perpendicular to
  the edge to set the radius / distance, with a live measurement, alongside the
  existing value popup + slider.
- **Multi-body rotate panel** — three axis sliders + text entry + Apply/Reset/
  Close for rotating large multi-body selections. Works around the live-rotate
  gizmo being too slow on heavy selections without a renderer refactor.
- **Context-aware Ctrl+A** — nothing selected → select every body; an edge
  selected → every edge on that body; in sketch → the whole sketch.
- **3DS Max-style ViewCube** with a rotation ring around it (replaces the older
  navigation buttons).
- **Trackpad navigation mode** in Settings for laptop use.
- **Click-and-drag multi-object box select** in 3D, with Ctrl to extend.
- **Adaptive view clipping** — near/far planes scale with orbit distance so
  distant geometry and the grid no longer vanish at far zoom.
- **Sketch-face grid** now matches the host face's bounds and centroid, instead
  of always rendering a fixed 100 × 100 mm patch at the origin.
- **Grid emphasis** — every 100 lines gets a heavier weight in addition to the
  10-line emphasis.

### Changed

- **STEP import/export** now applies a Z-up → Y-up rotation around X, matching
  Materializr's world convention; export inverts the rotation so a STEP round
  trip is a no-op.
- **Move gizmo on a multi-body selection** moves every selected body, not just
  the one the gizmo is anchored to. The drag caches its pivot at start so the
  motion doesn't jolt as bodies translate beneath the renderer.
- **Sketch transforms (Copy / Mirror / Rotate)** can be invoked with nothing
  selected and will operate on the whole sketch.
- **Sketch on face** centres the face in the viewport on entry and restores the
  prior camera on exit; the camera save/restore is preserved across sketch
  enter/leave round trips.
- **Camera "up" preservation** when aligning to a sketch face via projection,
  so face-orientated views don't surprise-flip.
- **Documentation** split out of README into `docs/` (architecture, building,
  features, getting-started, usage) and the Help menu picks them up.

### Fixed

- **Critical: undo replay was deleting unselected bodies** when applied as a
  batched in-session op. `ReplayOp` now distinguishes "project reload" (where
  bodies not in the snapshot should be removed) from "in-session batch" (where
  they must not), via a `fromReload` flag.
- **Grid drifting vertically at far zoom** — the depth bias was a constant in
  NDC, so its world-space size grew with depth. Now it's a fraction of the ray
  length to the near point.
- **Sketch element drag** — clicking and holding a selected point or line
  translates the whole selection by the cursor delta each frame (was per-point
  only).
- **Sketch double-click selects everything** instead of being a no-op.
- **Multi-edge select** — Ctrl-clicking edges now extends the selection
  correctly (`SelectionManager::findEntry` matches by `IsSame()` on the shape).
- **No more jolt** when panning or orbiting during an extrude / push-pull /
  sketch placement — input projections are skipped while the camera is being
  dragged.
- **Copy offset** — duplicated sketch geometry now lands exactly on the
  original (was offset).
- **Far-zoom disappearance** of distant parts (same root cause as the grid
  drift; clip planes now adapt to orbit distance).
- **Sketch face grid** is centred on the face centroid rather than the origin.
- **Ortho view entry** retains the user's view orientation more sensibly
  (still has a known case noted below).

### Known issues

- **Sketch rotate-angle popup glitches on hover** — the small "Rotation (deg)"
  panel that appears after a rotate-gizmo drag flickers and becomes unclickable
  when the cursor is over it. The 15° drag snap works fine; only the type-in
  refinement is affected. Esc reverts and exits. To be fixed by porting the
  popup to true ImGui popup semantics (`OpenPopup` / `BeginPopup`) in a
  follow-up.
- **Push/pull multi-target preview artifacting on ARM** — visible on aarch64
  builds, not reproducible on x86_64. Deferred until a reliable repro path is
  found.
- **Ortho view entry rotates counter-clockwise** relative to expectation in
  some camera states. Exit is clean. Deferred for a separate pass.

## [0.1.0] — Initial release

Initial Materializr release. Parametric 3D CAD on OCCT with sketches, extrude,
push/pull, fillet, chamfer, transforms, plugin system, project save/load,
ViewCube, settings, Linux AppImage and Windows packaging.
