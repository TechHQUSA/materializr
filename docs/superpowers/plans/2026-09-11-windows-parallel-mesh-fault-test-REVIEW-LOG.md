# Plan Review Log: Windows parallel-mesh fault test + guard flip
Started 2026-09-11. MAX_ROUNDS=2 (bounded scope per advisor recommendation: single-file test changes + a one-line preprocessor flip, not a novel design surface like the Move Face saga). Codex CLI 0.153.4, model `gpt-6-astra` (per CLAUDE.md, review-incompatible for the built-in `/codex:review` feature but fine for the plain `codex exec` calls this skill uses).

## Round 1 — Codex

1. **Blocker: flipping the header guard does not enable the Windows test harness.** `tests/CMakeLists.txt:355` wraps application sources, GL sinks, and `MZR_PARALLEL_MESH_TESTING` in `if(NOT WIN32)`. The newly enabled tests will access private `Application` members without friendship and reference missing test hooks and implementations. This contradicts the "one-line Commit B" constraint.
2. **The new hardware-fault test can hang during local validation.** Its child has no watchdog; `ASSERT_EXIT` supplies no independent deadline when the binary is run directly (bypassing ctest's 60s TIMEOUT).
3. **The macOS probe does not establish independence from process-wide signal initialization.** The plan claimed it worked "without `TestSignalInit.cpp`'s help," but that initializer is force-linked on Unix and was active during the probe. `SetThreadLocalSignal()` does not replace global handlers on POSIX.
4. **Passing the proposed test does not prove the claimed Windows SEH-to-OCCT-translator conversion specifically** — `ParallelMesh.cpp`'s worker catch chain maps `Standard_Failure`, `std::exception`, and `catch(...)` to the same result, so MSVC's own `catch(...)` under `/EHa` could produce the same passing result even if OCCT's translator were missing.
5. **Commit A's required Linux validation is absent** from the prescribed steps (all macOS-only).

VERDICT: REVISE

### Claude's response

- **#1 (blocker) — accepted as a real, serious gap I'd missed.** Verified directly against `tests/CMakeLists.txt:355-384` and `Application.h:119-120`: the entire app-source/GL-sink/`MZR_PARALLEL_MESH_TESTING` wiring for `test_parallel_mesh` is `if(NOT WIN32)`, and `MZR_PARALLEL_MESH_TESTING` is what makes `friend struct ParallelMeshTestAccess` exist at all. Rewrote Task 3: it now includes de-indenting that CMake block to run unconditionally (the underlying sources already build for Windows as part of the shipping `materializr`/`materializr_core` targets; the genuinely untested pieces are `MZR_PARALLEL_MESH_TESTING`'s alternate code paths and `parallel_mesh_gl_sinks.cpp`'s stub-linking behavior under MSVC). Dropped "Commit B is one line" from Global Constraints and the commit message; expanded the CI-round budget language in Step 8 to call out this piece as the likely source of extra rounds.
- **#2 (watchdog) — accepted, adopted verbatim.** Added the same detached-thread watchdog pattern from Task 1 into the new test's `ASSERT_EXIT` body (5s deadline vs. Task 1's 3s, since this path does real OCCT meshing work on 3 successful jobs, not just a dequeue throw).
- **#3 (TestSignalInit correction) — accepted, corrected the Background section.** Confirmed via `OSD.hxx`'s doc comment: `SetThreadLocalSignal` "includes `_set_se_translator()` **on Windows platform**" specifically — implying it does NOT independently install the process-wide POSIX `sigaction()` handler; that's `OSD::SetSignal()`'s job, done process-wide by `TestSignalInit.cpp` on every non-Windows test binary today, including during the macOS probe. Rewrote the Background section to state this accurately instead of implying `SetThreadLocalSignal` is self-sufficient on all platforms — it's only self-sufficient on Windows, which is exactly why Windows doesn't need a `TestSignalInit.cpp` equivalent.
- **#4 (scope the claim) — accepted, adopted the plan's own suggested fix (scope to containment) rather than building a Windows-only discriminating probe** (a probe that could tell OCCT's translator apart from MSVC's blanket `catch(...)` would need to inspect the exception's dynamic type inside the worker, which is more invasive than this test needs to be, and the practical property this codebase actually cares about — a fault doesn't crash the process — doesn't require that distinction). Renamed the test to `RealHardwareFaultInWorkerIsContainedNotFatal` and rewrote its comment and the plan's Task 2 framing to make the narrower claim explicit.
- **#5 (Linux validation) — rejected the literal fix, adopted a corrected framing.** Checked `.github/workflows/*.yml`: only `macos.yml` and `windows.yml` run `ctest` at all — `linux.yml` only builds the AppImage and never runs the test suite. There is no Linux test CI in this project to gate on, and no Linux dev machine in this session, so "require a local Linux run" isn't achievable and "require Linux CI to pass" doesn't correspond to anything that exists. Corrected the Global Constraint to state this precisely: local macOS verification now, `macos.yml` CI on push, no Linux test gate exists in this project.

## Round 2 — Codex

**Material blocker: the GL sinks are incompatible with Windows's GLEW dispatch, not merely unverified linker behavior.** `gl_common.h` includes GLEW on Windows; modern GL names resolve through GLEW function-pointer aliases, so `parallel_mesh_gl_sinks.cpp`'s ordinary function definitions cannot serve as portable overrides there. Undefining the macros wouldn't fix callers either — `ShapeRenderer`/`Viewport` code still dispatches through GLEW pointers, and the headless constructor skips window/context init, so unresolved dispatch becomes a runtime crash the moment a GL call happens. Also corrected: the plan's claim that this links "no real GL import library" is wrong — the copied `materializr` link dependencies include both `OpenGL::GL` and `GLEW::GLEW`.

**Fix:** add a Windows-compatible test GL dispatch layer to Task 3's scope, route the exercised calls through it before any `Application` construction, validate on Windows CI.

VERDICT: REVISE

### Claude's response

Verified directly rather than taking the critique at face value: read `src/gl_common.h:50-61`, confirming `<GL/glew.h>` is included only `#elif defined(_WIN32)`, with macOS using `<OpenGL/gl3.h>` directly. Downloaded the real GLEW 2.3.1 release header (GitHub's git tree has no checked-in `glew.h` — it's generated at release time — so fetched the `glew-2.3.1.zip` release asset) and confirmed for all ~20 stubbed functions: 16 of them (everything past GL 1.1 — VAOs, buffers, framebuffers, renderbuffers) are `#define`d to `GLEW_GET_FUN(__glewX)`, where `__glewX` is a global function-pointer variable only `glewInit()` populates (never called here), and `GLEW_GET_FUN(x)` is plain `x` in GLEW's default non-`GLEW_MX` build — so a same-named function in the sink file is simply dead code, never invoked; the real call goes through a null pointer and crashes. The remaining 4 (`glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameteri`) are GL 1.1 core, exported directly by `opengl32.dll`/`.lib` even on Windows, NOT behind GLEW — these can stay as plain functions.

Accepted in full. Added a new file to Task 3, `tests/parallel_mesh_gl_sinks_win32.cpp`, that reassigns the 16 confirmed `__glew*` pointer globals to no-op sinks at static-init time (same pattern as the existing `TestSignalInit.cpp`, which already relies on running before any test body), using the exact symbol/typedef names read from the real GLEW 2.3.1 header.

## Self-check before presenting to the user (advisor call, not a Codex round — MAX_ROUNDS=2 was already spent)

Before treating round 2's fix as final, consulted the advisor tool (sees the full session transcript) given the pattern of two straight rounds each finding a real, previously-missed Windows-specific blocker outside the originally-scoped files. It found two more, one confirmed real, one checked and inapplicable:

1. **Confirmed real, fixed the fix.** My round-2 answer's plan to add `APIENTRY` to the 4 GL-1.1 sink functions was itself wrong: `parallel_mesh_gl_sinks.cpp` includes `gl_common.h` → `<GL/glew.h>` on `_WIN32`, which declares those 4 functions `GLAPI void GLAPIENTRY glGenTextures(...)`, where `GLAPI` = `WINGDIAPI` = `__declspec(dllimport)` on Windows (confirmed against the same downloaded GLEW 2.3.1 header). Defining a `dllimport`-declared function in the same translation unit is a hard MSVC compile error (C2491) — not a linking or runtime concern like the other 16. Fix adopted: don't redefine these 4 on Windows at all; `parallel_mesh_gl_sinks.cpp` compiles only on non-Windows (unchanged from today), and the new Windows-only file supplies just the 16 GLEW-indirected functions. The real `opengl32.dll` GL 1.1 entry points are harmless no-ops without a rendering context, same as every other unstubbed GL 1.1 call already relies on in the existing file.
2. **Checked, does not apply.** Possible risk: copying `materializr`'s `LINK_LIBRARIES` onto `test_parallel_mesh` could drag in `SDL2::SDL2main`, duplicating `gtest_main`'s entry point on Windows (a known vcpkg SDL2 pitfall). `grep -n SDL2main CMakeLists.txt` returns nothing — this project links only `SDL2::SDL2`, never `SDL2::SDL2main`, on any platform. No fix needed; noted in the commit message as checked.

This is now the third real, previously-missed Windows-specific finding across two Codex rounds plus this self-check, each outside the files originally scoped as "the design surface" (`ParallelMesh.h`, `test_parallel_mesh.cpp`). MAX_ROUNDS=2 is spent and round 2's fix (before this self-check) was never itself re-reviewed by Codex. Per the skill's hard rule ("the loop ALWAYS terminates at MAX_ROUNDS... hand it to the human to break the tie"), stopped and asked the user how to proceed rather than unilaterally starting a round 3.

**User's decision: extend 1-2 more Codex rounds.** MAX_ROUNDS raised to 4.

## Round 3 — Codex

1. **The watchdog is still not actually portable.** The plan removes `<unistd.h>` but continues calling `_exit`; `_exit` is POSIX/MSVC-specific and is not guaranteed by `<cstdlib>` — the standard function supplied there is `std::_Exit`.
2. **The hardware-fault test uses `intptr_t` without including its defining header.** Not guaranteed portable to MSVC or by C++17 without `<cstdint>`.
3. **The Windows sink count is wrong throughout the plan.** The proposed file installs 17 GLEW-indirected functions, not 16 (nine buffer/VAO, three framebuffer, five renderbuffer). The code was already correct; only the prose count was wrong, repeatedly.
4. **Task 3's declared file/interface inventory contradicts its implementation.** Says only two files change and "no new C++ symbols," while Step 3 creates a whole new file with 17 sink functions and a static initializer.
5. **Commit A's message falsely implies `_exit()` is POSIX-only when the implementation was retaining it for Windows too.**

VERDICT: REVISE

### Claude's response

All 5 accepted — all straightforward, verifiable, no judgment calls needed:
- **#1** — checked: MSVC does declare its own `_exit()`, but in `<process.h>`, not `<cstdlib>` as the plan claimed. Switched every `_exit(` in Task 1 and Task 2's code to `std::_Exit(` (C++11-standard, `<cstdlib>`, actually portable), added `#include <cstdint>` alongside the already-planned `#include <cstdlib>` addition, updated the Global Constraints bullet and Commit A's message to state the corrected reasoning instead of the wrong one.
- **#2** — fixed alongside #1: `volatile intptr_t` → `volatile std::intptr_t`.
- **#3** — recounted directly against `tests/parallel_mesh_gl_sinks.cpp`'s actual 21 stubbed functions (confirmed by re-listing them): 4 are GL 1.1, 17 are GLEW-indirected. Fixed all 4 "16" references in the plan's prose; the win32 sink file's actual code already had the correct 17 functions — this was a documentation bug, not a code bug.
- **#4** — updated Task 3's **Files** list to include the new `tests/parallel_mesh_gl_sinks_win32.cpp`, and **Interfaces** to describe its self-contained static-initializer shape (no symbols consumed elsewhere, matching `TestSignalInit.cpp`'s existing pattern).
- **#5** — resolved as a side effect of #1: since Commit A no longer claims `_exit()` is POSIX-only (that claim is gone), the false statement Codex flagged no longer exists in the message.

Sending the corrected plan back for round 4 (of the user-approved 4-round budget).

## Round 4 — Codex

No material implementation problems remain. The Windows GLEW dispatch, GL 1.1 imports, CMake source wiring, death-test watchdogs, fault-injection types, and CI gate are now covered coherently. One cosmetic note: claimed Task 3 Step 4 repeats a comment line twice.

VERDICT: APPROVED

### Claude's response

Checked the cosmetic note before accepting it: grepped the plan document and the live `src/viewport/ParallelMesh.h` for the flagged text ("Checked for the one sharing pattern..."). It appears exactly once in each — the plan's Step 4 shows the *replacement* comment for the one existing block at `ParallelMesh.h:10-20`, not an addition alongside it. False positive, likely Codex reading the old and new comment text (which intentionally share an opening line, since Step 4 is an edit of that same block) as two separate occurrences. No change needed.

**Result: APPROVED after 4 rounds.** Two real, previously-missed Windows-specific blockers found and fixed (CMake test-wiring gate in round 1, GLEW pointer indirection in round 2), one more caught by self-check before round 3 (dllimport conflict on the GL 1.1 stubs), and four small portability/documentation fixes in round 3 (`std::_Exit`, `std::intptr_t`, the 16→17 count, Task 3's file/interface inventory). Commit A (Tasks 1-2) had zero findings across all 4 rounds. Commit B (Task 3) is where every real finding landed — consistent with it being the piece with no precedent anywhere else in this codebase and no way to verify locally.

## Act 3 — Build

### Round 1 — Codex build (Commit A: Tasks 1-2 only)

Scoped the build prompt explicitly to Tasks 1-2 (`tests/test_parallel_mesh.cpp` only) — Task 3 (guard flip, CMake wiring, GLEW sinks) deliberately excluded, to land on its own branch per the spec's own architecture. Codex reported: replaced the POSIX watchdog with the portable detached-thread version, added `RealHardwareFaultInWorkerIsContainedNotFatal` verbatim per spec, no deviations, no commit created. Proof output pasted: build clean, target test passes 3x, full 15-test suite passes.

### Claude's verdict

Verified independently, not taken on Codex's word:
- `git status`/`git diff --stat`: only `tests/test_parallel_mesh.cpp` touched (+62/-4), exactly the scoped file — no scope creep into Task 3's files.
- Read the full diff: matches the plan's Task 1/Task 2 code blocks byte-for-byte (watchdog thread, `std::_Exit` throughout, `std::intptr_t`, the new test's comment and body).
- Re-ran the proof commands myself rather than trusting the pasted output: rebuilt `test_parallel_mesh` clean, ran `OuterFaultLoadReturnsBeforeDeadline` three times (all pass, ~49ms each, no flakiness), ran `RealHardwareFaultInWorkerIsContainedNotFatal` (passes), ran the full 15-test file (all pass).
- Went further than the spec's own proof command: rebuilt the ENTIRE project and ran the full `ctest` suite (111 tests) to check for regressions outside the touched file. 100% passed, 159s total, 0 failures.

**Verdict: APPROVED.** Ready for human sign-off and commit.
