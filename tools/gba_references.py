#!/usr/bin/env python3
"""Export genuine linker references, without exporting ROM contents.

The reference ELF must reproduce the supplied BPEE ROM and have been linked
with --emit-relocs. Byte patterns inside graphics/audio are never references.
This metadata helps localization research; it is not a Spanish build recipe.
Requires pyelftools (only for this development tool).
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'builder'))
from emerald3ds_builder.rom import load_rom  # noqa: E402

ROM_BASE = 0x08000000


def pointer_target(rom: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(rom):
        raise ValueError('Linker reference lies outside the ROM')
    address = struct.unpack_from('<I', rom, offset)[0]
    if ROM_BASE <= address < ROM_BASE + len(rom):
        return address - ROM_BASE
    return None  # RAM, null and linker constants are legitimate too.


def thumb_call_target(rom: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(rom):
        raise ValueError('Call reference lies outside the ROM')
    first, second = struct.unpack_from('<HH', rom, offset)
    if first & 0xf800 != 0xf000 or second & 0xf800 != 0xf800:
        raise ValueError('Expected ARMv4T Thumb BL instruction')
    delta = ((first & 0x7ff) << 12) | ((second & 0x7ff) << 1)
    if delta & 0x400000:
        delta -= 0x800000
    target = offset + 4 + delta
    if not 0 <= target < len(rom):
        raise ValueError('Call target lies outside the ROM')
    return target


def read_references(elf_path: Path, rom: bytes) -> dict:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection

    references = []
    checked = 0
    with elf_path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf['e_machine'] != 'EM_ARM':
            raise ValueError('The reference ELF must be an ARM GBA build')
        for section in elf.iter_sections():
            address = section['sh_addr']
            size = section['sh_size']
            if (not section['sh_flags'] & 2 or section['sh_type'] == 'SHT_NOBITS'
                    or not ROM_BASE <= address < ROM_BASE + len(rom) or not size):
                continue
            offset = address - ROM_BASE
            if offset + size > len(rom) or section.data() != rom[offset:offset + size]:
                raise ValueError('ELF section does not reproduce BPEE: ' + section.name)
            checked += size
        if not checked:
            raise ValueError('ELF contains no matching ROM sections')
        for section in elf.iter_sections():
            if not isinstance(section, RelocationSection):
                continue
            for relocation in section.iter_relocations():
                address = relocation['r_offset']
                if not ROM_BASE <= address < ROM_BASE + len(rom):
                    continue
                offset = address - ROM_BASE
                kind = relocation['r_info_type']
                if kind == 2:  # R_ARM_ABS32
                    target = pointer_target(rom, offset)
                    label = 'pointer'
                elif kind == 10:  # R_ARM_THM_CALL / historical THM_PC22
                    target = thumb_call_target(rom, offset)
                    label = 'thumb_call'
                else:
                    continue
                if target is not None:
                    references.append({'offset': offset, 'target': target, 'kind': label})
    if not references:
        raise ValueError('No linker references found; relink the ELF with --emit-relocs')
    return {'format': 1, 'purpose': 'localization research; not a Spanish recipe',
            'checked_rom_bytes': checked,
            'references': sorted(references, key=lambda row: (row['offset'], row['kind']))}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--english', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    rom = load_rom(args.english).data
    if rom[0xac:0xb0] != b'BPEE':
        parser.error('The linker reference requires the clean English BPEE ROM')
    result = read_references(args.elf, rom)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(',', ':')) + '\n')
    print(f"Exported {len(result['references'])} linker references; no game bytes exported")


if __name__ == '__main__':
    main()
