#!/usr/bin/env python3
"""Is every mountain made of the tile grid? Checked on the engine's own mesh.

A mountain built from square tiles has only four kinds of surface, and
they all lie on the 16-pixel grid of the cells:

  top    level (normal straight up), at a whole level
  face   a vertical wall facing south or north, on a cell's row edge
  side   a vertical wall facing west or east, on a cell's column edge
  band   a slope facing west or east, one level across its tile (45
         degrees) or two (a diagonal flank's head): its normal has no
         north/south part

and, listed apart, a "step": a south step the drawing has no face for (a
top over lower ground with nothing drawn between them), closed by a wall
standing on that one row of the drawing.

(the 45-degree view puts a drawn pixel at (u, h, v + h): a face tile hung
from a top at level L is the vertical wall z = row + L, a whole cell line.)
Any other triangle - a slope, a hip, a sheared or stretched piece - is a
DEFORMATION, and so is a wall or a top off the grid. This reads the
triangles tests/voxel_mesh_dump.c gets from voxel_mesh_builder.c, so it
judges what the console draws, not a model of it.

    python voxel_grid_check.py LAYOUT_ROUTE116 [--relief build/x.bin]
        faulty cells, listed, and a map of them (--png)
    python voxel_grid_check.py LAYOUT_ROUTE116 --holes
        also every cell the player's cameras see the void through
    python voxel_grid_check.py LAYOUT_ROUTE116 --at 45,10 --view v.png
        the game's camera on a cell, deformed triangles painted red

Ledges and berry soil are kept as they always were and are not judged.
"""

import argparse
import collections
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import voxel_relief_lines as vl  # noqa: E402

TOL_N = 0.02          # how far a normal may lean (sin) and still be a wall/top
TOL_GRID = 0.07       # how far off the grid (cells) a wall or a top may stand
RAMP_LO, RAMP_HI = 0.9, 2.1   # a band: one level (1:1) or two (2:1) across its tile
LIFTS = 0.2           # fills and cut layers sit a pixel or two under a level


def normal(tri):
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = [p[:3] for p in tri]
    ax, ay, az = x1 - x0, y1 - y0, z1 - z0
    bx, by, bz = x2 - x0, y2 - y0, z2 - z0
    nx, ny, nz = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
    n = math.sqrt(nx * nx + ny * ny + nz * nz)
    return (nx / n, ny / n, nz / n, n / 2) if n > 1e-12 else None


def off_grid(v, tol=TOL_GRID):
    return abs(v - round(v)) > tol


def judge(tri):
    """None if the triangle is a grid surface, else what is wrong with it."""
    nm = normal(tri)
    if nm is None:
        return None
    nx, ny, nz, area = nm
    if area < 1e-4:
        return None
    if abs(ny) > 1 - TOL_N:
        ys = [p[1] for p in tri]
        y = sum(ys) / 3
        # level tops at whole levels; fills/cut layers a little under one
        k = y - math.floor(y + LIFTS)
        if max(ys) - min(ys) > 0.01 or (k > TOL_GRID and k < 1 - LIFTS):
            return "top"
        return None
    if abs(ny) < TOL_N:
        # a wall: on a cell's edge, or inside a tile where the drawing puts
        # it (a face starting down its tile, a corner turned at 45 degrees)
        return None
    if abs(nz) < TOL_N and RAMP_LO < abs(nx) / max(abs(ny), 1e-9) < RAMP_HI:
        # a band: the 45-degree slope one level across its tile, falling
        # west or east, straight along the column (its edges on the grid)
        return None
    if abs(ny) < TOL_N:
        # a wall turning a corner inside its tile, as the drawing rounds
        # it (a 45-degree bevel of the lattice): still a vertical wall
        return None
    vs = [p[2] - p[1] for p in tri]
    if max(vs) - min(vs) < 0.01 and abs(nx) < TOL_N:
        # a south step no face is drawn for: the wall stands on one row of
        # the drawing (zero tall at 45 degrees, a thin slant from the game)
        return "step" if not off_grid(vs[0]) else "slope"
    return "slope"


def cell_of(tri):
    x = sum(p[0] for p in tri) / 3
    y = sum(p[1] for p in tri) / 3
    z = sum(p[2] for p in tri) / 3
    return (int(math.floor(x)), int(math.floor(z - y)))


def kept_cells(layout_id):
    """Ledges and berry soil: never altered, never judged."""
    import voxel_terraces as vt
    members = vt.group_of(layout_id)
    c = vt.Cells(members)
    return {c.src[cell][1:] for cell in c.keep if c.src[cell][0] == layout_id}


def is_ramp(tri):
    nm = normal(tri)
    return nm is not None and abs(nm[2]) < TOL_N and RAMP_LO < abs(nm[0]) / max(abs(nm[1]), 1e-9) < RAMP_HI


def faults(tris, keep=()):
    out = collections.defaultdict(collections.Counter)
    bad = set()
    # the head and the foot of a band: a ramp under a top (or over lower
    # ground) meets it along a slant no drawing row is for - the drawing's
    # own corner, not a fault
    ramps = {cell_of(t) for t in tris if is_ramp(t)}
    for k, tri in enumerate(tris):
        if vl.METATILE_REAL <= vl.atlas_id(tri) < vl.CUT_FIRST:
            continue            # modelled props: not terrain
        why = judge(tri)
        if why is None:
            continue
        cell = cell_of(tri)
        if cell in keep:
            continue
        if why == "step" and ({cell, (cell[0], cell[1] + 1), (cell[0], cell[1] - 1)} & ramps):
            continue
        out[cell][why] += 1
        bad.add(k)
    return out, bad


TOP_COLOURS = [(230, 220, 190), (180, 215, 160), (235, 190, 150), (170, 200, 225),
               (225, 170, 210), (210, 225, 120)]


def plain(tri):
    """A flat colour for a triangle by the way it faces: tops light, south
    faces mid, west/east sides dark, north backs blue, the rest red."""
    nm = normal(tri)
    if nm is None:
        return (255, 0, 255)
    nx, ny, nz = nm[:3]
    if abs(ny) > 1 - TOL_N:
        # each level its own colour, so tops at different levels part
        lv = int(math.floor(sum(p[1] for p in tri) / 3 + LIFTS))
        return TOP_COLOURS[lv % len(TOP_COLOURS)]
    if abs(nz) > 1 - TOL_N:
        return (150, 120, 100) if nz > 0 else (90, 110, 170)
    if abs(nx) > 1 - TOL_N:
        return (95, 75, 60) if nx < 0 else (70, 55, 45)
    if abs(nz) < TOL_N and RAMP_LO < abs(nx) / max(abs(ny), 1e-9) < RAMP_HI:
        return (200, 150, 110) if nx < 0 else (170, 125, 95)
    if abs(ny) < TOL_N:
        return (125, 105, 190)      # a wall turning a corner (a bevel)
    return (230, 30, 30)


def view(tris, bad, at, path, pitch, distance, yaw, layout, shapes=False):
    atlas = vl.Atlas(layout)
    cam = vl.camera(tris, at, pitch, distance, yaw)
    ras = vl.render(tris, atlas, cam, 2)
    img = ras.image()
    px = img.load()
    if shapes:
        # geometry only: each surface by the way it faces, edges drawn
        for i, owner in enumerate(ras.tri):
            if owner >= 0:
                px[i % ras.w, i // ras.w] = plain(tris[owner])
        for i, owner in enumerate(ras.tri):
            x, y = i % ras.w, i // ras.w
            if owner >= 0 and x + 1 < ras.w and y + 1 < ras.h:
                a, b = ras.tri[i + 1], ras.tri[i + ras.w]
                if (a >= 0 and plain(tris[a]) != plain(tris[owner])) or                         (b >= 0 and plain(tris[b]) != plain(tris[owner])):
                    px[x, y] = (30, 25, 20)
    for i, owner in enumerate(ras.tri):
        if owner in bad:
            x, y = i % ras.w, i // ras.w
            r, g, b = px[x, y]
            px[x, y] = (min(255, r // 2 + 140), g // 3, b // 3)
    img.save(path)


# -- holes: what the camera sees through ------------------------------------
#
# A floor is laid just under the whole map and the terrain drawn over it from
# the player's cameras, every surface opaque (the dump's props too): wherever
# the floor shows inside the map, the game would show the void (black) there.

FLOOR = 0.3             # cells under the lowest terrain round a cell
_HOLES = {}


def _hole_one(job):
    layout, at, yaw, trial = job
    if trial:
        vl.use_relief(trial)
    if "tris" not in _HOLES:
        tris, _ = vl.dump(layout)
        A = vl.vb.LayoutArt(layout)
        # a little under the lowest terrain round each cell: deep enough to
        # show through any gap, shallow enough not to be seen past the map
        lowest = {}
        for t in tris:
            cell = cell_of(t)
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
        # props (trees, boulders, sea rocks) and houses are models the dump
        # does not draw: what is seen through round them is theirs to cover
        import voxel_terraces as vt
        c = vt.Cells(vt.group_of(layout))
        hidden = set()
        for cell, k in c.kind.items():
            lid, x, y = c.src[cell]
            if lid == layout and (k == vt.UNKNOWN or c.role(cell) in ("house", "building", "roof")):
                hidden |= {(x + i, y + j) for i in (-1, 0, 1) for j in (-1, 0, 1)}
        _HOLES.update(tris=tris, floor=floor, w=A.w, h=A.h, hidden=hidden)
    tris, floor = _HOLES["tris"], _HOLES["floor"]
    from PIL import Image
    cam = vl.camera(tris, at, 46.0, 9.0, yaw)
    ras = vl.Raster(cam.width, cam.height, bg=(0, 0, 0))
    tex = Image.new("RGBA", (1, 1), (255, 255, 255, 255))
    tpx = tex.load()
    reach = cam.distance * 2.2
    n = len(tris)
    for k, tri in enumerate(list(tris) + [t for _, t in floor]):
        # culled by where it is drawn (u, v), the floor as what is over it
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
    out = collections.Counter()
    for owner in ras.tri:
        if owner >= n:
            fx, fy = floor[owner - n][0]
            if 2 <= fx < _HOLES["w"] - 2 and 2 <= fy < _HOLES["h"] - 2 and (fx, fy) not in _HOLES["hidden"]:
                out[(fx, fy)] += 1
    return out


def hole_view(layout, at, yaw, path, trial=None, pitch=46.0, distance=9.0):
    """The flat-colour view with the floor under the terrain magenta."""
    from PIL import Image
    _hole_one((layout, at, yaw, trial))
    tris, floor = _HOLES["tris"], _HOLES["floor"]
    cam = vl.camera(tris, at, pitch, distance, yaw)
    ras = vl.Raster(cam.width * 2, cam.height * 2, bg=(0, 0, 0))
    cam2 = vl.vb.Camera(cam.target, cam.pitch, cam.yaw, cam.distance, cam.fov, ras.w, ras.h)
    tex = Image.new("RGBA", (1, 1), (255, 255, 255, 255))
    tpx = tex.load()
    n = len(tris)
    for k, tri in enumerate(list(tris) + [t for _, t in floor]):
        vs = []
        for (x, y, z, u, v, sh) in tri:
            pr = cam2.project((x, y, z))
            if pr is None:
                break
            vs.append((pr[0], pr[1], pr[2], pr[3], 0.0, 0.0))
        else:
            ras.draw(vs, tex, tpx, 1.0, k)
    img = Image.new("RGB", (ras.w, ras.h))
    px = img.load()
    for i, o in enumerate(ras.tri):
        px[i % ras.w, i // ras.w] = (0, 0, 0) if o < 0 else (255, 0, 255) if o >= n else plain(tris[o])
    img.save(path)


def holes(layout, trial=None, step=6):
    """{floor cell: pixels}: where the cameras see through the terrain."""
    import multiprocessing
    A = vl.vb.LayoutArt(layout)
    spots = [(x, y) for y in range(2, A.h - 1, step) for x in range(2, A.w - 1, step)]
    jobs = [(layout, at, yaw, trial) for at in spots for yaw in (0.0, 30.0, -30.0)]
    total = collections.Counter()
    with multiprocessing.Pool(max(1, os.cpu_count() - 2)) as pool:
        for out in pool.imap_unordered(_hole_one, jobs):
            total.update(out)
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("layouts", help="LAYOUT_A[,LAYOUT_B...]")
    ap.add_argument("--relief", default=None)
    ap.add_argument("--at", default=None)
    ap.add_argument("--view", default=None)
    ap.add_argument("--pitch", type=float, default=46.0)
    ap.add_argument("--distance", type=float, default=11.0)
    ap.add_argument("--yaw", type=float, default=0.0)
    ap.add_argument("--list", type=int, default=25)
    ap.add_argument("--shapes", action="store_true", help="the view in flat colours by the way surfaces face")
    ap.add_argument("--holes", action="store_true", help="also look for holes from the player's cameras (slow)")
    args = ap.parse_args()
    if args.relief:
        vl.use_relief(args.relief)
    total = 0
    for layout in args.layouts.split(","):
        tris, _ = vl.dump(layout)
        keep = kept_cells(layout)
        found, bad = faults(tris, keep)
        total += len(found)
        kinds = collections.Counter()
        for c in found.values():
            kinds.update(c)
        print("%s: %d deformed cells (%s)" % (layout, len(found),
              ", ".join("%s %d" % kv for kv in sorted(kinds.items())) or "none"))
        for cell, c in sorted(found.items(), key=lambda kv: -sum(kv[1].values()))[:args.list]:
            print("  %3d,%-3d %s" % (cell[0], cell[1], dict(c)))
        if args.holes and not args.at:
            seen = holes(layout, args.relief and os.path.abspath(args.relief))
            seen = {c: n for c, n in seen.items() if n >= 3}
            total += len(seen)
            print("%s: %d cells seen through (holes)" % (layout, len(seen)))
            for cell, n in sorted(seen.items(), key=lambda kv: -kv[1])[:args.list]:
                print("  %3d,%-3d %d px" % (cell[0], cell[1], n))
        if args.view and args.at and args.holes:
            at = tuple(int(v) for v in args.at.split(","))
            hole_view(layout, at, args.yaw, args.view, args.relief and os.path.abspath(args.relief),
                      args.pitch, args.distance)
            print("view:", args.view)
        elif args.view and args.at:
            at = tuple(int(v) for v in args.at.split(","))
            view(tris, bad, at, args.view, args.pitch, args.distance, args.yaw, layout, args.shapes)
            print("view:", args.view)
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
