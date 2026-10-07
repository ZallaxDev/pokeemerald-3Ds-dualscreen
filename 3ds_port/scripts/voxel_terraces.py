#!/usr/bin/env python3
"""Mountains read as the tileset builds them: whole levels, one tile a level.

The General tileset draws a mountain with a handful of square pieces, and
every one of them spans exactly one level (LEVEL pixels):

  top    flat, the terrace's surface (a rim tile is a top whose north edge
         is its back: the ground behind it is lower, never seen falling)
  face   the south wall, vertical: one tile down is one level down
  band   the west or east side, a 45-degree slope one tile wide: one tile
         across is one level down (high side towards the terrace)
  end    a face's end (074, 07d, 089, 07b): one level, falling the way its
         neighbours fall; down a column, the end of one row's face over the
         other end of the next row's is a diagonal edge - the two are one
         level, not two

So the heights of a mountain are not solved, they are counted, and every
point of it lies on a plane:

1. Cells are read off their pixels: ground (sand, grass, water), top, band
   (by a table: the hatching does not say which way it falls), other rock,
   or unknown (trees, boulders, props: the ground under them is not drawn).
2. A column of k levels of rock between two flat cells says the upper one
   stands k levels over the lower; a row of k bands says the same across.
   Terraces are the tops joined top to top (not across a rim's back) and the
   grounds joined ground to ground - but never two cells a face apart (a
   pier joining a beach to the land over it belongs to one of them).
   Every column and row votes; the levels are taken greatest vote first, a
   vote that contradicts what is already decided is dropped (it becomes a
   wall, never a bent surface). Each block of terraces the votes tie is
   then lifted to where the world already stands (the relief the world
   solver placed), so no seam with a neighbouring map moves.
3. Every rock cell has its upper level U: the one it hangs from, a level
   less for each level of rock above it in its stack. Where nothing counts
   it, it takes the level that leaves the most of its corners shared.
4. The heights live on the CORNERS of the cells (a 16-pixel lattice). A
   corner shared by cells that can all reach one level is that level for
   all of them, so neighbours meet without a crack; where they cannot, the
   edge is a step (a terrace's back, a wall drawn with the face).
5. A cell's surface follows from its four corners and nothing else: level,
   a plane (face or band), a hip (one corner high: two planes meeting on
   the diagonal, a convex corner) or a valley (one corner low: concave).

The result is exact at 45 degrees ((u, h, v + h) for any h) and made only of
planes, at any angle.

    python voxel_terraces.py LAYOUT_ROUTE106 [--png OUT --box x0,y0,x1,y1]
                             [--relief IN.bin OUT.bin] [--check]

gen_voxel_relief.py runs it over its own output for the maps in GROUPS
(VOXEL_TERRACES=all|none|LAYOUT_A,LAYOUT_B to choose others).
"""

import argparse
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_voxel_relief as gr  # noqa: E402
import voxel_building as vb  # noqa: E402
import voxel_props  # noqa: E402

LEVEL = 16
N = 4                       # lattice steps per cell side (5 x 5 points)
TOP_COLOURS = gr.ROCK_TOP | gr.ROCK_RIM
FACE_COLOURS = gr.ROCK_FACE
FLECK = gr.ROCK_FLECK

# The sides, by the way they fall (gen_voxel_relief.SIDE_WEST/EAST and their
# heads, drawn under a rim at the top of a band, and feet over the ground).
# The same drawn over the sea (Route 105's ridges, the rocks off Route 106)
# are 172-17c.
BAND_WEST = {0x070, 0x073, 0x088, 0x06b, 0x080, 0x083, 0x068,     # high side east
             0x172, 0x175, 0x177, 0x17a}
BAND_EAST = {0x072, 0x075, 0x0a2, 0x08a, 0x06d, 0x082, 0x084, 0x06a,  # high side west
             0x174, 0x176, 0x17c}
# Drawn whole on a terrace: a boulder's two halves are its top.
ON_TOP = gr.BOULDER
BOULDER_FOOT = {0x09b, 0x09c}
BOULDER_HEAD = {0x093, 0x094}
# Boulders drawn on whatever ground they stand on: the ground round them.
LOOSE = {0x0df, 0x0e0, 0x0e1, 0x0e2}
# A diagonal edge is drawn as a staircase of face ends: the end of one
# row's face over the other end of the next row's. Down a column the two are
# one wall, one level - not two faces.
UPPER_END = {0x074, 0x089}
LOWER_END = {0x07d, 0x07b, 0x078}

# The mountain's own tiles (the General tileset's rock, its rock over the
# sea, faces with doors and caves): nothing else is ever rock, whatever
# colours it is drawn in - soil, a sign, a house's wall share them.
MOUNTAIN = (set(range(0x064, 0x0c0)) | set(range(0x172, 0x193)) | {0x1a0, 0x1a8, 0x1b0, 0x1f0, 0x1f1}
            | gr.FACE_SOUTH | gr.BOULDER) - gr.DIRT

GROUND, TOP, ROCK, UNKNOWN = "G", "T", "R", "?"
WORLD = ("world",)      # the level the world is counted from
MAX_FALL = 3 * 16       # pixels: the most one tile of rock is stretched to fall
ANCHOR_MIN = 12         # cells: ground this big reaching a map's edge keeps its place


# -- 1. cells -----------------------------------------------------------------

_CLASSES = {}


def pixel_classes(art, m):
    """16 x 16 of 'T' (top or rim), 'F' (face) or 'G' (anything else) for
    metatile m; a fleck (a colour of both) goes with the most of its
    neighbours."""
    key = (art.primary, art.secondary, m)
    if key in _CLASSES:
        return _CLASSES[key]
    img = art.cell_image(m).load()
    cls = [["G"] * 16 for _ in range(16)]
    flecks = []
    for y in range(16):
        for x in range(16):
            c = img[x, y][:3]
            if c in TOP_COLOURS:
                cls[y][x] = "T"
            elif c in FACE_COLOURS:
                cls[y][x] = "F"
            elif c in FLECK:
                flecks.append((x, y))
    for x, y in flecks:
        votes = collections.Counter(cls[ny][nx] for ny in range(max(0, y - 1), min(16, y + 2))
                                    for nx in range(max(0, x - 1), min(16, x + 2))
                                    if (nx, ny) not in flecks)
        votes.pop("G", None)
        cls[y][x] = votes.most_common(1)[0][0] if votes else "T"
    _CLASSES[key] = cls
    return cls


def rim_pixels(art, m):
    img = art.cell_image(m).load()
    return sum(1 for y in range(6) for x in range(16) if img[x, y][:3] in gr.ROCK_RIM)


_TWINS = {}
TWIN_MATCH = 0.85       # share of rock pixels (and their kind) that must agree


def twin(art, m):
    """The General tileset's mountain tile a map's own tile is drawn as (a
    copy in its secondary tileset: Dewford's bands), or m itself."""
    if m < 0x200:
        return m
    key = (art.primary, art.secondary, m)
    if key not in _TWINS:
        mine = pixel_classes(art, m)
        rock = {(x, y) for y in range(16) for x in range(16) if mine[y][x] != "G"}
        best, score = m, 0.0
        if len(rock) >= 64 and art.primary == "gTileset_General":
            for r in sorted(MOUNTAIN):
                if r >= 0x200:
                    continue
                theirs = pixel_classes(art, r)
                both = {(x, y) for y in range(16) for x in range(16) if theirs[y][x] != "G"} | rock
                same = sum(1 for (x, y) in both if theirs[y][x] == mine[y][x]) / len(both)
                if same > score and same >= TWIN_MATCH:
                    best, score = r, same
        _TWINS[key] = best
    return _TWINS[key]


class Cells:
    """What each cell of a group of layouts is drawn as, on one canvas: the
    maps placed as they connect (gen_voxel_relief.DRAWN), so a mountain
    drawn across a seam is read as one."""

    def __init__(self, members):
        import voxel_cells as vc
        self.members = list(members)        # [(layout, x offset, y offset)]
        self.id = self.members[0][0]
        self.arts, self.roles_of = {}, {}
        self.src = {}                       # canvas cell -> (layout, x, y)
        self.kind, self.band, self.rim = {}, {}, set()
        self.ledges, self.keep = set(), set()
        self._over_water = {}
        berry = vc.behaviours().get("MB_BERRY_TREE_SOIL")
        for lid, ox, oy in self.members:
            art = self.arts[lid] = gr.layout_art(lid)
            roles = self.roles_of[lid] = gr.open_roles(lid)
            ledges = gr.ledge_cells(roles)
            entry = next(e for e in vb._layouts_json() if e["id"] == lid)
            props = voxel_props.cells_in(entry)
            for y in range(art.h):
                for x in range(art.w):
                    cell = (x + ox, y + oy)
                    if cell in self.src:
                        continue
                    self.src[cell] = (lid, x, y)
                    m = art.metatile(x, y)
                    if (x, y) in ledges:
                        self.ledges.add(cell)
                    if (x, y) in ledges or roles.behaviour(x, y) == berry or m in gr.DIRT:
                        self.keep.add(cell)
                    self._read(cell, art, roles, m, (x, y) in ledges, (x, y) in props)
        xs = [x for x, _ in self.src]
        ys = [y for _, y in self.src]
        self.xs = range(min(xs), max(xs) + 1)
        self.ys = range(min(ys), max(ys) + 1)

    def _read(self, cell, art, roles, m, ledge, prop):
        lx, ly = self.src[cell][1:]
        cls = pixel_classes(art, m)
        m = twin(art, m)
        t = sum(r.count("T") for r in cls) / 256.0
        f = sum(r.count("F") for r in cls) / 256.0
        role = roles.role_at(lx, ly)
        # the mountain's own tiles drawn over grass (a band's corner, 068)
        # read as a tree by their green: they are the rock they draw
        tree = role == "tree" and not (m in MOUNTAIN and t + f >= 0.25)
        if tree or prop or m in LOOSE:
            k = UNKNOWN     # a sea stack's model is drawn with band heads too
        elif (m in BAND_WEST or m in BAND_EAST) and not ledge:
            # a band, its head over the sea behind it too: rock
            k = ROCK
            self.band[cell] = "W" if m in BAND_WEST else "E"
        elif ledge or role in ("water", "ledge"):
            k = GROUND
        elif role == "stair" and self._in_face(art, lx, ly):
            k = ROCK        # stairs up a face: a level, as the face
        elif role == "floor" and m in MOUNTAIN and f >= 0.5:
            k = ROCK        # a cave's mouth, a door: walked into, drawn as the face
        elif role == "floor" and m in MOUNTAIN and f < 0.08 and t >= 0.45:
            k = TOP         # a path along a ridge: the top it is drawn as
        elif role in ("floor", "stair", "signpost") or m not in MOUNTAIN:
            # walked on, a sign, a patch of soil, a building's wall: never
            # the mountain, whatever colours it is drawn in
            k = GROUND
        elif m in BOULDER_FOOT and ly > 0 and twin(art, art.metatile(lx, ly - 1)) in BAND_WEST | BAND_EAST:
            # a boulder's lower half under a band: the rounded end of a
            # ridge, where its flank meets the face it ends in - rock, its
            # corner (a top there left a slanted step under the band)
            k = ROCK
        elif m in ON_TOP:
            k = TOP
        elif t + f < 0.25:
            k = GROUND
        elif f < 0.08 and t >= 0.45:
            k = TOP
        else:
            k = ROCK
        self.kind[cell] = k
        if k in (TOP, ROCK):
            g_top = sum(1 for yy in range(4) for xx in range(16) if cls[yy][xx] == "G")
            if rim_pixels(art, m) >= 3 or g_top >= 12:
                self.rim.add(cell)

    def art(self, cell):
        return self.arts[self.src[cell][0]]

    def at_edge(self, cell):
        """On its map's border: where it meets the next map."""
        lid, x, y = self.src[cell]
        art = self.arts[lid]
        return x in (0, art.w - 1) or y in (0, art.h - 1)

    def metatile(self, cell):
        lid, x, y = self.src[cell]
        return self.arts[lid].metatile(x, y)

    def role_id(self, cell):
        """The mountain tile a cell is drawn as (its General twin)."""
        lid, x, y = self.src[cell]
        return twin(self.arts[lid], self.arts[lid].metatile(x, y))

    def role(self, cell):
        lid, x, y = self.src[cell]
        return self.roles_of[lid].role_at(x, y)

    def water(self, cell):
        """Water, or a tree's crown drawn over it: the crown's lower layer is
        the drawing of the water beside it (Route 106's wood over the sea)."""
        if cell not in self.src:
            return False
        if self.role(cell) == "water":
            return True
        if cell not in self._over_water:
            self._over_water[cell] = self.crown(cell) and any(
                self._lower(cell) == self._lower(n) for n in self._four(cell)
                if n in self.src and self.role(n) == "water")
        return self._over_water[cell]

    def _four(self, cell):
        x, y = cell
        return ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))

    def _lower(self, cell):
        art = self.art(cell)
        return art.cell_image(self.metatile(cell), (0,)).convert("RGB").tobytes()

    def crown(self, cell):
        """The top of a tree drawn over what is behind it: the cell north of
        a tree, a quarter of it at least on the upper layer. What it stands
        for is the tree's; what is under it is not seen."""
        if cell not in self.src:
            return False
        lid, x, y = self.src[cell]
        art, roles = self.arts[lid], self.roles_of[lid]
        if y + 1 >= art.h or roles.role_at(x, y) == "tree" or roles.role_at(x, y + 1) != "tree":
            return False
        m = art.metatile(x, y)
        hi, lo = art.cell_image(m).load(), art.cell_image(m, (0,)).load()
        return sum(1 for j in range(16) for i in range(16) if hi[i, j] != lo[i, j]) >= 64

    def walked(self, cell):
        """Walked on (the collision map): a floor, not water, a ledge, stairs."""
        return cell in self.src and self.role(cell) == "floor"

    @staticmethod
    def _in_face(art, x, y):
        """Stairs cut through a face: rock drawn beside them, west or east."""
        for nx in (x - 1, x + 1):
            if 0 <= nx < art.w:
                cls = pixel_classes(art, art.metatile(nx, y))
                if sum(r.count("F") for r in cls) >= 96:
                    return True
        return False


# -- 2. terraces and their levels ---------------------------------------------

class Offsets:
    """Union-find keeping each item's level over its root's."""

    def __init__(self):
        self.parent, self.off = {}, {}

    def find(self, a):
        if a not in self.parent:
            self.parent[a], self.off[a] = a, 0
            return a, 0
        path = []
        while self.parent[a] != a:
            path.append(a)
            a = self.parent[a]
        root = a
        total = 0
        for p in reversed(path):
            total += self.off[p]
            self.off[p] = total
            self.parent[p] = root
        return root, (self.off[path[0]] if path else 0)

    def join(self, a, b, d):
        """level(a) - level(b) = d. False when it contradicts what is known."""
        ra, oa = self.find(a)
        rb, ob = self.find(b)
        if ra == rb:
            return oa - ob == d
        # level(a) = L(ra) + oa, level(b) = L(rb) + ob: L(rb) = L(ra) + oa - ob - d
        self.parent[rb] = ra
        self.off[rb] = oa - ob - d
        return True


def runs_of(c):
    """[(direction, upper cell, rock cells from the upper end, lower cell)]:
    every column of rock cells between two flat cells (not ending at a rim's
    back, which is no face, nor hanging from water), every row of bands."""
    def flat(cell):
        return c.kind.get(cell) in (TOP, GROUND)

    runs = []
    for x in c.xs:
        for y in c.ys:
            nxt = (x, y + 1)
            if not flat((x, y)) or c.kind.get(nxt) != ROCK or nxt in c.band or c.water((x, y)):
                continue
            k = 1
            while c.kind.get((x, y + 1 + k)) == ROCK and (x, y + 1 + k) not in c.band:
                k += 1
            end = (x, y + 1 + k)
            if flat(end) and end not in c.rim:
                runs.append(("S", (x, y), [(x, y + 1 + i) for i in range(k)], end))
    for y in c.ys:
        for x in c.xs:
            nxt = (x + 1, y)
            if not flat((x, y)) or nxt not in c.band:
                continue
            side = c.band[nxt]
            k = 1
            while c.band.get((x + 1 + k, y)) == side:
                k += 1
            end = (x + 1 + k, y)
            cells = [(x + 1 + i, y) for i in range(k)]
            if not flat(end):
                continue
            if side == "E" and not c.water((x, y)):
                runs.append(("E", (x, y), cells, end))
            elif side == "W" and not c.water(end):
                runs.append(("W", end, cells[::-1], (x, y)))
    return runs


def terraces(c, runs):
    """{cell: terrace id} of the tops and grounds, joined like to like: tops
    top to top (not across a rim's back), grounds ground to ground and
    through what stands on them (trees, boulders, props). Never two cells a
    face or a band apart: a pier or a path that joins a beach to the land
    above it is one or the other, not both."""
    uf = {}
    apart = collections.defaultdict(set)
    for _, hi, cells, lo in runs:
        apart[hi].add(lo)
        apart[lo].add(hi)
    members = {}

    def find(a):
        while uf.setdefault(a, a) != a:
            uf[a] = uf[uf[a]]
            a = uf[a]
        return a

    def join(a, b):
        ra, rb = find(a), find(b)
        if ra == rb:
            return
        ma, mb = members.get(ra, ()), members.get(rb, ())
        # a cell kept apart from one of the other side's
        if any(find(o) == rb for m in ma for o in apart[m]) or \
           any(find(o) == ra for m in mb for o in apart[m]):
            return
        uf[rb] = ra
        if mb:
            members.setdefault(ra, set()).update(mb)
            members.pop(rb, None)

    for cell in apart:
        members[cell] = {cell}
    edges = []
    for (x, y), k in c.kind.items():
        if k not in (TOP, GROUND, UNKNOWN):
            continue
        find((x, y))
        for n in ((x + 1, y), (x, y + 1)):
            nk = c.kind.get(n)
            if k == TOP:
                # a rim's back: the top behind it is another terrace
                if nk != TOP or (n[1] == y + 1 and n in c.rim):
                    continue
                # a boulder drawn on a ridge's end caps it: the end is its
                # own terrace (a ridge falls at its end, a band less high)
                if (c.metatile((x, y)) in ON_TOP) != (c.metatile(n) in ON_TOP):
                    continue
            elif nk not in (GROUND, UNKNOWN):
                continue
            # first what the collision map ties: ground walked from one cell
            # to the next with no ledge or stairs between is one level (the
            # player walks it level), water surfed across is one body; then
            # the ground's own edges (a shore, the land round a house); last
            # what a map's own tileset draws on it
            ma, mb = c.metatile((x, y)), c.metatile(n)
            wa, wb = c.water((x, y)), c.water(n)
            if (wa and wb) or (k == GROUND and nk == GROUND and c.walked((x, y)) and c.walked(n)):
                rank = 0
            elif ma < 0x200 and mb < 0x200:
                rank = 1
            else:
                rank = 2
            # a shore last: land is joined to land first, so a wood on a
            # ridge's top is one terrace (kept apart from the sea at the
            # ridge's foot) before a tree drawn over the water could tie a
            # few of its cells to the sea (Route 106's east end)
            if wa != wb:
                rank += 3
            edges.append((rank, (x, y), n))
    for _, a, b in sorted(edges):
        join(a, b)
    return {cell: find(cell) for cell in uf if c.kind[cell] != UNKNOWN}


def steps(c, cells, direction):
    """The levels down a run of rock cells: one a cell, but an upper face
    end over a lower one is one diagonal wall. Returns the level each cell
    hangs from, counted from the top (0, 1, ...), and the run's levels."""
    out, n = [], 0
    for i, cell in enumerate(cells):
        if direction == "S" and i > 0 and c.role_id(cells[i - 1]) in UPPER_END \
                and c.role_id(cell) in LOWER_END and out[-1] == n - 1:
            out.append(out[-1])
            continue
        out.append(n)
        n += 1
    return out, n


def solve_levels(c, comp, runs, reference=None, meets=None):
    """{terrace: level}, the votes that lost. `reference`: {cell: height} of
    the world as it stands (the relief the world solver placed); each block
    of terraces tied by votes is lifted to agree with it most, so no seam
    with a neighbouring map moves. Without it, its biggest ground is 0.
    `meets`: the edge cells whose reference is the level of the map across
    the seam (seam_meets) - only those hold a terrace where it stands."""
    runs = [r for r in runs if r[1] in comp and r[3] in comp and comp[r[1]] != comp[r[3]]]
    # a band's head under a rim turns the corner (the band beside it may
    # start a row lower): it says the least
    vote = collections.Counter()
    hidden = collections.Counter()
    for d, hi, cells, lo in runs:
        head = d != "S" and any(cell in c.rim for cell in cells)
        key = (comp[hi], comp[lo], steps(c, cells, d)[1])
        vote[key] += 1 if head else 2
        # a face whose foot a house or a wood stands on: what it does there
        # is not seen
        hidden[key] += d == "S" and ((c.role(lo) == "wall" and c.metatile(lo) >= 0x200)
                                     or c.kind.get((lo[0], lo[1] + 1)) == UNKNOWN)
    off = Offsets()
    dropped = []
    size = collections.Counter(comp.values())
    cells_of = collections.defaultdict(list)
    for cell, t in comp.items():
        cells_of[t].append(cell)
    # first where the world already stands: the ground of each map that
    # reaches its edges where it meets the next map keeps its level - not an
    # edge with no map beyond it, nor one the next map's ground does not
    # meet (the wood on Route 106's ridge, read as the sea it is drawn over
    # by the world's solve, under the same wood on Dewford's): there it
    # holds nothing, and the votes say where it stands
    # a terrace's kind is its cells' (its id may be a tree it was joined through)
    kind_of = {t: c.kind[cells[0]] for t, cells in cells_of.items()}
    if reference is not None:
        for t, cells in sorted(cells_of.items()):
            if kind_of[t] != GROUND or len(cells) < ANCHOR_MIN:
                continue
            edge = [cell for cell in cells if c.at_edge(cell) and cell in reference
                    and (meets is None or cell in meets)]
            if len(edge) < 2:
                continue
            got = collections.Counter(LEVEL * round(reference[cell] / LEVEL) for cell in cells
                                      if cell in reference and cell not in c.keep)
            if got:
                off.join(WORLD, t, -got.most_common(1)[0][0])
    # within a map before across a seam (where two maps the world placed
    # apart disagree, the step is at their seam, not in a mountain), then
    # the most columns first; a tie goes to the one-level step (the commonest)
    def across(a, b):
        return c.src[a][0] != c.src[b][0]

    # the weight a vote is kept by: a seam's half (two maps the world placed
    # apart: the step goes to their seam rather than into a mountain), a
    # hidden face's quarter (the step goes where a house or a wood hides it)
    def weight(key, n):
        w = n * (0.5 if across(key[0], key[1]) else 1.0)
        if hidden[key] * 2 >= n:
            w *= 0.25
        return w

    for (a, b, k), n in sorted(vote.items(), key=lambda kv: (-weight(kv[0], kv[1]), kv[0][2],
                                                             kv[0][0], kv[0][1])):
        if not off.join(a, b, k * LEVEL):
            dropped.append(((a, b, k), n))
    roots = collections.defaultdict(list)
    for t in set(comp.values()):
        roots[off.find(t)[0]].append(t)
    level = {}
    world_root = off.find(WORLD)[0]
    for r, members in roots.items():
        rel = {t: off.find(t)[1] for t in members}
        grounds = [t for t in members if kind_of[t] == GROUND]
        lift = None
        if r == world_root:
            lift = -off.find(WORLD)[1]
        elif reference is not None:
            diffs = collections.Counter()
            for t in (grounds or members):
                for cell in cells_of[t]:
                    if cell in reference and cell not in c.keep:
                        diffs[LEVEL * round((reference[cell] - rel[t]) / LEVEL)] += 1
            if diffs:
                lift = max(diffs.items(), key=lambda kv: (kv[1], -abs(kv[0])))[0]
        if lift is None:
            ref = max(grounds or members, key=lambda t: (size[t], t))
            lift = -rel[ref]
        for t in members:
            level[t] = rel[t] + lift
    return level, dropped


# -- 3. the level each rock cell hangs from -----------------------------------

def upper_levels(c, comp, level, runs):
    U = {}
    for side, hi, cells, lo in runs:
        if hi not in comp or lo not in comp:
            continue
        top, bottom = level[comp[hi]], level[comp[lo]]
        if top <= bottom:
            continue        # a vote that lost: says nothing
        idx, _ = steps(c, cells, side)
        for i, cell in zip(idx, cells):
            # more cells than levels: the last ones stand on the lowest
            u = max(top - LEVEL * i, bottom + LEVEL)
            if cell not in U or u > U[cell]:
                U[cell] = u
    # a ridge drawn with bands alone (no top between its two flanks): each
    # flank counts up from the ground at its foot to the crest
    crests = set()
    for y in c.ys:
        for x in c.xs:
            if (x, y) not in comp or (x + 1, y) not in c.band:
                continue
            # west flank: ground, W bands..., then E bands, then ground
            if c.band[(x + 1, y)] != "W":
                continue
            k = 1
            while c.band.get((x + 1 + k, y)) == "W":
                k += 1
            if c.band.get((x + 1 + k, y)) != "E":
                continue
            j = 1
            while c.band.get((x + 1 + k + j, y)) == "E":
                j += 1
            end = (x + 1 + k + j, y)
            if end not in comp:
                continue
            lo_w, lo_e = level[comp[(x, y)]], level[comp[end]]
            crest = max(lo_w + LEVEL * k, lo_e + LEVEL * j)
            for i in range(k):
                U.setdefault((x + 1 + i, y), crest - LEVEL * (k - 1 - i))
                crests.add((x + 1 + i, y))
            for i in range(j):
                U.setdefault((x + 1 + k + i, y), crest - LEVEL * i)
                crests.add((x + 1 + k + i, y))
    # bands down a column are one band: the same level
    changed = True
    while changed:
        changed = False
        for (x, y), side in c.band.items():
            if (x, y) in U:
                continue
            for n in ((x, y - 1), (x, y + 1)):
                if c.band.get(n) == side and n in U:
                    U[(x, y)] = U[n]
                    changed = True
                    break
    # the rest from what is round them: first what holds them (the surface
    # above, the high side of a band), then what is beside, then the foot
    todo = [cell for cell, k in c.kind.items() if k == ROCK and cell not in U]
    for _ in range(16):
        left = []
        for (x, y) in todo:
            side = c.band.get((x, y))
            high = (x - 1, y) if side == "E" else (x + 1, y) if side == "W" else (x, y - 1)
            # under a face, a level down; under a band (its foot), beside
            under = 0 if side or (x, y - 1) in c.band else -LEVEL
            tiers = [[(high, 0, under)],
                     [((x - 1, y), 0, 0), ((x + 1, y), 0, 0)] + (
                         [((x, y - 1), 0, 0), ((x, y + 1), 0, 0)] if side else []),
                     [((x, y + 1), LEVEL, LEVEL)]]
            got = None
            for tier in tiers:
                cand = []
                for n, node, rock in tier:
                    if n in comp:
                        cand.append(level[comp[n]] + node)
                    elif n in U:
                        cand.append(U[n] + rock)
                if cand:
                    got = max(cand)
                    break
            if got is None:
                left.append((x, y))
            else:
                U[(x, y)] = got
        if len(left) == len(todo):
            break
        todo = left
    for cell in todo:
        U[cell] = LEVEL
    # what nothing counted stays within the levels round it: no lower than
    # the lowest surface near it, no higher than the highest (the surface it
    # hangs from may be the only one near: it is not lifted over it)
    counted = {cell for _, hi, cells, lo in runs for cell in cells} | crests
    for (x, y) in [cell for cell, k in c.kind.items() if k == ROCK and cell not in counted]:
        near = [level[comp[n]] for n in ((x + dx, y + dy) for dx in (-2, -1, 0, 1, 2)
                                         for dy in (-2, -1, 0, 1, 2)) if n in comp]
        if near:
            U[(x, y)] = max(min(near), min(max(near), U[(x, y)]))
    return U


def unknown_levels(c, comp, level, U):
    """Trees, boulders, props: what is under them is not drawn. A patch of
    them stands at one level, the lowest ground it can stand on - beside it,
    or at the foot of the rock above it: a wood in front of a cliff hides
    the cliff, it does not climb it."""
    out = {}
    seen = set()
    for start, k in sorted(c.kind.items()):
        if k != UNKNOWN or start in seen:
            continue
        patch, todo = [], [start]
        seen.add(start)
        while todo:
            x, y = todo.pop()
            patch.append((x, y))
            for n in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if c.kind.get(n) == UNKNOWN and n not in seen:
                    seen.add(n)
                    todo.append(n)
        cand, wet = [], []
        for (x, y) in patch:
            for n in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if n in comp:
                    (wet if c.water(n) else cand).append(level[comp[n]])
            if (x, y - 1) in U:
                cand.append(U[(x, y - 1)] - LEVEL)
        # what stands mostly in the sea (a rock off the shore) stands on the
        # sea; what stands mostly on land (a wood by the beach) on the land
        cand = wet if len(wet) > len(cand) else (cand or wet)
        v = min(cand) if cand else 0
        for cell in patch:
            out[cell] = v
    return out


def settle(c, U, flat, fixed):
    """Each rock cell takes, of its level and the ones a level up or down,
    the one that leaves the most of its corners shared with every cell round
    them (a corner all of them can reach is one point: no crack, no wall).
    Cells a column or a row of faces counted (`fixed`) move only for a clear
    gain. Repeated until nothing moves."""
    def options(cell):
        if cell in flat:
            return {flat[cell]}
        return {U[cell], U[cell] - LEVEL}

    def score(x, y):
        n = 0
        for gx, gy in ((x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)):
            common = None
            for cell in ((gx - 1, gy - 1), (gx, gy - 1), (gx - 1, gy), (gx, gy)):
                if cell in c.kind:
                    o = options(cell)
                    common = set(o) if common is None else common & o
            n += bool(common)
        return n

    # a band is the step of the level it climbs to: it is not moved
    rock = sorted(cell for cell, k in c.kind.items() if k == ROCK and cell not in c.band)
    for _ in range(8):
        moved = 0
        for cell in rock:
            here = U[cell]
            best, best_score = here, score(*cell)
            x, y = cell
            for u in (here + LEVEL, here - LEVEL):
                U[cell] = u
                sc = score(*cell)
                need = 2 if cell in fixed else 1
                # as good, and hung from the rock above or beside at that
                # level: a face's end goes on with its face
                held = u > here and cell not in fixed and any(
                    U.get(n) == u or flat.get(n) == u for n in ((x, y - 1), (x - 1, y), (x + 1, y)))
                if sc >= best_score + need or (sc == best_score and held):
                    best, best_score = u, sc
            U[cell] = best
            moved += best != here
        if not moved:
            break
    return U


# -- 4. corners ---------------------------------------------------------------

NAMES = ("NW", "NE", "SW", "SE")


def corner_pixel_top(c, cell, corner):
    """Is the drawing top-coloured at that corner of the cell?"""
    cls = pixel_classes(c.art(cell), c.metatile(cell))
    xs = range(0, 3) if corner in ("NW", "SW") else range(13, 16)
    ys = range(0, 3) if corner in ("NW", "NE") else range(13, 16)
    return sum(1 for yy in ys for xx in xs if cls[yy][xx] == "T") >= 5


def default_high(c, cell, k):
    """Is corner k of a rock cell high in its own piece? A face is high
    along its north edge, a band along its high side."""
    side = c.band.get(cell)
    if side == "W":
        return NAMES[k] in ("NE", "SE")
    if side == "E":
        return NAMES[k] in ("NW", "SW")
    return NAMES[k] in ("NW", "NE")


def corners(c, U, flat):
    """{cell: [NW, NE, SW, SE]} levels."""
    def options(cell):
        if cell in flat:
            return {flat[cell]}
        return {U[cell], U[cell] - LEVEL}

    def resolve(cells):
        common = None
        for cell, k, opt in cells:
            common = set(opt) if common is None else common & opt
        if common and len(common) == 1:
            return next(iter(common))
        if common:
            # rock only round it, either level: the pieces say, if they
            # agree; else the drawing (top colour at that corner)
            votes = {default_high(c, cell, k) for cell, k, _ in cells if cell not in flat}
            if len(votes) == 1:
                top = votes.pop()
            else:
                top = any(corner_pixel_top(c, cell, NAMES[k]) for cell, k, _ in cells)
            return max(common) if top else min(common)
        return None

    out = {cell: [None] * 4 for cell in c.kind}
    # the corner of each of the four cells round lattice point (gx, gy)
    around = (((-1, -1), 3), ((0, -1), 2), ((-1, 0), 1), ((0, 0), 0))
    for gy in range(c.ys.start, c.ys.stop + 1):
        for gx in range(c.xs.start, c.xs.stop + 1):
            cells = []
            for (dx, dy), k in around:
                cell = (gx + dx, gy + dy)
                if cell in c.kind:
                    cells.append((cell, k, options(cell)))
            if not cells:
                continue
            # a rim's north edge is its back: what is behind it is not tied
            # to it, the cells north of the point and south of it part
            lower = [e for e in cells if e[1] in (0, 1)]
            if any(e[0] in c.rim for e in lower):
                groups = [lower, [e for e in cells if e[1] in (2, 3)]]
            else:
                groups = [cells]
            for group in groups:
                if not group:
                    continue
                v = resolve(group)
                flats = [flat[cell] for cell, k, opt in group if cell in flat]
                for cell, k, opt in group:
                    if v is not None:
                        out[cell][k] = v
                    elif cell in flat:
                        # a step: each cell keeps its own, rock its own piece's
                        out[cell][k] = flat[cell]
                    elif default_high(c, cell, k):
                        out[cell][k] = U[cell]
                    else:
                        # rock reaches the ground at its foot, however far
                        # down it is: its fall stretches, it leaves no step
                        low = U[cell] - LEVEL
                        below = [f for f in flats if f < low]
                        out[cell][k] = max(min(below), U[cell] - MAX_FALL) if below else low
    return out


def piece_cost(c, cell, v):
    """How far four corners [NW, NE, SW, SE] are from what a tile of rock is:
    one level of fall (no more, no less, never flat), towards its own side -
    a face falls south, a band towards its low side."""
    a, b, d, e = v
    r = max(v) - min(v)
    cost = 4.0 if r == 0 else abs(r - LEVEL) / LEVEL * 1.5
    side = c.band.get(cell)
    if side == "E":
        wrong = max(0, b - a) + max(0, e - d)
    elif side == "W":
        wrong = max(0, a - b) + max(0, d - e)
    else:
        wrong = max(0, d - a) + max(0, e - b)
    return cost + 2.0 * wrong / LEVEL


def lattice(c, flat, init):
    """The corners solved together. A lattice point is one height for every
    cell round it, so no two cells part there - except where the drawing
    says they part: a rim's back (the cells behind it), and two flat cells
    at different levels (a step no rock is drawn in; each keeps its own).
    A point a flat cell touches is that cell's level; the rest, touched by
    rock only, take the heights that leave every tile of rock closest to
    one level of fall towards its own side (piece_cost), all at once."""
    around = (((-1, -1), 3), ((0, -1), 2), ((-1, 0), 1), ((0, 0), 0))
    owner = {}          # (cell, k) -> point id
    fixed = {}          # point id -> level
    domain = {}         # point id -> allowed levels (None: any)
    value = {}
    for gy in range(c.ys.start, c.ys.stop + 1):
        for gx in range(c.xs.start, c.xs.stop + 1):
            cells = [((gx + dx, gy + dy), k) for (dx, dy), k in around if (gx + dx, gy + dy) in c.kind]
            if not cells:
                continue
            lower = [e for e in cells if e[1] in (0, 1)]
            if any(e[0] in c.rim for e in lower):
                groups = [("lo", lower), ("hi", [e for e in cells if e[1] in (2, 3)])]
            else:
                groups = [("all", cells)]
            for tag, group in groups:
                if not group:
                    continue
                pid = (gx, gy, tag)
                levels = {flat[cell] for cell, k in group if cell in flat}
                rocks = [(cell, k) for cell, k in group if cell not in flat]
                if len(levels) == 1:
                    fixed[pid] = value[pid] = levels.pop()
                    for e in group:
                        owner[e] = pid
                    continue
                for cell, k in group:
                    if cell in flat:        # a flat step: each its own
                        own = (gx, gy, tag, cell)
                        fixed[own] = value[own] = flat[cell]
                        owner[(cell, k)] = own
                for e in rocks:
                    owner[e] = pid
                if rocks:
                    domain[pid] = sorted(levels) if levels else None
                    vals = [init[cell][k] for cell, k in rocks]
                    v = max(set(vals), key=lambda x: (vals.count(x), x))
                    if levels and v not in levels:
                        v = min(levels, key=lambda f: abs(f - v))
                    value[pid] = v
    rock_cells = [cell for cell, k in c.kind.items() if cell not in flat]
    points_of = {cell: [owner[(cell, k)] for k in range(4)] for cell in rock_cells}
    cells_at = collections.defaultdict(list)
    for cell, pts in points_of.items():
        for p in set(pts):
            cells_at[p].append(cell)

    def cost(cell):
        return piece_cost(c, cell, [value[p] for p in points_of[cell]])

    free = sorted(p for p in cells_at if p not in fixed)
    for _ in range(24):
        moved = 0
        for p in free:
            here = value[p]
            if domain[p] is not None:
                cand = list(domain[p])
            else:
                near = {value[q] for cell in cells_at[p] for q in points_of[cell]}
                cand = sorted(near | {here - LEVEL, here + LEVEL})
            best, best_cost = here, None
            for v in cand:
                value[p] = v
                total = sum(cost(cell) for cell in cells_at[p]) + 0.01 * abs(v - init_of(p, cells_at, points_of, init)) / LEVEL
                if best_cost is None or total < best_cost - 1e-9:
                    best, best_cost = v, total
            value[p] = best
            moved += best != here
        if not moved:
            break
    out = {}
    for cell in c.kind:
        if cell in flat:
            out[cell] = [flat[cell]] * 4
        else:
            out[cell] = [value[p] for p in points_of[cell]]
    return out


def init_of(p, cells_at, points_of, init):
    """The height a point had before the solve (the first cell's)."""
    cell = cells_at[p][0]
    return init[cell][points_of[cell].index(p)]


# -- 5. shapes ----------------------------------------------------------------

def shape(cn):
    """5 x 5 heights of a cell from its corners [NW, NE, SW, SE]: two flat
    triangles, split along the diagonal that falls the most (a hip's or a
    valley's crease runs through its odd corner). Every edge is the straight
    line between its two corners, so cells sharing corners meet exactly."""
    nw, ne, sw, se = cn
    grid = [[0.0] * (N + 1) for _ in range(N + 1)]
    main = abs(nw - se) >= abs(ne - sw)
    for j in range(N + 1):
        for i in range(N + 1):
            u, v = i / N, j / N
            if main:        # crease NW-SE
                if u >= v:
                    h = nw + (ne - nw) * u + (se - ne) * v
                else:
                    h = nw + (sw - nw) * v + (se - sw) * u
            else:           # crease NE-SW
                if u + v <= 1:
                    h = nw + (ne - nw) * u + (sw - nw) * v
                else:
                    h = se + (sw - se) * (1 - u) + (ne - se) * (1 - v)
            grid[j][i] = h
    return grid


# -- 5. blocks ----------------------------------------------------------------
#
# Every cell is one piece of the tile grid, kept inside its own square:
#
#   F  a top: level all over
#   S  a face: the vertical south wall of one level (at 45 degrees a drawn
#      pixel at (u, h, v + h) with h falling a pixel per pixel down the tile
#      is the wall z = row + level)
#   W  a band falling west: a 45-degree slope one level across the tile,
#   E  high on its east side (W) or on its west side (E); W2/E2 the same
#      falling two levels (where a diagonal flank has one tile for two)
#   cSE/cSW/vSE/vSW  a face and a band in one tile (a terrace's corner, a
#      face's end turning into the band of the next step: what 07d draws)
#
# Wherever two pieces differ along their shared edge, the engine stands a
# wall there (records): no piece is ever bent, stretched or cut across.

FIN = 3.0               # a band climbing past the terrace it flanks
STEEP = 0.4             # a band falling two levels in its tile (a flank's
                        # head on a diagonal edge, one tile for two levels)
FACE_SHARE = 0.4        # of its pixels drawn as face: a face, else a top
# A face meeting a band in one tile (a face's end turning into the band of
# the next step, a terrace's corner): cSE/cSW convex, vSE/vSW concave, the
# band falling east/west.
CORNERS = ("cSE", "cSW", "vSE", "vSW")
CORNER_KINDS = list(CORNERS) if os.environ.get("VOXEL_TERRACES_CORNERS", "1") != "0" else []
CORNER = 0.3            # taking a corner piece instead of the drawing's kind


def is_face(c, cell):
    if cell in c.band:
        return False
    cls = pixel_classes(c.art(cell), c.metatile(cell))
    f = sum(row.count("F") for row in cls)
    return f >= FACE_SHARE * 256


def foot_of(piece):
    kind, top = piece
    return top - LEVEL if kind == "S" or kind in CORNERS else top


def blocks(c, U, flat, kept=None):
    """{cell: (kind, top level)}: one piece a cell.

    The kind is the drawing's (a band a ramp its way, a tile mostly drawn as
    face a face, else a top) and the level the one its column or row counts
    (U). Then the pieces of rock are settled together: each takes the kind
    and level, of a few round its own, that leaves it meeting its neighbours
    best - an edge two pieces share at the same heights costs nothing, a
    step costs by its height. A step west or east is a block's side (half);
    a top standing over lower ground south of it (no face drawn for the
    drop) a fault; a back (the ground behind a top lower than it) is how
    terraces end and costs little. Moving off the counted level costs, and so does taking the
    other kind than the drawing's. A pit (rock rising behind a face's foot
    or a band's: open to the sky between two walls) is never worth it. Flat
    terraces, ledges and berry soil do
    not move; nothing is ever bent: every piece stays one of four."""
    out = {}
    pref = {}
    for cell in c.kind:
        if cell in flat:
            out[cell] = ("F", flat[cell])
            x, y = cell
            if c.metatile(cell) in BOULDER_HEAD:
                # a boulder's upper half beside a terrace a level higher: the
                # flank of that terrace's rounded head, drawn over the lower
                # one - a band up to it, not a side wall the drawing lacks
                for n, side in (((x + 1, y), "W"), ((x - 1, y), "E")):
                    if c.kind.get(n) == TOP and flat.get(n) == flat[cell] + LEVEL:
                        out[cell] = (side, flat[n])
        elif cell in c.band:
            x, y = cell
            side = c.band[cell]
            hi, lo = ((x + 1, y), (x - 1, y)) if side == "W" else ((x - 1, y), (x + 1, y))
            if hi in flat and lo in flat and flat[hi] <= flat[lo]:
                # a band between two grounds at one level (a sand bank the
                # solve put level with the sea): nothing to fall, it lies flat
                out[cell] = ("F", flat[hi])
                continue
            pref[cell] = c.band[cell]
            out[cell] = (c.band[cell], U[cell])
        else:
            pref[cell] = "S" if is_face(c, cell) else "F"
            out[cell] = (pref[cell], U[cell])
    grid = {cell: piece_grid(*p) for cell, p in out.items()}
    kept = kept or {}
    grid.update(kept)
    trees = {cell for cell, k in c.kind.items() if k == UNKNOWN and c.role(cell) == "tree"}
    free = sorted(cell for cell in out if cell not in flat and cell not in c.keep
                  and c.kind[cell] in (ROCK, TOP))

    def step(p, q, back):
        """The cost of an edge point at heights p (north/west) and q. `back`:
        north/south, and what the cost of the south one rising is (a top's
        back over the ground behind it: cheap; rock rising behind a face's
        foot: a pit between two walls, open to the sky)."""
        d = abs(p - q)
        if d < 0.5:
            return 0.0
        k = 1.0 + 0.5 * (d / LEVEL - 1.0)
        if back is None:
            return 0.5 * k          # west/east: a block's side, a wall
        return k * (back if q > p else 1.0)

    def kind_at(n, piece):
        return piece[0] if n is None else (out[n][0] if n in out and n not in kept else "F")

    def cost(cell, g, piece):
        x, y = cell
        total = 0.0
        for n, mine, theirs, ns in (((x - 1, y), "W", "E", False), ((x + 1, y), "E", "W", False),
                                    ((x, y - 1), "N", "S", True), ((x, y + 1), "S", "N", True)):
            if n not in grid or n in trees:
                # (a tree's ground is not drawn: the face behind it goes on
                # down to it, whatever meets it - behind_trees)
                continue
            a, b = edge(g, mine), edge(grid[n], theirs)
            if n in kept and any(q - p > 0.5 for p, q in zip(a, b)):
                # a ledge or berry soil keeps its own relief and walls: it
                # never stands over a piece beside it (no wall would close it)
                total += 10.0
            if mine in ("W", "N"):
                a, b = b, a         # p is the north / west one
            back = None
            if not ns:
                # a band standing over the terrace on its high side (or a
                # neighbour band over us on its high side) is a fin: the
                # flank climbs past what it is the flank of
                west, east = (n, cell) if mine == "W" else (cell, n)
                kw, ke = kind_at(west, piece) if west != cell else piece[0],                     kind_at(east, piece) if east != cell else piece[0]
                kw, ke = kw[0], ke[0]
                fin = (kw == "W" and max(p - q for p, q in zip(a, b)) > 0.5) or                       (ke == "E" and max(q - p for p, q in zip(a, b)) > 0.5)
                if fin:
                    total += FIN
            if ns:
                north = kind_at(n, piece) if mine == "N" else piece[0]
                south = piece[0] if mine == "N" else kind_at(n, piece)
                # behind a top the ground runs on under the next (a back);
                # behind a face's foot, or a band's under a face or a band,
                # rock rising is a pit between two walls
                back = 0.3 if north == "F" or (north != "S" and south == "F") else pit[0]
            total += sum(step(p, q, back) for p, q in zip(a, b)) / (N + 1)
        total += 0.25 * abs(piece[1] - U[cell]) / LEVEL
        if piece[0] in CORNERS:
            total += CORNER
        elif cell in pref and piece[0][0] != pref[cell]:
            total += 0.6
        elif piece[0] in ("W2", "E2"):
            total += STEEP
        return total

    # the pits' cost rises in steps: with it low every piece first finds
    # the level its own kind and its neighbours want; raised, the pieces
    # round a pit move to close it (one at a time could not: a band's foot
    # over a face needs the face to give way first)
    pit = [0.3]
    for pit[0], sweeps in ((0.3, 10), (1.0, 10), (3.0, 10), (10.0, 30)):
        for _ in range(sweeps):
            moved = 0
            for cell in free:
                kind, top = out[cell]
                if cell in c.band and kind == "F":
                    continue
                kinds = [c.band[cell], c.band[cell] + "2"] if cell in c.band else ["F", "S"] + CORNER_KINDS
                tops = {U[cell] + LEVEL * k for k in (-2, -1, 0, 1, 2)} | {top}
                best, best_cost = out[cell], cost(cell, grid[cell], out[cell])
                for k2 in kinds:
                    for t in sorted(tops):
                        piece = (k2, t)
                        if piece == out[cell]:
                            continue
                        cc = cost(cell, piece_grid(*piece), piece)
                        if cc < best_cost - 1e-6:
                            best, best_cost = piece, cc
                if best != out[cell]:
                    out[cell] = best
                    grid[cell] = piece_grid(*best)
                    moved += 1
            if not moved:
                break
    # a boulder's upper half over a band: the rounded head a crest starts
    # with, drawn on the ridge it rises from - the crest's own band from
    # there (as a flat top the crest rose at the next row, a cap left on it)
    for cell in sorted(out, key=lambda cl: -cl[1]):
        x, y = cell
        s = (x, y + 1)
        if c.metatile(cell) in BOULDER_HEAD and cell in flat and s in c.band and out[s][0][0] in "WE":
            out[cell] = out[s]
    return out


def piece_grid(kind, top):
    """5 x 5 heights of a piece."""
    if kind in CORNERS:
        # a face and a band meeting: one level down, the face falling south
        # and the band east or west, the nearer of the two (convex: a
        # terrace's corner, face under the diagonal, band over it) or the
        # farther (concave: the two walls meeting inside)
        east = kind[2] == "E"
        pick = max if kind[0] == "c" else min
        return [[top - LEVEL / N * pick(i if east else N - i, j) for i in range(N + 1)] for j in range(N + 1)]
    if kind == "S":
        return [[top - LEVEL * j / N] * (N + 1) for j in range(N + 1)]
    if kind in ("W", "W2"):
        drop = LEVEL * (2 if kind == "W2" else 1)
        return [[top - drop + drop * i / N for i in range(N + 1)] for _ in range(N + 1)]
    if kind in ("E", "E2"):
        drop = LEVEL * (2 if kind == "E2" else 1)
        return [[top - drop * i / N for i in range(N + 1)] for _ in range(N + 1)]
    return [[top] * (N + 1) for _ in range(N + 1)]


def behind_trees(c, flat, grids):
    """A wood standing at a cliff's foot hides the cliff, it does not cut it
    off: under each tree the face over it goes on down, a pixel a row, to
    the ground the wood stands on - the mountain as if the trees were not
    there (they are models of their own, drawn over it)."""
    step = LEVEL / N
    for cell in sorted(grids, key=lambda cl: (cl[1], cl[0])):
        if c.kind.get(cell) != UNKNOWN or c.role(cell) != "tree" or cell in c.keep:
            continue
        x, y = cell
        north = grids.get((x, y - 1))
        if north is None or (x, y - 1) in c.keep:
            continue
        low = flat.get(cell, min(min(r) for r in grids[cell]))
        top = north[N]
        if max(top) <= low + 0.5:
            continue
        grids[cell] = [[max(low, top[i] - j * step) for i in range(N + 1)] for j in range(N + 1)]


def group_of(layout_id):
    """[(layout, x, y)]: the drawn group a layout is solved in, placed."""
    for name, members in gr.DRAWN.items():
        if layout_id in members:
            return sorted(((lid, ox, oy) for lid, (ox, oy) in members.items()),
                          key=lambda m: (m[0] != layout_id, m[0]))
    return [(layout_id, 0, 0)]


def reference_heights(relief, members):
    """{canvas cell: height in the world} of a parsed relief.bin's cells
    (centre point), the base where a cell is not lifted."""
    import voxel_relief_fixes as vrf
    out = {}
    for lid, ox, oy in members:
        t = relief["layouts"].get(vrf.layout_index(lid))
        if t is None:
            continue
        art = gr.layout_art(lid)
        for y in range(art.h):
            for x in range(art.w):
                g = t["cells"].get((x, y))
                out[(x + ox, y + oy)] = t["base"] + (g[12] if g else 0)
    return out


def seam_meets(relief, members):
    """{canvas cell}: the edge cells of a group whose height in a parsed
    relief.bin is the height of the cell across the seam, on the map
    connected there (within a half level) - where the two maps meet."""
    import voxel_relief_fixes as vrf
    size = {lid: (gr.layout_art(lid).w, gr.layout_art(lid).h) for lid, _, _ in members}
    at = {lid: (ox, oy) for lid, ox, oy in members}

    def height(lid, x, y):
        t = relief["layouts"].get(vrf.layout_index(lid))
        if t is None:
            return None
        g = t["cells"].get((x, y))
        return t["base"] + (g[12] if g else 0)

    def cell_at(lid, edge, i):
        w, h = size[lid]
        return {"down": (i, h - 1), "up": (i, 0), "right": (w - 1, i), "left": (0, i)}[edge]

    out = set()
    for a, b, direction, offset in gr.map_links():
        if a not in at or b in at:
            continue
        a_art, b_art = gr.layout_art(a), gr.layout_art(b)
        size[b] = (b_art.w, b_art.h)
        for ea, ia, eb, ib in gr._seam_cells(a, b, direction, offset, size):
            ax, ay = cell_at(a, ea, ia)
            ha, hb = height(a, ax, ay), height(b, *cell_at(b, eb, ib))
            if ha is not None and hb is not None and abs(ha - hb) <= LEVEL // 2:
                out.add((ax + at[a][0], ay + at[a][1]))
    return out


def solve(layout_id, reference=None):
    """The terraces of the group a layout is drawn in. `reference`: a parsed
    relief.bin whose world placement is kept."""
    members = group_of(layout_id)
    c = Cells(members)
    runs = runs_of(c)
    comp = terraces(c, runs)
    ref = reference_heights(reference, members) if reference is not None else None
    meets = seam_meets(reference, members) if reference is not None else None
    level, dropped = solve_levels(c, comp, runs, ref, meets)
    U = upper_levels(c, comp, level, runs)
    fixed = {cell for d, hi, cells, lo in runs if hi in comp and lo in comp
             and level[comp[hi]] - level[comp[lo]] == LEVEL * steps(c, cells, d)[1] for cell in cells}
    flat = {cell: level[t] for cell, t in comp.items()}
    flat.update(unknown_levels(c, comp, level, U))
    U = settle(c, U, flat, fixed)
    kept = {}
    if reference is not None:
        # ledges and berry soil keep the relief they had: their neighbours
        # meet (and wall themselves against) what is really there
        import voxel_relief_fixes as vrf
        for cell in c.keep:
            lid, x, y = c.src[cell]
            t = reference["layouts"].get(vrf.layout_index(lid))
            if t is None:
                continue
            g = t["cells"].get((x, y))
            kept[cell] = [[t["base"] + (g[j * (N + 1) + i] if g else 0) for i in range(N + 1)]
                          for j in range(N + 1)]
    pieces = blocks(c, U, flat, kept)
    grids = {cell: piece_grid(*p) for cell, p in pieces.items()}
    grids.update(kept)
    behind_trees(c, flat, grids)
    return {"cells": c, "comp": comp, "level": level, "U": U, "flat": flat,
            "pieces": pieces, "grids": grids, "dropped": dropped}


# -- into relief.bin ----------------------------------------------------------

CUT_LIFT = 2          # pixels: background lifted more than this is cut away
FACES = (0x07c, 0x0a9, 0x079, 0x091)   # a step's wall: the first face the map draws
BLOCK_WALLS = 4       # voxel_relief.h VOXEL_RELIEF_BLOCK_WALLS >> 8: west and east walls of the face too
PLAIN = 1             # its flat layer is the ground behind it, plain


def edge(grid, side):
    """The five heights along one edge of a cell, west to east / north to south."""
    if side == "N":
        return grid[0]
    if side == "S":
        return grid[N]
    return [grid[j][0 if side == "W" else N] for j in range(N + 1)]


def _sample(g, px, py):
    fx, fy = px / (16 / N), py / (16 / N)
    a, b = min(int(fx), N - 1), min(int(fy), N - 1)
    tx, ty = fx - a, fy - b
    return ((g[b][a] * (1 - tx) + g[b][a + 1] * tx) * (1 - ty)
            + (g[b + 1][a] * (1 - tx) + g[b + 1][a + 1] * tx) * ty)


_ROCK_OF = {}
_PLAIN_OF = {}


def behind(c, out, cell, lid):
    """The ground that runs on under a cell, seen past its back: what the
    cell north of it is (flat) or stands on (its own cut's ground), else the
    first flat drawing up its column - in the map's own tileset. Right under
    rock (a band's or a face's foot, a back the flat drawing has touching
    the rock over it) the gap the 3D view opens is that rock's own block:
    the drawing never shows ground there."""
    x, y = cell
    for k in range(1, 4):
        n = (x, y - k)
        if n not in c.src or c.src[n][0] != lid:
            break
        if k == 1 and c.kind[n] == ROCK:
            return c.metatile(n)
        if c.kind[n] in (GROUND, TOP):
            # the plain ground, not the drawing of its edge: a shore tile's
            # foam ran on under the rock as a white line along its foot
            l2, nx, ny = c.src[n]
            if lid not in _ROCK_OF.setdefault(id(c), {}):
                _ROCK_OF[id(c)][lid] = {c.src[r][1:] for r, k in c.kind.items()
                                        if k in (ROCK, TOP) and c.src[r][0] == lid}
            key = (id(c), lid, nx, ny)
            if key not in _PLAIN_OF:
                _PLAIN_OF[key] = gr.plain_ground(c.arts[lid], c.roles_of[lid], _ROCK_OF[id(c)][lid], nx, ny, "S")
            m = _PLAIN_OF[key]
            return c.metatile(n) if m is None else m
        if n in out and out[n][2] is not None:
            return out[n][2]
    return 0x071


def low_side(c, cell):
    """Rows of bits, 1 where a band's tile draws the top beyond the band's
    foot: past the band's last face-coloured pixel on its low side, a row at
    a time, and above where the band starts (its head) past that edge
    carried on up. None for a cell that is no band."""
    side = c.band.get(cell)
    if side is None:
        return None
    cls = pixel_classes(c.art(cell), c.metatile(cell))
    rows = {}
    for j in range(16):
        fx = [i for i in range(16) if cls[j][i] == "F"]
        if fx:
            rows[j] = max(fx) if side == "E" else min(fx)
    if len(rows) < 3:
        return None
    js = sorted(rows)
    # the edge carried on above (and below) the band: a straight line
    n = len(js)
    mj = sum(js) / n
    mx = sum(rows[j] for j in js) / n
    sj = sum((j - mj) ** 2 for j in js) or 1.0
    slope = sum((j - mj) * (rows[j] - mx) for j in js) / sj
    out = []
    for j in range(16):
        e = rows[j] if j in rows else mx + slope * (j - mj)
        bits = 0
        for i in range(16):
            beyond = i > e + 0.5 if side == "E" else i < e - 0.5
            if beyond and cls[j][i] == "T":
                bits |= 1 << i
        out.append(bits)
    return out


def records(sol):
    """{canvas cell: [variant mask, "fill" or None, foot, ground metatile or
    None, wall, sides, flags]}: cut tiles, the ground behind run on, cliff
    walls. Heights in the world's levels."""
    c, grids = sol["cells"], sol["grids"]
    rock = {cell for cell, k in c.kind.items() if k in (ROCK, TOP)}
    local_rock = collections.defaultdict(set)
    for cell in rock:
        lid, x, y = c.src[cell]
        local_rock[lid].add((x, y))
    face_of = {}
    for lid, ox, oy in c.members:
        art = c.arts[lid]
        drawn = {art.metatile(x, y) for y in range(art.h) for x in range(art.w)}
        face_of[lid] = next((f for f in FACES if f in drawn), FACES[0])
    out = {}
    # rows north first: a fill takes what the cell north of it stands on
    for (x, y) in sorted(grids, key=lambda cell: (cell[1], cell[0])):
        g = grids[(x, y)]
        lid, lx, ly = c.src[(x, y)]
        art, roles = c.arts[lid], c.roles_of[lid]
        rec = [None, 0, None, None, 0, 0]
        # cliff walls: an edge standing over its neighbour's
        for n, mine, theirs in (((x - 1, y), "W", "E"), ((x + 1, y), "E", "W"), ((x, y + 1), "S", "N")):
            if n not in grids:
                continue
            if any(p - q > 0.5 for p, q in zip(edge(g, mine), edge(grids[n], theirs))):
                rec[3] = face_of[lid]
                rec[4] = 0xFF
                rec[5] |= BLOCK_WALLS
        # its background, lifted off the ground it is: cut away
        # the ground a rock tile draws round its rock (a band's sand corner,
        # the sea past a ridge's flank) is not rock: cut away, it lies flat
        # at the foot as the ground it is (the pieces stay whole)
        # (stairs cut through a face are all stairs: their shaded steps are
        # no ground to cut away - cut, they showed the void)
        mask = (gr._cut_mask(art, roles, lx, ly, local_rock[lid])
                if c.kind[(x, y)] in (ROCK, TOP) and c.role((x, y)) != "stair" else None)
        low = low_side(c, (x, y))
        foot = None
        if low and any(low):
            # the top beyond a band's foot, drawn in its tile: the terrace a
            # level down, behind it
            mask = [(a | b) for a, b in zip(mask or [0] * 16, low)]
            foot = min(min(r) for r in g)
        if mask and any(mask):
            grounds = [sol["flat"][n] for n in ((x, y - 1), (x - 1, y), (x + 1, y), (x, y + 1))
                       if n in sol["flat"] and c.kind.get(n) in (GROUND, UNKNOWN)]
            if foot is None:
                foot = min(grounds) if grounds else min(min(r) for r in g)
            elif grounds:
                foot = min([foot] + grounds)
            lifted = sum(1 for j in range(16) for i in range(16)
                         if (mask[j] >> i) & 1 and _sample(g, i + 0.5, j + 0.5) - foot > CUT_LIFT)
            if lifted >= 4:
                rec[0], rec[1] = mask, foot
                rec[2] = gr._behind(art, roles, lx, ly, local_rock[lid], mask)
                if rec[2] is not None and gr._plain_background(art, art.metatile(lx, ly), mask, rec[2]):
                    rec[5] |= PLAIN
                if rec[3] is not None:
                    # a cut tile's sides go down only from its rock's own
                    # pixels (the cleared ground is no wall: seen through
                    # it, a face-textured side stood as a dark slab), as
                    # the edge column of its own drawing
                    bits = 0
                    for k in range(4):
                        for j in range(4 * k, 4 * k + 4):
                            if not mask[j] & 1:
                                bits |= 1 << k
                            if not (mask[j] >> 15) & 1:
                                bits |= 1 << (4 + k)
                    rec[4] = bits
                    rec[5] &= ~BLOCK_WALLS
        # its back over lower ground: that ground runs on under it
        if rec[0] is None and (x, y - 1) in grids:
            north = edge(grids[(x, y - 1)], "S")
            if any(p - q > 0.5 for p, q in zip(edge(g, "N"), north)):
                rec[0] = "fill"
                rec[1] = min(north)
                rec[2] = behind(c, out, (x, y), lid)
        if rec[0] is not None or rec[3] is not None:
            out[(x, y)] = rec
    # under every piece of rock standing over ground round it, the lowest of
    # that ground runs on: wherever two pieces leave a gap the eye can find
    # (where three or four cells meet at different levels), it finds ground
    # there, never the void
    for (x, y) in sorted(grids, key=lambda cell: (cell[1], cell[0])):
        if c.kind.get((x, y)) not in (ROCK, TOP) or (x, y) in c.keep:
            continue
        rec = out.get((x, y))
        if rec is not None and rec[0] is not None:
            continue
        lid = c.src[(x, y)][0]
        near = [min(min(r) for r in grids[n]) for n in ((x + i, y + j) for i in (-1, 0, 1) for j in (-1, 0, 1))
                if n in grids and n != (x, y) and c.src[n][0] == lid]
        own = min(min(r) for r in grids[(x, y)])
        if not near or min(near) > own - 0.5:
            continue
        rec = out.setdefault((x, y), [None, 0, None, None, 0, 0])
        rec[0], rec[1], rec[2] = "fill", min(near), behind(c, out, (x, y), lid)
    # a back k levels over the ground behind it stands k rows south of that
    # ground (a pixel at height h is drawn h rows north of where it is): the
    # ground runs on under every one of those rows, not only the first
    for (x, y), rec in sorted(out.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        if rec[0] != "fill":
            continue
        rise = max(edge(grids[(x, y)], "N")) - rec[1]
        # (the fill lies two pixels under the ground it runs on, so a rise of
        # one level leaves a sliver at the back unless the next row has it too)
        for k in range(1, int((rise + 2.5) // LEVEL) + 1):
            n = (x, y + k)
            if n not in grids or c.src[n][0] != c.src[(x, y)][0]:
                break
            if min(min(r) for r in grids[n]) <= rec[1] + 0.5:
                break       # down to that ground already
            m = out.setdefault(n, [None, 0, None, None, 0, 0])
            if m[0] is None:
                m[0], m[1], m[2] = "fill", rec[1], rec[2]
    return out


def merge(sol, relief):
    """Put a solved group into a parsed relief.bin (voxel_relief_fixes.parse),
    in place of what the generator had there, each layout over its own base.
    Ledges and berry soil keep their own cells and records."""
    import voxel_relief_fixes as vrf
    c = sol["cells"]
    recs = records(sol)
    keyof = {(m, tuple(rows)): i for i, (l2, m, rows) in enumerate(relief["variants"])}
    for lid, ox, oy in c.members:
        lay = vrf.layout_index(lid)
        art = c.arts[lid]
        t = relief["layouts"].setdefault(lay, {"w": art.w, "h": art.h, "flags": vrf.DRAWN,
                                               "base": 0, "cells": {}})
        base = t["base"]
        mine = [cell for cell, src in c.src.items() if src[0] == lid]
        keep = {c.src[cell][1:] for cell in mine if cell in c.keep}
        cells = {cell: g for cell, g in t["cells"].items() if cell in keep}
        for cell in mine:
            xy = c.src[cell][1:]
            if xy in keep:
                continue
            g = [v - base for row in sol["grids"][cell] for v in row]
            # a level cell with a wall down its side needs its own grid: the
            # engine stands walls only from cells it has relief for
            if c.kind[cell] in (ROCK, TOP) or any(abs(v) > 0.25 for v in g) or cell in recs:
                cells[xy] = g
        t["cells"] = cells
        t["flags"] |= vrf.DRAWN
        cuts = {k: v for k, v in relief["cuts"].items() if k[0] != lay or (k[1], k[2]) in keep}
        for cell in mine:
            xy = c.src[cell][1:]
            if cell not in recs or xy in keep:
                continue
            mask, foot, ground, wall, sides, flags = recs[cell]
            if mask is None or mask == "fill":
                k = vrf.NO_VARIANT
            else:
                m = art.metatile(*xy)
                key = (m, tuple(mask))
                if key not in keyof:
                    keyof[key] = len(relief["variants"])
                    relief["variants"].append((lay, m, list(mask)))
                k = keyof[key]
            cuts[(lay,) + xy] = [k, int(round(foot - base)), vrf.NONE16 if ground is None else ground,
                                 vrf.NONE16 if wall is None else wall, sides, flags]
        relief["cuts"] = cuts
    return relief


def cracks(sol):
    """[(cell, neighbour, edge)]: every edge where two cells part with no
    wall closing it (the higher one west, east or north of the step) and no
    ground run on behind it (the higher one south): must be empty."""
    c, g = sol["cells"], sol["grids"]
    rec = records(sol)
    out = []
    for (x, y) in g:
        for n, mine, theirs in (((x + 1, y), "E", "W"), ((x, y + 1), "S", "N")):
            if n not in g:
                continue
            a, b = edge(g[(x, y)], mine), edge(g[n], theirs)
            if max(p - q for p, q in zip(a, b)) > 0.5 and not (rec.get((x, y)) and rec[(x, y)][3]):
                out.append(((x, y), n, mine))
            if max(q - p for p, q in zip(a, b)) > 0.5:
                closed = rec.get(n) and (rec[n][3] if mine == "E" else rec[n][0])
                if not closed:
                    out.append(((x, y), n, mine))
    return out


# The maps built this way, by the order the player reaches them (and Route
# 116, the reference): the rest keep gen_voxel_relief.py's solve until they
# are looked at. VOXEL_TERRACES=all for every drawn group, =none for none,
# or a list of layouts.
GROUPS = ("LAYOUT_ROUTE104", "LAYOUT_ROUTE105", "LAYOUT_ROUTE106", "LAYOUT_ROUTE116")


def chosen_layouts():
    env = os.environ.get("VOXEL_TERRACES", "").strip()
    if env == "none":
        return []
    if env == "all":
        return sorted(min(members) for members in gr.DRAWN.values())
    if env:
        return [l if l.startswith("LAYOUT_") else "LAYOUT_" + l.upper() for l in env.split(",")]
    return list(GROUPS)


def apply_file(path, layouts=None, quiet=False):
    """Rebuild the chosen groups of a relief.bin written by gen_voxel_relief
    (its world placement kept: the bases and every map's ground stay where
    the world solver put them)."""
    import voxel_relief_fixes as vrf
    blob = open(path, "rb").read()
    relief = vrf.parse(blob)
    reference = vrf.parse(blob)
    done = set()
    for lid in (chosen_layouts() if layouts is None else layouts):
        if lid in done:
            continue
        sol = solve(lid, reference)
        done.update(m[0] for m in sol["cells"].members)
        merge(sol, relief)
        if not quiet:
            print("terraces %-40s %3d terraces, %d votes dropped" % (
                "+".join(m[0][7:] for m in sol["cells"].members), len(set(sol["comp"].values())),
                len(sol["dropped"])))
    open(path, "wb").write(vrf.pack(relief))


# -- views --------------------------------------------------------------------

def overlay(sol, path, box=None, scale=4):
    """The drawing with each cell's level (white: flat; yellow U: the level a
    rock cell hangs from), its kind and its corners (magenta), in levels."""
    from PIL import Image, ImageDraw
    c = sol["cells"]
    x0, y0, x1, y1 = box or (c.xs.start, c.ys.start, c.xs.stop, c.ys.stop)
    img = Image.new("RGB", ((x1 - x0) * 16, (y1 - y0) * 16))
    for y in range(y0, y1):
        for x in range(x0, x1):
            if (x, y) in c.src:
                img.paste(c.art((x, y)).cell_image(c.metatile((x, y))).convert("RGB"),
                          ((x - x0) * 16, (y - y0) * 16))
    img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    d = ImageDraw.Draw(img)
    S = 16 * scale
    for y in range(y0, y1):
        for x in range(x0, x1):
            if (x, y) not in c.kind:
                continue
            X, Y = (x - x0) * S, (y - y0) * S
            k = c.kind[(x, y)]
            if (x, y) in sol["flat"]:
                txt, col = "%d" % (sol["flat"][(x, y)] // LEVEL), (255, 255, 255)
            else:
                txt, col = "U%d" % (sol["U"][(x, y)] // LEVEL), (255, 255, 0)
            d.rectangle([X, Y, X + S - 1, Y + S - 1], outline=(0, 0, 0))
            d.text((X + S // 2 - 6, Y + S // 2 - 6), txt, fill=(0, 0, 0))
            d.text((X + S // 2 - 7, Y + S // 2 - 7), txt, fill=col)
            d.text((X + 2, Y + 1), k + c.band.get((x, y), "") + ("r" if (x, y) in c.rim else ""),
                   fill=(0, 255, 255))
            d.text((X + 3, Y + S - 12), sol["pieces"][(x, y)][0], fill=(255, 128, 255))
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("layout")
    ap.add_argument("--png", default=None)
    ap.add_argument("--box", default=None)
    ap.add_argument("--relief", nargs=2, default=None, metavar=("IN", "OUT"),
                    help="write the group into a copy of a relief.bin, keeping its world placement")
    ap.add_argument("--check", action="store_true", help="list edges left open (must be none)")
    args = ap.parse_args()
    import voxel_relief_fixes as vrf
    relief = vrf.parse(open(args.relief[0], "rb").read()) if args.relief else None
    sol = solve(args.layout, relief)
    print("%s: %d terraces, %d votes dropped" % (
        "+".join(m[0] for m in sol["cells"].members), len(set(sol["comp"].values())), len(sol["dropped"])))
    for v, n in sol["dropped"]:
        print("  dropped", v, n)
    if args.check:
        open_edges = cracks(sol)
        print("open edges:", len(open_edges), open_edges[:12])
    if args.png:
        box = tuple(int(t) for t in args.box.split(",")) if args.box else None
        overlay(sol, args.png, box)
    if args.relief:
        merge(sol, relief)
        open(args.relief[1], "wb").write(vrf.pack(relief))
        print("relief ->", args.relief[1])


if __name__ == "__main__":
    main()
