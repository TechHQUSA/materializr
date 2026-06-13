# Contributing to Materializr

Thanks for your interest! Materializr is parametric 3D CAD built on the
OpenCASCADE kernel with a Dear ImGui / SDL2 interface. It runs on Linux,
Windows, and Android from a single codebase.

## Reporting bugs / requesting features

Use the issue templates. For bugs, a small `.materializr` project (or STEP file)
that reproduces the problem is the most valuable thing you can attach. For
usage questions, prefer [Discussions](https://github.com/materializr-cad/materializr/discussions).

## Building

- **Desktop (Linux/Windows):** see [`BUILD.md`](BUILD.md). In short — configure a
  CMake build dir, build the `materializr` target, and run `ctest` for the unit
  suites. You need OpenCASCADE, SDL2, and a C++17 compiler.
- **Android:** see [`android/README.md`](android/README.md). `android/scripts/setup-deps.sh`
  fetches and SHA-256-verifies SDL2 / FreeType / OpenCASCADE and cross-compiles
  them for arm64, then `./gradlew assembleDebug` builds the APK.

Please make sure the desktop build compiles and `ctest` passes before opening a PR.

## Architecture conventions

A few rules keep the codebase coherent — PRs are reviewed against them:

- **Features live in plugins.** A new modeling operation, importer/exporter, or
  tool belongs in `src/plugins/` (see any existing `*Plugin.cpp` and
  `src/plugin/`), registered via `REGISTER_PLUGIN`. Don't bolt feature logic into
  the core `Application`. Generic *infrastructure* a plugin needs (a new
  contribution type, a render hook) can go in core, but the feature itself stays
  in its plugin.
- **Touch is a runtime mode, not a build flag.** Adaptations for touch screens
  go behind `materializr::touchMode()` (see `src/touch_mode.h`), never
  `#if defined(__ANDROID__)`. A tablet with a keyboard/mouse runs the desktop
  model, and a desktop with a touchscreen can opt in — so the behavior must be a
  runtime decision. Reserve `#if __ANDROID__` for genuinely platform-specific
  code (JNI, SAF, GL ES differences).
- **Don't break desktop.** Android/touch work must leave the desktop builds
  unchanged. The Linux/Windows CI runs on every push.
- **Match the surrounding style.** Comment density, naming, and idiom should
  read like the file you're editing.

## Pull requests

Keep them focused — one logical change per PR. Fill out the PR template,
including how you tested. By contributing you agree your work is licensed under
**GPL-3.0-or-later**, the project's license.
