#!/usr/bin/env python3
"""Regenerate docs/units-audit.md and docs/units-audit-allow.txt.

Inventory for the display-units sweep: every numeric control and every `mm`
literal under src/, classified by dimension / class. The classifier is a
heuristic over the source line; rows it cannot decide from the line alone are
pinned in OVERRIDE. Anything left LENGTH? or READOUT-LITERAL is work.

    python3 tools/units_audit.py
"""
import collections, os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CONTROLS = r'InputFloat\(|InputDouble\(|InputScalar|SliderFloat\(|DragFloat\(|inputNumber\(|amountField\(|parseFinite\('
LITERALS = r'\bmm\b'
SKIP_CTRL = ("src/ui/NumField.h", "src/ui/LengthField.h", "src/core/NumParse.h", "src/ui/TouchWidgets", "src/core/Units.h")
SKIP_LIT  = ("src/core/Units.h", "src/core/LengthEdit.h", "src/ui/LengthField.h", "i18n_catalogue.h")

# (file, line) -> dimension, for rows the line alone does not reveal.
OVERRIDE = {
    ("src/ui/MaterialPanel.cpp", 100): "ratio",
    ("src/ui/MaterialPanel.cpp", 101): "ratio",
    ("src/app/Application_Dialogs.cpp", 362): "px/ui",     # grid line thickness
    ("src/app/Application_Dialogs.cpp", 1274): "angle",    # sketch pattern angle (touch)
    ("src/app/Application_Dialogs.cpp", 2784): "angle",    # plane rotation X/Y/Z
    ("src/app/Application_Dialogs.cpp", 2785): "angle",
    ("src/app/Application_Dialogs.cpp", 2786): "angle",
}

def grep(pattern):
    out = subprocess.run(["grep", "-rnE", pattern, "src/", "--include=*.cpp", "--include=*.h"],
                         cwd=ROOT, capture_output=True, text=True).stdout
    for l in out.splitlines():
        f, ln, code = l.split(":", 2)
        yield f, int(ln), code

def is_comment(code):
    c = code.strip(); return c.startswith("//") or c.startswith("*") or c.startswith("/*")
def before_comment(code):
    i = code.find("//"); return code if i < 0 else code[:i]

# Literals that legitimately keep the word "mm" (checked by hand). Keyed by a
# fragment of the line, not its number — numbers drift under every edit above.
LITERAL_ALLOW = [
    ("src/app/Application_Dialogs.cpp", "verify print scale",   "print scale bar: a physical 50 mm reference on paper"),
    ("src/app/Application_Dialogs.cpp", "a 50 mm scale bar",    "help text describing that scale bar"),
    ("src/app/Application_Dialogs.cpp", 'InputText("##mm"',     "ImGui widget id, not user-visible"),
    ("src/ui/Toolbar.cpp",              "10 mm / R5 mm shape",  "tooltip prose naming the primitive defaults"),
    ("src/modeling/SvgImport.cpp",      'nsvgParse(',           "the SVG file's own unit, not the display unit"),
    ("src/ui/TouchWidgets.h",           'const char* suffix = "mm"', "default parameter; length callers pass unitSuffix()"),
    ("src/modeling/FilletOp.cpp",       "%.2fx%.2fx%.2f mm",    "stderr diagnostic (continuation line)"),
    ("src/modeling/FilletOp.cpp",       "result volume ~= 0",   "stderr diagnostic"),
    ("src/modeling/ShellOp.cpp",        "(thickness %.3f mm)",  "stderr diagnostic"),
    ("src/modeling/ShellOp.cpp",        "failed at thickness",  "stderr diagnostic"),
    ("src/plugins/SvgImportPlugin.cpp", "on the ground plane",  "stderr diagnostic (continuation line)"),
]

def classify_literal(f, code, ln=None):
    if any(f == af and frag in code for af, frag, _ in LITERAL_ALLOW): return "allowed-by-hand"
    if is_comment(code) or not re.search(LITERALS, before_comment(code)): return "comment"
    if re.search(r"src/io/(Svg|Dxf|Stl|ThreeMf|Obj|Iges|Brep)", f) or "nanosvg" in f: return "export/import-format"
    if "fprintf" in code or "stderr" in code or "cerr" in code: return "diagnostic"
    if "ios_" in f or "mobile_files" in f: return "platform-string"
    if any(k in code for k in ("fmtLength", "fmtArea", "fmtVolume", "fmtVec3", "unitSuffix", "trFormat", "lengthText")): return "CONVERTED"
    if re.search(r'"[^"]*\bmm\b[^"]*"', before_comment(code)): return "READOUT-LITERAL"
    return "identifier/other"

def classify_control(f, ln, code):
    if (f, ln) in OVERRIDE: return OVERRIDE[(f, ln)]
    c = code.lower()
    if any(k in c for k in ("angle", "deg", "taper", "rotate", "sweep", "tilt", "draft", "rot")): return "angle"
    if "%%" in code or any(k in c for k in ("percent", "opacity", "alpha", "scale u", "scale v")): return "percent"
    if any(k in c for k in ("px", "pixel", "linewidth", "line width", "sensitivity", "uiscale")): return "px/ui"
    if any(k in c for k in ("sec", "seconds", "time", "interval", "probe")): return "seconds"
    if any(k in c for k in ("count", "copies", "sides", "segments", "instances", "turns", "starts", "inputnumberint")): return "count"
    if any(k in c for k in ("lengthfield", "lengthslider", "amountlengthfield", "parselength", "lengthtextfield")): return "CONVERTED"
    return "LENGTH?"

def main():
    ctrl = [(classify_control(f, ln, code), f, ln, code.strip())
            for f, ln, code in grep(CONTROLS) if not is_comment(code) and not any(s in f for s in SKIP_CTRL)]
    lit  = [(classify_literal(f, code, ln), f, ln, code.strip())
            for f, ln, code in grep(LITERALS) if not any(s in f for s in SKIP_LIT)]
    cc, lc = collections.Counter(r[0] for r in ctrl), collections.Counter(r[0] for r in lit)
    def row(d, f, ln, code):
        return "| %s | %s:%d | `%s` |\n" % (d, f, ln, code[:110].replace("|", "\\|"))
    with open(os.path.join(ROOT, "docs/units-audit.md"), "w") as o:
        o.write("# Display-units audit\n\nGenerated by `tools/units_audit.py`. Every numeric control and every `mm` literal in\n`src/`, classified. LENGTH? and READOUT-LITERAL rows are the work list: each ends up CONVERTED\nor in an allow class (comment, export/import-format, diagnostic, platform-string). The\nclassifier is heuristic; rows it cannot decide are pinned in the script's OVERRIDE map.\n\n")
        o.write("## Controls by dimension\n\n" + "".join("- %s: %d\n" % kv for kv in sorted(cc.items())) + "\n| dim | file:line | code |\n|---|---|---|\n")
        for r in sorted(ctrl, key=lambda r: (r[0] != "LENGTH?", r[1], r[2])): o.write(row(*r))
        o.write("\n## `mm` literals by class\n\n" + "".join("- %s: %d\n" % kv for kv in sorted(lc.items())) + "\n| class | file:line | code |\n|---|---|---|\n")
        for r in sorted(lit, key=lambda r: (r[0] != "READOUT-LITERAL", r[1], r[2])): o.write(row(*r))
    with open(os.path.join(ROOT, "docs/units-audit-allow.txt"), "w") as a:
        for d, f, ln, _ in lit:
            if d in ("comment", "export/import-format", "diagnostic", "platform-string", "identifier/other", "allowed-by-hand"):
                a.write("%s:%d:\n" % (f, ln))
    print("controls:", dict(cc)); print("literals:", dict(lc))
    return 0

if __name__ == "__main__":
    sys.exit(main())
