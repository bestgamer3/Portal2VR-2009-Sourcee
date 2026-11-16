#!/usr/bin/env python3
"""Verify every active MinHook target is also enabled in Hooks::Hooks."""
from __future__ import annotations
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "L4D2VR" / "hooks.cpp").read_text(encoding="utf-8", errors="replace")
# Remove comments before comparing registrations.
source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
source = re.sub(r"//[^\n]*", "", source)
created = set(re.findall(r"\b(hk\w+)\.createHook\s*\(", source))
enabled = set(re.findall(r"\bENABLE_HOOK\s*\(\s*(hk\w+)\s*\)", source))
missing_enable = sorted(created - enabled)
without_target = sorted(enabled - created)

print(f"Active createHook targets: {len(created)}")
print(f"Enabled hook registrations: {len(enabled)}")
if missing_enable:
    print("ERROR: created hooks missing from enable list:")
    for name in missing_enable:
        print(f"  {name}")
if without_target:
    print("ERROR: enable list includes hooks without active createHook calls:")
    for name in without_target:
        print(f"  {name}")
if missing_enable or without_target:
    raise SystemExit(1)
print("PASS: every active created hook has a matching enable registration.")
