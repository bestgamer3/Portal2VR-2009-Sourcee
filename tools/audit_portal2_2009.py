#!/usr/bin/env python3
"""Audit a Portal 2 PE32 build against Portal2VR's current function signatures.

Usage:
  py tools/audit_portal2_2009.py "C:/path/to/Portal 2/bin" --source .

No game binaries are modified. The script scans only executable PE sections.
"""
from __future__ import annotations

import argparse
import datetime as dt
import re
import struct
import sys
from pathlib import Path

MODULE_FILES = {
    "client.dll": ("client.dll", "Client.dll"),
    "engine.dll": ("engine.dll", "Engine.dll"),
    "server.dll": ("server.dll", "Server.dll"),
    "materialsystem.dll": ("materialsystem.dll", "MaterialSystem.dll"),
    "vgui2.dll": ("vgui2.dll", "VGUI2.dll"),
    "vguimatsurface.dll": ("vguimatsurface.dll", "VGUIMatSurface.dll"),
}
INTERFACES = {
    "client.dll": ["VClientEntityList003", "VEngineRenderView013"],
    "engine.dll": ["EngineTraceClient003", "VEngineClient013", "VEngineRenderView013",
                   "VModelInfoClient004", "VEngineModel016"],
    "materialsystem.dll": ["VMaterialSystem080"],
    "vgui2.dll": ["VGUI_InputInternal001"],
    "vguimatsurface.dll": ["VGUI_Surface031"],
}


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def read_pe(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"{path.name}: missing DOS MZ header")
    pe = u32(data, 0x3C)
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path.name}: missing PE signature")
    machine = u16(data, pe + 4)
    section_count = u16(data, pe + 6)
    timestamp = u32(data, pe + 8)
    optional_size = u16(data, pe + 20)
    opt = pe + 24
    magic = u16(data, opt)
    if magic != 0x10B:
        raise ValueError(f"{path.name}: not PE32 (optional header magic 0x{magic:X})")
    image_base = u32(data, opt + 28)
    size_of_image = u32(data, opt + 56)
    export_rva = u32(data, opt + 96)
    export_size = u32(data, opt + 100)
    section_table = opt + optional_size
    sections = []
    for i in range(section_count):
        off = section_table + 40 * i
        name = data[off:off + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size, virtual_address, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        characteristics = u32(data, off + 36)
        sections.append({"name": name, "va": virtual_address, "vsize": virtual_size,
                         "raw_size": raw_size, "raw_ptr": raw_ptr, "chars": characteristics})

    def rva_to_file(rva: int) -> int | None:
        for section in sections:
            span = max(section["vsize"], section["raw_size"])
            if section["va"] <= rva < section["va"] + span:
                delta = rva - section["va"]
                if delta >= section["raw_size"]:
                    return None
                result = section["raw_ptr"] + delta
                return result if result < len(data) else None
        if 0 <= rva < len(data):
            return rva
        return None

    exports: set[str] = set()
    if export_rva and export_size:
        eoff = rva_to_file(export_rva)
        if eoff is not None and eoff + 40 <= len(data):
            number_of_names = u32(data, eoff + 24)
            names_rva = u32(data, eoff + 32)
            names_off = rva_to_file(names_rva)
            if names_off is not None:
                for i in range(min(number_of_names, 20000)):
                    name_rva = u32(data, names_off + i * 4)
                    name_off = rva_to_file(name_rva)
                    if name_off is None:
                        continue
                    end = data.find(b"\0", name_off, min(len(data), name_off + 512))
                    if end != -1:
                        exports.add(data[name_off:end].decode("ascii", "replace"))

    return {"path": path, "data": data, "machine": machine, "timestamp": timestamp,
            "image_base": image_base, "size_of_image": size_of_image,
            "sections": sections, "exports": exports}


def find_module(folder: Path, names: tuple[str, ...]) -> Path | None:
    by_lower = {p.name.lower(): p for p in folder.iterdir() if p.is_file()}
    for name in names:
        if name.lower() in by_lower:
            return by_lower[name.lower()]
    return None


def active_offset_patterns(offsets_file: Path) -> list[tuple[str, str, int, list[int | None], int]]:
    code = offsets_file.read_text(encoding="utf-8", errors="replace")
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    declaration = re.compile(
        r'Offset\s+(\w+)\s*=\s*\{\s*"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*"([^"]+)"(?:\s*,\s*(\d+))?\s*\}',
        re.S)
    items = []
    for name, module, offset, signature, sig_offset in declaration.findall(code):
        pattern = [None if token in ("?", "??") else int(token, 16)
                   for token in signature.split()]
        items.append((name, module.lower(), int(offset, 16), pattern, int(sig_offset or 0)))
    return items


def scan_text(image: dict, pattern: list[int | None]) -> list[int]:
    data = image["data"]
    sections = image["sections"]
    best_start = 0
    best = b""
    i = 0
    while i < len(pattern):
        if pattern[i] is None:
            i += 1
            continue
        j = i
        while j < len(pattern) and pattern[j] is not None:
            j += 1
        if j - i > len(best):
            best_start = i
            best = bytes(pattern[i:j])
        i = j
    if not best:
        return []

    matches: list[int] = []
    for section in sections:
        if not (section["chars"] & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
            continue
        # Only actual file bytes are searched; no zero-filled virtual tail.
        start = section["raw_ptr"]
        size = min(section["raw_size"], section["vsize"] or section["raw_size"])
        block = data[start:start + size]
        search_at = 0
        while True:
            hit = block.find(best, search_at)
            if hit < 0:
                break
            candidate = hit - best_start
            if candidate >= 0 and candidate + len(pattern) <= len(block):
                if all(wanted is None or block[candidate + k] == wanted
                       for k, wanted in enumerate(pattern)):
                    matches.append(section["va"] + candidate)
                    if len(matches) > 1:
                        return matches
            search_at = hit + 1
    return matches


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary_dir", type=Path, help="Folder containing the Portal 2 DLLs")
    parser.add_argument("--source", type=Path, default=Path.cwd(), help="Project root (default: current folder)")
    parser.add_argument("--out", type=Path, help="Optional report output path")
    args = parser.parse_args()

    report: list[str] = []
    report.append("PORTAL 2 PE32 / FUNCTION SIGNATURE AUDIT")
    report.append("========================================")
    report.append(f"Binary folder: {args.binary_dir.name} (folder supplied by caller)")
    report.append("")

    images: dict[str, dict] = {}
    for logical_name, names in MODULE_FILES.items():
        path = find_module(args.binary_dir, names)
        if not path:
            report.append(f"{logical_name}: NOT FOUND (tried {', '.join(names)})")
            continue
        try:
            image = read_pe(path)
        except Exception as exc:
            report.append(f"{logical_name}: INVALID - {exc}")
            continue
        images[logical_name] = image
        stamp = dt.datetime.fromtimestamp(image["timestamp"], tz=dt.timezone.utc)
        arch = "PE32/i386" if image["machine"] == 0x14C else f"machine 0x{image['machine']:04X}"
        report.append(f"{logical_name}: {arch}; timestamp {stamp:%Y-%m-%d %H:%M:%S} UTC; "
                      f"ImageBase 0x{image['image_base']:08X}; SizeOfImage 0x{image['size_of_image']:X}")
        report.append(f"  CreateInterface export: {'yes' if 'CreateInterface' in image['exports'] else 'no'}")
        raw = image["data"]
        for interface in INTERFACES.get(logical_name, []):
            present = interface.encode("ascii") in raw
            report.append(f"  interface string {interface}: {'present' if present else 'not found'}")

    offsets_file = args.source / "L4D2VR" / "offsets.h"
    if not offsets_file.exists():
        offsets_file = args.source / "portal2-2009" / "L4D2VR" / "offsets.h"
    exe_path = find_module(args.binary_dir, ("hl2.exe", "HL2.exe"))
    if exe_path:
        try:
            exe_image = read_pe(exe_path)
            stamp = dt.datetime.fromtimestamp(exe_image["timestamp"], tz=dt.timezone.utc)
            report.append(f"hl2.exe: PE32/i386; timestamp {stamp:%Y-%m-%d %H:%M:%S} UTC; "
                          f"ImageBase 0x{exe_image['image_base']:08X}; SizeOfImage 0x{exe_image['size_of_image']:X}")
        except Exception as exc:
            report.append(f"hl2.exe: INVALID - {exc}")

    report.append("")
    report.append("ACTIVE FUNCTION SIGNATURES (executable sections only)")
    report.append("------------------------------------------------------")
    if not offsets_file.exists():
        report.append(f"Could not find offsets.h under source root {args.source}")
    else:
        for name, module, old_rva, pattern, sig_offset in active_offset_patterns(offsets_file):
            image = images.get(module)
            if not image:
                report.append(f"{name}: {module} missing")
                continue
            matches = scan_text(image, pattern)
            old_start = old_rva - sig_offset
            old_match = old_start >= 0 and (old_start in matches)
            if not old_match:
                # Avoid reporting a potentially large set of signature hits; scanner stops after 2.
                report.append(f"{name:34} old=0x{old_rva:06X}  matches={len(matches)} "
                              f"candidate(s)={', '.join(f'0x{x + sig_offset:X}' for x in matches) or '-'}")
            else:
                report.append(f"{name:34} old=0x{old_rva:06X}  exact signature match")

    report.append("")
    report.append("Notes:")
    report.append("- Interface strings are evidence of identifiers embedded in a module, not proof that")
    report.append("  CreateInterface accepts them or that the consuming SDK vtable layout is compatible.")
    report.append("- Signature candidates must be checked in disassembly and at runtime before hooking.")
    report.append("- This audit does not install hooks, modify binaries, or build a DLL.")

    output = "\n".join(report) + "\n"
    print(output, end="")
    if args.out:
        args.out.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
