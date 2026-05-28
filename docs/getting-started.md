# Getting Started

This guide walks you from a fresh download to your first solid model in about
five minutes.

## Install

### Linux (AppImage)

The AppImage is a single-file portable binary — no install, no system
dependencies beyond OpenGL drivers:

```bash
chmod +x Materializr-x86_64.AppImage
./Materializr-x86_64.AppImage
```

Grab the latest build from the
[releases page](https://github.com/materializr-cad/materializr/releases).

### Windows

Two options on the same releases page:

- **`Materializr-windows-x64.zip`** — portable. Unzip anywhere, double-click
  `materializr.exe`. Settings and projects are written to `%APPDATA%` so the
  install location can stay read-only.
- **`Materializr-Setup.exe`** — NSIS installer. Adds a Start-menu shortcut and
  an uninstaller. Choose this if you'd rather Windows manage the install.

Windows SmartScreen may warn that the publisher is unrecognised (we don't
code-sign). Click *More info → Run anyway*.

### macOS

Not currently supported. The codebase is portable enough that a build is
feasible (see [building.md](building.md) for the obstacles), but no binaries
are produced yet.

## Your first model

The default workspace shows a 20 mm demo cube and three panels: the **Tools**
toolbar on the left, **Items** + **History** + **Properties** on the right,
and a status bar at the bottom. The cube is purely decorative — feel free to
delete it from the Items panel (press <kbd>Delete</kbd> with the body selected).

### 1. Start a sketch

With nothing selected, click **Sketch on XY** in the toolbar. The camera snaps
to a top-down orthographic view aligned to the world XY plane. A face-on grid
appears.

### 2. Draw a rectangle

Click **Rectangle**, then click two opposite corners on the grid. Or type a
size (e.g. `40`) right after the first click to lock the rectangle to that
side length in the direction of the cursor.

### 3. Finish the sketch

Click **Finish Sketch** (or press <kbd>Enter</kbd>). The sketch is saved into
the Items panel and is now selectable like any other geometry.

### 4. Extrude

Hover inside the rectangle until the region highlights cyan. Click it to
select. Then click **Push / Pull** in the toolbar. A green arrow appears
sticking out of the region — drag it (or type a value in the popup) to set the
height. Press <kbd>Enter</kbd> to confirm.

You now have a solid box.

### 5. Add a fillet

Click near one of the top edges (the cursor turns the edge green when it's
within 8 px). Click **Fillet**. A handle appears pointing outward from the
edge — drag it away from the edge to grow the radius (starts at 0.1 mm), or
type a value. <kbd>Enter</kbd> confirms.

### 6. Save

<kbd>Ctrl</kbd>+<kbd>S</kbd> → pick a name → done. The project file (`.materializr`) stores the
final geometry plus the operation history so you can reopen and continue.

## Where to go next

- **[features.md](features.md)** — what every tool does.
- **[usage.md](usage.md)** — workflow recipes (boolean ops, gizmos, sketches on
  faces, keyboard shortcuts).
- The in-app **Help → User Guide** is a condensed version of this guide.
