#!/usr/bin/env python3
"""Inspect MSVC x86 RTTI/vtables for known Portal 2 (July 2009) classes.

This is a static-analysis helper. It never modifies or executes the game binaries.
Usage: py tools/audit_rtti_targets.py <Client.dll> [--server <Server.dll>]
"""
from __future__ import annotations
import argparse
import struct
from pathlib import Path

EXECUTE = 0x20000000


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from('<H', data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from('<I', data, off)[0]


def pe32(path: Path) -> dict:
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError(f'{path}: no MZ header')
    pe = u32(data, 0x3C)
    if data[pe:pe+4] != b'PE\0\0' or u16(data, pe+4) != 0x14C:
        raise ValueError(f'{path}: not PE32/i386')
    nsec = u16(data, pe+6)
    opt_size = u16(data, pe+20)
    opt = pe+24
    if u16(data, opt) != 0x10B:
        raise ValueError(f'{path}: not PE32 optional header')
    image_base = u32(data, opt+28)
    section_table = opt+opt_size
    sections = []
    for i in range(nsec):
        off = section_table + i*40
        name = data[off:off+8].split(b'\0', 1)[0].decode('ascii', 'replace')
        vsize, rva, raw_size, raw_ptr = struct.unpack_from('<IIII', data, off+8)
        flags = u32(data, off+36)
        sections.append(dict(name=name, rva=rva, vsize=vsize, raw_size=raw_size,
                             raw_ptr=raw_ptr, flags=flags))

    def rva_to_off(rva: int) -> int | None:
        for sec in sections:
            span = max(sec['vsize'], sec['raw_size'])
            if sec['rva'] <= rva < sec['rva']+span:
                delta = rva-sec['rva']
                if delta < sec['raw_size']:
                    return sec['raw_ptr']+delta
        return rva if 0 <= rva < len(data) else None

    def in_exec(rva: int) -> bool:
        return any((s['flags'] & EXECUTE) and s['rva'] <= rva <
                   s['rva'] + max(s['vsize'], s['raw_size']) for s in sections)

    return dict(path=path, data=data, base=image_base, sections=sections,
                rva_to_off=rva_to_off, in_exec=in_exec)


def find_type_descriptor(img: dict, decorated_name: str) -> int | None:
    data = img['data']
    target = decorated_name.encode('ascii') + b'\0'
    pos = data.find(target)
    if pos < 0:
        return None
    # Locate the containing file-backed section; MSVC x86 TypeDescriptor is
    # two DWORDs immediately before the decorated name.
    for sec in img['sections']:
        if sec['raw_ptr'] <= pos < sec['raw_ptr']+sec['raw_size']:
            delta = pos-sec['raw_ptr']
            if delta < 8:
                return None
            return sec['rva'] + delta - 8
    return None


def vtables_for_type(img: dict, decorated_name: str) -> list[dict]:
    data, base = img['data'], img['base']
    td_rva = find_type_descriptor(img, decorated_name)
    if td_rva is None:
        return []
    td_va = base+td_rva
    td_bytes = struct.pack('<I', td_va)
    cols: set[int] = set()
    # CompleteObjectLocator layout on x86: signature, offset, cdOffset,
    # pTypeDescriptor, pClassDescriptor. pTypeDescriptor is at +0x0C.
    for sec in img['sections']:
        raw = data[sec['raw_ptr']:sec['raw_ptr']+sec['raw_size']]
        pos = 0
        while True:
            hit = raw.find(td_bytes, pos)
            if hit < 0:
                break
            col_rva = sec['rva'] + hit - 12
            col_off = img['rva_to_off'](col_rva)
            if col_off is not None and col_off+20 <= len(data):
                sig, offset, cd, ptd, _pchd = struct.unpack_from('<IIIII', data, col_off)
                if sig == 0 and ptd == td_va:
                    cols.add(col_rva)
            pos = hit+1

    results = []
    for col_rva in sorted(cols):
        col_va = base+col_rva
        col_bytes = struct.pack('<I', col_va)
        for sec in img['sections']:
            raw = data[sec['raw_ptr']:sec['raw_ptr']+sec['raw_size']]
            pos = 0
            while True:
                hit = raw.find(col_bytes, pos)
                if hit < 0:
                    break
                # vftable points to the first function pointer after the COL.
                vt_rva = sec['rva'] + hit + 4
                vt_off = img['rva_to_off'](vt_rva)
                if vt_off is not None:
                    entries = []
                    for slot in range(80):
                        p = vt_off + 4*slot
                        if p+4 > len(data):
                            break
                        va = u32(data, p)
                        rva = va-base
                        if not img['in_exec'](rva):
                            break
                        entries.append(rva)
                    results.append(dict(col=col_rva, vtable=vt_rva, offset=u32(data, img['rva_to_off'](col_rva)+4), slots=entries))
                pos = hit+1
    # De-duplicate repeated data references.
    unique = {}
    for item in results:
        unique[(item['col'], item['vtable'])] = item
    return list(unique.values())


def report(img: dict, class_name: str, slots: list[int]) -> list[str]:
    lines = [f'{img["path"].name}: {class_name}']
    tables = vtables_for_type(img, class_name)
    if not tables:
        return lines + ['  RTTI/vtable not found']
    for table in tables:
        tag = 'primary' if table['offset'] == 0 else f'base-subobject offset {table["offset"]}'
        lines.append(f'  {tag}; COL RVA 0x{table["col"]:X}; vtable RVA 0x{table["vtable"]:X}; {len(table["slots"])} executable slots')
        for slot in slots:
            if slot < len(table['slots']):
                lines.append(f'    slot {slot}: RVA 0x{table["slots"][slot]:X}')
            else:
                lines.append(f'    slot {slot}: unavailable')
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('client', type=Path)
    ap.add_argument('--server', type=Path)
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    images = [pe32(args.client)]
    if args.server:
        images.append(pe32(args.server))

    lines = ['PORTAL 2 JULY 2009 — MSVC RTTI / VTABLE AUDIT',
             'Static analysis only; no function calls or hooks are executed.', '']
    for img in images:
        if img['path'].name.lower() == 'client.dll':
            lines += report(img, '.?AVCViewRender@@', [6])
            lines += report(img, '.?AVClientModePortalNormal@@', [22, 34])
        if img['path'].name.lower() == 'server.dll':
            lines += report(img, '.?AVCPortal_Player@@', [46])
        lines.append('')

    text = '\n'.join(lines)
    print(text)
    if args.out:
        args.out.write_text(text+'\n', encoding='utf-8')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
