# Materializr

**Open-source parametric 3D CAD.**

Materializr is a desktop CAD application for solid modelling with a
constraint-based 2D sketcher, a linear parametric history, and a unified
push/pull workflow — built on OpenCASCADE for the geometry kernel, Dear ImGui
for the interface, and OpenGL for rendering. Single binary, no install on
Linux, MSI/zip on Windows.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## What's inside

- 2D constraint-based sketcher with general region detection — intersecting
  and overlapping shapes both pick up as individually selectable regions.
- Modelling: push/pull, extrude, revolve, sweep, loft, fillet, chamfer, shell,
  booleans, mirror, linear/radial pattern, split, copy, delete, align.
- Interactive gizmos for translate / rotate / scale with live mm / ° / %
  measurements and grid-snap.
- Linear undoable history with editable steps, breakpoints, delete-with
  -conflict-detection, and project-file persistence.
- STEP / IGES / STL / DXF / SVG / glTF I/O and a native `.materializr`
  project format that stores bodies + colours + sketches + history.
- PBR rendering with material presets, dark/light themes, and a dockable UI.

## Install

**Linux (AppImage)** — single portable binary:

```bash
chmod +x Materializr-x86_64.AppImage
./Materializr-x86_64.AppImage
```

**Windows** — `Materializr-windows-x64.zip` (portable) or
`Materializr-Setup.exe` (NSIS installer).

Latest builds are on the
[releases page](https://github.com/materializr-cad/materializr/releases).

## Documentation

- **[Getting Started](docs/getting-started.md)** — install + your first sketch
  in five minutes.
- **[Features](docs/features.md)** — full list of what every tool does.
- **[Usage Guide](docs/usage.md)** — workflow recipes and keyboard shortcuts.
- **[Building from Source](docs/building.md)** — native Linux, Docker AppImage,
  and Windows (MSVC + vcpkg).
- **[Architecture](docs/architecture.md)** — code layout, design patterns,
  tech stack.

The app also ships an in-app **Help → User Guide**, a **Keyboard Shortcuts**
panel, and **Help → Check for Updates** that queries this repo's releases API.

## License

MIT — see [LICENSE](LICENSE).

## Contributing

Contributions welcome. Please open an issue first to discuss substantial
changes; for small fixes, a PR is fine.

## Credits

- **R4stl1n** — original project.
- **stevebushwa** — design, testing, direction.
- **Claude (Anthropic)** — pair-coding collaborator.

## Acknowledgments

- [OpenCASCADE Technology](https://dev.opencascade.org/) — geometry kernel
- [Dear ImGui](https://github.com/ocornut/imgui) — immediate-mode GUI
- [GLFW](https://www.glfw.org/) — windowing and input
- [GLM](https://github.com/g-truc/glm) — math library
- [libcurl](https://curl.se/libcurl/) — update checking
