# Windows Parallel-Mesh Fault Test + Guard Flip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove `ParallelMesh`'s worker-thread hardware-fault handling with a genuine SIGSEGV (not a thrown C++ exception), make the existing `test_parallel_mesh.cpp` suite portable to MSVC, then flip `MZR_PARALLEL_MESH_SUPPORTED` on for `_WIN32` and verify on real Windows CI before it ever reaches `main`.

**Architecture:** Two independent commits. Commit A (safe, mergeable immediately): a portable watchdog replaces `outerFaultLoad`'s `alarm()`/`_exit()`/`<unistd.h>`, and a new test injects a real null-pointer dereference into `ParallelMeshOptions::mesh` inside an `ASSERT_EXIT` death test, proving a genuine hardware fault is contained (batch completes, other jobs still succeed) rather than crashing the process. Both changes are gated behind the *existing* `#if defined(MZR_PARALLEL_MESH_SUPPORTED)` block, so Windows behavior is unchanged by Commit A - it is provably safe to land the normal way. Commit B (risky, isolated on a branch): flips the guard for `_WIN32` **and** ports the test target's Windows-excluded CMake wiring (Task 3 covers why this is not a one-line change) - activating all 15 tests in `test_parallel_mesh.cpp` on Windows CI for the first time ever. It only lands after a real Windows Actions run is green.

**Tech Stack:** C++17, GoogleTest death tests (`ASSERT_EXIT`), OCCT 7.9.3 `OSD`/`Standard_ErrorHandler`, CMake, GitHub Actions (`windows.yml`).

**Spec:** This document; there is no separate design doc. Findings that motivate it are recorded in `~/.claude/projects/-Users-laptop-Documents-Coding-projects-Materialzr/memory/windows-parallel-mesh-audit.md` and `parallel-load-mesh-plan.md`.

## Global Constraints

- Do not touch anything under `#if !defined(MZR_PARALLEL_MESH_SUPPORTED)` (mobile) - mobile stays out of scope; no CI exists for it.
- Commit A must build and pass locally on macOS with zero behavior change on Windows (the guard stays off for Windows until Commit B). There is no Linux test CI in this project to gate on - `linux.yml` only builds the AppImage, it never runs `ctest` (confirmed: `grep -l ctest .github/workflows/*.yml` matches only `macos.yml` and `windows.yml`). Commit A's actual cross-platform check is: local macOS run now, plus `macos.yml` on push.
- Commit B is **not** a one-line change - see Task 3's background. It touches `ParallelMesh.h`'s guard AND the CMake wiring in `tests/CMakeLists.txt` that currently excludes Windows from the test target's app-source/GL-sink/`MZR_PARALLEL_MESH_TESTING` setup. No other logic changes bundled in beyond what Task 3 enumerates.
- Never push Commit B to `upstream/main` directly. It lands only after a real Windows Actions run on the branch/PR is green - this is a deliberate deviation from this project's usual direct-push workflow, because the risk here is a dormant, never-before-compiled-on-MSVC test target.
- Only `<unistd.h>` and `alarm()` are POSIX-only and need replacing (round 3 correction: `_exit()` itself is NOT the portable choice it first looks like - MSVC declares a same-named `_exit()` in `<process.h>`, a different header than POSIX's `<unistd.h>`. Use `std::_Exit()` from `<cstdlib>` instead, the C++11-standard, guaranteed-portable equivalent, everywhere this plan's new/modified code needs to force-exit a death-test child).
- Use `std::intptr_t` from `<cstdint>`, not bare `intptr_t`, in the fault-injection code - not guaranteed available without that include.
- The fault must be a genuine access violation (SIGSEGV/AV), not FPE - `ParallelMesh.cpp:117` calls `OSD::SetThreadLocalSignal(OSD_SignalMode_Set, Standard_False)`, which leaves floating-point signals disabled. A null-pointer write is unconditionally intercepted regardless of that flag.
- The injected pointer write must not be foldable/warnable-away by the optimizer: route it through a `volatile` intermediate.
- Any `ASSERT_EXIT` test whose child body can plausibly hang (not just crash or exit cleanly) must carry its own watchdog thread - don't rely solely on the outer `ctest` 60-second `TIMEOUT` (`tests/CMakeLists.txt:354`), since Task 2's Step 3 runs the binary directly, bypassing that timeout entirely.

---

## Background (for the implementer - do not skip)

`tests/test_parallel_mesh.cpp` has 14 tests, ALL gated behind `#if defined(MZR_PARALLEL_MESH_SUPPORTED)` (currently true on macOS/Linux only - `src/viewport/ParallelMesh.h:21`: `#if !defined(_WIN32) && !defined(MZ_MOBILE)`). Flipping that guard for `_WIN32` does not just enable the pool in the shipped app - it activates the **entire test file** on Windows CI for the first time, including a headless `Application` construction/destruction that no other test file in this repo exercises (`grep -rl "Application app;" tests/*.cpp` returns only this file).

Two things were already checked and are **not** blockers:

1. **`/EHa` already reaches the test build.** `CMakeLists.txt:510` calls `add_compile_options(/EHa)` for MSVC before `add_subdirectory(tests)` at `CMakeLists.txt:551` - directory-scoped compile options are inherited by subdirectories added afterward, so `materializr_core` and every test executable get `/EHa` too. (This closes contract 5 from `parallel-load-mesh-plan.md` more completely than that note assumed.)
2. **The headless `Application` path is already platform-neutral.** `Application::Application()` (`src/app/Application.cpp:213`) has an early-return branch under `#ifdef MZR_PARALLEL_MESH_TESTING` that constructs only `ProjectSession`/`ShapeRenderer`/`EdgeRenderer`/`Viewport` - no `Window`, no GL context, nothing OS-specific. This is the path every one of the 14 tests uses.

One thing was verified empirically in this session, live on macOS, and is the premise this whole plan depends on: a real SIGSEGV thrown from inside `ParallelMeshOptions::mesh` on a worker thread **is** caught (the batch completes, other jobs still succeed, no crash) - confirmed with a throwaway `ASSERT_EXIT` probe against the unmodified code, then reverted. No test in the existing suite exercises this; `FailedJobContinuesAndFallbackMeshesExactlyOnce` (line 290) and `OuterFaultLoadReturnsBeforeDeadline` (line 379) only ever throw plain C++ exceptions (`Standard_Failure`, `std::runtime_error`) - never a hardware fault. What this test proves is scoped precisely to that observable behavior - fault containment plus continued batch processing - not to which specific mechanism performed the catch (see Task 2's note on this).

**Correction from round 1 of plan review - `TestSignalInit.cpp` is load-bearing on POSIX, not redundant.** `TestSignalInit.cpp` (calls `OSD::SetSignal()`, installing a process-wide `sigaction()`-based handler) is force-linked into test binaries only `if(NOT WIN32)` (`tests/CMakeLists.txt:134`), and it WAS active during the macOS probe (macOS test binaries are `NOT WIN32`). `OSD.hxx`'s doc comment draws a real platform distinction that matters here: `SetThreadLocalSignal` "includes `_set_se_translator()` **on Windows platform**" - i.e. only on Windows is the per-thread call alone sufficient to install the fault-to-exception mechanism from scratch. On POSIX, the underlying `sigaction()` registration is inherently process-wide; `SetThreadLocalSignal` there mainly sets the (thread-local) floating-point mask, riding on a handler someone else already installed via `OSD::SetSignal()`. So the macOS probe's success rested on `TestSignalInit.cpp`'s process-wide call being present - which it is, in every macOS/Linux test binary today (not an artificial or unusual condition, just worth stating accurately rather than implying `SetThreadLocalSignal` is self-sufficient everywhere). This is exactly why Windows does **not** need a `TestSignalInit.cpp`-equivalent: its mechanism (`_set_se_translator`) is per-thread-installing by construction, and `ParallelMesh.cpp:117` already calls `SetThreadLocalSignal` in every worker on every platform.

**New finding from round 1 of plan review - the Windows guard flip is not a one-line change.** `tests/CMakeLists.txt:355-384` wraps a large block in `if(NOT WIN32)`: it re-adds nearly all of the `materializr` app target's sources (via `get_target_property(materializr SOURCES)`, filtered to `src/` and excluding what's already in `materializr_core`) directly onto `test_parallel_mesh`, adds `parallel_mesh_gl_sinks.cpp` (GL-call stubs so the real `ShapeRenderer` links without a graphics context), compiles a *second*, separately-instrumented copy of `ParallelMesh.cpp`, and defines `MZR_PARALLEL_MESH_TESTING` (which is what makes `Application.h:120`'s `friend struct ParallelMeshTestAccess` and `Application.cpp:213`'s headless-constructor branch exist at all). None of this runs on Windows today. Simply flipping `ParallelMesh.h`'s guard would make `test_parallel_mesh.cpp`'s `#if defined(MZR_PARALLEL_MESH_SUPPORTED)` block compile on Windows while the CMake target it's compiled into still lacks `Application.cpp`, `MZR_PARALLEL_MESH_TESTING`, and the GL sinks - a guaranteed compile/link failure. Task 3 now covers porting this block, not just the header guard.

---

## Task 1: Portable watchdog for `outerFaultLoad`

**Files:**
- Modify: `tests/test_parallel_mesh.cpp:1-51` (includes), `:106-122` (`outerFaultLoad`)

**Interfaces:**
- No new symbols consumed by later tasks. `outerFaultLoad`'s signature (`void outerFaultLoad(const std::string& path)`) and the test that calls it (`OuterFaultLoadReturnsBeforeDeadline`, line 379) are unchanged.

The current code:

```cpp
#if defined(MZR_PARALLEL_MESH_SUPPORTED)
#include "app/Application.h"
#include "viewport/ShapeRenderer.h"
#include <unistd.h>
#include <signal.h>
...
void outerFaultLoad(const std::string& path) {
    alarm(3);
    Application app;
    ...
    _exit(::testing::Test::HasFailure() ? 1 : 0);
}
#endif
```

`alarm(3)` and `<unistd.h>` are POSIX-only and do not exist on MSVC. `<signal.h>` is included but nothing in the file calls `signal()`/`raise()` directly (grep confirms) - it is dead weight left over from an earlier version and can be dropped in the same edit. `_exit()` (lowercase) is NOT the portable replacement it first looks like - round 3 of plan review caught this: MSVC declares its own same-named `_exit()`, but in `<process.h>`, a different header than POSIX's `<unistd.h>`; relying on it being available via some other transitive include is fragile. `std::_Exit()` (capital E, from `<cstdlib>`) is the actual C++11-standard, guaranteed-portable equivalent - used throughout this plan's new/modified code instead.

- [ ] **Step 1: Replace the POSIX watchdog with a portable detached thread**

Remove `#include <unistd.h>` and `#include <signal.h>` from the `#if defined(MZR_PARALLEL_MESH_SUPPORTED)` block at the top of the file (lines 34-35). Add `#include <cstdlib>` (for `std::_Exit`, used here and in Task 2) and `#include <cstdint>` (for `std::intptr_t`, used in Task 2) to the top-level, unconditional include block (with the other `<...>` includes around line 22-29).

Replace `outerFaultLoad`:

```cpp
void outerFaultLoad(const std::string& path) {
    // alarm(3) was POSIX-only. A detached watchdog thread that force-exits
    // after the same 3-second deadline is the portable equivalent: if the
    // real load call below hangs (the bug this test guards against), the
    // watchdog fires std::_Exit(2) before ASSERT_EXIT's own timeout, so the
    // failure is "wrong exit code" instead of an indefinite hang.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::_Exit(2);
    }).detach();
    Application app;
    Access::setup(app) = [](auto& options) {
        options.workerCount = 2;
        options.beforeDequeue = [] { throw std::runtime_error("dequeue"); };
    };
    Access::after(app) = [](const auto& batch) {
        EXPECT_LT(batch.completedJobs, batch.results.size());
        EXPECT_EQ(batch.threadsStarted, 2u);
        for (const auto& r : batch.results) EXPECT_FALSE(r.completed);
    };
    EXPECT_TRUE(Access::load(app, path));
    EXPECT_EQ(upload(app), 4);
    for (auto j : jobsFrom(Access::doc(app))) EXPECT_EQ(countUnmeshedFaces(j.shape), 0);
    std::_Exit(::testing::Test::HasFailure() ? 1 : 0);
}
```

The detached thread outlives `outerFaultLoad` on purpose - this runs inside an `ASSERT_EXIT` death-test child process (a `threadsafe`-style re-exec/fork per `GTEST_FLAG_SET(death_test_style, "threadsafe")` at line 381), so the whole process exits via one of the two `std::_Exit()` calls; there is no parent-process leak to worry about.

- [ ] **Step 2: Build and run the modified test on macOS**

```bash
cmake --build build --target test_parallel_mesh -j 8
./build/tests/test_parallel_mesh --gtest_filter='*OuterFaultLoadReturnsBeforeDeadline*'
```
Expected: `[ PASSED ] 1 test.` Run it 3 times in a row to rule out flakiness from the new thread-based timing (the old `alarm()` and the new watchdog both have the same 3-second budget against the same real load call, so timing behavior should be identical).

- [ ] **Step 3: Run the full `test_parallel_mesh` suite**

```bash
./build/tests/test_parallel_mesh
```
Expected: all 14 tests still pass (this task must not change behavior for any test other than the one being made portable).

## Task 2: Permanent hardware-fault-injection test

**Files:**
- Modify: `tests/test_parallel_mesh.cpp` (new `TEST`, placed after `OuterFaultLoadReturnsBeforeDeadline` at line 383, before `PartialLaunchJoinsBeforeFallback`)

**Interfaces:**
- Consumes: `boxes(size_t n)` (line 60), `mesh(const ParallelMeshJob&, float, float)` (line 67), `parallelMesh()` (`ParallelMesh.h:76`), `ParallelMeshOptions` (`ParallelMesh.h:64`), `kDefl`/`kAng` (line 58).
- Produces: nothing consumed by later tasks - this is a leaf test.

This is the test that actually answers contract 6/the open item in `windows-parallel-mesh-audit.md`: "no worker-thread fault-injection test has ever been run on real Windows CI." It must inject a genuine hardware fault, not a C++ throw, because that is the one code path every existing fault test in this file skips.

**Scope note (from round 1 of plan review):** this test proves fault *containment* - the process doesn't crash, the failing job is marked not-ok, and the worker keeps draining the remaining jobs. It does not, by itself, distinguish *which* mechanism performed the catch. On Windows specifically, MSVC's `catch(...)` under `/EHa` catches a raw structured exception directly, independent of whether OCCT's own `_set_se_translator`-based translation (installed by `OSD::SetThreadLocalSignal`) is wired correctly - so a pass here does not isolate "OCCT's translator works" from "MSVC's blanket `/EHa` catch-all works." Both are already true today per `ParallelMesh.cpp:141-147`'s `catch(const Standard_Failure&) / catch(const std::exception&) / catch(...)` chain, and the practically-relevant property for this codebase either way is the observable one this test checks: a hardware fault in a worker does not take down the process. Treat the test's name and comment accordingly - it is not a claim about OCCT's internals.

- [ ] **Step 1: Write the test**

```cpp
// 9b. Every other fault-injection test above throws a plain C++ exception.
// This one injects a genuine hardware fault (SIGSEGV here; on Windows an
// access violation under /EHa) to prove it is CONTAINED - the process does
// not crash, the failing job is marked not-ok, and the worker keeps
// draining the remaining jobs. It does not isolate which specific
// mechanism performed the catch (OCCT's OSD::SetThreadLocalSignal
// translator vs. MSVC's own catch(...)-under-/EHa) - both already exist in
// the worker's catch chain (ParallelMesh.cpp:141-147) and either is
// sufficient for the property this test checks. Verified empirically on
// macOS before this test was written (see windows-parallel-mesh-audit.md);
// this is what makes that verification permanent and, once the guard below
// is flipped, gives it its first real Windows CI run.
TEST(ParallelMesh, RealHardwareFaultInWorkerIsContainedNotFatal) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_EXIT({
        // Same watchdog pattern as outerFaultLoad: if fault containment
        // somehow deadlocks instead of completing or crashing, this forces
        // a diagnosable exit instead of an indefinite hang - ctest's own
        // 60s TIMEOUT (tests/CMakeLists.txt:354) does not apply when this
        // binary is run directly, as Step 3 below does.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::_Exit(2);
        }).detach();
        auto jobs = boxes(); // kMinJobsForPool is 4; boxes() defaults to 4
        ParallelMeshOptions options;
        options.workerCount = 1; // deterministic: one worker drains the queue
        options.mesh = [](const auto& j, float d, float a) {
            if (j.bodyId == 0) {
                // volatile: keeps /O2 and -O2 from proving this is UB and
                // folding it away or erroring at compile time.
                volatile std::intptr_t zero = 0;
                *reinterpret_cast<volatile int*>(zero) = 1;
            }
            mesh(j, d, a);
        };
        const auto batch = parallelMesh(jobs, kDefl, kAng, options);
        const bool ok = batch.path == ParallelMeshPath::Pooled &&
            batch.results.size() == 4 && batch.completedJobs == 4 &&
            batch.results[0].completed && !batch.results[0].ok &&
            batch.results[1].completed && batch.results[1].ok &&
            batch.results[2].completed && batch.results[2].ok &&
            batch.results[3].completed && batch.results[3].ok;
        std::_Exit(ok ? 0 : 1);
    }, ::testing::ExitedWithCode(0), "")
        << "a hardware fault in one job must be contained, not crash the "
           "process, and must not stop the worker from draining the "
           "remaining jobs";
}
```

- [ ] **Step 2: Verify it fails on the platform gate for the right reason first**

This step is a sanity check that the test is actually gated correctly, not a normal red-green cycle (the underlying mechanism this test checks already works - that was proven live in this session). Confirm the test only compiles under `MZR_PARALLEL_MESH_SUPPORTED`:

```bash
grep -n "RealHardwareFaultInWorkerIsContainedNotFatal" tests/test_parallel_mesh.cpp
```
Confirm the match falls between the `#if defined(MZR_PARALLEL_MESH_SUPPORTED)` at line 126 and its matching `#endif` (currently at line 543, after the shift from Task 1's edits) - i.e. inside the Unix/Linux-only block, not in the `#else` branch with `PlatformFallsBackWithoutLaunching`.

- [ ] **Step 3: Build and run it in isolation**

```bash
cmake --build build --target test_parallel_mesh -j 8
./build/tests/test_parallel_mesh --gtest_filter='*RealHardwareFaultInWorkerIsContainedNotFatal*'
```
Expected: `[ PASSED ] 1 test.` If it instead reports "died but not with expected exit code" or a signal-based death, STOP - that would mean the fault conversion does not actually work on this machine's OCCT build, contradicting the live probe from earlier in this session, and nothing past this point in the plan should proceed until that is understood.

- [ ] **Step 4: Run the full suite once more**

```bash
./build/tests/test_parallel_mesh
```
Expected: 15 tests, all passing.

- [ ] **Step 5: Commit A**

```bash
git add tests/test_parallel_mesh.cpp
git commit -m "$(cat <<'EOF'
tests: Make the parallel-mesh fault tests portable, add a real hardware-fault test

outerFaultLoad used alarm() from <unistd.h>, POSIX-only, which would have
broken compilation the moment MZR_PARALLEL_MESH_SUPPORTED is ever enabled
for _WIN32. Replaced it with a portable watchdog thread, and switched
_exit() to std::_Exit() throughout (the former is a same-named but
different, non-portable MSVC/POSIX CRT extension declared in different
headers on each platform; the latter is the actual C++11-standard,
guaranteed-portable equivalent).

Every existing fault-injection test in this file throws a plain C++
exception. None of them exercise what OCC_CATCH_SIGNALS + OSD::Set-
ThreadLocalSignal actually exist for: converting a genuine hardware fault
into something catch(...) receives instead of a process crash. Added a
test that injects a real SIGSEGV into a pool worker's mesh call.

Both changes are gated behind the existing, unmodified
MZR_PARALLEL_MESH_SUPPORTED guard - no behavior changes on Windows.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

This commit is safe to push to `upstream/main` the normal way (direct push, per this project's convention) once reviewed - it changes nothing for Windows.

## Task 3: Flip the guard for Windows and port the test wiring

**Files:**
- Modify: `src/viewport/ParallelMesh.h:21-23`
- Modify: `tests/CMakeLists.txt:355-384` (drop the `if(NOT WIN32)` wrapper; make the `parallel_mesh_gl_sinks*.cpp` source conditional on `WIN32`)
- Create: `tests/parallel_mesh_gl_sinks_win32.cpp` (Windows-only; test-only GLEW function-pointer initialization, no symbols consumed elsewhere)

**Interfaces:**
- `tests/parallel_mesh_gl_sinks_win32.cpp` produces no symbols any other file references - it's a self-contained static initializer (`InstallGlewSinks`/`g_installGlewSinks`, file-local `namespace {}`) that runs before `main()`, same shape as the existing `TestSignalInit.cpp`. Nothing in this task changes any function signature or type used elsewhere.

**Why this task grew from "flip one line" (round 1 of plan review):** `tests/CMakeLists.txt:355-384` currently wraps ALL of the following in `if(NOT WIN32)`, purely because there was no point compiling it while the pool itself was Windows-disabled:
- re-adding the `materializr` app target's own sources onto `test_parallel_mesh` (via `get_target_property(materializr SOURCES)`, filtered to `src/` and deduplicated against what `materializr_core` already has) - this is what actually supplies `Application.cpp`, `ShapeRenderer.cpp`, etc. to the test binary;
- `tests/parallel_mesh_gl_sinks.cpp` - stub definitions of the GL entry points (`glGenVertexArrays` etc., see the file) so the real `ShapeRenderer` links without a graphics context;
- a second, separately-compiled copy of `ParallelMesh.cpp` built with `MZR_PARALLEL_MESH_TESTING`;
- the `MZR_PARALLEL_MESH_TESTING` compile definition itself, which is what makes `Application.h:120`'s `friend struct ParallelMeshTestAccess` and `Application.cpp:213`'s headless-constructor early-return branch exist at all - without it, `test_parallel_mesh.cpp`'s `ParallelMeshTestAccess` struct fails to compile (no friendship) and nothing supplies a headless `Application`.

The `materializr`/`materializr_core` app sources and the `MZR_PARALLEL_MESH_TESTING` code paths are not Windows-specific in what they do (the underlying sources already build and ship for Windows today as the real shipping app), so those two are low-risk to un-gate.

**Correction from round 2 of plan review - the GL sinks are the one piece that is genuinely NOT portable as written, and needs new code, not just un-gating.** `src/gl_common.h:50-51` includes `<GL/glew.h>` on `_WIN32` specifically because `opengl32.dll` only exports GL 1.1; everything from GL 1.2 onward (which is most of what `tests/parallel_mesh_gl_sinks.cpp` stubs) is reached through GLEW, and GLEW does **not** export those names as ordinary linkable symbols. Downloaded and inspected the real GLEW 2.3.1 header to confirm exactly how: `#define glGenVertexArrays GLEW_GET_FUN(__glewGenVertexArrays)`, where `__glewGenVertexArrays` is a global `PFNGLGENVERTEXARRAYSPROC` function-pointer variable that only `glewInit()` populates (from a live GL context - never called in this headless path), and `GLEW_GET_FUN(x)` expands to plain `x` in GLEW's default (non-`GLEW_MX`) build. So on Windows, `ShapeRenderer.cpp`'s calls to e.g. `glGenVertexArrays(...)` are calls *through that pointer*, not calls to a symbol named `glGenVertexArrays` - a same-named function definition in `parallel_mesh_gl_sinks.cpp` compiles and links (it's just an unrelated, uncalled symbol) but is never invoked; the real, null, never-initialized `__glewGenVertexArrays` pointer gets dereferenced instead, crashing the test process the moment `Viewport`'s or `ShapeRenderer`'s construction touches it. Confirmed against the real GLEW 2.3.1 release header (`vcpkg.json` has no version override for `glew`, so this project floats on vcpkg's baseline - GLEW's `__glew*`/`GLEW_GET_FUN` architecture has been stable for well over a decade, so this is expected to still match, but that specific pinned version was not itself inspected).

Four of the stubbed functions are different again: `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameteri` are GL 1.1 core, not behind GLEW's `__glew*` indirection on Windows - but they are **not** safe to redefine either. Checked the same downloaded header: `<GL/glew.h>` declares them `GLAPI void GLAPIENTRY glGenTextures(...)` etc., where `GLAPI` expands to `WINGDIAPI` on Windows, which is `#define WINGDIAPI __declspec(dllimport)`. `parallel_mesh_gl_sinks.cpp` includes `gl_common.h` → transitively `<GL/glew.h>` on `_WIN32` - so in that same translation unit these 4 names are already declared `dllimport`, and MSVC rejects a same-TU *definition* of a `dllimport` function outright (error C2491) - a compile failure, not the silent dead-code/null-pointer problem the other 17 have (`tests/parallel_mesh_gl_sinks.cpp` stubs 21 functions total; 4 are GL 1.1, the remaining 17 are GLEW-indirected on Windows). There is also nothing to gain by fighting this: the real `opengl32.dll` these link against exports GL 1.1 as ordinary no-op-without-a-context calls (the same thing every OTHER GL 1.1 call already relies on in the existing macOS/Linux sink file, which stubs only the newer functions and leaves GL 1.1 calls like `glViewport`/`glDelete*` to reach the real, context-less, harmless system implementation). So on Windows, these 4 simply should not be redefined at all - let them reach the real `opengl32.dll`.

**Second correction, found while writing this plan's code (checked, not merely suspected):** the natural first fix - decorate the existing 4 GL-1.1 functions with a calling-convention macro and let them compile everywhere - does not work. `parallel_mesh_gl_sinks.cpp` includes `gl_common.h`, which pulls in `<GL/glew.h>` on `_WIN32`; that header declares `glGenTextures`/`glBindTexture`/`glTexImage2D`/`glTexParameteri` as `GLAPI void GLAPIENTRY glGenTextures(...)`, where `GLAPI` expands to `WINGDIAPI` on Windows, which is `#define WINGDIAPI __declspec(dllimport)` (confirmed against the same downloaded header). Defining a function MSVC has already seen declared `dllimport` in the same translation unit is a hard compile error (C2491), not a linking or runtime concern. There is also no need to fight this: `opengl32.dll` (linked via the copied `OpenGL::GL`) exports real GL 1.1 entry points that are simply no-ops without a current rendering context - exactly what the existing macOS/Linux sink file already relies on for every OTHER GL 1.1 call it does *not* stub (`glViewport`, `glDelete*`, etc.). So on Windows, these 4 functions should not be redefined at all.

Step 3 below reflects this: `parallel_mesh_gl_sinks.cpp` is compiled only on non-Windows (unchanged from today), and a new Windows-only file supplies just the 17 GLEW-indirected reassignments. Even with the exact GLEW symbol names confirmed against a real downloaded header, none of this has ever been compiled or linked on MSVC - treat it as the most likely source of Windows CI failures in Step 9, not a settled fact.

- [ ] **Step 1: Create a dedicated branch for this task, off Commit A**

```bash
git checkout -b windows/enable-parallel-mesh-pool
```

- [ ] **Step 2: Drop the Windows exclusion from the test-wiring block**

In `tests/CMakeLists.txt`, change:
```cmake
if(NOT WIN32)
    get_target_property(_parmesh_app_sources materializr SOURCES)
    ...
    target_link_libraries(test_parallel_mesh PRIVATE
        $<TARGET_PROPERTY:materializr,LINK_LIBRARIES>)
endif()
```
to remove the `if(NOT WIN32)` / `endif()` wrapper entirely (de-indent the body), so this wiring runs unconditionally - mirroring the header's new `#if !defined(MZ_MOBILE)` (this desktop `CMakeLists.txt` has no mobile build path at all, so "unconditional" here is the correct match, not an oversight). Update the block's leading comment (currently just above line 355) to say this now applies on Windows too, and why (see this task's background above), rather than leaving stale reasoning that implies Windows is still excluded.

One line inside that block needs to become platform-conditional rather than simply unconditional - change:
```cmake
target_sources(test_parallel_mesh PRIVATE parallel_mesh_gl_sinks.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/ParallelMesh.cpp)
```
to:
```cmake
target_sources(test_parallel_mesh PRIVATE
    ${CMAKE_SOURCE_DIR}/src/viewport/ParallelMesh.cpp)
if(WIN32)
    target_sources(test_parallel_mesh PRIVATE parallel_mesh_gl_sinks_win32.cpp)
else()
    target_sources(test_parallel_mesh PRIVATE parallel_mesh_gl_sinks.cpp)
endif()
```
`parallel_mesh_gl_sinks.cpp` itself is unchanged - it keeps compiling on macOS/Linux exactly as it does today.

- [ ] **Step 3: Add the Windows GLEW pointer-reassignment sink**

Create `tests/parallel_mesh_gl_sinks_win32.cpp` - this supplies ONLY the 17 functions GLEW indirects on Windows; it does not touch the 4 GL-1.1 functions at all (see this task's background: those must reach the real `opengl32.dll`, unstubbed):

```cpp
// Windows-only. gl_common.h routes everything past GL 1.1 through GLEW on
// this platform (opengl32.dll only exports GL 1.1) - the plain-function
// stubs in parallel_mesh_gl_sinks.cpp are invisible to those calls, which go
// through GLEW's __glew* function-pointer globals instead
// (#define glGenVertexArrays GLEW_GET_FUN(__glewGenVertexArrays), and
// GLEW_GET_FUN(x) is plain x in GLEW's default build - confirmed against the
// real GLEW 2.3.1 release header; this project floats on vcpkg's baseline
// GLEW version, which was not itself inspected, but this architecture has
// been stable for well over a decade). glewInit() would normally populate
// these pointers from a live GL context, which the headless test path never
// creates - so they start out null, and the first call through one of them
// crashes. This static initializer points them at the same no-op sinks the
// GL 1.1 functions get from parallel_mesh_gl_sinks.cpp, before any
// Viewport/ShapeRenderer construction can run.
#include "gl_common.h"

namespace {
void APIENTRY sinkGenVertexArrays(GLsizei n, GLuint* ids) { while (n--) *ids++ = 0; }
void APIENTRY sinkGenBuffers(GLsizei n, GLuint* ids) { while (n--) *ids++ = 0; }
void APIENTRY sinkBindVertexArray(GLuint) {}
void APIENTRY sinkBindBuffer(GLenum, GLuint) {}
void APIENTRY sinkBufferData(GLenum, GLsizeiptr, const void*, GLenum) {}
void APIENTRY sinkEnableVertexAttribArray(GLuint) {}
void APIENTRY sinkVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) {}
void APIENTRY sinkDeleteVertexArrays(GLsizei, const GLuint*) {}
void APIENTRY sinkDeleteBuffers(GLsizei, const GLuint*) {}
void APIENTRY sinkGenFramebuffers(GLsizei n, GLuint* ids) { while (n--) *ids++ = 0; }
void APIENTRY sinkBindFramebuffer(GLenum, GLuint) {}
void APIENTRY sinkFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint) {}
void APIENTRY sinkGenRenderbuffers(GLsizei n, GLuint* ids) { while (n--) *ids++ = 0; }
void APIENTRY sinkBindRenderbuffer(GLenum, GLuint) {}
void APIENTRY sinkRenderbufferStorage(GLenum, GLenum, GLsizei, GLsizei) {}
void APIENTRY sinkRenderbufferStorageMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei) {}
void APIENTRY sinkFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint) {}

struct InstallGlewSinks {
    InstallGlewSinks() {
        __glewGenVertexArrays = sinkGenVertexArrays;
        __glewGenBuffers = sinkGenBuffers;
        __glewBindVertexArray = sinkBindVertexArray;
        __glewBindBuffer = sinkBindBuffer;
        __glewBufferData = sinkBufferData;
        __glewEnableVertexAttribArray = sinkEnableVertexAttribArray;
        __glewVertexAttribPointer = sinkVertexAttribPointer;
        __glewDeleteVertexArrays = sinkDeleteVertexArrays;
        __glewDeleteBuffers = sinkDeleteBuffers;
        __glewGenFramebuffers = sinkGenFramebuffers;
        __glewBindFramebuffer = sinkBindFramebuffer;
        __glewFramebufferTexture2D = sinkFramebufferTexture2D;
        __glewGenRenderbuffers = sinkGenRenderbuffers;
        __glewBindRenderbuffer = sinkBindRenderbuffer;
        __glewRenderbufferStorage = sinkRenderbufferStorage;
        __glewRenderbufferStorageMultisample = sinkRenderbufferStorageMultisample;
        __glewFramebufferRenderbuffer = sinkFramebufferRenderbuffer;
    }
};
// Static-init, same pattern as TestSignalInit.cpp: runs before main(), so
// the pointers are live before Application's headless constructor (which
// builds a Viewport/ShapeRenderer) ever executes.
const InstallGlewSinks g_installGlewSinks;
} // namespace
```

(The CMake wiring for this file was already added in Step 2's edit - nothing further to wire here.)

- [ ] **Step 4: Flip the header guard**

```cpp
// Checked for the one sharing pattern this codebase actually produces
// (SplitBodyOp's uncopied siblings; see claimShape in ParallelMesh.cpp) and
// for one concurrent-allocator hazard (BRepMeshData_Model's private,
// thread-safe allocator; OSD::SetThreadLocalSignal on each worker),
// against the installed macOS/Linux OCCT builds, and against Windows via a
// source/config-level audit (vcpkg's pinned 7.9.3 portfile, Microsoft's own
// CRT heap thread-safety guarantee, and a real hardware-fault test exercised
// on Windows CI - see windows-parallel-mesh-audit.md project memory). This
// is not a guarantee against every possible OCCT-internal concurrency
// hazard, only the ones checked. Mobile builds were never checked at all -
// they still fall back to the existing sequential loop. Named once here so
// every #if that gates the pool (this header, ParallelMesh.cpp,
// Application.cpp, the tests) shares one definition rather than repeating
// the raw condition.
#if !defined(MZ_MOBILE)
#define MZR_PARALLEL_MESH_SUPPORTED 1
#endif
```

(Only the condition changes, from `!defined(_WIN32) && !defined(MZ_MOBILE)` to `!defined(MZ_MOBILE)`; the comment is updated to stop saying Windows is unverified.)

- [ ] **Step 5: Confirm the mobile `#else` branch still reads correctly**

Read `tests/test_parallel_mesh.cpp:536-543` (`PlatformFallsBackWithoutLaunching`, in the `#else` branch). After this change it compiles only when `MZ_MOBILE` is defined - confirm that is the intended remaining scope (it is: mobile has no CI and was never in scope for this plan) and that nothing else in the codebase's `#else` branches assumed "not-Windows-and-not-mobile" vs. "just not mobile" in a way this narrows incorrectly. Search for other sites sharing the raw condition:

```bash
grep -rn "MZR_PARALLEL_MESH_SUPPORTED\|defined(_WIN32) && !defined(MZ_MOBILE)" src/ tests/ | grep -v ParallelMesh.h
```
Every other site should reference `MZR_PARALLEL_MESH_SUPPORTED` (the macro), not repeat the raw platform condition - confirm this holds; if any site duplicates the raw condition instead of using the macro, add it to this task's scope and fix it here rather than leaving a mismatched gate.

- [ ] **Step 6: Local build sanity check (macOS)**

```bash
cmake --build build --target materializr test_parallel_mesh -j 8
./build/tests/test_parallel_mesh
```
Expected: unchanged - the macOS behavior of `MZR_PARALLEL_MESH_SUPPORTED` was already true and stays true; this step just confirms the header/CMake edits didn't break anything on the platform that can actually be checked locally (e.g. a stray paren, or the de-indented CMake block losing a line).

- [ ] **Step 7: Commit B**

```bash
git add src/viewport/ParallelMesh.h tests/CMakeLists.txt tests/parallel_mesh_gl_sinks.cpp tests/parallel_mesh_gl_sinks_win32.cpp
git commit -m "$(cat <<'EOF'
app: Enable the parallel load-mesh pool on Windows

MZR_PARALLEL_MESH_SUPPORTED was gated off for _WIN32 pending: (a) proof a
worker thread's hardware fault is actually contained under Windows's
/EHa-enabled exception handling, and (b) an OCCT-allocator concurrency
audit. Both are now done - (a) by the hardware-fault test added in the
prior commit, about to get its first real Windows CI run; (b) by a
source/config-level audit (see windows-parallel-mesh-audit.md project
memory) since no Windows binary disassembly was possible from this
development machine.

tests/CMakeLists.txt's test_parallel_mesh wiring (app sources, GL sinks,
MZR_PARALLEL_MESH_TESTING) was also excluded on Windows purely as a side
effect of the pool itself being disabled there - ungated it in the same
commit since flipping the header guard alone would leave that target
unable to compile/link on Windows at all.

The existing GL sinks (parallel_mesh_gl_sinks.cpp) only work because
macOS/Linux export post-1.1 GL entry points as ordinary linkable symbols.
Windows routes those through GLEW's function-pointer globals instead, so
a same-named function definition is simply never called - added
parallel_mesh_gl_sinks_win32.cpp, compiled only on Windows, to reassign
GLEW's __glew* pointers directly for those 17 functions. The remaining 4
(GL 1.1, not behind GLEW) are declared dllimport by <GL/glew.h> on
Windows and cannot be redefined at all without a compile error - left
unstubbed there, reaching the real opengl32.dll, whose GL 1.1 entry
points are harmless no-ops without a current rendering context (the same
thing every OTHER GL 1.1 call already relies on in the unchanged
non-Windows sink file).

Checked for the common vcpkg pitfall of SDL2::SDL2main duplicating
gtest_main's entry point when materializr's LINK_LIBRARIES get copied
onto this test target: this project links only SDL2::SDL2, no
SDL2main reference exists anywhere in CMakeLists.txt, so it does not
apply here.

This activates all 15 tests in test_parallel_mesh.cpp on Windows CI for
the first time, including a headless Application construction/destruction
this project has never run on Windows before. Not landing on main until
that run is green.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Push the branch and get a real Windows CI run**

```bash
git push upstream windows/enable-parallel-mesh-pool
gh pr create --repo materializr-cad/materializr \
  --base main --head windows/enable-parallel-mesh-pool \
  --title "app: Enable the parallel load-mesh pool on Windows" \
  --body "Draft PR to trigger windows.yml's pull_request check. Not for merge until Windows Actions is green. See the two commits' messages and windows-parallel-mesh-audit.md for the full audit trail." \
  --draft
```
`windows.yml` triggers on `pull_request: branches: [main]`, so opening this PR (even in draft) starts a real Windows Actions run without touching `main`.

- [ ] **Step 9: Watch the run**

```bash
gh pr checks --repo materializr-cad/materializr windows/enable-parallel-mesh-pool --watch
```
Four outcomes, roughly in order of likelihood given the GL-sink reassignment in Step 3 is the least-certain part of this plan:
1. **CMake/link failure in the newly-ungated test wiring** (e.g. a `__glew*` symbol name mismatch against the actual vcpkg-pinned GLEW version, or a `get_target_property` genexpr behaving differently under the Visual Studio generator). Read the actual linker/CMake error - do not guess; if it's a symbol-name mismatch, fetch the actual GLEW header vcpkg installs for this build (the CI log or a vcpkg cache path will show the exact version) rather than assuming 2.3.1's names still apply. Fix, push, re-watch.
2. **Runtime crash from a GLEW pointer still being null** (Step 3's reassignment missed a function `ShapeRenderer`/`Viewport` actually calls in the headless path, or ran after construction instead of before via static-init ordering). Read the crash location, add the missing pointer assignment, push, re-watch.
3. **Compiles but a test fails for an unrelated reason.** Read which test and how (assertion failure vs. process crash/timeout). A crash specifically in `RealHardwareFaultInWorkerIsContainedNotFatal` would mean fault containment does not actually work on Windows and the guard must be reverted, not patched around - report this to the user rather than trying to force a pass.
4. **Green.** Proceed to Step 10.

Budget 2-3 push-and-watch rounds for the header/pool-logic concerns this plan already vetted, but expect the GL-sink reassignment (Step 3) may need additional rounds specifically to get right under MSVC - it was designed against a downloaded reference GLEW header, not compiled or linked against the project's actual vcpkg-pinned version, and this is a genuinely new mechanism with no precedent elsewhere in this codebase. Each round is a full OCCT vcpkg build unless the cache hits.

- [ ] **Step 10: Human gate**

Report to the user: the Windows Actions run URL and result, the full diff, and ask whether to merge the PR or close it and land Commit B via their usual direct-push workflow instead (their call - this only deviated from direct-push for the CI-gate reason above, not as a standing change to how this project ships). Do not push Commit B to `main` without an explicit yes.

## Out of scope

- Mobile (`MZ_MOBILE`) - no CI, not investigated, guard condition for it is untouched.
- Any change to `ParallelMesh.cpp`'s actual pool logic, `claimShape`, or the allocator audit itself - those are already done/shipped.
- A real Windows binary disassembly - not possible from this (macOS) session; the audit and this plan's CI run are the closest available substitutes.
