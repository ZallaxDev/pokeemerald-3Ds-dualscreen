#!/usr/bin/env python3
"""Hand corrections to the voxel terrain, applied over what the generators read
off the drawing.

gen_voxel_relief.py reads every height off the art; where it reads one wrong,
the correction is written down here instead of in the generator, as numbers:
assets/voxel/relief_fixes.json. Nothing in it comes from the cartridge - it
holds heights in pixels, metatile ids and names - so it ships with the
generators and every build, the builder's included, gets the same terrain.

    {
      "version": 1,
      "pieces": {                       one shape, reused wherever it is enabled
        "face_s_07c": {
          "tileset": "gTileset_General",   the tileset the metatile is drawn from
          "metatile": 124,                  its id (>= 512: the secondary's)
          "grid": [25 x px],                the 5x5 lattice, row major, over the
                                            cell's level (0 = its top terrace)
          "note": "..."
        }
      },
      "layouts": {
        "LAYOUT_ROUTE106": {
          "base": 32,                       the whole map's level, px (optional)
          "pieces": ["face_s_07c",          enabled on this map: every cell of
                     {"id": "x", "rect": [x0, y0, x1, y1]}],   that metatile
          "cells": {
            "49,12": {
              "level": 32,                  the cell's level, px over the base
                                            (default: its top, read off the
                                            generated lattice to 16 px)
              "piece": "face_s_07c",        this piece here, enabled or not
              "nopiece": true,              no enabled piece here
              "grid": [25 x px],            the lattice outright, over the base
              "offset": 8,                  added to every point
              "remove": true,               not lifted at all
              "cut": {"wall": 124, "sides": 255, "ground": 1, "foot": 16,
                      "flags": 0, "variant": "keep" | "none"}
            }
          }
        }
      },
      "furniture": {"pc1f": {"ball_w": {"height": 12, "base": 10}}}
    }

In a cell, the order is: the generated lattice, then a piece (the cell's own
"piece", else the first enabled one for its metatile), then "grid", then
"offset". "remove" drops the cell and its cut record.

    python voxel_relief_fixes.py romfs/voxel/relief.bin    applies the fixes in place
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ROOT = os.path.dirname(PORT)
FIXES = os.environ.get("VOXEL_RELIEF_FIXES",
                       os.path.join(PORT, "assets", "voxel", "relief_fixes.json"))

MAGIC = b"VXL4"
SIDE = 5
DRAWN = 0x8000
UNIT2 = 0x4000
NO_VARIANT = 0xFFFF
NONE16 = 0xFFFF
NO_FACE = 0xFFFE


# --- relief.bin, read and written without loss ---------------------------------

def parse(blob):
    """{"layouts": {index: {w, h, flags, base, cells{(x, y): [25 px]}}},
    "variants": [(layout, metatile, rows[16])],
    "cuts": {(layout, x, y): [variant, foot, ground, wall, sides, flags]}}"""
    if blob[:4] != MAGIC:
        raise ValueError("not a relief.bin")
    count, side = struct.unpack_from("<HH", blob, 4)
    if side != SIDE:
        raise ValueError("lattice side %d" % side)
    layouts = {}
    for i in range(count):
        lid, n, w, hh, off, base = struct.unpack_from("<HHHHIh", blob, 8 + 14 * i)
        unit = 2 if hh & UNIT2 else 1
        cells = {}
        for k in range(n):
            x, y = blob[off + 27 * k], blob[off + 27 * k + 1]
            cells[(x, y)] = [v * unit for v in struct.unpack_from("<25b", blob, off + 27 * k + 2)]
        layouts[lid] = {"w": w, "h": hh & 0x3FFF, "flags": hh & (DRAWN | UNIT2),
                        "base": base, "cells": cells}
    variants, cuts = [], {}
    if blob[-4:] == b"CUTS":
        at = struct.unpack_from("<I", blob, len(blob) - 8)[0]
        nv, nc = struct.unpack_from("<HH", blob, at)
        at += 4
        for _ in range(nv):
            lay, m, *rows = struct.unpack_from("<HH16H", blob, at)
            variants.append((lay, m, rows))
            at += 36
        for _ in range(nc):
            lay, x, y, k, foot, ground, wall, sides, flags = struct.unpack_from("<HBBHhHHBB", blob, at)
            cuts[(lay, x, y)] = [k, foot, ground, wall, sides, flags]
            at += 14
    return {"layouts": layouts, "variants": variants, "cuts": cuts}


def pack(relief):
    tables = []
    for lid in sorted(relief["layouts"]):
        t = relief["layouts"][lid]
        if not t["cells"] and not t["base"]:
            continue
        flags = t["flags"]
        values = [v for g in t["cells"].values() for v in g]
        if values and not flags & UNIT2 and (max(values) > 127 or min(values) < -128):
            flags |= UNIT2
        unit = 2 if flags & UNIT2 else 1
        tables.append((lid, t, flags, unit))
    head = MAGIC + struct.pack("<HH", len(tables), SIDE)
    offset = len(head) + 14 * len(tables)
    idx, body = bytearray(), bytearray()
    for lid, t, flags, unit in tables:
        cells = sorted(t["cells"].items(), key=lambda kv: (kv[0][1], kv[0][0]))
        idx += struct.pack("<HHHHIh", lid, len(cells), t["w"], t["h"] | flags,
                           offset + len(body), int(t["base"]))
        for (x, y), g in cells:
            body += struct.pack("<BB", x, y)
            body += struct.pack("<25b", *(max(-128, min(127, int(round(v / unit)))) for v in g))
    blob = bytes(head + idx + body)
    table = struct.pack("<HH", len(relief["variants"]), len(relief["cuts"]))
    for lay, m, rows in relief["variants"]:
        table += struct.pack("<HH16H", lay, m, *rows)
    for (lay, x, y), c in sorted(relief["cuts"].items()):
        table += struct.pack("<HBBHhHHBB", lay, x, y, *c)
    return blob + table + struct.pack("<I", len(blob)) + b"CUTS"


# --- the maps the fixes name ----------------------------------------------------

_LAYOUTS = None


def layouts():
    """layouts.json's entries, in gMapLayouts order (index = position + 1)."""
    global _LAYOUTS
    if _LAYOUTS is None:
        _LAYOUTS = json.load(open(os.path.join(ROOT, "data", "layouts", "layouts.json"),
                                  encoding="utf-8"))["layouts"]
    return _LAYOUTS


def layout_index(name):
    for i, e in enumerate(layouts()):
        if e.get("id") == name:
            return i + 1
    return None


def layout_entry(name):
    return next((e for e in layouts() if e.get("id") == name), None)


def blocks(entry):
    raw = open(os.path.join(ROOT, entry["blockdata_filepath"]), "rb").read()
    return struct.unpack("<%dH" % (len(raw) // 2), raw)


def level_of(grid):
    """A cell's level when the fixes give none: the top of its lattice, to the
    terrace (16 px)."""
    if not grid:
        return 0
    return int(round(max(grid) / 16.0)) * 16


def load_fixes(path=None):
    path = path or FIXES
    if not os.path.exists(path):
        return {"version": 1, "pieces": {}, "layouts": {}, "furniture": {}}
    fixes = json.load(open(path, encoding="utf-8"))
    for k in ("pieces", "layouts", "furniture"):
        fixes.setdefault(k, {})
    return fixes


def _enabled(spec, pieces):
    """[(piece, rect or None)] enabled on a map, known pieces only."""
    out = []
    for p in spec.get("pieces", []):
        pid, rect = (p, None) if isinstance(p, str) else (p.get("id"), p.get("rect"))
        if pid in pieces:
            out.append((pieces[pid], rect))
    return out


def apply(relief, fixes, report=None):
    """The fixes over a parsed relief, in place. `report`, a list, gets one
    line per map changed."""
    pieces = fixes.get("pieces", {})
    for name, spec in fixes.get("layouts", {}).items():
        index = layout_index(name)
        entry = layout_entry(name)
        if index is None:
            if report is not None:
                report.append("%s: no such layout" % name)
            continue
        t = relief["layouts"].get(index)
        if t is None:
            t = relief["layouts"][index] = {"w": entry["width"], "h": entry["height"],
                                            "flags": 0, "base": 0, "cells": {}}
        if "base" in spec:
            t["base"] = int(spec["base"])
        enabled = _enabled(spec, pieces)
        cells_fix = spec.get("cells", {})
        changed = 0
        if enabled or cells_fix:
            mt = blocks(entry)
            w = entry["width"]
            primary, secondary = entry["primary_tileset"], entry["secondary_tileset"]

            def tileset_of(m):
                return primary if m < 512 else secondary

            if enabled:
                for y in range(entry["height"]):
                    for x in range(w):
                        key = "%d,%d" % (x, y)
                        if cells_fix.get(key, {}).get("nopiece") or "piece" in cells_fix.get(key, {}):
                            continue
                        m = mt[y * w + x] & 0x3FF
                        for pc, rect in enabled:
                            if pc.get("metatile") != m or pc.get("tileset", tileset_of(m)) != tileset_of(m):
                                continue
                            if rect and not (rect[0] <= x < rect[2] and rect[1] <= y < rect[3]):
                                continue
                            g = t["cells"].get((x, y))
                            lvl = cells_fix.get(key, {}).get("level", level_of(g))
                            t["cells"][(x, y)] = [lvl + v for v in pc["grid"]]
                            changed += 1
                            break
            for key, fix in cells_fix.items():
                x, y = (int(v) for v in key.split(","))
                if fix.get("remove"):
                    t["cells"].pop((x, y), None)
                    relief["cuts"].pop((index, x, y), None)
                    changed += 1
                    continue
                g = t["cells"].get((x, y))
                lvl = fix.get("level", level_of(g))
                if fix.get("piece") in pieces:
                    g = [lvl + v for v in pieces[fix["piece"]]["grid"]]
                elif "level" in fix and g is None and "grid" not in fix:
                    g = [lvl] * 25
                if "grid" in fix:
                    g = [int(v) for v in fix["grid"]]
                if "offset" in fix:
                    g = [v + int(fix["offset"]) for v in (g or [0] * 25)]
                if g is not None:
                    t["cells"][(x, y)] = g
                if "cut" in fix:
                    c = fix["cut"]
                    rec = relief["cuts"].get((index, x, y), [NO_VARIANT, 0, NONE16, NONE16, 0xFF, 0])
                    if c.get("variant") == "none":
                        rec[0] = NO_VARIANT
                    for i, k in ((1, "foot"), (4, "sides"), (5, "flags")):
                        if k in c:
                            rec[i] = int(c[k])
                    for i, k in ((2, "ground"), (3, "wall")):
                        if k in c:
                            v = c[k]
                            rec[i] = NONE16 if v is None else (NO_FACE if v == "noface" else int(v))
                    if c.get("delete"):
                        relief["cuts"].pop((index, x, y), None)
                    else:
                        relief["cuts"][(index, x, y)] = rec
                        if (x, y) not in t["cells"]:
                            t["cells"][(x, y)] = [lvl] * 25
                changed += 1
        if report is not None and (changed or "base" in spec):
            report.append("%s: %d cell(s)%s" % (name, changed,
                                                ", base %+d px" % t["base"] if "base" in spec else ""))
    return relief


def apply_file(path, fixes_path=None, quiet=False):
    fixes = load_fixes(fixes_path)
    if not fixes.get("layouts"):
        return
    report = []
    with open(path, "rb") as f:
        relief = apply(parse(f.read()), fixes, report)
    with open(path, "wb") as f:
        f.write(pack(relief))
    if not quiet:
        for line in report:
            print("relief fix:", line)


def furniture_pieces(room, pieces, fixes_path=None):
    """A room's pieces (voxel_building_specs) with the heights the fixes give."""
    over = load_fixes(fixes_path).get("furniture", {}).get(room, {})
    if not over:
        return pieces
    out = []
    for pc in pieces:
        o = over.get(pc["name"])
        if o:
            pc = dict(pc)
            for k in ("height", "base"):
                if k in o:
                    pc[k] = o[k]
        out.append(pc)
    return out


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    apply_file(sys.argv[1])
