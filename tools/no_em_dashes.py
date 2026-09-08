#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fail if any tracked file contains an em-dash.

Project rule: no em-dashes, anywhere - use a hyphen. Enforcing it needs a tool
rather than a habit, because the character has two spellings that no single
grep finds together: the literal U+2014, and the six-character escape
form a C++ string uses for the same three bytes. The escape form is the one that survived
every previous sweep, in 54 user-facing strings.

    python3 tools/no_em_dashes.py

Exits 1 and lists the sites. Wire it into CI as one step; there is nothing to
regenerate and nothing to keep in sync.
"""
import io, os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
# Built from code points rather than written out, so this file does not match
# its own search and fail on a clean tree. It did exactly that when first
# committed - the pre-commit run passed only because the file was still
# untracked and git ls-files skipped it, which is its own small lesson about
# testing a gate in the state it will actually run in.
_EM = chr(0x2014)
FORMS = (_EM,
         "".join("\\x%02X" % b for b in _EM.encode("utf-8")),   # upper-case escapes
         "".join("\\x%02x" % b for b in _EM.encode("utf-8")))   # lower-case escapes

def main():
    files = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).split("\n")
    bad = []
    for rel in filter(None, files):
        path = os.path.join(ROOT, rel)
        try:
            text = io.open(path, encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue          # a binary or unreadable file cannot carry source text
        for n, line in enumerate(text.split("\n"), 1):
            if any(f in line for f in FORMS):
                bad.append((rel, n, line.strip()[:100]))
    for rel, n, line in bad:
        print(f"{rel}:{n}: {line}")
    if bad:
        print(f"FAIL: {len(bad)} line(s) contain an em-dash; use a hyphen")
        return 1
    print("no em-dashes")
    return 0

if __name__ == "__main__":
    sys.exit(main())
