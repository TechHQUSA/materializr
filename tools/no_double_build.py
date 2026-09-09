#!/usr/bin/env python3
"""Fail if an OCCT boolean is performed twice for one result.

The two-shape constructor of BRepAlgoAPI_Cut/Fuse/Common is marked "Obsolete"
in OCCT's own header and it PERFORMS the operation. So this shape

    BRepAlgoAPI_Cut cut(body, tool);
    cut.SetFuzzyValue(1.0e-4);
    cut.Build();

runs the whole boolean twice for a byte-identical result, and every setter
written between the two lines reaches only the second run. Both halves are
invisible at review time: the code reads exactly like the correct thing.

Thirty-two sites had drifted into it before this gate existed, costing a
second full boolean each (927 ms per cut on the 300-hole plate) and silently
voiding eight SetFuzzyValue calls. Use materializr::setBooleanShapes from
modeling/BoolArgs.h, which constructs empty and declares the operands, so one
Build() runs one boolean with the setters in effect.

A two-shape constructor with NO trailing Build() is fine and not reported:
that is a single build, just written through the obsolete spelling.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTOR = re.compile(r'BRepAlgoAPI_(Cut|Fuse|Common|Section)\s+([A-Za-z_]\w*)\s*\(')


def statement_end(text, open_paren):
    """Index of the ')' closing the argument list that starts at open_paren."""
    depth = 0
    for i in range(open_paren, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return i
    return None


def top_level_arg_count(args):
    depth = 0
    n = 1
    for ch in args:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif ch == ',' and depth == 0:
            n += 1
    return n


def scan(path):
    lines = path.read_text(encoding='utf-8', errors='replace').split('\n')
    out = []
    for idx, line in enumerate(lines):
        stripped = line.lstrip()
        if stripped.startswith('//') or stripped.startswith('*'):
            continue
        m = CTOR.search(line)
        if not m:
            continue
        var = m.group(2)
        window = '\n'.join(lines[idx:idx + 20])
        start = window.index('(', m.start(2) - m.start())
        end = statement_end(window, start)
        if end is None:
            continue
        if top_level_arg_count(window[start + 1:end]) < 2:
            continue          # already the empty-construct form
        after = window[end + 1:]
        if re.search(re.escape(var) + r'\s*\.\s*Build\s*\(\s*\)', after):
            out.append((idx + 1, var))
    return out


def main():
    hits = []
    for path in sorted((ROOT / 'src').rglob('*')):
        if path.suffix in ('.cpp', '.h', '.hxx'):
            for ln, var in scan(path):
                hits.append((path.relative_to(ROOT), ln, var))
    if not hits:
        print('no double-built booleans')
        return 0
    print('Boolean performed twice for one result '
          '(two-shape constructor plus an explicit Build):\n')
    for rel, ln, var in hits:
        print(f'  {rel}:{ln}  {var}')
    print('\nConstruct empty and use materializr::setBooleanShapes '
          '(modeling/BoolArgs.h),\nso the single Build() runs one boolean '
          'with the setters in effect.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
