#!/usr/bin/env python3
"""Fails if CurVersion (src/aboutdialog.cpp) and the top CHANGELOG.md entry differ."""
import re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
code = re.search(r'QString\s+CurVersion\s*=\s*"([^"]+)"', (root / "src/aboutdialog.cpp").read_text())
log = re.search(r'^## \[([^\]]+)\]', (root / "CHANGELOG.md").read_text(), re.M)
if not code or not log:
    sys.exit("could not find CurVersion or a CHANGELOG entry")
if not re.fullmatch(r"\d+\.\d+\.\d+", code.group(1)):
    sys.exit(f"CurVersion {code.group(1)!r} is not MAJOR.MINOR.PATCH")
if code.group(1) != log.group(1):
    sys.exit(f"CurVersion {code.group(1)} != CHANGELOG top entry {log.group(1)}")
print("version ok:", code.group(1))
