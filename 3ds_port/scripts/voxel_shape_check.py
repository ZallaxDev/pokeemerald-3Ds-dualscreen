#!/usr/bin/env python3
"""What the player's camera sees wrong in the mountains, counted on the
engine's own mesh (tests/voxel_mesh_dump.c over voxel_mesh_builder.c).

A mountain read off the drawing is made of three surfaces only: level tops,
vertical south faces and 45-degree bands. From the game's cameras (pitch
43, distance 11, yaw 0 and +-25) every visible pixel is judged by the
triangle it shows:

  hole     the void (a floor laid under the whole map shows through)
  deformed a surface that is none of the three (a slant, a twist, a hip)
  side     a vertical wall facing west or east, or turning a corner: the
           drawing never draws one (seen as a dark slit or a spindle)
  tiny     a piece of surface smaller than SMALL of a tile on screen, cut
           off from the rest of its kind: geometry the drawing does not
           need (teeth along a face's lip, slivers, notches)

Cells under props, trees and houses (models the dump does not draw) and
ledges and berry soil (never altered) are not judged.

    python voxel_shape_check.py LAYOUT_ROUTE116 [--relief build/x.bin]
                                [--at X,Y --view OUT.png] [--list 20]
"""

import argparse
import collections
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import voxel_relief_lines as vl  # noqa: E402
import voxel_grid_check as gc  # noqa: E402

PITCH, DISTANCE = 43.0, 11.0
YAWS = (0.0, 25.0, -25.0)
STEP = 5                 # cells between cameras
SMALL = 0.08             # of a tile's area on screen: a piece this small is a fault
FLOOR = 0.3
KINDS = ("hole", "deformed", "side", "tiny")

_S = {}


def surface(tri):
    """'top', 'face', 'band', 'side' or 'deformed' for a terrain triangle."""
    nm = gc.normal(tri)
    if nm is None:
        return None
    nx, ny, nz, area = nm
    if abs(ny) > 1 - gc.TOL_N:
        return "top"
    if abs(nz) > 1 - gc.TOL_N:
        return "face"
    if abs(ny) < gc.TOL_N:
        return "side"
    if abs(nz) < gc.TOL_N and gc.RAMP_LO < abs(nx) / max(abs(ny), 1e-9) < gc.RAMP_HI:
        return "band"
    return "deformed"


def _setup(layout, trial):
    if trial:
        vl.use_relief(trial)
    tris, _ = vl.dump(layout)
    A = vl.vb.LayoutArt(layout)
    lowest = {}
    for t in tris:
        cell = gc.cell_of(t)
        h = min(p[1] for p in t)
        if h < lowest.get(cell, 1e9):
            lowest[cell] = h
    floor = []
    for y in range(A.h):
        for x in range(A.w):
            near = [lowest[(x + i, y + j)] for i in (-1, 0, 1) for j in (-1, 0, 1)
                    if (x + i, y + j) in lowest]
            low = (min(near) if near else 0.0) - FLOOR
            a = (x, low, y + low, 0, 0, 1)
            b = (x + 1, low, y + low, 0, 0, 1)
            c = (x + 1, low, y + 1 + low, 0, 0, 1)
            d = (x, low, y + 1 + low, 0, 0, 1)
            floor += [((x, y), (a, b, c)), ((x, y), (a, c, d))]
    import voxel_terraces as vt
    c = vt.Cells(vt.group_of(layout))
    hidden, mine = set(), set()
    for cell, k in c.kind.items():
        lid, x, y = c.src[cell]
        if lid != layout:
            continue
        if k == vt.UNKNOWN or c.role(cell) in ("house", "building", "roof"):
            hidden |= {(x + i, y + j) for i in (-1, 0, 1) for j in (-1, 0, 1)}
        if cell in c.keep:
            hidden.add((x, y))
        if k in (vt.ROCK, vt.TOP):
            mine.add((x, y))
    kinds = []
    cells = []
    for t in tris:
        prop = vl.METATILE_REAL <= vl.atlas_id(t) < vl.CUT_FIRST
        kinds.append(None if prop else surface(t))
        cells.append(gc.cell_of(t))
    _S.update(tris=tris, floor=floor, w=A.w, h=A.h, hidden=hidden, mine=mine, kinds=kinds, cells=cells)


def shoot(at, yaw):
    """(raster, owners, n): the view from one camera, terrain and floor."""
    from PIL import Image
    tris, floor = _S["tris"], _S["floor"]
    cam = vl.camera(tris, at, PITCH, DISTANCE, yaw)
    ras = vl.Raster(cam.width, cam.height, bg=(0, 0, 0))
    tex = Image.new("RGBA", (1, 1), (255, 255, 255, 255))
    tpx = tex.load()
    reach = cam.distance * 2.2
    n = len(tris)
    for k, tri in enumerate(list(tris) + [t for _, t in floor]):
        mx = sum(p[0] for p in tri) / 3 - at[0]
        mz = sum(p[2] - p[1] for p in tri) / 3 - at[1]
        if abs(mx) > reach or abs(mz) > reach:
            continue
        vs = []
        for (x, y, z, u, v, sh) in tri:
            pr = cam.project((x, y, z))
            if pr is None:
                break
            sx, sy, d, iw = pr
            vs.append((sx, sy, d, iw, 0.0, 0.0))
        else:
            ras.draw(vs, tex, tpx, 1.0, k)
    return cam, ras, n


def tile_area(cam, at):
    """Screen pixels a level tile covers round the camera's cell."""
    g = cam.target[1]
    pts = [cam.project((at[0] + dx, g, at[1] + dz + g)) for dx, dz in ((0, 0), (1, 0), (1, 1), (0, 1))]
    if any(p is None for p in pts):
        return 400.0
    a = 0.0
    for i in range(4):
        x0, y0 = pts[i][:2]
        x1, y1 = pts[(i + 1) % 4][:2]
        a += x0 * y1 - x1 * y0
    return abs(a) / 2


def judge(ras, n, small, marks=None):
    """{cell: Counter(kind: pixels)} of one view (`marks`: {pixel: kind}
    filled with every faulty pixel)."""
    marks = {} if marks is None else marks
    kinds, cells, floor = _S["kinds"], _S["cells"], _S["floor"]
    hidden, mine = _S["hidden"], _S["mine"]
    w, h = ras.w, ras.h
    out = collections.defaultdict(collections.Counter)
    # what each pixel shows: its surface kind and, for tops, the level
    label = [None] * (w * h)
    for i, o in enumerate(ras.tri):
        if o < 0:
            continue
        if o >= n:
            cell = floor[o - n][0]
            # a map's border: what lies past it is the next map's
            border = cell[0] in (0, _S["w"] - 1) or cell[1] in (0, _S["h"] - 1)
            if cell not in hidden and not border:
                out[cell]["hole"] += 1
                marks[i] = "hole"
            continue
        k = kinds[o]
        cell = cells[o]
        if k is None or cell in hidden:
            continue
        if k in ("deformed", "side"):
            # a wall belongs to whichever cell it stands on - a ground cell
            # beside a band's foot too
            out[cell][k] += 1
            marks[i] = k
        if cell not in mine:
            continue
        if k == "top":
            lv = int(math.floor(sum(p[1] for p in _S["tris"][o]) / 3 + gc.LIFTS))
            label[i] = ("top", lv)
        else:
            label[i] = (k, None)
    # tiny pieces: connected runs of one kind (one level for tops) too small
    seen = bytearray(w * h)
    for i0 in range(w * h):
        if seen[i0] or label[i0] is None:
            continue
        lab = label[i0]
        stack, comp = [i0], []
        seen[i0] = 1
        while stack:
            i = stack.pop()
            comp.append(i)
            x, y = i % w, i // w
            for j in ((i - 1) if x > 0 else -1, (i + 1) if x + 1 < w else -1,
                      (i - w) if y > 0 else -1, (i + w) if y + 1 < h else -1):
                if j >= 0 and not seen[j] and label[j] == lab:
                    seen[j] = 1
                    stack.append(j)
        if len(comp) < small:
            for i in comp:
                o = ras.tri[i]
                if 0 <= o < n:
                    out[cells[o]]["tiny"] += 1
                    marks[i] = "tiny"
    return out


def _one(job):
    layout, at, yaw, trial = job
    if "tris" not in _S:
        _setup(layout, trial)
    cam, ras, n = shoot(at, yaw)
    small = max(6.0, SMALL * tile_area(cam, at))
    return judge(ras, n, small)


def check(layout, trial=None):
    import multiprocessing
    A = vl.vb.LayoutArt(layout)
    spots = [(x, y) for y in range(2, A.h - 1, STEP) for x in range(2, A.w - 1, STEP)]
    jobs = [(layout, at, yaw, trial) for at in spots for yaw in YAWS]
    total = collections.defaultdict(collections.Counter)
    with multiprocessing.Pool(max(1, os.cpu_count() - 2)) as pool:
        for out in pool.imap_unordered(_one, jobs):
            for cell, c in out.items():
                total[cell].update(c)
    return total


def view(layout, at, yaw, path, trial=None):
    """The shapes from one camera, every fault painted."""
    from PIL import Image
    _setup(layout, trial)
    cam, ras, n = shoot(at, yaw)
    small = max(6.0, SMALL * tile_area(cam, at))
    tris = _S["tris"]
    img = Image.new("RGB", (ras.w, ras.h))
    px = img.load()
    for i, o in enumerate(ras.tri):
        x, y = i % ras.w, i // ras.w
        if o < 0:
            px[x, y] = (0, 0, 0)
        elif o >= n:
            px[x, y] = (70, 70, 70)
        else:
            px[x, y] = gc.plain(tris[o])
    marks = {}
    faults = judge(ras, n, small, marks)
    paint = {"hole": (255, 0, 255), "deformed": (255, 0, 0), "side": (255, 140, 0), "tiny": (0, 255, 255)}
    for i, k in marks.items():
        px[i % ras.w, i // ras.w] = paint[k]
    print("view %s yaw %g: %s" % (at, yaw, dict(sum((c for c in faults.values()), collections.Counter()))))
    img.resize((img.width * 3, img.height * 3), Image.NEAREST).save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("layouts")
    ap.add_argument("--relief", default=None)
    ap.add_argument("--at", default=None)
    ap.add_argument("--yaw", type=float, default=0.0)
    ap.add_argument("--view", default=None)
    ap.add_argument("--list", type=int, default=20)
    args = ap.parse_args()
    trial = os.path.abspath(args.relief) if args.relief else None
    if args.view:
        view(args.layouts, tuple(int(v) for v in args.at.split(",")), args.yaw, args.view, trial)
        return
    bad = 0
    for layout in args.layouts.split(","):
        total = check(layout, trial)
        sums = collections.Counter()
        cells = collections.Counter()
        for cell, c in total.items():
            for k, v in c.items():
                if v >= 3:
                    sums[k] += v
                    cells[k] += 1
        bad += sum(cells.values())
        print("%s: %s" % (layout, ", ".join("%s %d cells (%d px)" % (k, cells[k], sums[k]) for k in KINDS)))
        worst = sorted(total.items(), key=lambda kv: -sum(kv[1].values()))[:args.list]
        for cell, c in worst:
            print("  %3d,%-3d %s" % (cell[0], cell[1], dict(c)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
