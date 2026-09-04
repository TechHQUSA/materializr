# Plan Review Log: sketch Mirror emits live symmetry constraints

Phase 0 (recon) complete; Phase 1 (interrogation) DEFERRED at the user's direction — the plan
is drafted from recon and goes to Codex first, so its findings shape the interrogation and the
research instead of the other way round. MAX_ROUNDS=5.
Reviewer: gpt-5.6-sol (from ~/.codex/config.toml), codex-cli 0.144.1, read-only every round.

## Round 1 — Codex (thread 01a0692f-7636-71b2-b3e9-025deb41bd7e): VERDICT: REVISE

The plan is not ready to implement. The parser risk is resolved, but the proposed data model and solver behavior have several material holes.

- **Risk 3 is answered:** the current K-row loader range-checks `tval` and skips unknown constraint types at [ProjectIO.cpp:740](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/io/ProjectIO.cpp:740). An older build whose maximum is `CircleGap` will drop `Symmetric` rows while retaining the copied geometry; it will not mangle subsequent rows. **Fix:** replace Risk 3 with this verified behavior and add a compatibility fixture proving it.

- **Storing the axis ID in `value` breaks constraint remapping.** `CombineSketchesOp` remaps only `entityA` and `entityB`, so merged symmetric constraints will keep an axis ID from the source sketch and may accidentally bind to an unrelated entity in the destination ([CombineSketchesOp.cpp:59](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/CombineSketchesOp.cpp:59)). **Fix:** add an explicit `entityC` field and update serialization/remapping, or centralize typed reference enumeration before using `value`.

- **Deleting the axis will not prune the constraints.** `pruneOrphanPoints()` validates only `entityA` and `entityB`; an axis hidden in `value` remains dangling and the solver will silently report the constraint satisfied when lookup fails ([Sketch.cpp:725](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/Sketch.cpp:725)). **Fix:** represent the axis as `entityC` and include it in centralized deletion validation.

- **A point on the axis is not “already constrained.”** If the source and mirrored point are welded and no constraint is emitted, that point may later move off the axis, after which it is no longer self-symmetric. The self-symmetry condition has one independent equation—point-on-line—not zero. **Fix:** emit a point-on-line constraint for welded self-pairs, adding that constraint type if necessary.

- **The proposed correction is underspecified and the “half the error” description is mathematically wrong.** `computeError` returns only a scalar norm, which contains no correction direction; the midpoint construction can project exactly for a fixed axis, but it is not “moving by half the error” and must be formulated directly from the two-dimensional residual. **Fix:** specify and test the exact vector projection \(q=(R(A)+B)/2,\ A'=R(q),\ B'=q\), including zero-length-axis handling.

- **There is no convergence argument for a movable axis.** Alternating exact projections against Horizontal, Vertical, Coincident, Fixed, and multiple shared symmetry pairs can oscillate because the solver applies undamped corrections in order ([SketchSolver.cpp:194](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchSolver.cpp:194)). Merely listing this as a risk is insufficient for a foundational constraint. **Fix:** either fix the axis by construction and test adversarial constraint orderings, or add damping/residual-vector tests with explicit non-convergence handling.

- **“Make the line Fixed” is not one operation.** `Fixed` targets a point and consumes two equations ([SketchSolver.cpp:59](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchSolver.cpp:59)); fixing a line requires two Fixed constraints, four equations, and stored coordinates for both endpoints. **Fix:** explicitly create and fix both axis endpoints, or introduce a fixed-entity representation.

- **Risk 2’s DOF premise is incorrect.** A generic symmetry relation contributes two equations even when the line moves; line motion adds variables but does not reduce the relation’s rank. Rank drops in degenerate cases such as `A == B`, a zero-length axis, duplicate pairs, or relations already implied by other constraints. **Fix:** document generic rank as two, special-case self-pairs as one, reject degenerate axes, and stop presenting the literal tally as reliable redundancy detection.

- **The DOF tally will falsely report over-constraint for shared/welded and duplicate relationships.** `commitMirror` can weld to any coincident existing point, not only a source point on the axis ([SketchTool.cpp:3600](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchTool.cpp:3600)); blindly subtracting two for every pair double-counts dependent equations. **Fix:** deduplicate normalized `(A,B,axis)` triples and either compute Jacobian rank or label the current DOF value explicitly approximate.

- **Arc mirroring is incomplete.** Symmetric center and endpoints do not constrain the separately stored `SketchArc::radius`; the code explicitly acknowledges that arc points and stored radius can become inconsistent ([Sketch.cpp:226](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/Sketch.cpp:226)). The plan adds `Equal` only for circles. **Fix:** add Equal-radius constraints for mirrored arcs too, and test endpoint/radius coherence after solving.

- **Existing constraints on source geometry are not mirrored.** Horizontal/Vertical may happen to survive reflection, but dimensions, tangent relations, equalities, and topology involving selected entities will not be cloned or transformed; the result can immediately deform away from a true mirrored feature except at paired points. **Fix:** define which source constraints are copied/transformed, or explicitly scope the feature to geometry-only symmetry and test every supported entity.

- **Construction flags are lost on copied entities.** Current `addLine`, `addCircle`, `addArc`, and `addSpline` calls create default non-construction copies, regardless of the source flag ([SketchTool.cpp:3609](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchTool.cpp:3609)). **Fix:** preserve `isConstruction` for every mirrored source entity.

- **The claimed UI deletability is not implemented by the stated plan.** The Properties panel only renders a muted bullet for non-dimensional constraints and has no delete control ([PropertiesPanel.cpp:914](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/ui/PropertiesPanel.cpp:914)); viewport picking/rendering also requires explicit handling for a new type. **Fix:** name, render, select, and delete Symmetric constraints explicitly, with one undoable mutation.

- **Performance is worse than “40 more evaluations.”** Each symmetry evaluation would scan lines to resolve the axis, while `getPoint` itself is linear; multiplied by constraints and solver iterations this trends toward quadratic work during dragging. **Fix:** add direct indexed entity lookup or cache the resolved axis per sweep, then benchmark a large mirrored sketch interactively.

- **The tests proposed are far too narrow.** A serialization round trip will not catch oscillation, self-pairs, deleted axes, merged sketches, welded existing points, arcs, splines, construction flags, duplicate constraints, undo/redo, or old-reader behavior. **Fix:** require focused tests for each of those cases plus iteration-count/performance bounds.

- **A simpler architecture was dismissed too quickly.** A first-class mirror relation/group can recompute copied entities directly from source geometry and an axis without forcing a weak projection solver to emulate parametric dependency or corrupting DOF counts. **Fix:** compare a derived mirror-group implementation against Symmetric constraints before committing to dozens of coupled solver rows.

Several cited locations are also stale: `computeError` begins at line 249, not around 286; the solve loop citation at 195 and `applyCorrection` at 574 are accurate; serialization at 397 is accurate, but the relevant parser is at 740–782.

VERDICT: REVISE
### Claude's response (round 1)

**Verified every load-bearing claim before acting on it** — Codex was right on all four I
checked: the K-row loader range-checks and skips unknown types (`ProjectIO.cpp:745`),
`CombineSketchesOp` remaps only entityA/entityB (`:59`), `pruneOrphanPoints` validates only
those two (`Sketch.cpp:723`), and `Fixed` is a POINT constraint costing 2 equations
(`SketchSolver.cpp:59`). Two of my line cites were stale, as it said.

**Three things I had outright wrong**, all in the direction of making the constraint design
look easier than it was: a point on the axis is NOT already-constrained (it needs
point-on-line, one equation, not zero); a movable axis does not reduce the equation count
(symmetry is rank 2 regardless); and "make the axis Fixed" is two `Fixed` points and four
equations, not one operation.

**Escalated finding 16 to the user rather than absorbing it.** Codex argued a derived mirror
relation beats symmetry constraints in this specific solver. That is an architecture fork, and
the user had chosen the constraint route partly on my recommendation that it matched Onshape —
so it was theirs to re-decide with the new evidence, not mine to quietly reverse. The user
chose the derived relation.

**PLAN.md rewritten, not patched.** The derived architecture dissolves findings 4, 5, 6, 7, 8,
9, 11 and most of 14 — no correction formula, no convergence question, no rank analysis, no
mirroring of source constraints, and O(pairs) per solve instead of per iteration.

Carried into the rewrite as work items rather than dissolved: finding 2/3 (referential
integrity — now group-level, centralised in `pruneOrphanPoints`, and `CombineSketchesOp` must
remap the new container), 10 (arc radius — now a plain copy), 12 (construction flags lost by
today's `commitMirror` — a pre-existing bug the rewrite fixes), 13 (UI needs its own
affordance: Break link / Delete mirror), 15 (test breadth).

Rejected: nothing outright. Finding 1's request to "add a compatibility fixture" is folded
into the test list rather than kept as a separate item.

## Round 2 — Codex: VERDICT: REVISE

The derived architecture removes the projection-convergence and symmetry-rank problems, but several correctness issues remain.

- **There are three sketch writers, not two.** Besides top-level `ProjectIO::save` and `ProjectIO::writeSketchBody`, `SketchEditOp.cpp` has an independent snapshot writer at [SketchEditOp.cpp:214](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchEditOp.cpp:214). Missing it loses mirror groups from persisted undo/redo steps. **Fix:** update all three writers—or delete the duplicate writer and delegate to `ProjectIO::writeSketchBody`.

- **The cited second serializer location is wrong.** `ProjectIO.cpp:1583` writes splines; the second ProjectIO sketch writer begins at [ProjectIO.cpp:1543](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/io/ProjectIO.cpp:1543). **Fix:** correct the plan and enumerate exact reader/writer entry points in tests.

- **The shared on-axis point rule is still incorrect.** An ordinary welded point can later move off the axis while remaining shared by source and image topology; the resulting entities are no longer reflections. This is the previous self-symmetry flaw relocated outside the solver. **Fix:** always create a distinct paired derived point, even when initially coincident, unless an explicit point-on-axis relation permanently owns the shared point.

- **A constraint on derived geometry cannot remain unresolved.** During `solve`, such a constraint can move derived points and ordinary source points for up to 50 iterations before recomputation overwrites only the derived side; it can corrupt the source, force false non-convergence, and invalidate DOF. **Fix:** reject every driving constraint referencing a derived point/entity at creation and load validation; optionally permit reference-only dimensions but refresh them after recomputation.

- **Post-solve recomputation currently occurs too late for reference dimensions.** `refreshReferenceValues()` runs before either return ([SketchSolver.cpp:218](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchSolver.cpp:218)); recomputing afterward leaves measurements attached to derived geometry one solve behind. **Fix:** recompute mirrors before refreshing reference values at both exits.

- **“After the solver has converged” is false at one hook.** The iteration-limit exit is explicitly non-converged, yet the image will be derived from partially corrected source geometry. That may be acceptable, but the plan must not promise convergence. **Fix:** define recomputation as reflecting the solver’s final state regardless of success and add a failed-solve test.

- **The hook does not cover all sketch mutation paths.** It covers current production calls to `solve`, but `SketchEditOp::execute()` and `undo()` copy snapshots without solving ([SketchEditOp.cpp:26](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchEditOp.cpp:26)), and direct geometry mutators can also run without a solve. **Fix:** make mirror consistency a `Sketch` invariant invoked after snapshot restoration and relevant source/axis mutations, not merely a solver side effect.

- **The “eight solve call sites” claim is stale.** Current production code has ten: five in `Application_Viewport.cpp`, three in `Application.cpp`, one in `SketchTool.cpp`, one in `PropertiesPanel.cpp`, and one in `SketchEditOp.cpp`. **Fix:** remove the fragile count and state that the solver hook covers all calls through `SketchSolver::solve`.

- **DOF subtraction is underspecified for curves.** A derived circle contributes a derived center’s two coordinates plus its independent radius; an unconstrained arc is currently counted as seven mutable values due to three points plus stored radius ([SketchSolver.cpp:94](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchSolver.cpp:94)). “Derived circles/arcs likewise” is not implementable precision. **Fix:** subtract two per derived point and one per derived circle/arc radius, with tests for partially shared curve points and no constraints allowed on derived curves.

- **Persisted `derived` flags can disagree with the group container.** Old readers discard groups but may still read newly appended flags, malformed files may claim ownership without a group, and Break link must clear flags across every owned element and point. **Fix:** either derive ownership exclusively from validated groups or normalize all flags from group membership immediately after loading.

- **Referential integrity is not central merely because it is placed in `pruneOrphanPoints`.** `removeElement()` itself performs no pruning ([Sketch.cpp:670](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/Sketch.cpp:670)); correctness depends on every caller remembering a later cleanup call. **Fix:** route deletion through a single group-aware removal API or make `removeElement` perform mirror cleanup transactionally.

- **Axis deletion followed by Break link must also define constraints on the former image.** If reference constraints are allowed on derived entities, they should survive; if malformed driving constraints exist, clearing `derived` suddenly activates them. **Fix:** validate or explicitly transform/delete associated constraints during Break link.

- **Source deletion is ambiguous for shared source points.** Deleting one source entity does not necessarily delete its points because another source entity may use them; pair ownership by both point and element can therefore leave derived elements partially alive. **Fix:** define cleanup from the element-pair graph first, then remove point pairs only when no surviving mirrored element uses them.

- **`CombineSketchesOp` needs more than ID remapping.** Its `maxId()` includes constraints but would not include mirror-group IDs, and its source-to-destination map is built incrementally ([CombineSketchesOp.cpp:8](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/CombineSketchesOp.cpp:8)). **Fix:** allocate unique group IDs separately, remap every axis/source/derived reference only after the complete entity map exists, validate the result, and normalize ownership flags.

- **Dropping a malformed group during combine cannot simply “leave plain geometry” without clearing all flags.** Otherwise those entities remain non-draggable and continue reducing DOF despite having no relation. **Fix:** implement Break link as one reusable normalization operation and invoke it for failed group remaps.

- **Nested-group refusal is not fully specified.** Rejecting the axis itself as a selected source does not prevent choosing a derived line as the axis, using a derived entity as a source, loading a cyclic file, or sharing outputs between groups. **Fix:** reject any derived source or derived axis at creation and run cycle/ownership validation on load and combine.

- **Undo snapshots copy the new vector automatically, but persisted history does not until the third writer is fixed.** In-memory `*m_target = *m_before/after` will preserve a value member, but load-time rehydration uses the duplicated `SketchEditOp` serializer. **Fix:** add separate in-memory undo/redo and save–reload–undo tests; do not call snapshotting “verified” based only on default copy semantics.

- **Old-build degradation needs a fixture, not just token-chain reasoning.** Unknown `MIRROR_COUNT` and member rows are ignored one line at a time ([ProjectIO.cpp:665](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/io/ProjectIO.cpp:665)), which is safe only if each pair is self-contained on its own line and derived flags do not lock geometry in old builds. **Fix:** specify the exact line grammar and test loading a new file with a reader lacking mirror support.

- **Recompute cost is understated for element property copying.** Each point and entity pair currently implies linear scans, producing quadratic behavior across large groups. **Fix:** build ID-to-pointer maps once per recomputation and process all groups through them.

VERDICT: REVISE
### Claude's response (round 2)

Architecture survived — nothing in round 2 argued against deriving; all 18 findings were
precision gaps in my spec. Verified the load-bearing ones against the code before acting:
the third writer exists (`SketchEditOp.cpp:214`), the second ProjectIO writer is at `:1543`
(my `:1583` cite pointed at the spline line inside it), `removeElement` (`Sketch.cpp:670`)
does no pruning, an arc is 7 values for 5 freedoms (`SketchSolver.cpp:92-98`), and —
the one that broke my design — `SketchEditOp::execute()` restores a snapshot with
`*m_target = *m_after` and **never solves** (`:26`), so a solver-only hook would have shown a
stale image after every undo.

Accepted all 18. The structural changes: recompute became a **Sketch invariant** (solver both
exits, before refreshReferenceValues; plus after snapshot restore; plus commitMirror) rather
than a solver hook; the shared-on-axis point special case is **deleted** (round 1's
self-symmetry flaw, relocated — a point on the axis just reflects to itself); driving
constraints on derived geometry are **rejected at creation and load** rather than left
unresolved; deletion routes through one group-aware path with element-pair-first cleanup;
Break link became one reusable normalisation every abandonment path calls; three writers, with
a recommendation to delete the duplicate; flags normalised from groups on load; a line grammar
that survives an unknown-token reader; and a Tests section covering each case rather than a
round-trip.

Rejected: nothing. Two findings were downgraded to work items rather than open questions —
recompute cost (build id→pointer maps once per recomputation) and cross-group sharing (refused
at creation, validated on load).

## Round 3 — Codex: VERDICT: REVISE

The architecture is substantially better, but the invariant is still incomplete and the DOF test expectation is wrong.

- **The plan contradicts itself on welded points.** Approach 4 correctly requires distinct paired points, but “Key decisions” still says “Welded points are shared, not paired.” **Fix:** remove the stale key decision and update the stale assumptions section before implementation.

- **The invariant misses gizmo cancellation.** Gizmo dragging solves continuously, so images follow the dragged source; Escape then restores source points with direct `movePoint()` calls and does not solve or recompute ([Application.cpp:3099](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application.cpp:3099)). The image therefore remains at the cancelled drag position. **Fix:** call `recomputeMirrors()` after gizmo rollback and add a drag→Escape regression test.

- **Snapshot restoration exists outside `SketchEditOp`.** Sketch-pattern preview and cancellation assign whole sketches directly at [Application_InteractiveOps.cpp:2151](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application_InteractiveOps.cpp:2151) and [Application_InteractiveOps.cpp:2223](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application_InteractiveOps.cpp:2223). These snapshots may currently be coherent, but the claimed invariant still depends on that accidental property. **Fix:** introduce one `Sketch::restoreFrom()` operation that assigns, validates, and recomputes, and replace every whole-sketch assignment with it.

- **Loading does not explicitly re-establish geometric consistency.** The plan normalizes flags and validates groups on load, but saved pair coordinates may be stale, rounded, or hand-edited. No stated step recomputes them after parsing. **Fix:** validate, normalize, and recompute once at the end of `parseSketchBodyImpl`, and test a fixture containing deliberately wrong derived coordinates.

- **Combine likewise needs an explicit final recompute.** Complete ID remapping and flag normalization do not guarantee coordinates are current after malformed-group recovery or source transformations. **Fix:** call group validation and `recomputeMirrors()` after the merge is fully assembled.

- **A zero-length axis remains unhandled.** The axis is movable and may become degenerate through dragging or a Coincident constraint; reflection then divides by zero or produces NaNs. The Tests section contains no degenerate-axis case. **Fix:** reject coincident axis endpoints at creation and define load/runtime behavior—prefer Break link or retain the last valid result—then test it.

- **The DOF arithmetic is mostly correct, but the stated test oracle is not.** Subtracting two per derived point and one per derived circle/arc radius matches the existing storage-based tally. However, `commitMirror` also creates a free construction axis whose two endpoints add four DOF because construction points are included in `pointCount()` ([SketchSolver.cpp:31](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchSolver.cpp:31)). A mirrored sketch therefore does **not** generally have the same DOF/state as the original. **Fix:** expect `original DOF + 4` for a newly materialized free axis, or reuse an existing axis if unchanged DOF is required.

- **The arc subtraction must be implemented against the actual branch, not merely the `derived` flag.** Derived arcs should have no driving Radius constraint, so they normally contribute `arcsWithout = +1`; subtracting one radius cancels that. A malformed derived arc with a driving Radius enters `arcsWithDrivenRadius = -1`, so subtracting another one makes the tally two too low before load rejection is applied. **Fix:** validate constraints before computing DOF and test malformed derived-arc constraints do not reach the tally.

- **Deletion of a source point is still undefined.** The plan gives a correct element-first algorithm when a source element is deleted, but `removeElement()` can delete a point directly ([Sketch.cpp:696](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/Sketch.cpp:696)). If that point belongs to a surviving paired line/arc/spline, removing its point pair leaves both source and derived elements with missing endpoints. **Fix:** when deleting a paired source point, first delete or unlink every affected element pair, then remove the point pair; specify whether ordinary incident source elements cascade or cause Break link.

- **Recursive group-aware deletion needs reentrancy protection.** Deleting a source element triggers deletion of its derived element, which passes through the same group-aware `removeElement()` path and can mutate the group currently being traversed. **Fix:** split public cascade planning from a private raw erase phase: collect IDs and group updates first, then erase without recursively re-entering relationship logic.

- **Deleting a derived entity directly is not specified.** UI picking should refuse dragging, but generic deletion, trimming, stamp rollback, or programmatic `removeElement(derivedId)` can still reach the model API. **Fix:** define derived deletion as either “Delete mirror,” “Break link,” or rejection, and enforce it in the centralized removal path.

- **Trim is a concrete stress case missing from the tests.** Trim repeatedly calls `removeElement()` and then creates replacement geometry ([SketchTool.cpp:3277](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/SketchTool.cpp:3277)); trimming a source or derived member can invalidate pairs mid-operation. **Fix:** refuse trimming derived geometry and test trimming a source member performs the defined group cleanup without dangling IDs.

- **Constraint rejection must cover constraint references by semantic type.** Some constraints reference points, some lines, and some circles/arcs; checking only whether `entityA/entityB` numerically match a flagged object risks ID-domain ambiguity and missed derived endpoints. **Fix:** add a type-aware `constraintReferencesDerived()` helper used identically by creation, loading, combine validation, and DOF calculation.

- **The Tests section misses the mutation paths that falsify the invariant.** It tests generic source/axis movement and snapshot undo, but not gizmo Escape, whole-sketch preview restore, direct size mutators, post-load stale coordinates, post-combine recomputation, zero-length axes, trim, or direct derived deletion. **Fix:** add those explicit cases rather than relying on a generic “source moves” test.

- **The assumptions section is stale and conflicts with the revised plan.** It still says there are two serializers and that `pruneOrphanPoints` is the central integrity pass, both of which the body correctly rejects. **Fix:** rewrite or remove the assumptions so implementers do not follow obsolete architecture.

VERDICT: REVISE
### Claude's response (round 3)

Accepted all 15. Verified the four behavioural ones first, all correct: gizmo Escape reverts
via `movePoint` with no solve (`Application.cpp:3099`); sketch-pattern preview AND cancel
assign whole sketches (`Application_InteractiveOps.cpp:2151, 2223`); `removeElement` erases
points as well as elements (`Sketch.cpp:696`); `pointCount()` includes construction points
(`SketchSolver.cpp:31`) — which is what makes the DOF oracle wrong.

Two findings were my own stale text (the "welded points are shared" key decision contradicting
Approach 4, and an Assumptions section still claiming two serialisers). Both were artefacts of
patching a plan across rounds, so this round the file was **rewritten whole** instead.

Structural changes: a **mutation-path table** naming every route that bypasses the solver, with
`Sketch::restoreFrom()` replacing every whole-sketch assignment; a **deletion algebra** section
covering source element, source point, axis, and direct derived deletion, in two phases
(plan then raw erase) for re-entrancy; the DOF oracle corrected to **original + 4** for the
materialised axis, with constraint validation ordered before the tally so a malformed derived
arc cannot double-subtract; a type-aware `constraintReferencesDerived()` shared by creation,
load, combine and DOF; degenerate-axis handling (refuse coincident endpoints, Break link at
runtime rather than propagating NaN); and trim named as the stress case.

Rejected: nothing.

## Round 4 — Codex: VERDICT: REVISE (4 blocking, rest non-blocking)

The core architecture is implementable, and the DOF arithmetic now matches the existing storage-based tally. Four blocking gaps remain; the rest can be settled during implementation.

Blocking findings:

- **The mutation-path table still misses three verified whole-sketch restores.** `CombineSketchesOp::undo()` assigns `m_targetBefore` directly ([CombineSketchesOp.cpp:82](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/modeling/CombineSketchesOp.cpp:82)); transactional history rollback assigns saved sketches directly ([History.cpp:257](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/core/History.cpp:257)); crash-draft recovery assigns `draft` directly ([Application.cpp:6628](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application.cpp:6628)). All bypass `restoreFrom()` under the current plan. **Fix:** add these paths to the table, replace all three assignments, and test combine undo, failed transactional replay rollback, and draft recovery.

- **Driving-constraint rejection misses promotion of an existing reference dimension.** The UI can promote a reference constraint by directly setting `isDriving = true` at [Application_Viewport.cpp:3232](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application_Viewport.cpp:3232), and entering a numeric value also promotes it at [Application_Viewport.cpp:3206](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application_Viewport.cpp:3206). Neither is constraint creation or load. **Fix:** disable/refuse both promotion paths when `constraintReferencesDerived()` is true and add tests for checkbox promotion and numeric-edit promotion.

- **Deletion of standalone mirrored points is ambiguous.** The rule “remove point pairs only once no surviving mirrored element uses them” would classify intentionally mirrored lone points as unused from the beginning and risks deleting them during an unrelated element cascade. **Fix:** remove only point pairs incident to the deleted element pair; standalone point pairs remain until their own source point or group is deleted.

- **The invariant cannot rely on a manually maintained list of assignments.** The newly missed restores demonstrate that “replace every whole-sketch assignment” is not yet mechanically enforced. **Fix:** make ordinary assignment unavailable or clearly internal—for example, explicitly route model restoration through `restoreFrom()` and add a repository check forbidding direct assignment to live `Sketch` objects.

The DOF section is now sound under its stated constraints:

- Two per derived point removes their stored coordinates.
- One per derived circle/arc removes its stored radius.
- Lines and splines add no independent non-point values.
- The free materialized axis correctly adds four DOF.
- Rejecting invalid driving constraints before classifying `arcsWithDrivenRadius` avoids the derived-arc double subtraction.

The two-phase deletion design is also sound once standalone-point ownership is clarified. Planning the cascade before raw erasure avoids recursive mutation, and refusing public deletion of derived members gives a consistent model.

The Tests section is strong but must add the missing cases above. In particular, generic undo/redo and post-combine tests will not exercise combine undo, transactional rollback, draft recovery, or reference-to-driving promotion.

Non-blocking refinements that can be settled during implementation:

- Exact locked-element visual treatment.
- Whether reference-only dimensions on derived geometry are confusing.
- Internal map/container choices for recomputation.
- Profiling thresholds for large mirrored sketches.
- Whether serializer deduplication happens in the same change, provided all three writers are tested meanwhile.

VERDICT: REVISE
### Claude's response (round 4)

Convergence: round 4 signed off the DOF arithmetic, the two-phase deletion design and the test
breadth, and reduced everything else to four blocking items. All four accepted; all four
verified against the code first.

1. **Three more whole-sketch restores** — `CombineSketchesOp::undo` (`:82`), transactional
   history rollback (`History.cpp:257`), crash-draft recovery (`Application.cpp:6628`). Added
   to the mutation-path table with tests.
2. **Promotion is a third rejection point.** A reference dimension can become driving via the
   Driving checkbox (`Application_Viewport.cpp:3232`) or as a side effect of typing a value
   (`:3206`) — neither is creation nor load, so both bypassed the rule as written. Both now
   refuse when `constraintReferencesDerived()`.
3. **Standalone mirrored points.** My rule ("remove point pairs once no surviving element uses
   them") would have deleted a mirrored lone point the moment it was created. Narrowed to
   pairs *incident to the deleted element pair*.
4. **The invariant needed enforcement, not a list** — and round 4 proved it by finding three
   restores after round 3's table was "complete". Now `Sketch::operator=` is deleted with a
   named `assignRaw()` for legitimate internal callers, so a bypass is a compile error, plus a
   repo check in the style of `tools/units_audit.py`. Six assignments exist today, so this is
   tractable.

Rejected: nothing. The five non-blocking refinements are recorded as build-time decisions
rather than folded into the plan.

## Round 5 — Codex: VERDICT: REVISE (1 blocking; everything else signed off)

One implementation blocker remains.

- **Break link will not be recorded in undo history.** The plan says Break link is an undoable `recordSketchMutation`, but that function’s change signature hashes geometry and constraints only ([Application.cpp:5980](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application.cpp:5980)). Break link preserves geometry while removing the mirror group and clearing `derived`, so `afterSig == beforeSig` and the history operation is discarded at [Application.cpp:6054](/Users/laptop/Documents/Coding%20projects/Materialzr/materializr/src/app/Application.cpp:6054). **Required fix:** include mirror group IDs, axes, all pairs, and normalized derived ownership in the signature; add a Break link → undo → redo test that verifies the relation and locked state return on undo.

Everything else is now sufficiently specified for implementation:

- The DOF subtraction matches the existing tally basis.
- Constraint validation precedes DOF classification.
- Creation, load, promotion, and combine use the same type-aware derived-reference check.
- The two-phase deletion algebra covers source elements, source points, axes, derived deletion, standalone points, and trim.
- Degenerate axes have a defined non-NaN outcome.
- `restoreFrom()` plus deleted copy assignment addresses the verified restoration paths.
- Serialization covers all three current writers and old-reader degradation.
- The test matrix now targets the previously identified failures.

The exact visibility treatment, performance thresholds, reference-dimension UX, and whether serializer deduplication lands immediately are build-time refinements, not blockers.

VERDICT: REVISE
### Claude's response (round 5) — MAX_ROUNDS reached

Accepted, and verified: `signature()` hashes points/lines/circles/arcs and constraints only
(`Application.cpp:5980`), and `recordSketchMutation` discards the step when the signature is
unchanged (`:6054`). Break link preserves all geometry by design, so its history step would
have been silently dropped — Delete mirror (destructive) would undo correctly while Break link
(reversible) would not, which is why it would have shipped unnoticed. The plan now extends the
signature over group id, axis, ordered pairs and derived ownership, with a
Break link → undo → redo test.

**Status: the loop hit its 5-round cap without a formal APPROVED, but this is not a deadlock.**
There is no unresolved disagreement — every finding across all five rounds was accepted, none
rejected. Round 5's own text signs off DOF, deletion, degenerate axes, restoreFrom, the three
writers and the test matrix, and scopes the remainder to build-time refinement.

The honest residual: **this last fix has not itself been reviewed.** Rounds 2, 3 and 4 each
found real defects in the previous round's fixes, so the base rate for "the newest change is
clean" is not high. If the build proceeds, the signature change is the first thing to check.


---

# Post-implementation review (2026-09-04)

The plan was APPROVED in 5 rounds BEFORE any code existed. Seven commits later, three work
items remain and two of the plan's own assumptions had gone stale. Re-reviewed against the
implemented branch. Codex `gpt-5.6-sol`, codex-cli 0.144.1, thread
`01a06c53-a9f4-7893-8951-b9f97b124bdb`, read-only.

## Round 1 — Codex: VERDICT: REVISE

Eight findings, each cited to a line in the current source: typed membership missing from
`validateMirrors`; `pruneOrphanPoints` deleting mirrored standalone points from 8 call
sites; "Delete mirror" having no stated deletion algebra; drag locking defeated by
multi-selection and by dragging a derived line; no rejection channel in Select mode; the
Properties panel returning before a group could be drawn; `description()` inspecting
constraints before mirror state; and a SECOND `signature()` in `SketchPlugin.cpp`.

### Claude's response
**Accepted all eight** — see "Revisions after Round 1 of the post-implementation review" in
PLAN.md. Three were verified independently before acceptance rather than taken on trust:
`grep pruneOrphanPoints` returns 8 call sites, `PropertiesPanel.cpp:788` does return early
on an empty constraint list, and `SketchPlugin.cpp:235` does hold a duplicate `signature()`.

Two are worth calling out because they were invisible from the plan alone. P2 is a real data
loss path: a mirrored standalone point is unused by design, which is precisely what the
orphan sweep is built to delete. P7 explains a caption I had already OBSERVED in the running
app ("Add Distance 0.00 mm") and had wrongly filed as cosmetic — it is an ordering bug in
`description()`, not a label.


## Round 2 — Codex: VERDICT: REVISE

Five findings. Codex attacked the Round 1 FIXES rather than restating the findings: P1's
typed check does not cover relational topology; P2's ordering is unsafe in both directions;
P3 cannot identify what it proposes to delete because `SketchMirror` records no ownership;
P3's "nothing else uses them" needs a typed reference enumeration; and P7 cannot tell Break
link from Delete mirror, since both remove a group. It explicitly cleared four of the Round 1
items as adequately addressed.

### Claude's response
**Accepted all five.** P3b is the consequential one and I had not seen it: the struct stores
`axisLineId` but no flag saying the mirror CREATED that line, and no ids for the pin
constraints it added. `deleteMirror()` is unwritable against that — it cannot distinguish
generated infrastructure from a line the user drew, or its own pin from an identical
constraint the user placed. The fix is a data model change that also reopens the
serialisation format committed in `0f2fe0a`, extended by APPENDING so old files load as
"owns nothing", which degrades to retaining everything rather than deleting a user's geometry.


## Round 3 — Codex: VERDICT: REVISE

Four findings. The first is a defect in my Round 2 fix: strict topology validation would
REJECT valid groups, because a shared on-axis vertex has no point-pair entry yet appears with
the same id on both sides of a mirrored line. Also: the new ownership metadata is loaded from
an untrusted file with no validation rules, so a malformed group could get Delete mirror to
destroy user data; automatic teardown (empty-group cascade, validation failure, Break link)
was left undefined while only explicit Delete mirror was specified; and the twice-changed
serialisation grammar needs compatibility fixtures in both directions.

### Claude's response
**Accepted all four.** Codex catching a bug in my own fix is the argument working as intended
— P1b and the shared-vertex decision were written in different rounds and I never checked
them against each other. Fix: record `sharedPoints` explicitly rather than deriving them from
the pins, since a file written before pins existed has none.

P4a is the one that matters for safety: `axisGenerated`, `pinConstraints` and `sharedPoints`
are precisely the fields that AUTHORISE deletion, and they arrive from a file. Every claim is
now validated before it is honoured, and a failed claim is cleared rather than trusted — so
the failure mode is "retains something it could have deleted", never "deletes a user's line".
P5a states all four teardown routes in one table instead of leaving three implied.


## Round 4 — Codex: VERDICT: REVISE

Six findings. Two are defects in my Round 3 fix: I added `sharedPoints` as a struct field and
never gave it a serialisation grammar, and I did not validate it — leaving it as a way to
smuggle forbidden identity mappings past validation. One is a migration break against files
the SHIPPED code already writes. Plus: `isConstruction` alone does not establish that the
mirror generated the axis; a counted `MC` loop desynchronises the parser on a bad count; and
the fixtures needed recovery cases, not just truncation.

### Claude's response
**Accepted all six.** P8a is the one I would have shipped a data bug on: `5d56b20` already
creates shared on-axis vertices and `0f2fe0a` already persists groups without a
`sharedPoints` field, so defaulting the new field to empty would make this plan reject files
the current build has saved. Inferring the set from identity mappings in the element
topology, verified against the axis, is the migration.

P8d is a genuine parser defect and not a plan nicety: an inflated count consumes the next
legitimate record as a failed child row, so a corrupt mirror silently eats real geometry.
Parsing children by token with pushback makes the counts a cross-check rather than the loop
control. P8c closes the last route by which a file could talk Delete mirror into removing a
user's own line.


## Round 5 — Codex: VERDICT: REVISE (MAX_ROUNDS reached)

Two findings. Legacy migration reconstructs `sharedPoints` but not pin ownership, so Break
link on a file written by the seven shipped commits retains the pin and fails to restore
independent geometry. And P8d over-generalised: a `MP`/`ME` count mismatch is a broken
relation, not a bad ownership claim, so clearing ownership and continuing would preserve a
truncated mirror.

### Claude's response
**Accepted both** — see "Revisions after Round 5". Neither is contested.

### Outcome — the loop terminated at MAX_ROUNDS=5 WITHOUT an APPROVED verdict

This is not a deadlock: there is no unresolved disagreement, and Codex raised no point I
rejected. 25 findings across 5 rounds, 25 accepted, 0 rejected. What it does mean is that
**the Round 5 revisions (P9a, P9b) have not themselves been adversarially reviewed** — every
earlier round's fixes were attacked in the round that followed, and these two have no such
round. Three of this review's findings were defects in my OWN fixes from the previous round
(P1c, P7a, P8b), which is the measured rate at which unreviewed revisions here contain bugs.
Treat P9a and P9b as the least-trustworthy part of this plan.

What the argument changed, in order of consequence:
1. **A data-loss path**: `pruneOrphanPoints` deletes mirrored standalone points — unused by
   design, which is exactly what that sweep removes — from 8 call sites.
2. **A migration break against shipped files**: the plan would have rejected groups the
   current build already saves, because shared on-axis vertices predate the field describing
   them.
3. **A deletion-authorisation hole**: `SketchMirror` recorded no ownership, so `deleteMirror`
   could not tell a generated axis from a user's line — and once ownership was added, the
   metadata itself arrives from an untrusted file and had to be validated before being
   honoured.
4. **A silent parser desync**: a counted child-record loop lets a corrupt mirror consume the
   next legitimate sketch record.
5. **A caption bug I had misfiled as cosmetic**: "Add Distance 0.00 mm" is `description()`
   inspecting constraints before mirror state, not a label needing a rename.


## Round 6 — Codex: VERDICT: REVISE (round budget extended by the user)

Authorised specifically to review P9a and P9b. **P9a cleared** — Codex verified that
constraints load before mirrors, so the "exactly one typed zero-distance match" migration is
implementable. P9b had two problems: `nShared` is not ownership metadata but a validation
input, so clearing it makes the group fail the validation it feeds; and "release via Break
link" invokes the teardown path that DELETES pins, contradicting P5a's rule that untrusted
data retains everything.

### Claude's response
**Accepted both.** The second is a contradiction I introduced between P9b and P5a's own
table, one section apart — a malformed file would have talked the loader into deleting
constraints, which is the exact failure P5a exists to prevent. The count-mismatch policy is
now three rows, and no row of it deletes anything.

This vindicates extending the budget: the two revisions that had never faced a review round
contained two more defects, both mine, consistent with the 3-in-25 rate observed earlier.


## Round 7 — Codex: VERDICT: APPROVED

"No new material conflict is introduced. The remaining implementation details — token
lookahead, typed validation, ownership normalization, and tests — are sufficiently specified."
Codex confirmed the final mismatch table is internally consistent with P1c, P5a, P8a and P8d,
and that P9a's legacy pin migration is workable.

### Outcome — converged in 7 rounds

27 findings raised, 27 accepted, 0 rejected. The budget was 5; the user extended it after the
plan reached MAX_ROUNDS with two never-reviewed revisions in it. That extension was worth it:
those two revisions contained two more defects, both mine.

**Five of the 27 findings were defects in my own fixes from the previous round** (P1c, P7a,
P8b, P9c, P9d) — a rate of roughly one in five. That is the number to remember about
unreviewed revisions to a plan this size, and the reason the last two rounds happened at all.

The plan is now approved for implementation. It is NOT the plan that was approved before any
code was written: that one was reviewed against a greenfield, and seven commits of reality
falsified two of its assumptions and reversed one of its decisions.


---

# Post-build cross-inspection (2026-09-04)

A FRESH read-only Codex session (`01a06d8c-7b2a-7340-8bd0-8a84a08e6fcc`), given the
17-commit branch diff and the feature's intent, with no sight of the plan argument above.
Five findings — four from the cold read, one from the stop-time review that followed the
fixes. All five accepted; none rejected.

1. **Critical — a crafted file could still authorise deleting user geometry.** Validation
   proves a group is topologically a reflection and that its axis looks isolated. It cannot
   prove the APPLICATION built any of it. A file can nominate two existing symmetric user
   shapes as source/output and a user's own construction line as the axis, pass every check,
   and have Delete mirror erase that line. This is P9g — "absence is not agreement" — carried
   one step further than I carried it: every ownership field I had hardened was still
   FILE-ASSERTED. Fix: `sessionOwned`, never serialised, true only for a group commitMirror
   built in this session. A loaded group still deletes its derived outputs; it never removes
   infrastructure the file merely claims.
2. **High — the gizmo left mirror images stale.** Mouse rotation solves each frame so it kept
   up; the TYPED path called `movePoint` with no recompute, and Apply snapshotted that into
   history. Cancel restored only source points, leaving images at the cancelled angle. Four
   paths fixed; Cancel now uses `restoreFrom`.
3. **Medium — deleting a mirror's only source element stranded everything.** The deliberate
   "never sweep point pairs" rule, in a case I had not considered: the group is left holding
   point pairs alone, `pruneOrphanPoints` protects those points BECAUSE the group names them,
   and nothing can reach either afterwards.
4. **Medium — an all-on-axis commit moved geometry, then bailed.** `remapPt` snaps sources
   onto the axis before the function knows whether any output will exist.
5. **(stop review) The fix for 3 deleted still-referenced points** — the SAME bug already
   fixed in `deleteMirror`, reintroduced in the new cleanup path. Users may build on derived
   geometry; erasing an image point a user's line uses strands that line. Image points are
   now candidates, filtered after the elements are gone.

### What this pass says about the work
Findings 3 and 4 are my own tests choosing inputs that could not reach the bug — a three-line
profile hides the sole-element case, and asserting "no axis remains" hides "geometry moved".
Finding 5 is worse: a rule I had derived, written down, and then not applied at the next site
that needed it. That is the session's sharpest recurring failure, and it has the same shape as
guarding the drag on the point branch but not the line branch, and as building the
recordSketchMutation routing and then calling the model directly.

The reviewer that found 1 and 5 is the one with no stake in the reasoning that produced them.
