#!/usr/bin/env python3
"""Does the game draw every pixel of a mountain where its drawing says?

The drawing is the 45-degree view, and every pixel of it says what it is a
picture of: a top (flat), a face (a wall falling south, one pixel down a
row), a band (a west or east slope, one pixel down a column), the ground.
Checked on the engine's own mesh (tests/voxel_mesh_dump.c), three ways:

  pixels   every pixel of the drawing, on the surface the game draws it on:
           a top's on a level surface, a face's on a vertical wall, a band's
           on a 45-degree slope. A face lying on a top (a corner tile drawn
           as a flat box), a top stood up as a wall, any bent surface: a
           fault. Pixels at the edge of their kind are not judged (a 4-pixel
           lattice cannot follow every outline).
  seams    a top drawn across the edge between two tiles (no rim between
           them: the rim is a terrace's back) is one surface: both sides at
           one height. A corner tile sunk under its terrace, a terrace split
           in two levels the player walks across: a fault.
  extra    what the game builds that no pixel is drawn on (the side walls of
           a block, a step's slanted strip) seen from the player's camera:
           a dark slot or a triangle sticking out of the drawing.

    python voxel_art_check.py LAYOUT_ROUTE116 [--relief build/x.bin]
        faulty cells, listed, and the totals
    python voxel_art_check.py LAYOUT_ROUTE116 --png out.png
        the drawing with every fault painted on it

Ledges, berry soil and stairs are kept as they always were and are not
judged. Exit status 1 when anything is found.
"""

import argparse
import collections
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_voxel_relief as gr  # noqa: E402
import voxel_grid_check as gc  # noqa: E402
import voxel_relief_lines as vl  # noqa: E402
import voxel_terraces as vt  # noqa: E402

TOP, WALL, RAMP, BENT = "top", "wall", "ramp", "bent"
PIXEL_FAULT = 12        # pixels of one tile on the wrong surface: a faulty cell
SEAM_FAULT = 3          # rows of one edge whose two tops part
SEAM_TOL = 1.5 / 16     # levels: a top may part from its neighbour by this much
EXTRA_FAULT = 60        # screen pixels (at 2x) of undrawn surface seen in a cell
ON = 0.05               # cells: how near a vertex must be to its texel's place


def orient(tri):
    nm = gc.normal(tri)
    if nm is None or nm[3] < 1e-5:
        return None
    nx, ny, nz = nm[:3]
    if abs(ny) > 1 - gc.TOL_N:
        return TOP
    if abs(ny) < gc.TOL_N:
        return WALL
    if abs(nz) < gc.TOL_N and gc.RAMP_LO < abs(nx) / max(abs(ny), 1e-9) < gc.RAMP_HI:
        return RAMP
    return BENT


WANT = {"T": TOP, "G": TOP, "F": WALL, "B": RAMP}
_CLS = {}


def classes(art, m):
    """16 x 16 of T / F / B / G: what each pixel of metatile m is a picture
    of (a band's hatching is B; whatever is not the mountain's tileset is
    ground)."""
    key = (art.primary, art.secondary, m)
    if key not in _CLS:
        r = vt.twin(art, m)
        if r not in vt.MOUNTAIN:
            out = [["G"] * 16 for _ in range(16)]
        else:
            band = r in vt.BAND_WEST or r in vt.BAND_EAST
            # a band's hatching is drawn in both colours: all of it slopes
            out = [["B" if (band and c != "G") else c for c in row] for row in vt.pixel_classes(art, m)]
        _CLS[key] = out
    return _CLS[key]


def core(cls, i, j):
    """A pixel in a 4 x 4 block of the lattice all of one kind: what the
    lattice (a point every 4 pixels) can draw as it is drawn."""
    c = cls[j][i]
    bi, bj = i - i % 4, j - j % 4
    return all(cls[y][x] == c for y in range(bj, bj + 4) for x in range(bi, bi + 4))


def rim(art, m, i, j):
    img = art.cell_image(m).load()
    return img[i, j][:3] in gr.ROCK_RIM


def texels(tris, art, cuts):
    """{(cell, i, j): (height, orientation, triangle)}: what the 45-degree
    view shows of each pixel of the drawing (the highest surface drawing
    it), and [triangles drawn with no pixel of their own place]."""
    seen = {}
    extra = []
    S = vl.GRID * 16
    for k, tri in enumerate(tris):
        aid = vl.atlas_id(tri)
        if vl.METATILE_REAL <= aid < vl.CUT_FIRST:
            continue            # ground under a modelled prop
        clear = None
        if aid >= vl.CUT_FIRST:
            if aid - vl.CUT_FIRST >= len(cuts):
                continue
            clear = cuts[aid - vl.CUT_FIRST][2]
        sx, sy = (aid % vl.GRID) * 16, (aid // vl.GRID) * 16
        tex = [(p[3] * S - sx, p[4] * S - sy) for p in tri]
        # its place: the cell whose drawing the texels sit on, the same
        # for all three corners (a wall textured by the world is not)
        place = set()
        for (x, y, z, u, v, s), (tx, ty) in zip(tri, tex):
            cx, cy = x - tx / 16.0, (z - y) - ty / 16.0
            if abs(cx - round(cx)) > ON or abs(cy - round(cy)) > ON:
                place = None
                break
            place.add((int(round(cx)), int(round(cy))))
        o = orient(tri)
        if o is None:
            continue
        if not place or len(place) != 1:
            if o != TOP:
                extra.append(k)
            continue
        cell = place.pop()
        (u0, v0), (u1, v1), (u2, v2) = tex
        area = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0)
        if abs(area) < 1e-9:
            continue
        ys = [p[1] for p in tri]
        for j in range(max(0, int(math.floor(min(v0, v1, v2)))), min(16, int(math.ceil(max(v0, v1, v2))))):
            for i in range(max(0, int(math.floor(min(u0, u1, u2)))), min(16, int(math.ceil(max(u0, u1, u2))))):
                cu, cv = i + 0.5, j + 0.5
                a = ((u1 - cu) * (v2 - cv) - (u2 - cu) * (v1 - cv)) / area
                b = ((u2 - cu) * (v0 - cv) - (u0 - cu) * (v2 - cv)) / area
                c = 1.0 - a - b
                if a < -1e-6 or b < -1e-6 or c < -1e-6:
                    continue
                if clear is not None and (clear[j] >> i) & 1:
                    continue
                h = a * ys[0] + b * ys[1] + c * ys[2]
                key = (cell, i, j)
                if key not in seen or h > seen[key][0] + 1e-6:
                    seen[key] = (h, o, k, aid if aid < vl.METATILE_REAL else cuts[aid - vl.CUT_FIRST][1])
    return seen, extra


def judged_cells(layout):
    """The cells judged: not ledges, berry soil, stairs."""
    c = vt.Cells(vt.group_of(layout))
    skip = set()
    for cell in c.kind:
        lid, x, y = c.src[cell]
        if lid != layout:
            continue
        if cell in c.keep or c.role(cell) == "stair":
            skip.add((x, y))
    return skip


def pixel_faults(seen, art, skip):
    out = collections.defaultdict(collections.Counter)
    for (cell, i, j), (h, o, k, m) in seen.items():
        if cell in skip:
            continue
        cls = classes(art, m)
        c = cls[j][i]
        if not core(cls, i, j):
            continue
        if o == BENT:
            out[cell]["bent"] += 1
        elif o != WANT[c]:
            out[cell]["%s on %s" % ({"T": "top", "G": "ground", "F": "face", "B": "band"}[c], o)] += 1
    return {cell: n for cell, n in out.items() if sum(n.values()) >= PIXEL_FAULT}


def seam_faults(seen, art, skip):
    """[(cell, neighbour, rows)]: tops drawn across an edge, parted."""
    out = []
    cells = {key[0] for key in seen}
    for (x, y) in cells:
        for n, edge in (((x + 1, y), "E"), ((x, y + 1), "S")):
            if n not in cells or (x, y) in skip or n in skip:
                continue
            bad = 0
            for r in range(16):
                a = ((x, y), 15, r) if edge == "E" else ((x, y), r, 15)
                b = (n, 0, r) if edge == "E" else (n, r, 0)
                if a not in seen or b not in seen:
                    continue
                ha, oa, _, ma = seen[a]
                hb, ob, _, mb = seen[b]
                if oa != TOP or ob != TOP:
                    continue
                if classes(art, ma)[a[2]][a[1]] != "T" or classes(art, mb)[b[2]][b[1]] != "T":
                    continue
                if edge == "S" and vt.rim_pixels(art, mb) >= 3:
                    continue    # a rim: the terrace's back, lower ground behind
                if abs(ha - hb) > SEAM_TOL:
                    bad += 1
            if bad >= SEAM_FAULT:
                out.append(((x, y), n, bad))
    return out


def extra_faults(tris, extra, layout, step=6, pitch=43.0, distance=14.0):
    """{cell: screen pixels} of surfaces with no drawing of their own seen
    from the player's camera (straight on, as the game holds it)."""
    from PIL import Image
    art = vl.vb.LayoutArt(layout)
    ex = set(extra)
    total = collections.Counter()
    tex = Image.new("RGBA", (1, 1), (255, 255, 255, 255))
    tpx = tex.load()
    for y0 in range(3, art.h, step):
        for x0 in range(3, art.w, step):
            cam = vl.camera(tris, (x0, y0), pitch, distance, 0.0)
            ras = vl.Raster(cam.width * 2, cam.height * 2)
            cam2 = vl.vb.Camera(cam.target, cam.pitch, cam.yaw, cam.distance, cam.fov, ras.w, ras.h)
            reach = distance * 1.6
            for k, tri in enumerate(tris):
                if vl.METATILE_REAL <= vl.atlas_id(tri) < vl.CUT_FIRST:
                    continue
                mx = sum(p[0] for p in tri) / 3 - x0
                mz = sum(p[2] - p[1] for p in tri) / 3 - y0
                if abs(mx) > reach or abs(mz) > reach:
                    continue
                vs = []
                for (x, y, z, u, v, s) in tri:
                    pr = cam2.project((x, y, z))
                    if pr is None:
                        break
                    vs.append((pr[0], pr[1], pr[2], pr[3], 0.0, 0.0))
                else:
                    ras.draw(vs, tex, tpx, 1.0, k)
            # the middle of the screen only: each spot judges its own square
            for idx, owner in enumerate(ras.tri):
                if owner in ex:
                    t = tris[owner]
                    cell = gc.cell_of(t)
                    if abs(cell[0] - x0) <= step // 2 and abs(cell[1] - y0) <= step // 2:
                        total[cell] += 1
    return {cell: n for cell, n in total.items() if n >= EXTRA_FAULT}


def check(layout, png=None, extra=True):
    tris, _ = vl.dump(layout)
    art = gr.layout_art(layout)
    cuts = vl.relief_cuts()
    skip = judged_cells(layout)
    seen, undrawn = texels(tris, art, cuts)
    px = pixel_faults(seen, art, skip)
    seams = seam_faults(seen, art, skip)
    ext = extra_faults(tris, undrawn, layout) if extra else {}
    ext = {c: n for c, n in ext.items() if c not in skip}
    if png:
        paint(layout, art, px, seams, ext, png)
    return px, seams, ext


def paint(layout, art, px, seams, ext, path, scale=3):
    from PIL import Image, ImageDraw
    A = vl.vb.LayoutArt(layout)
    img = Image.new("RGB", (A.w * 16, A.h * 16))
    for y in range(A.h):
        for x in range(A.w):
            img.paste(A.cell_image(A.metatile(x, y)).convert("RGB"), (x * 16, y * 16))
    img = img.resize((img.width * scale, img.height * scale))
    d = ImageDraw.Draw(img)
    S = 16 * scale
    for (x, y) in px:
        d.rectangle([x * S + 1, y * S + 1, x * S + S - 2, y * S + S - 2], outline=(255, 0, 0), width=2)
    for (x, y), n in ext.items():
        d.rectangle([x * S + 4, y * S + 4, x * S + S - 5, y * S + S - 5], outline=(255, 0, 255), width=2)
    for (x, y), (nx, ny), _ in seams:
        if nx > x:
            d.line([(nx * S, y * S + 2), (nx * S, y * S + S - 2)], fill=(0, 255, 255), width=4)
        else:
            d.line([(x * S + 2, ny * S), (x * S + S - 2, ny * S)], fill=(0, 255, 255), width=4)
    img.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("layouts")
    ap.add_argument("--relief", default=None)
    ap.add_argument("--png", default=None)
    ap.add_argument("--list", type=int, default=20)
    ap.add_argument("--no-extra", action="store_true", help="skip the camera pass (fast)")
    args = ap.parse_args()
    if args.relief:
        vl.use_relief(args.relief)
    total = 0
    for layout in args.layouts.split(","):
        px, seams, ext = check(layout, args.png, not args.no_extra)
        kinds = collections.Counter()
        for n in px.values():
            kinds.update(n)
        print("%s: pixels %d cells (%s) | seams %d | extra %d cells" % (
            layout, len(px), ", ".join("%s %d" % kv for kv in kinds.most_common()) or "none",
            len(seams), len(ext)))
        for cell, n in sorted(px.items(), key=lambda kv: -sum(kv[1].values()))[:args.list]:
            print("  px    %3d,%-3d %s" % (cell[0], cell[1], dict(n)))
        for a, b, n in sorted(seams, key=lambda s: -s[2])[:args.list]:
            print("  seam  %3d,%-3d | %3d,%-3d  %d rows" % (a[0], a[1], b[0], b[1], n))
        for cell, n in sorted(ext.items(), key=lambda kv: -kv[1])[:args.list]:
            print("  extra %3d,%-3d %d px" % (cell[0], cell[1], n))
        total += len(px) + len(seams) + len(ext)
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
