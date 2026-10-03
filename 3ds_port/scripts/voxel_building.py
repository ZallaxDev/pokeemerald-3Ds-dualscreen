#!/usr/bin/env python3
"""Buildings rebuilt in 3D from their own drawing, pixel for pixel.

The premise
-----------
An Emerald building is drawn in a fixed oblique projection: a wall is seen
face on and the ground straight from above, both at one pixel per pixel. That
is exactly what an orthographic camera pitched at 45 degrees sees (scaled by
sqrt 2). A point (X, Y, Z) of the world - X east, Y up, Z south, in pixels -
lands on the art at

    u = X        v = Z - Y

so the drawing is not a texture to be wrapped around a guessed box: it is the
model seen from one direction. Every surface is textured by that fact - one
art texel per GBA pixel seen across the surface at 45 degrees, `density_check`
proves it for every triangle - so nothing is ever stretched or squashed, and
`ortho_check` renders the model in the GBA projection and compares it pixel
for pixel with the art over the regions a spec declares exact.

The drawing is shallower than the building. Emerald draws a four-row house in
five rows of art, which read literally gives a house two tiles deep. A model
keeps the real footprint (the collision) and covers the depth the drawing
lacks the way the building would: more courses of the same roof tiles, more
plaster from the same column (`Strip`, `Tile`). What the drawing never shows -
the sides, the back - is dressed from the same art. No new pixels are painted
and nothing depends on the game's camera, so the model stays right at any
pitch or yaw.

The ground
----------
Emerald paints the ground under a building's edges into the building's own
metatiles. A pixel belongs to the building when its top layer is opaque or
when its bottom-layer 8x8 tile is not one of the ground tiles around it. The
rest is transparent in the model's texture, and the cells the building covers
get the plain ground metatile drawn flat underneath instead.

Modelling vocabulary
--------------------
  Proj     a wall placed at its drawn depth: u = X, v = Z - Y, exactly.
  Strip    courses laid up a face from its eave: the drawn eave rows, then
           repeats of a clean course, at the 45-degree density.
  Tile     an art rectangle repeated 1:1 (side walls, corner posts).
  Prism    a (Z, Y) polygon extruded over X; its edges take Proj/Strip/Tile,
           its two end caps are tiled by Bands.
  HipRoof  a hipped roof with ridge teeth and cap, all four slopes tiled.
  Frustum  walls on any convex plan, a sloped roof band and a flat top
           (the octagonal Pokemon Center and Mart).
  Cylinder a short round thing (a vent), projected where it is drawn.
  Vault    a barrel vault on a flat top (the Pokemon Center's crown).
  Walls    walls along an open plan, projected (a chamfered porch).
  Relief   a solid read column by column off its drawing (hedges, walls).
  Mound    a rounded rock: every drawn pixel lifted onto its own dome.
"""

import math
import os
import struct
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dump_region_art import incbin_map, read_u16, Tilesets, ROOT  # noqa: E402

import json  # noqa: E402

NUM_PRIMARY = 512
EPS = 1e-6

# Face shades for the model's faces: the drawing already carries Emerald's lighting, so a face the GBA camera sees
# keeps shade 1 and only the sides the drawing never lit are darkened.
SHADE_ART = 1.0
SHADE_WEST = 0.80
SHADE_EAST = 0.72
SHADE_BACK = 0.66
# A face the drawing never shows whose winding is its outside (a rock's back,
# turned every way): the console lights it as wound, not by a side's rule.
SHADE_WOUND = 0.90


# ── Art ─────────────────────────────────────────────────────────────────────

class LayoutArt:
    """A layout's metatiles, composed the way the GBA composes them."""

    def __init__(self, layout_id):
        self.attrs = incbin_map(r"gMetatileAttributes_(\w+)\[\]\s*=\s*INCBIN_U16\(\"([^\"]+)\"\)")
        self.mts = incbin_map(r"gMetatiles_(\w+)\[\]\s*=\s*INCBIN_U16\(\"([^\"]+)\"\)")
        layouts = json.load(open(os.path.join(ROOT, "data", "layouts", "layouts.json"),
                                 encoding="utf-8"))["layouts"]
        entry = next(e for e in layouts if e["id"] == layout_id)
        self.layout_index = layouts.index(entry) + 1
        self.id = layout_id
        self.blocks = read_u16(os.path.join(ROOT, entry["blockdata_filepath"]))
        self.w, self.h = entry["width"], entry["height"]
        self.primary, self.secondary = entry["primary_tileset"], entry["secondary_tileset"]
        self.ts = Tilesets(self.primary, self.secondary)
        self.pm = read_u16(self.mts[self.primary])
        self.sm = read_u16(self.mts[self.secondary])

    def metatile(self, x, y):
        return self.blocks[y * self.w + x] & 0x3FF

    def entries(self, m):
        if m < NUM_PRIMARY:
            return self.pm[m * 8:m * 8 + 8]
        return self.sm[(m - NUM_PRIMARY) * 8:(m - NUM_PRIMARY) * 8 + 8]

    def subtiles(self, m, layer):
        """Four 8x8 blocks of (rgb, colour index), flips applied, row major."""
        out = []
        for entry in self.entries(m)[layer * 4:layer * 4 + 4]:
            data = self.ts.subtile(entry & 0x3FF, (entry >> 12) & 0xF)
            fx, fy = entry & 0x400, entry & 0x800
            out.append([data[(7 - y if fy else y) * 8 + (7 - x if fx else x)]
                        for y in range(8) for x in range(8)])
        return out

    def ground_tiles(self, metatiles):
        """The bottom-layer 8x8 drawings of the given ground metatiles."""
        found = set()
        for m in metatiles:
            for block in self.subtiles(m, 0):
                found.add(tuple(rgb for rgb, _ in block))
        return found

    def ground_pixels(self, metatiles):
        """The bottom layers of the ground metatiles, pixel by pixel."""
        out = []
        for m in metatiles:
            low = self.subtiles(m, 0)
            grid = [[None] * 16 for _ in range(16)]
            for q in range(4):
                for k in range(64):
                    grid[(q >> 1) * 8 + k // 8][(q & 1) * 8 + k % 8] = low[q][k][0]
            out.append(grid)
        return out

    def building_art(self, x0, y0, w, h, ground, cells=None, ground_px=None, upper=False):
        """RGBA image of the cells, with every ground pixel transparent.

        A bottom-layer 8x8 block that is one of the ground's is ground whole.
        Where the building's own blocks carry the ground painted in beside it
        (a hedge's flank, a fence post) the test goes down to the pixel: with
        `ground_px`, a bottom-layer pixel equal to a ground metatile's pixel at
        the same place in its cell is ground too. `cells` limits the image to
        the (cx, cy) cells that belong to the object. With `upper`, only the
        upper layer: an object drawn whole on it (a railing) over a bottom
        layer that is the map's ground, edges and all.
        """
        img = Image.new("RGBA", (w * 16, h * 16), (0, 0, 0, 0))
        px = img.load()
        for cy in range(h):
            for cx in range(w):
                if cells is not None and (cx, cy) not in cells:
                    continue
                m = self.metatile(x0 + cx, y0 + cy)
                low, high = self.subtiles(m, 0), self.subtiles(m, 1)
                for q in range(4):
                    is_ground = tuple(rgb for rgb, _ in low[q]) in ground
                    for k in range(64):
                        lx, ly = (q & 1) * 8 + k % 8, (q >> 1) * 8 + k // 8
                        X, Y = cx * 16 + lx, cy * 16 + ly
                        rgb1, i1 = high[q][k]
                        rgb0, _ = low[q][k]
                        if i1:
                            px[X, Y] = rgb1 + (255,)
                        elif upper:
                            continue
                        elif not is_ground:
                            if ground_px and any(g[ly][lx] == rgb0 for g in ground_px):
                                continue
                            px[X, Y] = rgb0 + (255,)
        return img

    def cell_image(self, m, layers=(0, 1)):
        """A whole metatile as the ground renderer draws it (with `layers`
        (0,), its lower layer alone)."""
        img = Image.new("RGBA", (16, 16), (0, 0, 0, 255))
        px = img.load()
        low, high = self.subtiles(m, 0), self.subtiles(m, 1)
        for q in range(4):
            for k in range(64):
                X = (q & 1) * 8 + k % 8
                Y = (q >> 1) * 8 + k // 8
                rgb1, i1 = high[q][k]
                px[X, Y] = (rgb1 if i1 and 1 in layers else low[q][k][0]) + (255,)
        return img


# ── Texture mappings ───────────────────────────────────────────────────────

class Proj:
    """The GBA projection: u = X, v = Z - Y, with the rows clamped.

    Rows outside [lo, hi) are drawn by some other surface in the art; where
    this face reaches into them it is hidden in the GBA view (under an eave,
    behind a ridge) and takes the nearest row of its own instead of borrowing
    another surface's pixels.
    """

    def __init__(self, lo=None, hi=None):
        self.lo, self.hi = lo, hi


PROJ = Proj()


class Tile:
    """An art rectangle repeated at one texel per pixel.

    `s` runs along the face's horizontal direction (X for an edge face, Z for
    a cap), `t` runs down from `top`. `period` defaults to the rectangle.
    """

    def __init__(self, u0, v0, u1, v1, s0=0.0, top=None, flip=False):
        self.rect = (u0, v0, u1, v1)
        self.s0, self.top, self.flip = s0, top, flip


class Band:
    """A cap material over the heights [y0, y1).

    `tile` repeats along Z from `z0` (southwards edge first). `front` puts an
    extra art strip at the south end of the band - a facade's corner post.
    """

    def __init__(self, y0, y1, tile, z0, front=None, back=None, z1=None):
        self.y0, self.y1, self.tile, self.z0 = y0, y1, tile, z0
        self.front, self.back = front, back
        self.length = (z0 - z1) if z1 is not None else 1e9


class Prism:
    """A (Z, Y) polygon extruded over [x0, x1].

    `poly` lists (z, y) points counter-clockwise when seen from the west
    (+Z to the right, +Y up): south face first is the natural reading order.
    `edges` maps an edge index (from point i to point i+1) to its material;
    anything absent is PROJ when it faces the GBA camera and skipped when it
    faces straight down. `caps` is the band list for both ends, or None.
    """

    def __init__(self, name, x0, x1, poly, edges=None, caps=None,
                 west=True, east=True, skip=()):
        self.name, self.x0, self.x1 = name, x0, x1
        self.poly = poly
        self.edges = edges or {}
        self.caps = caps
        self.west, self.east = west, east
        self.skip = set(skip)


# ── Geometry kernel ───────────────────────────────────────────────────────

class Mesh:
    def __init__(self):
        self.tris = []  # ((x,y,z,u,v) * 3, shade, tag)

    def tri(self, a, b, c, shade, tag):
        self.tris.append(((a, b, c), shade, tag))

    def poly(self, pts, shade, tag):
        for i in range(1, len(pts) - 1):
            self.tri(pts[0], pts[i], pts[i + 1], shade, tag)


def clip(poly, axis, value, keep_greater):
    """Sutherland-Hodgman against one axis-aligned half plane.

    `poly` is a list of tuples; `axis` indexes the coordinate tested. Every
    other coordinate is interpolated, so texture coordinates carried in the
    tuple stay exact.
    """
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ina = (a[axis] >= value - EPS) if keep_greater else (a[axis] <= value + EPS)
        inb = (b[axis] >= value - EPS) if keep_greater else (b[axis] <= value + EPS)
        if ina:
            out.append(a)
        if ina != inb:
            t = (value - a[axis]) / (b[axis] - a[axis])
            out.append(tuple(a[k] + (b[k] - a[k]) * t for k in range(len(a))))
    return out


def triangulate(poly):
    """Ear clipping for a simple polygon of (z, y) points, either winding."""
    pts = list(range(len(poly)))
    area = sum(poly[i][0] * poly[(i + 1) % len(poly)][1] -
               poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))
    sign = 1 if area > 0 else -1
    tris = []

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    guard = 0
    while len(pts) > 3 and guard < 10000:
        guard += 1
        for k in range(len(pts)):
            i0, i1, i2 = pts[k - 1], pts[k], pts[(k + 1) % len(pts)]
            a, b, c = poly[i0], poly[i1], poly[i2]
            if cross(a, b, c) * sign <= EPS:
                continue
            inside = False
            for j in pts:
                if j in (i0, i1, i2):
                    continue
                p = poly[j]
                if (cross(a, b, p) * sign > EPS and cross(b, c, p) * sign > EPS
                        and cross(c, a, p) * sign > EPS):
                    inside = True
                    break
            if inside:
                continue
            tris.append((i0, i1, i2))
            del pts[k]
            break
        else:
            break
    if len(pts) == 3:
        tris.append(tuple(pts))
    return tris


def tile_pieces(poly2d, tile, s_of, t_of):
    """Split a planar convex polygon into pieces that each sit in one tile.

    `poly2d` carries (s, t, ...) where s, t are the face coordinates in
    pixels; each returned piece has its (u, v) appended. Within one piece the
    art mapping is a translation, so its UVs are exact.
    """
    u0, v0, u1, v1 = tile.rect
    pw, ph = u1 - u0, v1 - v0
    ss = [p[0] for p in poly2d]
    ts = [p[1] for p in poly2d]
    out = []
    i0 = math.floor((min(ss) - tile.s0) / pw + EPS)
    i1 = math.ceil((max(ss) - tile.s0) / pw - EPS)
    j0 = math.floor(min(ts) / ph + EPS)
    j1 = math.ceil(max(ts) / ph - EPS)
    for i in range(i0, i1):
        for j in range(j0, j1):
            piece = poly2d
            sa, sb = tile.s0 + i * pw, tile.s0 + (i + 1) * pw
            ta, tb = j * ph, (j + 1) * ph
            piece = clip(piece, 0, sa, True)
            if len(piece) >= 3:
                piece = clip(piece, 0, sb, False)
            if len(piece) >= 3:
                piece = clip(piece, 1, ta, True)
            if len(piece) >= 3:
                piece = clip(piece, 1, tb, False)
            if len(piece) < 3:
                continue
            res = []
            for p in piece:
                fs = p[0] - sa
                if tile.flip:
                    fs = pw - fs
                res.append(p + (u0 + fs, v0 + (p[1] - ta)))
            out.append(res)
    return out


def edge_normal(a, b):
    """Outward normal (nz, ny) of a CCW polygon edge a -> b in (z, y)."""
    dz, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dz, dy)
    return (dy / length, -dz / length)


def polygon_ccw(poly):
    area = sum(poly[i][0] * poly[(i + 1) % len(poly)][1] -
               poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))
    return area > 0


def emit_prism(mesh, pr):
    poly = pr.poly if polygon_ccw(pr.poly) else list(reversed(pr.poly))
    if poly is not pr.poly:
        raise ValueError("%s: polygon must be counter-clockwise (+Z right, +Y up)" % pr.name)
    n = len(poly)
    x0, x1 = pr.x0, pr.x1

    def proj_uv(x, z, y, lo, hi):
        v = z - y
        if lo is not None and v < lo:
            v = lo + 0.5
        if hi is not None and v > hi:
            v = hi - 0.5
        return (x, v)

    for i in range(n):
        if i in pr.skip:
            continue
        a, b = poly[i], poly[(i + 1) % n]
        nz, ny = edge_normal(a, b)
        mat = pr.edges.get(i)
        facing = nz + ny  # dot with the GBA view direction (0, 1, 1)
        if mat is None:
            if ny < -0.5 or facing <= EPS:
                continue  # underside, or edge-on / away and not dressed
            mat = PROJ
        shade = SHADE_ART if facing > EPS else SHADE_BACK
        tag = "%s.e%d" % (pr.name, i)
        if isinstance(mat, Proj):
            # Split the edge where the projected row crosses a clamp, so every
            # piece is either exactly projected or one constant row.
            cuts = [0.0, 1.0]
            va, vb = a[0] - a[1], b[0] - b[1]
            for lim in (mat.lo, mat.hi):
                if lim is not None and abs(vb - va) > EPS:
                    t = (lim - va) / (vb - va)
                    if EPS < t < 1 - EPS:
                        cuts.append(t)
            cuts.sort()
            for k in range(len(cuts) - 1):
                ta, tb = cuts[k], cuts[k + 1]
                pa = (a[0] + (b[0] - a[0]) * ta, a[1] + (b[1] - a[1]) * ta)
                pb = (a[0] + (b[0] - a[0]) * tb, a[1] + (b[1] - a[1]) * tb)
                mid_v = (pa[0] - pa[1] + pb[0] - pb[1]) / 2
                clamp_lo = mat.lo is not None and mid_v < mat.lo
                clamp_hi = mat.hi is not None and mid_v > mat.hi
                quad = []
                for (x, p) in ((x0, pa), (x1, pa), (x1, pb), (x0, pb)):
                    if clamp_lo:
                        uv = (x, mat.lo + 0.5)
                    elif clamp_hi:
                        uv = (x, mat.hi - 0.5)
                    else:
                        uv = proj_uv(x, p[0], p[1], None, None)
                    quad.append((x, p[1], p[0]) + uv)
                mesh.poly(quad, shade, tag + ("~clamp" if clamp_lo or clamp_hi else ""))
        elif isinstance(mat, Strip):
            start, end = (b, a) if mat.from_end else (a, b)
            tdir = _unit((0.0, end[1] - start[1], end[0] - start[0]))
            pts = [(x0, a[1], a[0]), (x1, a[1], a[0]), (x1, b[1], b[0]), (x0, b[1], b[0])]
            strip_face(mesh, pts, (0.0, start[1], start[0]), (1.0, 0.0, 0.0), tdir,
                       mat, shade, tag)
        elif isinstance(mat, Tile):
            # s along X, t measured down the edge from its upper end.
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            upper, lower = (a, b) if a[1] >= b[1] else (b, a)
            face = [(x0, 0.0), (x1, 0.0), (x1, length), (x0, length)]
            for piece in tile_pieces(face, mat, None, None):
                pts = []
                for s, t, u, v in piece:
                    f = t / length if length > EPS else 0.0
                    z = upper[0] + (lower[0] - upper[0]) * f
                    y = upper[1] + (lower[1] - upper[1]) * f
                    pts.append((s, y, z, u, v))
                mesh.poly(pts, shade, tag)

    if pr.caps is None:
        return
    tris = triangulate(poly)
    for side, x, shade in (("w", x0, SHADE_WEST), ("e", x1, SHADE_EAST)):
        if (side == "w" and not pr.west) or (side == "e" and not pr.east):
            continue
        for band in pr.caps:
            for tri in tris:
                piece = [poly[k] for k in tri]
                piece = clip(piece, 1, band.y0, True)
                if len(piece) >= 3:
                    piece = clip(piece, 1, band.y1, False)
                if len(piece) < 3:
                    continue
                emit_cap_piece(mesh, piece, band, x, side, shade,
                               "%s.cap%s" % (pr.name, side))


def emit_cap_piece(mesh, piece, band, x, side, shade, tag):
    """A cap piece tiled by its band: s runs north from the band's z0."""
    top = band.y1 if band.tile.top is None else band.tile.top
    strips = []
    s_front = 0.0
    if band.front is not None:
        fw = band.front.rect[2] - band.front.rect[0]
        strips.append((band.front, 0.0, fw))
        s_front = fw
    s_back = 1e9
    if band.back is not None:
        bw = band.back.rect[2] - band.back.rect[0]
        s_back = band.length - bw
    strips.append((band.tile, s_front, s_back))
    if band.back is not None:
        strips.append((band.back, s_back, s_back + bw))
    for tile, sa, sb in strips:
        pts = [(band.z0 - z, top - y) for z, y in piece]
        pts = clip(pts, 0, sa, True)
        if len(pts) >= 3:
            pts = clip(pts, 0, sb, False)
        if len(pts) < 3:
            continue
        shifted = Tile(*tile.rect, s0=sa, flip=(tile.flip != (side == "w")))
        for sub in tile_pieces(pts, shifted, None, None):
            out = [(x, top - t, band.z0 - s, u, v) for s, t, u, v in sub]
            mesh.poly(out, shade, tag)


# ── Faces laid in courses, and roofs ────────────────────────────────────

class Strip:
    """Tile courses laid up a face from its eave, one texel per GBA pixel.

    A roof is drawn as a few courses of tiles, fewer than a roof of the
    building's real depth needs. Rather than stretch them, the face is built
    the way a roof is: the eave course the art draws (`fixed`, rows [a, b),
    bottom row at the eave) and then as many more courses as the depth takes,
    repeating rows [c, d) of the same art upwards, starting at row `start`
    (default d - 1) so the pattern continues exactly where the fixed rows end.
    With no `repeat` the last fixed row is held (a face the art hides).

    Along the face the art is laid at cos(b) + sin(b) texels per pixel, b the
    face's pitch: the density at which a texel covers exactly one pixel seen
    from 45 degrees across its eave, as the GBA draws every surface. Walls
    and flat tops are then 1:1, and a 45-degree slope is sqrt 2 - which is
    what the drawing's own projection gives it.

    `wrap` = (lo, hi) samples u from that column range only, cyclically, for
    art whose edges are not the material (a verge, a neighbour). Keep hi - lo a
    multiple of the pattern's period so the phase is unchanged. `offset`
    shifts u instead.
    """

    def __init__(self, fixed, repeat=None, start=None, wrap=None, offset=0.0,
                 from_end=False, tail=None, repeat_offset=None):
        self.fixed, self.repeat = fixed, repeat
        self.start = (repeat[1] - 1) if (repeat and start is None) else start
        self.wrap, self.offset, self.from_end = wrap, offset, from_end
        # rows [a, b) laid at the far end of the face - a flat roof's back
        # parapet, which the repeated courses must stop short of
        self.tail = tail
        # u shift for the repeated courses and tail only: columns the drawing
        # fills with something else behind its front rows (a roof unit)
        self.repeat_offset = repeat_offset

    def segments(self, total):
        """(t0, t1, v at t0, constant) runs along the face, in texels."""
        if self.tail is not None:
            ta, tb = self.tail
            length = tb - ta
            if total > length + (self.fixed[1] - self.fixed[0]):
                body = Strip(self.fixed, self.repeat, self.start)
                return body.segments(total - length) + [(total - length, total, float(tb), False)]
        a, b = self.fixed
        out = []
        t = 0.0
        if b > a:
            out.append((0.0, min(total, b - a), float(b), False))
            t = b - a
        if t >= total - EPS:
            return out
        if self.repeat is None:
            out.append((t, total, a + 0.5, True))
            return out
        c, d = self.repeat
        first = self.start + 1 - c
        out.append((t, min(total, t + first), float(self.start + 1), False))
        t += first
        while t < total - EPS:
            out.append((t, min(total, t + (d - c)), float(d), False))
            t += d - c
        return out

    def u_pieces(self, s0, s1):
        """(s0, s1, du) spans: u = s + du inside each."""
        if self.wrap is None:
            return [(s0, s1, self.offset)]
        lo, hi = self.wrap
        period = hi - lo
        out = []
        k = math.floor((s0 - lo) / period + EPS)
        while lo + k * period < s1 - EPS:
            a, b = max(s0, lo + k * period), min(s1, lo + (k + 1) * period)
            if b > a + EPS:
                out.append((a, b, -k * period))
            k += 1
        return out


def strip_face(mesh, pts, origin, sdir, tdir, strip, shade, tag):
    """A planar convex face textured by a Strip.

    `sdir` is the unit direction along the eave (u follows it), `tdir` the unit
    direction up the face from the eave line through `origin` (v runs against
    it). Every piece is affine in its texture, so UVs are exact.
    """
    dens = abs(tdir[1]) + math.hypot(tdir[0], tdir[2])
    loc = []
    for p in pts:
        d = [p[i] - origin[i] for i in range(3)]
        s = sum(d[i] * sdir[i] for i in range(3))
        t = sum(d[i] * tdir[i] for i in range(3)) * dens
        loc.append((s, t) + tuple(p))
    tmax = max(q[1] for q in loc)
    smin, smax = min(q[0] for q in loc), max(q[0] for q in loc)
    fixed_len = strip.fixed[1] - strip.fixed[0]
    for (t0, t1, v0, const) in strip.segments(tmax):
        for (s0, s1, du) in strip.u_pieces(smin, smax):
            if strip.repeat_offset is not None and t0 >= fixed_len - EPS:
                du = strip.repeat_offset
            piece = clip(loc, 1, t0, True)
            for axis, val, keep in ((1, t1, False), (0, s0, True), (0, s1, False)):
                if len(piece) >= 3:
                    piece = clip(piece, axis, val, keep)
            if len(piece) < 3:
                continue
            out = []
            for q in piece:
                v = v0 if const else v0 - (q[1] - t0)
                out.append((q[2], q[3], q[4], q[0] + du, v))
            mesh.poly(out, shade, tag + ("~clamp" if const else ""))


def _unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


class HipRoof:
    """A hipped roof with its ridge along X, built from its drawing.

    All four sides are tiled slopes, so the roof reads as tiles from every
    side and needs no gable wall. The eave runs round at `y0` plus the fascia,
    the ridge is centred on the eave rectangle, front and back rise at `pitch`
    and the hips come in `run` pixels by the ridge. Front and back lay their
    courses along X; the hips lay the same courses along Z.
    """

    def __init__(self, name, x0, x1, zf, zb, y0, fascia, slope, teeth, cap,
                 pitch, run, ridge_u, end_tile, ridge=True, ridge_wrap=None):
        self.name = name
        self.x0, self.x1, self.zf, self.zb, self.y0 = x0, x1, zf, zb, y0
        self.fascia, self.slope, self.teeth, self.cap = fascia, slope, teeth, cap
        self.pitch, self.run, self.ridge_u, self.end_tile = pitch, run, ridge_u, end_tile
        self.ridge = ridge
        self.ridge_wrap = ridge_wrap
        self.ye = y0 + (fascia.fixed[1] - fascia.fixed[0])
        cd = cap[1] - cap[0]
        self.z_rf = (zf + zb) / 2.0 + cd / 2.0
        self.z_rb = self.z_rf - cd
        self.rise = math.tan(math.radians(pitch)) * (zf - self.z_rf)
        self.yr = self.ye + self.rise
        self.yt = self.yr + (teeth[1] - teeth[0])

    def slope_point(self, texels):
        """(z, y) of the front slope `texels` art rows above its eave."""
        run = self.zf - self.z_rf
        length = math.hypot(run, self.rise)
        t = texels / ((self.rise + run) / length)
        return (self.zf - t * run / length, self.ye + t * self.rise / length)

    def emit(self, mesh):
        x0, x1, zf, zb, y0, ye = self.x0, self.x1, self.zf, self.zb, self.y0, self.ye
        xa, xb = x0 + self.run, x1 - self.run
        zr, zq, yr, yt = self.z_rf, self.z_rb, self.yr, self.yt
        n = self.name
        up = (0.0, 1.0, 0.0)
        fas, sl = self.fascia, self.slope
        # fascia board, round all four sides
        strip_face(mesh, [(x0, y0, zf), (x1, y0, zf), (x1, ye, zf), (x0, ye, zf)],
                   (0, y0, zf), (1, 0, 0), up, fas, SHADE_ART, n + ".fascia_s")
        strip_face(mesh, [(x1, y0, zb), (x0, y0, zb), (x0, ye, zb), (x1, ye, zb)],
                   (0, y0, zb), (1, 0, 0), up, fas, SHADE_BACK, n + ".fascia_n")
        strip_face(mesh, [(x0, y0, zb), (x0, y0, zf), (x0, ye, zf), (x0, ye, zb)],
                   (x0, y0, 0), (0, 0, 1), up, fas, SHADE_WEST, n + ".fascia_w")
        strip_face(mesh, [(x1, y0, zf), (x1, y0, zb), (x1, ye, zb), (x1, ye, zf)],
                   (x1, y0, 0), (0, 0, 1), up, fas, SHADE_EAST, n + ".fascia_e")
        # the four slopes
        t_s = _unit((0, self.rise, -(zf - zr)))
        t_n = _unit((0, self.rise, (zf - zr)))
        t_w = _unit((self.run, self.rise, 0))
        t_e = _unit((-self.run, self.rise, 0))
        strip_face(mesh, [(x0, ye, zf), (x1, ye, zf), (xb, yr, zr), (xa, yr, zr)],
                   (0, ye, zf), (1, 0, 0), t_s, sl, SHADE_ART, n + ".slope_s")
        strip_face(mesh, [(x1, ye, zb), (x0, ye, zb), (xa, yr, zq), (xb, yr, zq)],
                   (0, ye, zb), (1, 0, 0), t_n, sl, SHADE_BACK, n + ".slope_n")
        strip_face(mesh, [(x0, ye, zb), (x0, ye, zf), (xa, yr, zr), (xa, yr, zq)],
                   (x0, ye, 0), (0, 0, 1), t_w, sl, SHADE_WEST, n + ".hip_w")
        strip_face(mesh, [(x1, ye, zf), (x1, ye, zb), (xb, yr, zq), (xb, yr, zr)],
                   (x1, ye, 0), (0, 0, 1), t_e, sl, SHADE_EAST, n + ".hip_e")
        if not self.ridge:
            return
        # Ridge: teeth to the south, cap on top, ends over the hips. Each half
        # takes its own end of the drawn ridge, so both end ornaments stay -
        # unless something stands on the drawn ridge (`ridge_wrap`), and then
        # the whole ridge is laid from the clean columns instead.
        lo, hi = self.ridge_u
        mid = (xa + xb) / 2.0
        if self.ridge_wrap is not None:
            spans = [(xa, xb, None)]
        else:
            spans = [(xa, mid, lo - xa), (mid, xb, hi - xb)]
        for (sa, sb, off) in spans:
            if off is None:
                teeth = Strip(self.teeth, wrap=self.ridge_wrap)
                cap = Strip(self.cap, wrap=self.ridge_wrap)
            else:
                teeth = Strip(self.teeth, offset=off)
                cap = Strip(self.cap, offset=off)
            strip_face(mesh, [(sa, yr, zr), (sb, yr, zr), (sb, yt, zr), (sa, yt, zr)],
                       (0, yr, zr), (1, 0, 0), up, teeth, SHADE_ART, n + ".teeth")
            strip_face(mesh, [(sa, yt, zr), (sb, yt, zr), (sb, yt, zq), (sa, yt, zq)],
                       (0, yt, zr), (1, 0, 0), (0, 0, -1), cap, SHADE_ART, n + ".cap")
        u0, v0, u1, v1 = self.end_tile.rect
        w, h = zr - zq, yt - yr
        for x, shade, flip in ((xa, SHADE_WEST, False), (xb, SHADE_EAST, True)):
            pts = [(x, yr, zq), (x, yr, zr), (x, yt, zr), (x, yt, zq)]
            us = [u0, u0 + w, u0 + w, u0]
            if flip:
                us = [u0 + w - (u - u0) for u in us]
            vs = [v0 + h, v0 + h, v0, v0]
            mesh.poly([p + (u, v) for p, u, v in zip(pts, us, vs)], shade, n + ".ridge_end")


def _plan_normal(p, q):
    """Outward normal of plan edge p -> q for a polygon listed clockwise on
    screen (+X right, +Z down): the south edge runs west to east."""
    dx, dz = q[0] - p[0], q[1] - p[1]
    n = math.hypot(dx, dz)
    return (-dz / n, dx / n)


def inset_plan(plan, d):
    """The convex plan polygon moved inwards by d on every edge."""
    lines = []
    count = len(plan)
    for i in range(count):
        p, q = plan[i], plan[(i + 1) % count]
        nx, nz = _plan_normal(p, q)
        lines.append(((p[0] - nx * d, p[1] - nz * d), (q[0] - p[0], q[1] - p[1])))
    out = []
    for i in range(count):
        (p1, d1), (p2, d2) = lines[i - 1], lines[i]
        den = d1[0] * d2[1] - d1[1] * d2[0]
        t = ((p2[0] - p1[0]) * d2[1] - (p2[1] - p1[1]) * d2[0]) / den
        out.append((p1[0] + d1[0] * t, p1[1] + d1[1] * t))
    return out


class Frustum:
    """Walls on a convex plan, a roof band sloping in, and a flat top.

    The shape of a Pokemon Center or a Mart: octagonal walls, a band of roof
    at `band_pitch` rising `band_rise`, and a plateau. Every face the drawing
    shows (its plan edge faces south or diagonally south) takes the GBA
    projection, so the front, the chamfers and the band reproduce the art
    exactly at 45 degrees; the chamfers are tagged "~proj" because a face
    drawn at 45 degrees in plan has no 1:1 reading. The sides and the back
    take `wall_side` / `band_side` strips, and the plateau is laid from its
    front edge northwards by `top`, so it grows by repeated rows to whatever
    depth the plan has.
    """

    def __init__(self, name, plan, wall_top, wall_side, band_rise, band_side, top):
        self.name, self.plan = name, plan
        self.wall_top, self.wall_side = wall_top, wall_side
        self.band_rise, self.band_side = band_rise, band_side
        self.top = top

    def _proj_face(self, mesh, quad, tag):
        mesh.poly([(x, y, z, x, z - y) for (x, y, z) in quad], SHADE_ART, tag)

    def emit(self, mesh):
        plan, n = self.plan, self.name
        # The walls go a pixel under the ground: the drawing outlines its
        # plinth one pixel outside the footprint, and the ground hides the rest.
        y0, y1 = -1.0, float(self.wall_top)
        y2 = y1 + self.band_rise
        top = inset_plan(plan, self.band_rise)
        count = len(plan)
        for i in range(count):
            p, q = plan[i], plan[(i + 1) % count]
            pi, qi = top[i], top[(i + 1) % count]
            nx, nz = _plan_normal(p, q)
            drawn = nz > 0.1
            diag = abs(nx) > 1e-6
            shade = SHADE_ART if drawn else (SHADE_WEST if nx < -0.3 else
                                             SHADE_EAST if nx > 0.3 else SHADE_BACK)
            wall = [(p[0], y0, p[1]), (q[0], y0, q[1]), (q[0], y1, q[1]), (p[0], y1, p[1])]
            band = [(p[0], y1, p[1]), (q[0], y1, q[1]), (qi[0], y2, qi[1]), (pi[0], y2, pi[1])]
            sdir = _unit((q[0] - p[0], 0.0, q[1] - p[1]))
            if drawn:
                self._proj_face(mesh, wall, n + ".wall" + ("~proj" if diag else ""))
                self._proj_face(mesh, band, n + ".band" + ("~proj" if diag else ""))
            else:
                strip_face(mesh, wall, (p[0], y0, p[1]), sdir, (0.0, 1.0, 0.0),
                           self.wall_side, shade, n + ".wall")
                tdir = _unit((-nx, 1.0, -nz))
                strip_face(mesh, band, (p[0], y1, p[1]), sdir, tdir,
                           self.band_side, shade, n + ".band")
        zfront = max(z for _, z in top)
        strip_face(mesh, [(x, y2, z) for x, z in reversed(top)], (0.0, y2, zfront),
                   (1.0, 0.0, 0.0), (0.0, 0.0, -1.0), self.top, SHADE_ART, n + ".top")


class Vault:
    """A barrel vault standing on a flat top, its axis front to back.

    `profile` is the arch, (x, height) points read off the drawn front arch
    column by column; the vault runs from `zf` back to `zb` at `y0`. Its top
    and front take the GBA projection - at 45 degrees the drawing itself, and
    in any other view a real curved roof - and the back arch repeats the
    front's texels. Faces tagged "~proj" (curved, no single 1:1 reading).
    """

    def __init__(self, name, profile, zf, zb, y0):
        self.name, self.profile = name, profile
        self.zf, self.zb, self.y0 = zf, zb, y0

    def emit(self, mesh):
        zf, zb, y0, n = self.zf, self.zb, self.y0, self.name
        pts = self.profile
        for (xa, ha), (xb, hb) in zip(pts, pts[1:]):
            top = [(xa, y0 + ha, zf), (xb, y0 + hb, zf), (xb, y0 + hb, zb), (xa, y0 + ha, zb)]
            mesh.poly([(x, y, z, x, z - y) for (x, y, z) in top], SHADE_ART, n + ".top~proj")
            if ha <= 0 and hb <= 0:
                continue
            front = [(xa, y0, zf), (xb, y0, zf), (xb, y0 + hb, zf), (xa, y0 + ha, zf)]
            mesh.poly([(x, y, z, x, zf - y) for (x, y, z) in front], SHADE_ART, n + ".front")
            back = [(xb, y0, zb), (xa, y0, zb), (xa, y0 + ha, zb), (xb, y0 + hb, zb)]
            mesh.poly([(x, y, z, x, zf - y) for (x, y, z) in back], SHADE_BACK, n + ".back")


class Walls:
    """Vertical walls along an open plan polyline, drawn where they stand.

    For a front the drawing shows whole - a porch with chamfered corners -
    every wall takes the GBA projection; diagonal ones are tagged "~proj".
    """

    def __init__(self, name, plan, y0, y1):
        self.name, self.plan, self.y0, self.y1 = name, plan, y0, y1

    def emit(self, mesh):
        for (p, q) in zip(self.plan, self.plan[1:]):
            quad = [(p[0], self.y0, p[1]), (q[0], self.y0, q[1]),
                    (q[0], self.y1, q[1]), (p[0], self.y1, p[1])]
            diag = abs(q[1] - p[1]) > 1e-6
            mesh.poly([(x, y, z, x, z - y) for (x, y, z) in quad], SHADE_ART,
                      self.name + (".wall~proj" if diag else ".wall"))


class Relief:
    """A solid read column by column off its own drawing: hedges, walls.

    In every pixel column the drawing shows a run of the object: its top,
    and at the south end its front, `height` rows of it. So the top of each
    run lies at Y = height from Z = start + height to the run's end, and the
    front stands at the end - both projected, so the drawing is reproduced
    exactly. Columns with the same run are merged; where neighbouring
    columns end at different depths, or at the back, the faces the drawing
    never shows take `side`, a tile of the object's own front.
    """

    def __init__(self, name, art, height, side, hull=0, bridge=0, foot=None, solid=False,
                 back=None, top_tile=None, against=False, seam=None, flank_tile=None,
                 seam_x=((), ())):
        self.name, self.art, self.height, self.side = name, art, height, side
        # A block of a longer run (a railing cut into blocks): `seam` is
        # (rows, open_s, open_n). The art's rows from `rows` on are the run
        # going on south, which this block's top covers; the columns in
        # open_s carry on past them, so no front stands at the seam, and
        # those in open_n come in from the block north: no back, and a run
        # no longer than `height` there is the north block's.
        self.seam = seam
        # A railing whose run goes down a column is drawn as a line - its
        # top - and never shows its bars: `flank_tile`, the bars of the same
        # railing where it runs along a row, dresses its sides, their gaps
        # kept.
        self.flank_tile = flank_tile
        # the rows (in cells) where the block goes on west and east of its
        # art: a seam, where no flank stands
        self.seam_x = seam_x
        # A piece of furniture stands on the floor along one line, its foot:
        # with `foot`, every column runs from its first drawn row down to it,
        # so a leaf's tip or a machine's rounded corner stands with the rest
        # of it (its own transparency cuts the outline) instead of alone,
        # where its short run would put it - behind the wall.
        self.foot = foot
        # A box of a thing (a shelf, a counter) has sides of its own to show:
        # `solid` dresses them with `side`, where an outline's envelope takes
        # the drawing (projected, so its gaps stay gaps).
        self.solid = solid
        # Real depth: the drawing shows as much of a thing's top as the GBA's
        # 45 degrees leave in view, a few rows of a machine against a wall,
        # but it stands on as many cells as the cartridge says it does. With
        # `back`, the top runs back to that Z, laid with its own drawn rows
        # repeated course by course - never stretched - and the faces it adds
        # are tagged "~depth": at 45 degrees they rise behind the drawing.
        self.back = back
        # What the depth the drawing does not show is laid with: its own top
        # rows repeated, or, with `top_tile`, a stretch of drawing that is a
        # top in full - a glass case's, where the front one is two rows deep.
        self.top_tile = top_tile
        # `against`: the back is a wall. A top drawn deeper than the room in
        # front of it stops there and stands the rest of its rows up the wall.
        self.against = against
        if back is not None:
            self.solid = True
        # A railing is mostly gaps: `hull` closes gaps down a column up to that
        # many pixels and `bridge` carries a column's run across that many
        # empty columns, so the solid is the railing's envelope and the
        # texture's own transparency cuts the bars out of it.
        self.hull, self.bridge = hull, bridge

    def runs(self):
        W, H = self.art.size
        if self.flank_tile is not None:
            H = self.flank_tile.rect[1]     # the bars' band below is no drawing
        px = self.art.load()
        cols = []
        for u in range(W):
            runs, v = [], 0
            while v < H:
                if px[u, v][3] >= 128:
                    v0 = v
                    while v < H and px[u, v][3] >= 128:
                        v += 1
                    runs.append((v0, v))
                else:
                    v += 1
            if self.hull and runs:
                merged = [runs[0]]
                for (a, b) in runs[1:]:
                    if a - merged[-1][1] <= self.hull:
                        merged[-1] = (merged[-1][0], b)
                    else:
                        merged.append((a, b))
                runs = merged
            cols.append(runs)
        if self.bridge:
            # an empty column between two identical ones takes their run
            for u in range(1, W - 1):
                if cols[u]:
                    continue
                left = next((cols[k] for k in range(u - 1, max(-1, u - 1 - self.bridge), -1) if cols[k]), None)
                right = next((cols[k] for k in range(u + 1, min(W, u + 1 + self.bridge)) if cols[k]), None)
                if left is not None and left == right:
                    cols[u] = list(left)
        if self.foot is not None:
            cols = [[(runs[0][0], max(self.foot, runs[-1][1]))] if runs else [] for runs in cols]
        if self.seam is not None:
            rows, _, open_n = self.seam
            cols = [[(a, b) for (a, b) in runs if a < rows
                     and not (u in open_n and a == 0 and b - a <= self.height)]
                    for u, runs in enumerate(cols)]
        return cols

    def emit(self, mesh):
        H, n = float(self.height), self.name
        cols = self.runs()
        W = len(cols)

        def drawn(runs):
            """(z0, z1, top, va) as the drawing shows each run of a column."""
            out = []
            for (va, vb) in runs:
                depth = vb - va - H
                out.append((va + H, vb, H, va) if depth > 0 else (vb, vb, float(vb - va), va))
            return out

        def solid(runs):
            """(z0, z1, top) footprint pieces of one column, at real depth."""
            out = []
            for (z0, z1, top, va) in drawn(runs):
                if self.back is not None and top == H and self.back < z0:
                    z0 = float(self.back)
                elif self.against and top == H and z0 < self.back < z1:
                    # drawn deeper than the room in front of the wall has:
                    # the top stops at the wall and the rest of its rows
                    # stand up against it (see emit)
                    z0 = float(self.back)
                out.append((z0, z1, top))
            return out

        # merged spans of identical columns
        u = 0
        while u < W:
            u1 = u + 1
            while u1 < W and cols[u1] == cols[u]:
                u1 += 1
            for (z0, z1, top), (zd, _, _, va) in zip(solid(cols[u]), drawn(cols[u])):
                x0, x1 = float(u), float(u1)
                zt = max(zd, z0)
                if z1 > zt:
                    quad = [(x0, top, zt), (x1, top, zt), (x1, top, z1), (x0, top, z1)]
                    mesh.poly([(x, y, z, x, z - y) for (x, y, z) in quad], SHADE_ART, n + ".top")
                if z0 > zd:
                    # A desk against the wall drawn with more top than there is
                    # floor in front of the wall: the rows behind the wall's
                    # face stand up against it, along their own lines of
                    # sight, so the drawing is still the drawing at 45 degrees.
                    # Half a pixel proud of the wall's face, which it would
                    # otherwise fight for the same depth.
                    zr = z0 + 0.5
                    rise = zr - va
                    quad = [(x0, top, zr), (x1, top, zr), (x1, rise, zr), (x0, rise, zr)]
                    mesh.poly([(x, y, z, x, z - y) for (x, y, z) in quad], SHADE_ART,
                              n + ".riser")
                if zd > z0 and self.top_tile is not None:
                    face = [(x0, 0.0), (x1, 0.0), (x1, zd - z0), (x0, zd - z0)]
                    for piece in tile_pieces(face, self.top_tile, None, None):
                        mesh.poly([(s, top, zd - t, uu, vv) for (s, t, uu, vv) in piece],
                                  SHADE_ART, n + ".top~depth")
                elif zd > z0:
                    # the top's own rows, course after course, back to its
                    # depth: its inner rows, and its back edge - the outline
                    # the drawing puts there - once, at the real back
                    rows = z1 - zd
                    first = va + 1.0 if rows >= 2 else float(va)
                    course = max(1.0, rows - (first - va))
                    edge = 1.0 if rows >= 2 and zd - z0 > 1 else 0.0
                    far = zd
                    while far > z0 + edge + 1e-6:
                        near, far = far, max(z0 + edge, far - course)
                        va0 = first + (course - (near - far))
                        quad = [(x0, top, far), (x1, top, far), (x1, top, near), (x0, top, near)]
                        mesh.poly([(x, y, z, x, va0 + (z - far)) for (x, y, z) in quad],
                                  SHADE_ART, n + ".top~depth")
                    if edge:
                        quad = [(x0, top, z0), (x1, top, z0), (x1, top, z0 + 1), (x0, top, z0 + 1)]
                        mesh.poly([(x, y, z, x, va + (z - z0)) for (x, y, z) in quad],
                                  SHADE_ART, n + ".top~depth")
                seam_s = (self.seam is not None and u in self.seam[1]
                          and z1 >= self.seam[0] + H - 1e-6)
                seam_n = self.seam is not None and u in self.seam[2] and va == 0
                front = [(x0, -1.0, z1), (x1, -1.0, z1), (x1, top, z1), (x0, top, z1)]
                if not seam_s:
                    mesh.poly([(x, y, z, x, z - y) for (x, y, z) in front], SHADE_ART, n + ".front")
                if z1 > z0 and not seam_n:
                    back = [(x1, -1.0, z0), (x0, -1.0, z0), (x0, top, z0), (x1, top, z0)]
                    if self.hull or (self.foot is not None and not self.solid):
                        # a railing's back takes the drawing where it stands,
                        # so its gaps stay gaps from the front as well
                        mesh.poly([(x, y, z, x, z - y) for (x, y, z) in back],
                                  SHADE_BACK, n + ".back~proj")
                    else:
                        self._tile(mesh, back, (x1, z0), (-1.0, 0.0), top, SHADE_BACK,
                                   n + (".back~depth" if self.back is not None else ".back"))
            u = u1

        # flanks: wherever one column's solid is not matched by its neighbour's
        def covered(pieces):
            return [(a, b, t) for (a, b, t) in pieces if b > a]

        for u in range(W + 1):
            left = covered(solid(cols[u - 1])) if u > 0 else []
            right = covered(solid(cols[u])) if u < W else []
            for pieces, others, facing in ((left, right, 1.0), (right, left, -1.0)):
                for (a, b, t) in pieces:
                    for (za, zb) in _subtract((a, b), [(c, d) for (c, d, s) in others if s >= t]):
                        x = float(u)
                        edge = self.seam_x[0] if u == 0 else self.seam_x[1] if u == W else None
                        if edge and all(r in edge for r in range(int(za) // 16,
                                                                 int(math.ceil(zb)) // 16 + 1)
                                        if r * 16 < zb):
                            continue    # the run goes on into the next block
                        if self.flank_tile is not None and zb - za >= 16:
                            if facing > 0:
                                quad = [(x, -1.0, zb), (x, -1.0, za), (x, t, za), (x, t, zb)]
                            else:
                                quad = [(x, -1.0, za), (x, -1.0, zb), (x, t, zb), (x, t, za)]
                            self._tile(mesh, quad, (x, 0.0), (0.0, 1.0), t,
                                       SHADE_EAST if facing > 0 else SHADE_WEST, n + ".flank",
                                       self.flank_tile)
                            continue
                        if self.hull or (self.foot is not None and not self.solid):
                            # the end of a railing: the drawn column there, projected
                            col = min(max(u - (1 if facing > 0 else 0), 0), W - 1) + 0.5
                            quad = [(x, -1.0, za), (x, -1.0, zb), (x, t, zb), (x, t, za)]
                            mesh.poly([(q[0], q[1], q[2], col, q[2] - q[1]) for q in quad],
                                      SHADE_EAST if facing > 0 else SHADE_WEST, n + ".flank~proj")
                            continue
                        tag = n + (".flank~depth" if self.back is not None else ".flank")
                        if facing > 0:
                            quad = [(x, -1.0, zb), (x, -1.0, za), (x, t, za), (x, t, zb)]
                            self._tile(mesh, quad, (x, zb), (0.0, -1.0), t, SHADE_EAST, tag)
                        else:
                            quad = [(x, -1.0, za), (x, -1.0, zb), (x, t, zb), (x, t, za)]
                            self._tile(mesh, quad, (x, za), (0.0, 1.0), t, SHADE_WEST, tag)

    def _tile(self, mesh, quad, origin, sdir, top, shade, tag, tile=None):
        """1:1 side tile laid along the face from `origin`, top row at `top`."""
        u0, v0, u1, v1 = (tile or self.side).rect
        tile = Tile(u0, v0, u1, v1)
        pts = []
        for (x, y, z) in quad:
            s = (x - origin[0]) * sdir[0] + (z - origin[1]) * sdir[1]
            pts.append((s, top - y, x, y, z))
        for piece in tile_pieces(pts, tile, None, None):
            mesh.poly([(q[2], q[3], q[4], q[5], q[6]) for q in piece], shade, tag)


def _subtract(span, others):
    """Parts of span (a, b) not covered by any of `others`."""
    out = [span]
    for (c, d) in others:
        nxt = []
        for (a, b) in out:
            if d <= a or c >= b:
                nxt.append((a, b))
                continue
            if c > a:
                nxt.append((a, c))
            if d < b:
                nxt.append((d, b))
        out = nxt
    return [(a, b) for (a, b) in out if b - a > 1e-6]


class Lifted:
    """A part standing on another: everything moved by (0, base, base).

    A point of the drawing lifted as the terrain relief lifts it keeps its
    place in the GBA view (v = Z - Y does not change), so a machine drawn on
    a counter stands on the counter's top and is still drawn exactly.
    """

    def __init__(self, part, base):
        self.part, self.base = part, float(base)

    def emit(self, mesh):
        inner = Mesh()
        if isinstance(self.part, Prism):
            emit_prism(inner, self.part)
        else:
            self.part.emit(inner)
        b = self.base
        for (tri, shade, tag) in inner.tris:
            mesh.tri(*[(x, y + b, z + b, u, v) for (x, y, z, u, v) in tri],
                     shade=shade, tag=tag)


class Mound:
    """A rounded thing - a rock in the sea - lifted pixel by pixel off its
    own drawing.

    Every point (u, v) of the drawing goes to (u, h, v + h), which the GBA's
    45 degrees see as that same point whatever h is: the drawing is the
    rock seen from there, exactly, and h is only how round it is. Each
    column of the drawing is a cut through a dome: an ellipse half `rise`
    times as tall as it is deep, standing on the water, its foot where the
    column's drawing ends and its top as far up as the column is drawn (the
    ray of the 45-degree view grazes it there). The point of the drawing is
    the point of that ellipse the view meets first. Behind the grazing point
    the ellipse goes on down to the water: the back no drawing shows, laid
    with the texels in front of it, and closed at both ends.

    `step` is the lattice in pixels across the columns; down a column the
    dome is cut as finely. Faces are projected ("~proj"): curved, not 1:1
    anywhere off the axis, and the texture's own transparency cuts the
    outline.

    `ring`: the colours of what the drawing puts round the rock's foot on
    the water (foam, its grey shadow). They are the sea's, not the rock's:
    laid flat on the water, textured from the band of the art below `rows`
    that holds them alone (Mound.with_ring), never lifted with the dome.
    """

    def __init__(self, name, art, rise=1.0, step=4, back_steps=3, ring=(), rows=None):
        self.name, self.rise, self.step, self.back_steps = name, rise, step, back_steps
        self.rows = rows if rows is not None else art.size[1]
        self.ring = {tuple(c) for c in ring}
        body = art.crop((0, 0, art.size[0], self.rows))
        bpx = body.load()
        for (x, y) in Mound.ring_pixels(body, self.ring):
            bpx[x, y] = (0, 0, 0, 0)
        self.art = body
        self.full = art
        # the band the faces no drawing shows are laid with (with_ring)
        self.back_band = 2 * self.rows if art.size[1] >= 3 * self.rows else 0

    @staticmethod
    def ring_pixels(art, ring):
        """The pixels of the ring's colours at the rock's foot: in each
        column, below the lowest pixel of the rock itself (or all of them,
        in a column with none). The same colour higher up is the rock's."""
        W, H = art.size
        px = art.load()
        out = set()
        for x in range(W):
            body = [y for y in range(H) if px[x, y][3] >= 128 and px[x, y][:3] not in ring]
            foot = body[-1] if body else -1
            out |= {(x, y) for y in range(foot + 1, H)
                    if px[x, y][3] >= 128 and px[x, y][:3] in ring}
        return out

    @staticmethod
    def with_ring(art, ring):
        """The texture, three bands of the drawing's size: the drawing
        without its ring; its ring alone; and the rock again, grown past its
        outline with its own edge colours - what the faces no drawing shows
        (the back, the ends) are laid with, so that no texel they sample is
        a hole."""
        ring = {tuple(c) for c in ring}
        W, H = art.size
        out = Image.new("RGBA", (W, H * 3), (0, 0, 0, 0))
        apx, opx = art.load(), out.load()
        foam = Mound.ring_pixels(art, ring)
        grown = {}
        for y in range(H):
            for x in range(W):
                if apx[x, y][3] >= 128:
                    opx[x, y + (H if (x, y) in foam else 0)] = apx[x, y]
                    if (x, y) not in foam:
                        grown[(x, y)] = apx[x, y]
        edge = list(grown)
        while edge and len(grown) < W * H:
            nxt = []
            for (x, y) in edge:
                for (i, j) in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= i < W and 0 <= j < H and (i, j) not in grown:
                        grown[(i, j)] = grown[(x, y)]
                        nxt.append((i, j))
            edge = nxt
        for (x, y), c in grown.items():
            opx[x, y + 2 * H] = c[:3] + (255,)
        return out

    def _spans(self):
        """(x, top, foot) at the lattice's lines across the columns: through
        the middle of a pixel column, its own drawn run, so the strips
        between them meet the outline at every pixel's centre and stand
        nowhere outside it; the two ends at the drawing's own edges. A line
        is left out where the outline between its neighbours is straight to
        within half a pixel, `step` columns at most."""
        W, H = self.art.size
        px = self.art.load()
        runs = []
        for u in range(W):
            v = [y for y in range(H) if px[u, y][3] >= 128]
            runs.append((v[0], v[-1] + 1) if v else None)
        cols = [u for u in range(W) if runs[u]]
        u0, u1 = cols[0], cols[-1] + 1
        for u in range(u0, u1):
            if runs[u] is None:     # a gap down the middle: bridged
                runs[u] = runs[u - 1]
        lines = [(float(u0),) + runs[u0]] + [(u + 0.5,) + runs[u] for u in range(u0, u1)]             + [(float(u1),) + runs[u1 - 1]]

        def straight(i, j):
            (xa, ta, fa), (xb, tb, fb) = lines[i], lines[j]
            for k in range(i + 1, j):
                x, t, f = lines[k]
                g = (x - xa) / (xb - xa)
                if abs(ta + (tb - ta) * g - t) > 0.5 or abs(fa + (fb - fa) * g - f) > 0.5:
                    return False
            return True

        keep, i = [0], 0
        while i < len(lines) - 1:
            j = i + 1
            while (j + 1 < len(lines) and lines[j + 1][0] - lines[i][0] <= self.step
                   and straight(i, j + 1)):
                j += 1
            keep.append(j)
            i = j
        return [lines[k] for k in keep]

    def emit(self, mesh):
        k = self.rise
        spans = self._spans()
        n = max(4, int(math.ceil(max(b - a for _, a, b in spans) / float(self.step))))
        mid_x = (spans[0][0] + spans[-1][0]) / 2.0
        cols = []
        for (x, vt, vb) in spans:
            L = float(vb - vt)
            a = L / (1.0 + math.sqrt(1.0 + k * k))
            b = k * a
            zc = vb - a
            front = []
            for i in range(n + 1):
                v = vt + L * i / n
                # ((v + t - zc) / a)^2 + (t / b)^2 = 1, the root nearest the eye
                A = 1.0 / (a * a) + 1.0 / (b * b)
                B = 2.0 * (v - zc) / (a * a)
                C = (v - zc) ** 2 / (a * a) - 1.0
                disc = max(0.0, B * B - 4 * A * C)
                t = max(0.0, (-B + math.sqrt(disc)) / (2 * A))
                front.append((x, t, v + t, x, v))
            back = []
            th0 = math.pi - math.atan2(b, a)
            for i in range(1, self.back_steps + 1):
                th = th0 + (math.pi - th0) * i / self.back_steps
                z, y = zc + a * math.cos(th), b * math.sin(th)
                if i == self.back_steps:
                    y = 0.0
                back.append((x, y, z, x, min(vb - 1e-3, max(vt, z - y)) + self.back_band))
            cols.append((front, back, zc))
        for c in range(len(cols) - 1):
            (fa, ba, za), (fb, bb, zb) = cols[c], cols[c + 1]
            for i in range(n):
                p, q, r, s_ = fa[i], fb[i], fb[i + 1], fa[i + 1]
                self._quad(mesh, p, q, r, s_, SHADE_ART, self.name + "~proj")
            ra, rb = [self._behind(fa[0])] + ba, [self._behind(fb[0])] + bb
            centre = ((fa[0][0] + fb[0][0]) / 2.0, 0.0, (za + zb) / 2.0)
            for i in range(len(ra) - 1):
                self._quad(mesh, ra[i], rb[i], rb[i + 1], ra[i + 1], SHADE_WOUND,
                           self.name + ".back~behind~proj", outward=centre)
        # the ring round its foot, flat on the water
        if self.ring:
            W, H = self.full.size
            box = self.full.crop((0, self.rows, W, min(H, 2 * self.rows))).getbbox()
            if box:
                x0, y0, x1, y1 = box
                e = 0.05
                q = [(x, e, v + e, x, v + self.rows) for (x, v) in
                     ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
                self._quad(mesh, q[0], q[1], q[2], q[3], SHADE_ART, self.name + ".ring~proj")
        # the two ends, closed by the column's own cut
        for (front, back, zc), side in ((cols[0], -1), (cols[-1], 1)):
            ring = [self._behind(p) for p in reversed(front)] + back
            if len(ring) < 3:
                continue
            x = ring[0][0]
            centre = (x - side, 0.0, zc)
            for i in range(1, len(ring) - 1):
                self._tri(mesh, ring[0], ring[i], ring[i + 1], SHADE_WOUND,
                          self.name + ".end~behind~proj", outward=centre)

    def _behind(self, p):
        """A point of the drawn front, textured from the band the faces no
        drawing shows are laid with."""
        return p[:4] + (p[4] + self.back_band,)

    @staticmethod
    def _area(a, b, c):
        e1 = [b[i] - a[i] for i in range(3)]
        e2 = [c[i] - a[i] for i in range(3)]
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
             e1[0] * e2[1] - e1[1] * e2[0])
        return n

    def _tri(self, mesh, a, b, c, shade, tag, outward=None):
        n = self._area(a, b, c)
        if sum(v * v for v in n) < 1e-12:
            return
        if outward is None:
            # drawn: it faces the 45-degree view
            if n[1] + n[2] < 0:
                b, c = c, b
        else:
            g = [(a[i] + b[i] + c[i]) / 3.0 - outward[i] for i in range(3)]
            if n[0] * g[0] + n[1] * g[1] + n[2] * g[2] < 0:
                b, c = c, b
        mesh.tri(a, b, c, shade, tag)

    def _quad(self, mesh, p, q, r, s, shade, tag, outward=None):
        self._tri(mesh, p, q, r, shade, tag, outward)
        self._tri(mesh, p, r, s, shade, tag, outward)


class Card:
    """A thing of leaves - a potted plant - as a card at its foot, facing the
    GBA camera, its drawing projected: seen from the console's camera it
    stands like the characters do. (A second card across it showed from
    above as a flame of leaf tips.)

    `art` is the piece's own image; `foot` the row it stands on; the columns
    it spans are read off the art. `voff`: the rows below the model's drawing
    where the card's own pixels are, so that it shows nothing else.
    """

    def __init__(self, name, art, foot, voff=0):
        self.name, self.art, self.foot, self.voff = name, art, float(foot), voff

    def emit(self, mesh):
        W, H = self.art.size
        px = self.art.load()
        cols = [u for u in range(W) if any(px[u, v][3] >= 128 for v in range(H))]
        rows = [v for v in range(H) if any(px[u, v][3] >= 128 for u in range(W))]
        if not cols:
            return
        u0, u1 = float(min(cols)), float(max(cols) + 1)
        v0, f = float(min(rows)), self.foot
        h = f - v0
        front = [(u0, -1.0, f), (u1, -1.0, f), (u1, h, f), (u0, h, f)]
        mesh.poly([(x, y, z, x, z - y + self.voff) for (x, y, z) in front], SHADE_ART,
                  self.name + ".card")


class Facet:
    """A vertical wall along a line of the plan, its drawing projected: a
    room's chamfered corner, drawn as a band between two parallel diagonals.

    `a` and `b` are its foot's ends (x, z) and `ha`, `hb` its heights there.
    """

    def __init__(self, name, a, b, ha, hb):
        self.name, self.a, self.b, self.ha, self.hb = name, a, b, float(ha), float(hb)

    def emit(self, mesh):
        (xa, za), (xb, zb) = self.a, self.b
        quad = [(xa, -1.0, za), (xb, -1.0, zb), (xb, self.hb, zb), (xa, self.ha, za)]
        mesh.poly([(x, y, z, x, z - y) for (x, y, z) in quad], SHADE_ART,
                  self.name + ".facet~proj")


class PlainWall:
    """A wall the drawing never shows - a room's side wall, edge on to the GBA
    camera - dressed with `tile` at one texel per pixel, laid from `a` to `b`
    (x, z) with its top row at `y1`. It faces left of the direction a->b."""

    def __init__(self, name, a, b, y0, y1, tile):
        self.name, self.a, self.b = name, a, b
        self.y0, self.y1, self.tile = float(y0), float(y1), tile

    def emit(self, mesh):
        (xa, za), (xb, zb) = self.a, self.b
        length = math.hypot(xb - xa, zb - za)
        dx, dz = (xb - xa) / length, (zb - za) / length
        face = [(0.0, 0.0), (length, 0.0), (length, self.y1 - self.y0), (0.0, self.y1 - self.y0)]
        for piece in tile_pieces(face, self.tile, None, None):
            pts = []
            for s, t, u, v in piece:
                pts.append((xa + dx * s, self.y1 - t, za + dz * s, u, v))
            mesh.poly(pts, SHADE_WEST, self.name + ".plain")


class Decal:
    """What a room paints on its floor in the cells a model covers: a flat
    quad per cell, drawn from the floor-only copy of the drawing that the
    model's art carries `v_offset` rows below its own."""

    def __init__(self, name, cells, v_offset, lift=0.5):
        self.name, self.cells, self.v_offset, self.lift = name, cells, v_offset, lift

    def emit(self, mesh):
        y, o = self.lift, self.v_offset
        for (i, j) in self.cells:
            x0, z0, x1, z1 = i * 16.0, j * 16.0, i * 16.0 + 16.0, j * 16.0 + 16.0
            quad = [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)]
            # Half a pixel above the floor drawn under it - any less and the
            # console's depth buffer cannot tell them apart up close - and
            # lifted along the GBA's line of sight (as the terrain's relief
            # lifts a cell), so at 45 degrees it covers its own rows exactly.
            mesh.poly([(x, yy, z + y, x, z + o) for (x, yy, z) in quad], SHADE_ART,
                      self.name + ".decal")


class Cylinder:
    """A short upright cylinder of elliptical footprint - a vent, a pot.

    A curved thing in the art has no eave to lay courses from, so its top and
    the side facing the camera take the GBA projection itself (u = X,
    v = Z - Y), which reproduces its drawing exactly at 45 degrees; the far
    side and the flanks take a 1:1 tile of the drawn near side. `density_check` exempts the
    projected faces (tag "~proj"): off-axis, a curved face is not 1:1 anywhere.
    """

    def __init__(self, name, cx, cz, rx, rz, y0, y1, back_tile, sides=16):
        self.name, self.cx, self.cz, self.rx, self.rz = name, cx, cz, rx, rz
        self.y0, self.y1, self.back_tile, self.sides = y0, y1, back_tile, sides

    def emit(self, mesh):
        ring = [(self.cx + self.rx * math.cos(2 * math.pi * k / self.sides),
                 self.cz + self.rz * math.sin(2 * math.pi * k / self.sides))
                for k in range(self.sides)]
        y0, y1 = self.y0, self.y1
        top = [(x, y1, z, x, z - y1) for (x, z) in ring]
        mesh.poly(top, SHADE_ART, self.name + ".top~proj")
        u0, v0, u1, v1 = self.back_tile.rect
        for k in range(self.sides):
            (xa, za), (xb, zb) = ring[k], ring[(k + 1) % self.sides]
            # outward normal of the segment on the ellipse
            nx, nz = (zb - za), -(xb - xa)
            if nx * ((xa + xb) / 2 - self.cx) + nz * ((za + zb) / 2 - self.cz) < 0:
                nx, nz = -nx, -nz
            nz /= math.hypot(nx, nz)
            quad = [(xa, y0, za), (xb, y0, zb), (xb, y1, zb), (xa, y1, za)]
            # Only a face turned to the camera is drawn in the art; one seen
            # edge on would sample the pixels just outside the drawn outline.
            if nz > 0.2:
                mesh.poly([p + (p[0], p[2] - p[1]) for p in quad],
                          SHADE_ART, self.name + ".side~proj")
            else:
                w = math.hypot(xb - xa, zb - za)
                ua = u0 + (k * 3) % max(1, int(u1 - u0 - w))
                vs = [v1, v1, v1 - (y1 - y0), v1 - (y1 - y0)]
                us = [ua, ua + w, ua + w, ua]
                mesh.poly([p + (u, v) for p, u, v in zip(quad, us, vs)],
                          SHADE_BACK if nz < 0 else SHADE_WEST, self.name + ".side")


# ── Model ─────────────────────────────────────────────────────────────────

class Model:
    def __init__(self, name, art, prisms, cells, ground_metatile):
        self.name, self.art, self.prisms = name, art, prisms
        self.cells = cells  # (w, h) in metatiles
        self.ground_metatile = ground_metatile
        self.mesh = Mesh()
        for part in prisms:
            if isinstance(part, Prism):
                emit_prism(self.mesh, part)
            else:
                part.emit(self.mesh)

    def triangle_count(self):
        return len(self.mesh.tris)


# ── Software rasterizer (preview and proof) ────────────────────────────────

class Raster:
    def __init__(self, w, h, bg=(40, 44, 60)):
        self.w, self.h = w, h
        self.color = [bg] * (w * h)
        self.depth = [-1e30] * (w * h)
        self.owner = [None] * (w * h)

    def draw(self, verts, tex, shade, owner=None):
        """verts: three (sx, sy, depth, invw, u, v); larger depth is nearer.

        u, v are perspective-divided by invw already (u*invw, v*invw), so the
        interpolation is perspective correct.
        """
        (x0, y0, d0, w0, u0, v0), (x1, y1, d1, w1, u1, v1), (x2, y2, d2, w2, u2, v2) = verts
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            return
        minx = max(0, int(math.floor(min(x0, x1, x2))))
        maxx = min(self.w - 1, int(math.ceil(max(x0, x1, x2))))
        miny = max(0, int(math.floor(min(y0, y1, y2))))
        maxy = min(self.h - 1, int(math.ceil(max(y0, y1, y2))))
        tw, th = tex.size
        tpx = tex.load()
        inv = 1.0 / area
        for py in range(miny, maxy + 1):
            cy = py + 0.5
            for px_ in range(minx, maxx + 1):
                cx = px_ + 0.5
                a = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) * inv
                b = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) * inv
                c = 1.0 - a - b
                if a < -1e-7 or b < -1e-7 or c < -1e-7:
                    continue
                d = a * d0 + b * d1 + c * d2
                idx = py * self.w + px_
                if d <= self.depth[idx]:
                    continue
                iw = a * w0 + b * w1 + c * w2
                u = (a * u0 + b * u1 + c * u2) / iw
                v = (a * v0 + b * v1 + c * v2) / iw
                tu = int(math.floor(u))
                tv = int(math.floor(v))
                if tu < 0 or tv < 0 or tu >= tw or tv >= th:
                    continue
                r, g, bb, al = tpx[tu, tv]
                if al < 128:
                    continue
                self.depth[idx] = d
                self.owner[idx] = owner
                self.color[idx] = (int(r * shade), int(g * shade), int(bb * shade))

    def image(self):
        img = Image.new("RGB", (self.w, self.h))
        img.putdata(self.color)
        return img


def ortho_check(model, out_png=None, exact=None, margin=32, reference=None):
    """Render the model in the GBA projection and compare it with the art.

    `exact` lists art rectangles (x0, y0, x1, y1[, behind]) the model must
    reproduce pixel for pixel (with `behind` true, geometry may show where the
    art has ground there: the real depth rising behind a drawn outline) - the facade and whatever else faces the camera at its
    drawn depth. Outside them the model may differ from the drawing, because
    it is deeper than the drawing. None means the whole art.

    Returns (wrong, missing, extra): pixels drawn with the wrong colour, art
    pixels nothing drew, and pixels drawn where the art has ground.

    With `reference`, the render (textured with the model's own art) is
    judged against that drawing instead: a model whose texture is laid out
    otherwise than the drawing (a rock's foam on a band of its own).
    """
    art = model.art
    W, H = art.size
    M = margin
    ras = Raster(W + 2 * M, H + M, bg=(0, 0, 0))
    for (tri, shade, tag) in model.mesh.tris:
        if "~depth" in tag or "~behind" in tag:
            continue  # real depth behind the drawing: not the drawing's to judge
        vs = []
        for (x, y, z, u, v) in tri:
            vs.append((x + M, z - y + M, y + z, 1.0, u, v))
        ras.draw(vs, art, shade, tag)
    regions = exact if exact is not None else [(0, 0, W, H)]
    apx = (reference if reference is not None else art).load()
    wrong = missing = extra = 0
    img = ras.image()
    diff = img.copy()
    dpx = diff.load()
    for y in range(H + M):
        for x in range(W + 2 * M):
            c = dpx[x, y]
            dpx[x, y] = (c[0] // 3, c[1] // 3, c[2] // 3)
    for region in regions:
        rx0, ry0, rx1, ry1 = region[:4]
        behind = len(region) > 4 and region[4]
        for y in range(ry0, ry1):
            for x in range(rx0, rx1):
                r, g, b, a = apx[x, y]
                idx = (y + M) * ras.w + x + M
                got = ras.owner[idx]
                col = ras.color[idx]
                if a >= 128:
                    if got is None:
                        missing += 1
                        dpx[x + M, y + M] = (255, 0, 255)
                    elif col != (r, g, b):
                        wrong += 1
                        dpx[x + M, y + M] = (255, 0, 0)
                    else:
                        dpx[x + M, y + M] = (r, g, b)
                elif got is not None and not behind:
                    extra += 1
                    dpx[x + M, y + M] = (0, 255, 255)
    if out_png:
        sheet = Image.new("RGB", ((W + 2 * M) * 3 + 8, H + M), (0, 0, 0))
        bgart = Image.new("RGB", (W, H), (0, 0, 0))
        bgart.paste(art, (0, 0), art)
        sheet.paste(bgart, (M, M))
        sheet.paste(img, (W + 2 * M + 4, 0))
        sheet.paste(diff, (2 * (W + 2 * M) + 8, 0))
        sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
        sheet.save(out_png)
    return wrong, missing, extra


def density_check(model, tolerance=1e-3):
    """Every art texel must cover one pixel seen across its face at 45 degrees.

    Along the horizontal direction of a face the art must run one texel per
    pixel; along its fall line cos(b) + sin(b) texels per pixel, b its pitch;
    and the two must not shear. That is the property that keeps a stretched
    or squashed texel from ever reaching the screen, and it holds for every
    face - the ones the drawing shows and the ones rebuilt from it. Faces
    that hold one row (hidden under an eave) are exempt.

    Returns a list of (tag, along, down, shear) for faces that break it.
    """
    bad = []
    for (tri, shade, tag) in model.mesh.tris:
        if tag.endswith("~clamp") or tag.endswith("~proj"):
            continue
        p = [tri[i][:3] for i in range(3)]
        q = [tri[i][3:] for i in range(3)]
        e1 = [p[1][k] - p[0][k] for k in range(3)]
        e2 = [p[2][k] - p[0][k] for k in range(3)]
        nrm = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
               e1[0] * e2[1] - e1[1] * e2[0])
        nl = math.sqrt(sum(c * c for c in nrm))
        if nl < 1e-9:
            continue
        nrm = tuple(c / nl for c in nrm)
        # horizontal in-plane direction h, fall line f
        if abs(nrm[1]) > 1 - 1e-9:
            h, f = (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)
        else:
            h = _unit((nrm[2], 0.0, -nrm[0]))
            f = (h[1] * nrm[2] - h[2] * nrm[1], h[2] * nrm[0] - h[0] * nrm[2],
                 h[0] * nrm[1] - h[1] * nrm[0])
        beta = math.asin(min(1.0, math.sqrt(nrm[0] ** 2 + nrm[2] ** 2)))
        expect = math.cos(beta) + math.sin(beta)
        # solve uv = A [a b]^T for the triangle's local coordinates
        a1, b1 = sum(e1[k] * h[k] for k in range(3)), sum(e1[k] * f[k] for k in range(3))
        a2, b2 = sum(e2[k] * h[k] for k in range(3)), sum(e2[k] * f[k] for k in range(3))
        det = a1 * b2 - a2 * b1
        if abs(det) < 1e-9:
            continue
        du1, dv1 = q[1][0] - q[0][0], q[1][1] - q[0][1]
        du2, dv2 = q[2][0] - q[0][0], q[2][1] - q[0][1]
        dh = ((du1 * b2 - du2 * b1) / det, (dv1 * b2 - dv2 * b1) / det)
        df = ((du2 * a1 - du1 * a2) / det, (dv2 * a1 - dv1 * a2) / det)
        along = math.hypot(*dh)
        down = math.hypot(*df)
        shear = abs(dh[0] * df[0] + dh[1] * df[1])
        if (abs(along - 1.0) > tolerance or abs(down - expect) > tolerance
                or shear > tolerance):
            bad.append((tag, along, down / expect, shear))
    return bad


class Camera:
    """The voxel renderer's follow camera, in world units (16 px)."""

    def __init__(self, target, pitch=40.0, yaw=0.0, distance=12.0, fov=35.0,
                 width=400, height=240):
        self.target = target
        self.pitch, self.yaw, self.distance, self.fov = pitch, yaw, distance, fov
        self.width, self.height = width, height
        p, yw = math.radians(pitch), math.radians(yaw)
        tx, ty, tz = target
        self.eye = (tx + math.sin(yw) * distance, ty + math.tan(p) * distance,
                    tz + math.cos(yw) * distance)
        f = [target[i] - self.eye[i] for i in range(3)]
        fl = math.sqrt(sum(c * c for c in f))
        self.f = [c / fl for c in f]
        up = (0.0, 1.0, 0.0)
        s = [self.f[1] * up[2] - self.f[2] * up[1],
             self.f[2] * up[0] - self.f[0] * up[2],
             self.f[0] * up[1] - self.f[1] * up[0]]
        sl = math.sqrt(sum(c * c for c in s))
        self.s = [c / sl for c in s]
        self.u = [self.s[1] * self.f[2] - self.s[2] * self.f[1],
                  self.s[2] * self.f[0] - self.s[0] * self.f[2],
                  self.s[0] * self.f[1] - self.s[1] * self.f[0]]
        self.t = 1.0 / math.tan(math.radians(fov) / 2)
        self.aspect = width / height

    def project(self, p):
        d = [p[i] - self.eye[i] for i in range(3)]
        x = sum(d[i] * self.s[i] for i in range(3))
        y = sum(d[i] * self.u[i] for i in range(3))
        z = sum(d[i] * self.f[i] for i in range(3))
        if z < 0.05:
            return None
        sx = (x * self.t / self.aspect / z * 0.5 + 0.5) * self.width
        sy = (0.5 - y * self.t / z * 0.5) * self.height
        return sx, sy, 1.0 / z, 1.0 / z


def render_scene(cam, items, scale=2):
    """items: list of (triangles, texture) where triangles are in world units
    as ((x,y,z,u,v)*3, shade)."""
    ras = Raster(cam.width * scale, cam.height * scale)
    cam2 = Camera(cam.target, cam.pitch, cam.yaw, cam.distance, cam.fov,
                  cam.width * scale, cam.height * scale)
    for tris, tex in items:
        for (tri, shade) in tris:
            vs = []
            ok = True
            for (x, y, z, u, v) in tri:
                pr = cam2.project((x, y, z))
                if pr is None:
                    ok = False
                    break
                sx, sy, d, iw = pr
                vs.append((sx, sy, d, iw, u * iw, v * iw))
            if ok:
                ras.draw(vs, tex, shade)
    return ras.image()


def model_world_tris(model, cell_x, cell_y):
    out = []
    for (tri, shade, tag) in model.mesh.tris:
        out.append((tuple((cell_x + x / 16.0, y / 16.0, cell_y + z / 16.0, u, v)
                          for (x, y, z, u, v) in tri), shade))
    return out


def ground_tris(layout, x0, y0, x1, y1, override, atlas_cache):
    """Flat ground for context; cells in `override` draw that metatile.

    Returns (tris, texture) using a small atlas of the metatiles involved.
    """
    ids = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            if 0 <= x < layout.w and 0 <= y < layout.h:
                m = override.get((x, y), layout.metatile(x, y))
                ids.setdefault(m, len(ids))
    cols = 16
    tex = Image.new("RGBA", (cols * 16, ((len(ids) + cols - 1) // cols) * 16), (0, 0, 0, 255))
    for m, i in ids.items():
        tex.paste(layout.cell_image(m), ((i % cols) * 16, (i // cols) * 16))
    tris = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            if not (0 <= x < layout.w and 0 <= y < layout.h):
                continue
            m = override.get((x, y), layout.metatile(x, y))
            i = ids[m]
            u0, v0 = (i % cols) * 16, (i // cols) * 16
            a = (x, 0, y, u0, v0)
            b = (x + 1, 0, y, u0 + 16, v0)
            c = (x + 1, 0, y + 1, u0 + 16, v0 + 16)
            d = (x, 0, y + 1, u0, v0 + 16)
            tris.append(((a, b, c), 1.0))
            tris.append(((a, c, d), 1.0))
    return tris, tex
