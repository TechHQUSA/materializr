# Materializr — YouTube Tutorial Outline

A loose segment-by-segment guide for a first tutorial video. Not a script —
each block is "what to show" + "what to say." Aim ~10–15 min. Record at a calm
pace; the inference/face-editing bits are the moments to slow down on.

---

## 0. Cold open (15–30 sec)
- Show a finished-ish part rotating in the viewport (something with a few holes,
  a chamfer, maybe text engraved).
- One line: *"This is Materializr — a free, open 3D CAD that aims for the middle
  ground: easier than the big parametric suites, more capable than the toy
  modelers."* Mention it's open source.

## 1. The lay of the land (1 min)
- Quick tour of the window: **viewport** (center), **Tools** panel (left),
  **Items** panel (right — bodies / sketches / construction), **History** at the
  bottom-right, **ViewCube** top-right.
- Navigation: right-drag orbit, middle-drag pan, scroll zoom, **Home** to reset.
- Don't dwell — just orient the viewer.

## 2. Sketching + the inference engine (2–3 min) — *a differentiator, slow down*
- Start a sketch on the ground plane. Draw a rectangle, a circle, a line.
- **Show the inferences**: hover to "charge" references, watch the guide lines
  (horizontal/vertical, parallel, perpendicular, tangent, on-line). Point out the
  dimension readout and angle snap.
- Mention the draw modes: rectangle center-vs-corner, circle center-vs-2-point,
  and the **polygon popout** (triangle…octagon).
- Close the loop; note it auto-closes / fills a region.

## 3. Sketch → solid (1–2 min)
- Select the region, **Extrude** (or Push/Pull on the sketch). Show + extrude and
  − cut.
- Quick detour: **Revolve** a profile around an axis to make something round.

## 4. Editing a solid (2 min)
- **Push/Pull** a face along its normal (extrude/cut).
- **Fillet** and **Chamfer** edges — show single + multi-edge, and the
  two-distance (asymmetric) chamfer.
- Mention these are parametric — you can re-open and edit values from history.

## 5. ⭐ Face editing — the headline (3–4 min) — *the star of 0.9.7, take your time*
This is the new unified mechanic. Lead with the **fitment story**: "a hole's in
the wrong spot, or a face needs to shift / tilt / resize — one set of buttons."

- Select a face. The **Transform** buttons become face tools:
  - **Move** — drag the arrows to slide the face in its plane; the body follows.
  - **Rotate** — two-ring gizmo, **sweep** a ring to tilt the face about its
    centre. Stack tilts on both rings. (Snaps to 1°.)
  - **Scale** — cube handles, grow/shrink about the centre; toggle **Uniform**
    off for per-axis (red/green) scaling.
- **Holes, the cool part**: on a face with a hole, show the three behaviors —
  - select nothing on the hole → it **stays put** while the face moves around it,
  - select the hole's **top edge** → it **slants**,
  - select the hole's **wall** → it stays a **vertical tube**.
- Emphasize: live ghost preview while dragging, rebuilds on release, fully
  undoable, and you can **chain** ops without re-clicking.

## 6. Patterns + duplication (1 min)
- **Linear / radial pattern** of a feature or body. Radial around an axis is a
  nice quick win to show.

## 7. The "wow" extras (2–3 min) — pick a couple, don't do all
- **Threads** — turn a cylinder into a real threaded rod/hole.
- **Text + SVG** — emboss/engrave text or an imported SVG logo onto a face
  (including curved faces via Project Sketch).
- **Shell** — hollow a body to a wall thickness.
- **Loft** — blend between two profiles.
- **Section View** — slice the model to see inside.
- **Construction planes / axes** — sketch on angled/derived planes.

## 8. Saving + exporting (1 min)
- Save the project (note autosave + draft recovery exist).
- **Export**: STEP (for other CAD), STL (for 3D printing), and others. Mention
  this is the bridge to a slicer / printer.

## 9. Outro (30 sec)
- Recap the middle-ground pitch. Be honest: *"it's pre-1.0, there are bugs we
  haven't found — report them."* Link the GitHub repo + releases.
- Ask for feedback on what to build next.

---

### Recording tips
- Turn on **angle/grid snap** before sketching so dimensions land clean on camera.
- For the face-editing segment, pre-build a simple box-with-a-hole so you can jump
  straight to the demo instead of modeling it live.
- The inference guides and the face gizmos are the two "watch this" moments —
  zoom the viewport in a bit so the guide lines / rings read clearly.
- Keep a body selected before hitting a Transform button, or the button does the
  body transform instead of the face op (good thing to mention out loud).
