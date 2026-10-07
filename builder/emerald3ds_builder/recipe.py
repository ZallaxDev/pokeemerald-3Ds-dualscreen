"""Recipes: how to rebuild each game-data file from the player's ROM.

A recipe lists, for every file of the data pack, its path, size, CRC-32 and a
sequence of operations that write it from left to right:

    ["C", rom_offset, length]         copy bytes from the ROM
    ["F", byte, length]               repeat one byte
    ["L", literal_offset, length]     bytes from the recipe's literal pool
    ["Z", rom_offset]                 LZ77-decompress (GBA BIOS format) data
                                      that starts at rom_offset
    ["R", rom_offset, src_stride, dst_stride, count, bitmap_index]
                                      copy `count` records, remapping bits:
                                      bitmap[i] is the source bit of
                                      destination bit i, or -1/-2 for a
                                      constant 0/1
    ["X", external_offset, length]    copy bytes from the externals: files
                                      that are not in the ROM, fetched at a
                                      pinned commit and converted by the
                                      builder (externals.py), one after the
                                      other in the recipe's order (schema 2)

An entry may also carry "patches": [dst_offset, literal_offset, length]
triples written over the result afterwards (pointers inside remapped records).

The literal pool holds only bytes that are not in the ROM: pointers into the
executable (they depend on its layout, not on the game) and data the port
itself adds. tools/gen_recipe.py writes recipes and audits the pool;
tools/release_audit.py re-checks it against the ROM.

Container: b"EM3DRCP1", u32 json_length, the JSON (LZMA), then the literal
pool (LZMA).
"""

from __future__ import annotations

import json
import lzma
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = b"EM3DRCP1"
SCHEMA = 1
SCHEMA_EXTERNALS = 2   # a recipe with externals (and "X" operations)


class RecipeError(Exception):
    pass


@dataclass
class Recipe:
    engine_abi: int
    rom_sha1: str
    release: str
    entries: list = field(default_factory=list)   # dicts: path, size, crc, ops
    bitmaps: list = field(default_factory=list)
    generated: list = field(default_factory=list)  # dicts: path, size, crc, generator
    inputs: list = field(default_factory=list)     # entries written into the generators' tree
    vtree: dict = field(default_factory=dict)      # names and ROM offsets for that tree
    externals: list = field(default_factory=list)  # dicts: repo, commit, path, sha256, steps, size, crc
    literals: bytes = b""

    def to_bytes(self) -> bytes:
        meta = {"schema": SCHEMA_EXTERNALS if self.externals else SCHEMA,
                "engine_abi": self.engine_abi, "rom_sha1": self.rom_sha1,
                "release": self.release, "entries": self.entries, "bitmaps": self.bitmaps,
                "generated": self.generated, "inputs": self.inputs, "vtree": self.vtree}
        if self.externals:
            meta["externals"] = self.externals
        blob = lzma.compress(json.dumps(meta, separators=(",", ":")).encode("utf-8"))
        return MAGIC + struct.pack("<I", len(blob)) + blob + lzma.compress(self.literals)

    @classmethod
    def from_bytes(cls, data: bytes) -> "Recipe":
        if data[:8] != MAGIC:
            raise RecipeError("not a Pokémon Emerald 3Ds Dual Screen recipe")
        (size,) = struct.unpack_from("<I", data, 8)
        meta = json.loads(lzma.decompress(data[12:12 + size]))
        if meta.get("schema") not in (SCHEMA, SCHEMA_EXTERNALS):
            raise RecipeError("unsupported recipe schema %r" % meta.get("schema"))
        return cls(engine_abi=meta["engine_abi"], rom_sha1=meta["rom_sha1"], release=meta["release"],
                   entries=meta["entries"], bitmaps=meta.get("bitmaps", []),
                   generated=meta.get("generated", []), inputs=meta.get("inputs", []),
                   vtree=meta.get("vtree", {}), externals=meta.get("externals", []),
                   literals=lzma.decompress(data[12 + size:]))

    def save(self, path: Path) -> None:
        Path(path).write_bytes(self.to_bytes())

    @classmethod
    def load(cls, path: Path) -> "Recipe":
        return cls.from_bytes(Path(path).read_bytes())


def recipe_rom_sha1(path: Path) -> str:
    """The SHA-1 of the ROM a recipe is made from, reading only its header."""
    with open(path, "rb") as f:
        head = f.read(12)
        if head[:8] != MAGIC:
            raise RecipeError("not a Pokémon Emerald 3Ds Dual Screen recipe")
        (size,) = struct.unpack_from("<I", head, 8)
        meta = json.loads(lzma.decompress(f.read(size)))
    return meta["rom_sha1"]


def lz77_decompress(src: bytes, offset: int) -> bytes:
    """GBA BIOS LZ77 (type 0x10)."""
    if src[offset] != 0x10:
        raise RecipeError("no LZ77 header at %#x" % offset)
    size = src[offset + 1] | (src[offset + 2] << 8) | (src[offset + 3] << 16)
    out = bytearray()
    pos = offset + 4
    while len(out) < size:
        flags = src[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                a, b = src[pos], src[pos + 1]
                pos += 2
                length = (a >> 4) + 3
                back = ((a & 0xF) << 8 | b) + 1
                if length <= back <= len(out):      # no overlap: one slice
                    start = len(out) - back
                    out += out[start:start + length]
                else:
                    for _ in range(length):
                        out.append(out[-back])
            else:
                out.append(src[pos])
                pos += 1
    return bytes(out[:size])


def apply_bitmap(src: bytes, dst: bytearray, bitmap: list[int]) -> None:
    for i, s in enumerate(bitmap):
        if s == -1:
            bit = 0
        elif s == -2:
            bit = 1
        else:
            bit = (src[s >> 3] >> (s & 7)) & 1
        if bit:
            dst[i >> 3] |= 1 << (i & 7)
        else:
            dst[i >> 3] &= ~(1 << (i & 7))


def build_entry(entry: dict, rom: bytes, literals: bytes, bitmaps: list, externals: bytes = b"") -> bytes:
    out = bytearray()
    for op in entry["ops"]:
        kind = op[0]
        if kind == "C":
            _, off, length = op
            if off < 0 or off + length > len(rom):
                raise RecipeError("%s: ROM range out of bounds" % entry["path"])
            out += rom[off:off + length]
        elif kind == "X":
            _, off, length = op
            if off < 0 or off + length > len(externals):
                raise RecipeError("%s: externals range out of bounds" % entry["path"])
            out += externals[off:off + length]
        elif kind == "F":
            out += bytes([op[1]]) * op[2]
        elif kind == "L":
            _, off, length = op
            out += literals[off:off + length]
        elif kind == "Z":
            out += lz77_decompress(rom, op[1])
        elif kind == "R":
            _, off, src_stride, dst_stride, count, index = op
            bitmap = bitmaps[index]
            for k in range(count):
                record = bytearray(dst_stride)
                apply_bitmap(rom[off + k * src_stride: off + (k + 1) * src_stride], record, bitmap)
                out += record
        else:
            raise RecipeError("%s: unknown operation %r" % (entry["path"], kind))
    for dst, off, length in entry.get("patches", ()):
        out[dst:dst + length] = literals[off:off + length]
    data = bytes(out)
    if len(data) != entry["size"] or (zlib.crc32(data) & 0xFFFFFFFF) != entry["crc"]:
        raise RecipeError("%s does not match the expected result (size %d/%d, CRC %08x/%08x)"
                          % (entry["path"], len(data), entry["size"], zlib.crc32(data) & 0xFFFFFFFF,
                             entry["crc"]))
    return data
