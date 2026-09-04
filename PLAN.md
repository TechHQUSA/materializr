# Plan: sketch Mirror becomes a derived relation, not a copy
_Locked via claudex-loop. R1 rejected the symmetry-constraint architecture (user chose the
alternative); R2 kept the architecture and found 18 spec gaps; R3 found 15 more, mostly
mutation paths that falsify the invariant. Rewritten whole at R3. R4 reduced it to four
blocking items, R5 to one — the undo-signature blind spot, fixed below. MAX_ROUNDS reached._

## Goal

`SketchTool::commitMirror` (`SketchTool.cpp:3592`) is a one-shot **copy**: it reflects the
selection through a transient gizmo line, adds new entities via a remap table, and returns.
Nothing links copy to original, so a later edit leaves the mirrored side behind — the bug.

Make the mirror a **derived relation**. The sketch records which entities mirror which, about
which axis; the mirrored geometry is **recomputed from the source**. Editing the original
moves the image because the image is not independent data.

## Why derived, not symmetry constraints

`SketchSolver::solve` (`SketchSolver.cpp:195`) is **undamped Gauss-Seidel projection** with a
**hand-counted DOF tally** (`:40-130`). Putting ~40 coupled symmetry rows per mirror into it
bought five problems: an unproven correction formula, oscillation whenever the axis moves, a
"fix the axis" step that is really two `Fixed` points and four equations, a rank tally that
double-counts welded/duplicate pairs, and source constraints that would need mirroring too.
Deriving dissolves all five — the image never enters the solver. Reflection is evaluated once
against the solver's **final** source state (converged or iteration-limited).

Given up, honestly: the mirrored side is not independently editable, and there is no free
manual Symmetric tool. Onshape models its *sketch* mirror with constraints; we match its
*behaviour* with the mechanism our solver can carry. Independence returns via **Break link**.

## Status — what this review is about

Seven commits are on `feature/sketch-mirror-derived`, 81/81 ctest green. SHIPPED: the data
model, deleted copy-assignment plus `restoreFrom` on every restore path, the solver hook and
DOF subtraction, `clear()` dropping groups, `commitMirror` building a real relation,
serialisation across all three writers, and the construction-line face fixes.

VERIFIED IN THE RUNNING APP: mirror a half-profile, drag a source corner, and the image
follows. That was the reported bug.

**Only these three remain, and they are what this review should attack:**
1. Break link / Delete mirror UI **plus the undo-signature extension** — Break link preserves
   every point, line and constraint, so `signature()` (`Application.cpp:5984`) hashes it
   identically and `recordSketchMutation` discards the history step. They must land together.
2. The per-pair deletion cascade in `removeElement`. Today it calls `validateMirrors()`, which
   breaks the WHOLE group when either half of any pair is deleted — safe, coarse, not the plan.
3. Derived points must read as locked in the pick path. Dragging one currently succeeds and is
   silently reverted by the next solve.

Two cosmetic defects found in the app, to land with (1): the on-axis pins render as user-visible
`0.00 mm` dimension labels, and the history caption reads "Add Distance 0.00 mm", not "Mirror".

## Data model

```cpp
struct SketchMirror {
    int id;                                   // own id sequence, not the entity sequence
    int axisLineId;                           // a REAL construction line in this sketch
    std::vector<std::pair<int,int>> points;   // (source, derived)
    std::vector<std::pair<int,int>> lines, circles, arcs, splines;   // element pairs
};
```
Derived entities are **real** — `buildWires()` and everything downstream are unchanged. What
is new is that their values are outputs. Points/lines/circles/arcs/splines gain
`bool derived = false`, following the `isConstruction` / `fromText` precedent (`Sketch.h:17-24`).
`derived` is **never trusted from a file**: it is normalised from validated group membership
on load, on combine, and after Break link.

## The invariant, and every path that must maintain it

`Sketch::recomputeMirrors()` — for each group, for each point pair set the derived point to
`reflect(source.pos, axis)`; copy radius for circles/arcs; copy `isConstruction`; preserve the
arc start/end swap (reflection reverses winding, `SketchTool.cpp:~3625`). It builds
**id→pointer maps once per call** and processes all groups through them; naive pair walking is
quadratic through `getPoint`'s linear scan.

A solver hook alone is insufficient — these paths mutate a sketch without solving, each
verified against the code:

| path | file:line | why it breaks |
|---|---|---|
| undo/redo snapshot restore | `SketchEditOp.cpp:26` | `*m_target = *m_after`, no solve |
| gizmo drag **cancel** | `Application.cpp:3099` | Escape reverts each point via `movePoint`, no solve |
| sketch-pattern preview | `Application_InteractiveOps.cpp:2151` | `*m_activeSketch = *m_sketchPatternBefore` |
| sketch-pattern cancel | `Application_InteractiveOps.cpp:2223` | same whole-sketch assignment |
| project load | `parseSketchBodyImpl` | saved coordinates may be stale/rounded/hand-edited |
| sketch combine | `CombineSketchesOp` | coordinates not current after remap/recovery |
| **combine undo** | `CombineSketchesOp.cpp:82` | assigns `m_targetBefore` directly |
| **transactional history rollback** | `History.cpp:257` | `restoreSnapshot` assigns saved sketches directly |
| **crash-draft recovery** | `Application.cpp:6628` | `restoreSketchDraftNow` assigns `draft` directly |

So: introduce **`Sketch::restoreFrom(const Sketch&)`** — assign, validate groups, normalise
flags, recompute — and replace **every** whole-sketch assignment with it.

**Enforced mechanically, not by a maintained list.** Round 4 found three more restores after
round 3 had "completed" the table, which is the proof that enumeration does not hold. Two
layers: (a) make copy-assignment of a live `Sketch` unavailable — `Sketch& operator=(const
Sketch&) = delete;` with a named `assignRaw()` for the few internal callers that genuinely
need it (snapshot construction), so a bypass is a **compile error**, not a review miss;
(b) a repo check in the `tools/` style of `units_audit.py`, failing on any
`*<sketch-expr> = ` outside `Sketch.cpp` and the snapshot writers. There are 6 such
assignments today, so the deletion is tractable rather than a rewrite. Then call
`recomputeMirrors()` from: both exits of `SketchSolver::solve` (**before**
`refreshReferenceValues()`, or a reference dimension on derived geometry reads one solve
behind), the end of gizmo cancel, the end of `parseSketchBodyImpl`, the end of the combine
merge, and the end of `commitMirror`. The plan states no call-site count — the earlier "eight"
was already stale.

## Deletion algebra

`Sketch::removeElement` (`Sketch.cpp:670`) does no pruning today and erases **points as well
as elements** (`:696`), so it — not `pruneOrphanPoints` — is the integrity point. Make it
group-aware, in two phases to avoid re-entrancy: **plan** the full cascade (ids to erase,
group edits) with no mutation, then **erase raw** without re-entering relationship logic.
Deleting a source element cascades to its derived element through the same public API, so
without this split the traversal mutates the group it is walking.

- **source element** → delete its derived element via the element-pair graph, then remove
  **only the point pairs incident to that element pair**, and only where no surviving mirrored
  element still uses them. Deliberately NOT "any pair no element uses": a mirrored **standalone
  point** is used by no element from the moment it is created, and that rule would delete it
  immediately, or sweep it away during an unrelated cascade. A standalone point pair survives
  until its own source point, or the group, is deleted.
- **source point** → first delete or unlink every element pair incident on it, then remove the
  point pair. Ordinary (non-paired) incident elements follow existing behaviour.
- **axis line** → **Break link**.
- **derived entity, deleted directly** (generic delete, trim, stamp rollback, or a
  programmatic `removeElement(derivedId)`) → **refused** at the centralised path. The user's
  routes to the same outcome are Break link and Delete mirror.
- **group emptied** → removed.

**Break link is one reusable normalisation**: clear `derived` on every owned entity, drop the
group, and validate constraints that referenced the former image (a reference dimension
survives; a malformed driving constraint must not silently activate). Every abandonment path
calls it — axis deletion, a failed combine remap, a malformed file — or those entities stay
non-draggable and keep reducing DOF while belonging to nothing.

**Trim** (`SketchTool.cpp:3277`) is the stress case: it calls `removeElement` repeatedly then
creates replacement geometry. Trimming derived geometry is refused; trimming a source member
must run the cascade above and leave no dangling ids.

## DOF

The tally is `2*numPoints + numCircles + arcsWithout - arcsWithDrivenRadius - numEquations`
(`SketchSolver.cpp:~120`), over **stored values**, and `pointCount()` includes construction
points (`:31`). Subtract **2 per derived point** and **1 per derived circle/arc radius**.

Two traps:
- **The test oracle is not "same DOF as the original".** `commitMirror` materialises a free
  construction axis whose two endpoints add 4 DOF. Expect **original + 4** for a newly created
  free axis, or reuse an existing line as the axis when unchanged DOF is wanted.
- **Arcs interact with the existing branches.** A derived arc normally has no driving Radius,
  so it already contributes `arcsWithout = +1`; subtracting its radius cancels that. A
  malformed derived arc *with* a driving Radius lands in `arcsWithDrivenRadius = -1`, and
  subtracting again puts the tally two too low. Constraint validation therefore runs **before**
  the tally, so malformed rows never reach it.

A single type-aware **`constraintReferencesDerived(c, sketch)`** helper is used identically by
creation, load validation, combine validation and the DOF pass — constraints reference points,
lines and circles/arcs in different slots, so numeric id matching alone risks ID-domain
ambiguity and missed endpoints.

## Constraints on derived geometry: rejected

A driving constraint on a derived entity can move **source** points for up to 50 iterations
before the recompute overwrites only the derived side — corrupting the source, causing false
non-convergence, and invalidating DOF. Rejected at creation, at load, **and at promotion** —
the third is the one that is easy to miss, because it is neither: a reference dimension that
already exists can be promoted to driving two ways, by the **Driving checkbox**
(`Application_Viewport.cpp:3232`) and by **typing a value**, which promotes as a side effect
(`:3206`). Both must refuse when `constraintReferencesDerived()` is true — the checkbox
disabled with a reason, the numeric edit accepted as a measurement without the promotion.
Reference-only dimensions stay permitted, refreshed after recomputation.

## Degenerate axis

The axis is movable and can become zero-length by dragging or a Coincident constraint;
reflection then divides by zero and produces NaNs across the sketch. Coincident axis endpoints
are refused at creation; at runtime and on load a degenerate axis triggers **Break link**
rather than propagating NaN. Tested.

## Serialisation

**Three writers**, not two: `ProjectIO::save` (`:373`), `ProjectIO::writeSketchBody` (`:1543`),
and an independent duplicate in `SketchEditOp.cpp:214` that persists undo/redo snapshots.
Missing the third loses groups from saved history. **Preferred: delete the duplicate and have
`SketchEditOp` delegate to `ProjectIO::writeSketchBody`** — one place for the next schema change.

Grammar, each row self-contained on one line so an unknown-token reader skips it cleanly:
```
MIRROR_COUNT n
M  <id> <axisLineId> <nPointPairs> <nElemPairs>
MP <srcId> <dstId>
ME <kind> <srcId> <dstId>
```
Old builds degrade gracefully — the reader ignores unknown tokens a line at a time
(`ProjectIO.cpp:665`) — proven by a **fixture test**, not by reasoning. Load ends with
validate → normalise flags → `recomputeMirrors()`, so stale or hand-edited coordinates are
corrected rather than trusted.

## Undo: the mutation signature must see mirrors

`recordSketchMutation` computes a `signature()` over **geometry and constraints only**
(`Application.cpp:5980`) and **discards the history step when `afterSig == beforeSig`**
(`:6054`). Break link deliberately preserves every point, line, circle and arc — it only drops
the group and clears `derived` — so its signature is unchanged and **the undo step would be
silently thrown away**. Delete mirror is safe (geometry disappears), which is exactly why this
would have shipped unnoticed: the destructive action is undoable, the reversible one is not.

Extend `signature()` to mix in, for every group: its id, `axisLineId`, the ordered point and
element pairs, and the normalised `derived` ownership. Then Break link, Delete mirror, and
mirror creation each produce a distinct signature and a real history step.

Test: **Break link → undo → redo** restores the relation *and* the locked state, not just the
geometry.

## UI

The Properties panel renders non-dimensional constraints as a muted bullet with no delete
control (`PropertiesPanel.cpp:914`), so a group needs its own affordance: list it, select it,
and offer **Break link** and **Delete mirror**, each one undoable `recordSketchMutation`. A
derived point must read as locked in the pick path (`Application_Viewport.cpp:6035`) — refused
with feedback, not silently reverting on the next solve.

## Key decisions & tradeoffs

- **Recompute after solve, not during** — the image is a function of the final source.
- **Derived entities are real, not virtual** — downstream needs no knowledge of mirrors; the
  cost is real ids kept in step by the recompute.
- **The axis is a real construction line** — it persists, serialises, can be dimensioned, and
  is free to move, which the constraint design could not safely allow.
- **A source point ON the axis is SHARED, not paired** — this reverses the original decision,
  which the build proved wrong. `buildWires` and `buildRegions` join segments by point **id**,
  so a second point at the same coordinate leaves a mirrored half-profile an open chain that
  refuses to extrude. The shared vertex is snapped exactly onto the axis and pinned there with
  a zero `DistancePointLine` — Onshape's coincident-to-mirror-line — because it is the only
  thing holding the two halves together. Every source point OFF the axis is paired as before.
- **Construction geometry never bounds or divides a face.** The axis lives inside the profile
  it mirrors about, which exposed that `buildWires` AND `buildRegions` both fed construction
  lines into face building. Fixed for lines in both; construction arcs and circles still
  divide, which is the same bug class with no observed failure driving it.
- **Break link over per-pair independence** — one explicit action converts a group to plain
  geometry, instead of Onshape's per-constraint deletion.
- **No migration** — old mirrored sketches stay dumb copies; a migration would have to guess.

## Assumptions (all re-verified at R3)

1. Solver is undamped projection/Gauss-Seidel, corrections in place — `SketchSolver.cpp:195-225`.
2. DOF is a hand-written tally over stored values; `pointCount()` includes construction points
   — `SketchSolver.cpp:31, 40-130`.
3. **Three** sketch writers — DONE: all three now call `ProjectIO::writeSketchMirrors`
   (`ProjectIO.cpp:417`, `:1675`, `SketchEditOp.cpp:306`).
4. Reader ignores unknown tokens line by line — `ProjectIO.cpp:665`.
5. Constraint loader range-checks and skips unknown types — `ProjectIO.cpp:745`.
6. `removeElement` prunes nothing and erases points too — `Sketch.cpp:670, 696`.
7. `SketchEditOp::execute/undo` restore snapshots without solving — `SketchEditOp.cpp:26`.
8. Gizmo Escape reverts via `movePoint` without solving — `Application.cpp:3099`.
9. Sketch-pattern preview/cancel assign whole sketches —
   `Application_InteractiveOps.cpp:2151, 2223`.
10. `CombineSketchesOp` remaps only `entityA`/`entityB`, builds its map incrementally, and
    `maxId()` would not see group ids — `CombineSketchesOp.cpp:8, 59`.
11. SUPERSEDED — `commitMirror` was rewritten (5d56b20). It no longer welds via
    `findCoincidentPoint`; `recomputeMirrors` carries `isConstruction` from source to image.
12. An arc is 7 stored values for 5 freedoms — `SketchSolver.cpp:92-98`.

## Risks / open questions

1. **Combine remains the most fragile path** — incremental map, its own id space, recovery via
   Break link. The likeliest place for a dangling id to survive review.
2. **Reference dimensions on derived geometry** are permitted; whether a dimension the user
   cannot promote to driving is confusing is a UX question this plan does not settle.
3. **Recompute cost** is O(pairs) per solve with maps built once — small, but unprofiled on a
   large mirrored sketch under drag.
4. **Deleting `Sketch::operator=` will surface callers** beyond the six counted, including in
   tests and possibly in snapshot construction where raw assignment is legitimate. The
   `assignRaw()` escape hatch keeps those working, but the split has to be got right or the
   compile error becomes noise people route around.

## Tests

- **recompute**: source moves → image follows; axis moves → image follows; **failed solve**
  (iteration limit) still leaves a consistent image
- **mutation paths**, one each: undo/redo; **gizmo drag → Escape**; sketch-pattern preview;
  sketch-pattern cancel; post-load fixture with deliberately wrong derived coordinates;
  post-combine; **combine undo**; **failed transactional replay rollback**; **crash-draft
  recovery**
- **promotion**: Driving checkbox refused on a derived-referencing dimension; numeric edit
  measures without promoting
- **ordering**: a reference dimension on derived geometry reads the current solve
- **DOF**: mirrored sketch = original **+4** with a new free axis; unchanged when reusing an
  existing line; derived circle/arc radii accounted; a malformed derived arc with a driving
  Radius is rejected before the tally
- **rejection**: driving constraint on derived, at creation and at load; derived entity as
  source; derived line as axis; cyclic file on load; **zero-length axis**
- **deletion**: source element; **shared source point**; **standalone mirrored point survives
  an unrelated element cascade**; axis line; direct derived deletion refused; **trim** of a
  source member; re-entrancy (cascade does not corrupt the walked group)
- **Break link**: flags cleared everywhere, DOF returns to normal, geometry survives;
  **undo restores the relation and the locked state** (guards the signature fix)
- **persistence**: all three writers round-trip; **old-reader fixture** loads a new file as
  plain geometry with nothing locked; save → reload → undo → redo
- **construction flags** preserved through mirroring (the pre-existing bug)
- **arcs**: winding swap and radius survive recompute

## Out of scope

Body-level `MirrorOp` staleness — a different defect with a different cause (no dependency
propagation in history; see `~/.claude/plans/mirror-staleness-options.md`). Migrating old
mirrored sketches. A manual Symmetric constraint. Mirroring about a point or a non-line axis.
Nested/chained mirror groups (refused at creation, validated on load).


---

# Revisions after Round 1 of the post-implementation review

Codex reviewed the plan against the SEVEN COMMITS now on the branch, not against a
greenfield. All eight findings verified in the source and accepted; three were checked
independently before acceptance (`pruneOrphanPoints` call sites, the Properties-panel
early return, the duplicate `signature()`). These supersede the sections above.

### P1. `validateMirrors()` must validate TYPED membership
Today it checks every pair id against ONE union of all entity ids (`Sketch.cpp:2244`), so a
malformed `ME line` pair holding POINT ids survives validation and marks those points
derived. The file is not trusted, so this is reachable. Validate each axis and each pair
against its own container, and reject: source==output aliasing, an entity owned by two
groups, a derived source, a derived axis, and cycles — all BEFORE normalising flags.

### P2. `pruneOrphanPoints()` will erase mirrored standalone points
A mirrored standalone point is used by no element BY DESIGN, which is exactly what this
global sweep deletes (`Sketch.cpp:710`). It runs from **8 call sites**, including the
generic delete path (`Application.cpp:6108`). Teach it to keep any point participating in a
surviving mirror point-pair, and run `validateMirrors()` AFTER pruning, not before.

### P3. "Delete mirror" needs a stated deletion algebra
The plan named the command without saying what it deletes: only the derived output, or also
the generated axis and its two endpoints, the on-axis pins, the shared vertices? Specify ONE
model-level `deleteMirror(id)` transaction with explicit ownership rules and constraint
cleanup. Delete the derived entities, the axis and its endpoints when nothing else uses
them, and the pins; never the source. Tests for a shared axis, a shared on-axis vertex, and
undo/redo.

### P4. Locking the picked point is not enough
`SketchTool::onMouseMove()` moves the WHOLE selected point set (`SketchTool.cpp:2068`), so a
drag begun on an ordinary point still drags derived points that happen to be selected, and a
drag begun on a derived LINE moves its endpoints. Refuse drag INITIATION whenever the
effective drag set contains any derived entity or derived endpoint. Selection stays allowed.

### P5. The refusal needs a delivery path
Select mode has no rejection channel; `pickSketchAt()` returns ids only
(`Application_Viewport.cpp:5964`). Add a Select-drag rejection result the viewport consumes
as a toast. Test that a refused drag produces neither movement NOR an undo step.

### P6. The Properties panel returns before it could show a group
`renderSketchConstraintsPanel()` returns early when there are no constraints
(`PropertiesPanel.cpp:788`) — and a mirror group can exist with no user constraints at all,
which is the common case. Render mirror groups in their own section BEFORE that return, or
Break link is unreachable in exactly the sketches that need it.

### P7. The history caption needs mirror awareness, not a relabelled pin
`SketchEditOp::description()` inspects constraint additions before geometry
(`SketchEditOp.cpp:61`), which is why creating a mirror reads "Add Distance 0.00 mm" — the
caption observed in the running app. Break link changes only mirror metadata and would stay
generic. Detect mirror-group add/remove FIRST and emit "Mirror", "Break mirror link" and
"Delete mirror".

### P8. There are TWO signature functions, not one
`SketchPlugin.cpp:235` carries its own `signature()` with the same `afterSig == beforeSig`
dedup as `Application.cpp:5984`. Extending only the latter leaves the identical
dropped-undo-step bug reachable through the plugin. Centralise ONE canonical sketch-state
signature — geometry topology, mirror metadata, ownership flags — and call it from both.


---

# Revisions after Round 2

Five findings, all accepted. Round 2 attacked the Round 1 fixes rather than repeating them,
and P3 turned out to need a DATA MODEL change — which also reopens the serialisation format
already committed in `0f2fe0a`.

### P1b. Validate relational topology, not just id types
Typed containers are necessary and not sufficient. A `lines` pair can name two REAL lines
whose endpoints do not correspond to the group's own point pairs — likewise circle centres,
spline control points, and arcs, where the derived arc's start/end are deliberately SWAPPED.
Such a group locks geometry that is not a reflection of anything. Validate each element
pair's COMPLETE point topology against the group's point mapping, honouring the arc swap.

### P2b. Validate, prune, validate
The ordering in P2 was unsafe in both directions. Protecting points using groups that have
not been validated lets a malformed group preserve arbitrary orphans; pruning first and
validating after strands the points of a group that gets dropped. The sequence is:
`validateMirrors()` → prune while protecting pairs of SURVIVING groups → `validateMirrors()`
again.

### P3b. `SketchMirror` must record what it OWNS
`deleteMirror()` cannot be written against the current struct: it stores `axisLineId` but not
whether the mirror CREATED that line, and it does not store the ids of the pin constraints it
added. So it cannot tell a generated axis from a line the user drew and reused, nor its own
pin from an identical `DistancePointLine` the user placed. Extend the model:

```cpp
struct SketchMirror {
    ...
    bool axisGenerated = false;        // commitMirror made the axis; safe to delete with it
    std::vector<int> pinConstraints;   // ids of the zero DistancePointLine rows it added
};
```
Anything NOT recorded as owned is retained by Delete mirror. Absent metadata therefore
degrades to "keep it", never to deleting a user's geometry.

**Serialisation follows** — the `M` record gains trailing fields and a new `MC` row, both
appended so an older reader still skips them cleanly and a file written before this change
loads with `axisGenerated=false` and no pins, i.e. the conservative retain-everything case:
```
M  <id> <axisLineId> <nPointPairs> <nElemPairs> <axisGenerated> <nPinConstraints>
MC <constraintId>
```

### P3c. "Nothing else uses it" needs a typed definition
Axis and endpoint cleanup cannot ask "is this id referenced anywhere" — that is the same
untyped-id ambiguity P1 exists to remove. Centralise ONE typed inbound-reference enumeration
covering: other mirror groups, line endpoints, circle centres, arc centre/start/end, spline
control points, polygons, and every constraint kind's `entityA`/`entityB`. Use it for axis
and endpoint cleanup, and nowhere else improvise.

### P7b. Break link and Delete mirror both remove a group
Detecting "a group vanished" cannot distinguish them, so both would caption identically.
Classify on the geometry: a removal whose owned entities SURVIVE is "Break mirror link"; a
removal whose derived outputs are also gone is "Delete mirror". Tests for both captions, and
for the undo step existing at all in each case.


---

# Revisions after Round 3

Four findings, all accepted. The first is a defect in my own Round 2 fix.

### P1c. Topology validation must know about shared vertices, so RECORD them
P1b as written would reject valid groups. A shared on-axis vertex deliberately has NO
point-pair entry, yet the mirrored line that uses it names the SAME point id on both sides —
which strict topology validation reads as a corrupt identity mapping. Deriving the shared set
from the pin constraints is not enough either: a file written before pins existed has none.
Record them explicitly:

```cpp
struct SketchMirror {
    ...
    std::vector<int> sharedPoints;   // on-axis vertices: their own image, never derived
};
```
Topology validation then permits an identity mapping ONLY for an id in `sharedPoints`, and
nowhere else. `sharedPoints` never sets the `derived` flag — a shared vertex is nobody's
image, which is what keeps it draggable and DOF-bearing.

### P4a. Ownership metadata arrives from an untrusted file, so validate it
`axisGenerated`, `pinConstraints` and `sharedPoints` are exactly the fields that authorise
DELETION, which makes them the most dangerous thing in the record. A malformed group could
claim a user's constraint as a pin, claim the same pin in two groups, or mark an arbitrary
line `axisGenerated` — and Delete mirror would then destroy user data. Before ANY ownership
claim is accepted: the constraint must exist, be `DistancePointLine`, have value 0, name this
group's axis as `entityB`, name one of this group's `sharedPoints` as `entityA`, and be
claimed by no other group; the axis must be a construction line. A claim that fails any test
is CLEARED, not honoured — the group survives owning nothing, so cleanup retains rather than
deletes.

### P5a. Every teardown route needs a stated policy, not just Delete mirror
Four routes end a group and P3b only covered one. The rule, stated once:

| route | owned pins | owned axis + endpoints | derived outputs |
|---|---|---|---|
| **Delete mirror** (explicit) | deleted | deleted if `axisGenerated` and unreferenced | deleted |
| **Break link** (explicit) | deleted | RETAINED | RETAINED, flags cleared |
| **empty-group cascade** | deleted | RETAINED | n/a — already gone |
| **validation failure** | RETAINED | RETAINED | RETAINED, flags cleared |

The pins go with the relation on every route it ends deliberately, because they exist only to
serve it — leaving one behind is a driving constraint the user never created, pinning a point
to a line that no longer means anything. Validation failure retains everything, because a
group that failed validation is exactly the group whose ownership claims cannot be trusted.
Only explicit Delete mirror ever removes geometry.

### P6a. Serialisation compatibility needs fixtures in both directions
The grammar changed twice now; assertions must replace assumption. Fixtures: an old-format
file into the new reader (absent trailing fields must initialise to `axisGenerated=false`,
no pins, no shared points — the retain-everything case); a new-format file into the old
reader (it must skip `MC` rows and resume correctly on the next record); and malformed input
— an `nPinConstraints` count that disagrees with the `MC` rows present, and a truncated `MC`
sequence at end of file. The reader consumes EXACTLY `nPinConstraints` rows and treats any
disagreement as a cleared ownership claim.


---

# Revisions after Round 4

Six findings, all accepted. Two are defects introduced by my own Round 3 fix, and one is a
migration break against files the SHIPPED code already writes.

### P7a. `sharedPoints` needs a grammar — I added the field and not the format
Full record set, all three writers and the reader:
```
M  <id> <axisLineId> <nPointPairs> <nElemPairs> <axisGenerated> <nPins> <nShared>
MP <srcId> <dstId>
ME <kind> <srcId> <dstId>
MC <constraintId>
MS <pointId>
```

### P8a. Legacy migration — the shipped format ALREADY has shared vertices
`5d56b20` creates shared on-axis vertices and `0f2fe0a` persists groups without any
`sharedPoints` field. Defaulting the new field to empty makes P1c reject those groups as
corrupt identity mappings — so this plan would break files the current build has already
saved. On load, when a group has no `MS` rows, INFER the set: every id that appears as an
identity mapping in the group's element topology, that exists, and that lies on the axis
within tolerance. Persist the inferred set on the next save. An id that fails either check is
not inferred, and the group fails validation as it should.

### P8b. Validate `sharedPoints` too
It authorises the identity mappings P1c otherwise forbids, so an unvalidated list is a way to
smuggle a corrupt group past validation. Every entry must: exist; actually occur as an
identity mapping in this group's topology; lie on the axis within tolerance; and never appear
as a derived output in any group. Otherwise discard the claim and let the group fail.

### P8c. `isConstruction` does not establish provenance
A file can mark a user's own construction line `axisGenerated` and have Delete mirror destroy
it. Require the full generated-axis contract before honouring the claim: the axis is a
construction line; both endpoints are construction points; those endpoints are referenced by
no other element; and no constraint references them except this group's own pins. If any part
fails, CLEAR `axisGenerated` — the axis is then retained forever, which is the safe direction.

### P8d. Counted child records can desynchronise the parser
"Consume exactly `nPins` rows" is a parser bug on malformed input: an inflated count eats the
next legitimate sketch record as a failed `MC`, and that record is then never parsed at all —
silent geometry loss from a corrupt file. Parse mirror child records by TOKEN instead: read
while the next line's token is one of `MP`/`ME`/`MC`/`MS`, and push back the first line that
is not. The counts become a cross-check that clears ownership claims on disagreement, never
the thing that drives the loop.

### P8e. Compatibility fixtures cover recovery, not just truncation
Add: a group with shared vertices and no `MS` rows (the migration path, P8a); `nPins` higher
than the `MC` rows present, FOLLOWED BY valid geometry that must still load; duplicate `MC`
and duplicate `MS` rows; and ownership claims naming ids that do not exist. Each asserts the
same two things — the sketch's geometry survives intact, and the ownership claim is cleared.


---

# Revisions after Round 5 (final — NOT re-reviewed, see the log)

Two findings, both accepted. The round budget ended here, so these two are the only changes
in this plan that no adversarial pass has seen.

### P9a. Legacy files need pin ownership inferred too, not just shared points
P8a migrates `sharedPoints` and stops. But the seven shipped commits ALSO created zero
`DistancePointLine` pins with no ownership record, so on a legacy file Break link would
retain the pin: the formerly shared vertex stays constrained to the axis and Break link fails
its one job, which is to hand back ordinary independent geometry. For each inferred shared
point, claim exactly the one constraint that is `DistancePointLine`, value 0, `entityA` that
point, `entityB` this group's axis. Exactly one match is claimed; zero or several is
ambiguity, and ambiguity RETAINS — the pin stays and the group owns nothing.
Test: legacy load → Break link → drag the formerly shared vertex off the axis.

### P9b. A missing pair is a broken RELATION, not a bad ownership claim
P8d over-generalised. `nPins`/`nShared` describe deletable infrastructure, so a disagreement
there can safely clear the ownership claim and continue. `nPointPairs`/`nElemPairs` describe
the relation itself: accepting fewer `MP`/`ME` rows than declared preserves a TRUNCATED
mirror — geometry silently un-owned, un-recomputed, and still on screen looking correct.
Split the policy:

**Superseded by R6 below** — `nShared` is not ownership metadata, and "break the group"
must not mean calling `breakMirrorLink()`.


---

# Revisions after Round 6

Round 6 was authorised specifically to review P9a and P9b, the two revisions no earlier pass
had seen. **P9a was cleared** — constraints load before mirrors, so the one-typed-match
migration rule is implementable as written. P9b had two problems, both accepted.

### P9c. `nShared` is NOT ownership metadata
I filed `sharedPoints` alongside `pinConstraints` as deletable infrastructure. It is not:
P1c REQUIRES it to validate identity mappings, so clearing it and keeping the group makes
that same group fail validation for the mappings the cleared field existed to authorise.
An `MS` count mismatch is a relation problem. Reconstruct the set with P8a's geometric
inference and validate it; if that fails, the group goes down the validation-failure path.

### P9d. "Break the group" must not call `breakMirrorLink()`
P9b said a bad `MP`/`ME` count should "release its geometry via Break link" — which, by P5a's
own table, DELETES the claimed pins. That is exactly backwards for malformed input: a
truncated record would talk the loader into deleting constraints, and P5a already says a
group whose data cannot be trusted retains everything. Malformed records take the
**validation-failure** path, not the explicit-teardown path.

### The corrected count-mismatch policy
| mismatch | path | effect |
|---|---|---|
| `nPointPairs` / `nElemPairs` vs `MP`/`ME` | validation failure | group dropped; pins, axis and ALL geometry retained; `derived` flags cleared |
| `nShared` vs `MS` | **clear, do NOT re-infer — see P9e** | no legitimate identity mappings remain, so topology validation drops the group; geometry retained |
| `nPins` vs `MC` | clear ownership claim | group survives owning no pins; nothing deleted |

Only the last row is an ownership-only failure, because `pinConstraints` is the only one of
the three fields that exists purely to authorise deletion. No malformed file, on any row of
this table, causes anything to be deleted.


---

# Revisions after implementation — P9e (supersedes P9c)

P9c said an `MS` count mismatch should reconstruct the shared set by inference and
validate it. **That is unsafe — and it was wrong in a plan that had already been
through seven adversarial rounds.** Shown by test, not by argument:

A file with its `MS` rows dropped still carries correctly-counted `MC` rows, so the
pins load straight from the record. Re-inferring `sharedPoints` then re-validates
exactly those pins, and `axisGenerated` survives with them. A record that misstated
its shared rows walked away with FULL deletion rights. The reasoning that made this
look safe — "sharedPoints is not ownership metadata, and the pins are gated anyway"
— missed that those pins never went through inference at all.

**The rule, for both fields:** a record that DECLARED its ownership columns never
re-infers anything. Inference exists solely to migrate records written before the
columns existed. `ownershipDeclared` carries that distinction from the reader,
survives a cleared claim, and is set by `commitMirror` too, so a group built in
memory states its ownership rather than having it guessed back.

A declared record with bad `MS` rows therefore has an empty shared set, its identity
mappings are unauthorised, and topology validation drops the group and releases the
geometry untouched. Correct outcome, no special case.

**Process note.** P9c was reviewed and approved. Approval is not proof — the defect
surfaced the moment the scenario was written as a test rather than reasoned about.
