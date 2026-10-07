#!/usr/bin/env python3
"""Building models, one reading of the drawing per building type.

Each spec names where a reference copy of the building sits (layout and cell
rectangle), which metatiles are the ground around it, the parts that rebuild
it, and the art rectangles the model must reproduce pixel for pixel when it
is rendered in the GBA's projection. Row numbers are art rows of that
rectangle.

Littleroot's houses
-------------------
The two houses are the same building. The drawing, from the top:

    rows  1- 8  upper ridge cap       rows 17-24 lower ridge cap  (the "ears")
    rows  9-15  upper ridge teeth     rows 25-31 lower ridge teeth
    rows 16-35  upper roof, 5 courses rows 32-51 lower roof, 5 courses
    rows 36-37  upper fascia          rows 52-53 lower fascia
    rows 38-44  upper storey wall     rows 54-79 ground floor facade

The ears are the same ridge and the same courses as the upper roof, sixteen
rows lower: a full-width lower roof, and a narrower upper storey that comes up
through it with its own roof.

The drawing is shallower than the house. Taken literally (every band at the
depth the 45-degree projection gives it) the house would be two tiles deep,
while it stands on four rows of collision. So the model keeps the real
footprint, and the depth the drawing does not have is covered the way a roof
covers it - with more courses of the same tiles (Strip), never with stretched
ones. The facade, the pent roof in front of the upper storey, the upper wall
and the upper roof's first four courses stay exactly the drawing; only the
roofs grow, by the courses the depth needs.

Both roofs are hipped: seen from the side they are tiles too, laid along the
side eave, so no gable wall of plaster ever shows.
"""

from voxel_building import (Band, Cylinder, Frustum, HipRoof, Prism, Proj, Strip, Tile,
                            Vault, Walls)

GRASS = 0x001

PITCH = 22.0        # degrees, front and back slopes
ROOF_COLUMNS = (12, 68)   # courses sampled away from the drawn verges


def littleroot_house(plaster_x):
    """Both houses. They share every row and every edge; only the middle of
    the facade is rearranged, so each names the 8-px column of plain plaster
    (no window, no door, no post) its walls are dressed from."""
    px0, px1 = plaster_x, plaster_x + 8
    front_z, back_z = 80, 16    # the four collision rows, 5..8
    overhang = 2

    lower = HipRoof(
        "roof_lo", 0, 82, zf=front_z + overhang, zb=back_z - overhang, y0=28,
        fascia=Strip((52, 54), wrap=ROOF_COLUMNS),
        # the seven rows drawn in front of the upper storey, then courses
        # continuing in phase (row 45 sits one above a course's first row)
        slope=Strip((45, 52), repeat=(16, 20), start=16, wrap=ROOF_COLUMNS),
        teeth=(25, 32), cap=(17, 25), pitch=PITCH, run=7,
        ridge_u=(0, 80), end_tile=Tile(0, 25, 8, 32))

    # The upper storey stands on the pent roof where its seventh row ends.
    z_wall, y_base = lower.slope_point(7)
    zf_hi = z_wall + overhang
    y0_hi = zf_hi - 38          # the upper fascia is drawn on rows 36-37
    zb_hi = (lower.zf + lower.zb) - zf_hi
    upper = HipRoof(
        "roof_hi", 8, 72, zf=zf_hi, zb=zb_hi, y0=y0_hi,
        fascia=Strip((36, 38), wrap=ROOF_COLUMNS),
        slope=Strip((32, 36), repeat=(16, 32), wrap=ROOF_COLUMNS),
        teeth=(9, 16), cap=(1, 9), pitch=PITCH, run=6,
        ridge_u=(8, 72), end_tile=Tile(12, 9, 20, 16))

    storey_back = zb_hi + overhang
    post = Tile(10, 38, 14, 45, top=y0_hi)
    storey = Prism(
        "storey", 10, 70,
        [(z_wall, 28), (z_wall, y0_hi), (storey_back, y0_hi), (storey_back, 28)],
        edges={0: Proj(38, 45), 2: Tile(20, 38, 28, 45, top=y0_hi)}, skip=(1, 3),
        caps=[Band(-64, 200, Tile(20, 38, 28, 45, top=y0_hi), z_wall,
                   front=post, back=post, z1=storey_back)])

    wall_top = 28
    corner = Tile(2, 54, 8, 80, top=26)
    base = Prism(
        "ground_floor", 2, 80,
        [(front_z, 0), (front_z, wall_top), (back_z, wall_top), (back_z, 0)],
        edges={0: Proj(54, 80), 2: Tile(px0, 54, px1, 80, top=26)}, skip=(1, 3),
        caps=[Band(0, 26, Tile(px0, 54, px1, 80, top=26), front_z,
                   front=corner, back=corner, z1=back_z),
              Band(26, wall_top + 1, Tile(px0, 54, px1, 55, top=wall_top), front_z)])
    return [base, lower, storey, upper]


# What the model must reproduce exactly: the ground floor, the pent roof in
# front of the upper storey, the upper storey, and the upper roof's fascia and
# first four courses - everything the drawing shows at its own depth.
HOUSE_EXACT = [(2, 54, 80, 80), (12, 45, 68, 54), (10, 38, 70, 45), (12, 16, 68, 38)]

def littleroot_lab():
    """Professor Birch's lab, cells 3..9 x 12..16 of Littleroot.

    The drawing, from the top (112 x 80):

        rows  1- 9  flat top of the roof, light panels, period 8 across
        rows 10-14  its front edge
        rows 15-49  olive tile courses, period 4 down and 8 across
        rows 50-52  fascia
        rows 53-79  facade: posts, two windows, the door

    and on the roof, x 16-47 rows 2-31, a ventilator: a box whose front is
    rows 25-31 and top rows 15-24, carrying a round cowl drawn as a circle
    (rows 2-24). The box stands on the slope exactly where its front row 31
    meets the courses, so its front is the drawing at 45 degrees.
    """
    front_z, back_z = 80, 16    # collision rows 13..16
    overhang = 2
    lab_columns = (8, 104)      # courses away from both verges (12 x 8)
    roof = HipRoof(
        "roof", 0, 114, zf=front_z + overhang, zb=back_z - overhang, y0=29,
        fascia=Strip((50, 53), wrap=lab_columns),
        slope=Strip((46, 50), repeat=(34, 46), wrap=lab_columns),
        teeth=(10, 15), cap=(1, 10), pitch=18.0, run=6,
        ridge_u=(0, 112), end_tile=Tile(48, 10, 57, 15),
        ridge_wrap=(48, 104))    # the drawn ridge is behind the vent at 17-47

    # Ventilator: box front bottom on the slope where art row 32 lies.
    zbox, ybox = roof.slope_point(50 - 32)
    ytop = zbox - 25
    grey = Tile(20, 25, 40, 32, top=ytop)
    box = Prism(
        "vent_box", 16, 48,
        [(zbox, ybox - 6), (zbox, ytop), (zbox - 10, ytop), (zbox - 10, ybox - 6)],
        edges={0: Proj(25, 32), 2: grey}, skip=(3,),
        caps=[Band(-64, 200, grey, zbox)])
    cowl = Cylinder("vent_cowl", 32, zbox - 10, 12, 9, ytop - 4, ytop + 4,
                    back_tile=Tile(24, 16, 40, 25))

    wall_top = 29
    post = Tile(2, 53, 8, 80, top=27)
    plaster = Tile(8, 53, 16, 80, top=27)
    base = Prism(
        "ground_floor", 2, 112,
        [(front_z, 0), (front_z, wall_top), (back_z, wall_top), (back_z, 0)],
        edges={0: Proj(53, 80), 2: plaster}, skip=(1, 3),
        caps=[Band(0, 27, plaster, front_z, front=post, back=post, z1=back_z),
              Band(27, wall_top + 1, Tile(8, 53, 16, 54, top=wall_top), front_z)])
    return [base, roof, box, cowl]


LAB_EXACT = [(2, 53, 112, 80), (8, 32, 104, 53), (48, 16, 104, 32)]

# The Center's crown, read off its drawn front arch (rows 16-23): the height
# of the arch over each column, 1 px at x 16 and 47 and 8 px from 27 to 36.
# Its back arch is drawn exactly 16 rows higher in every column, so the vault
# is 16 px deep - the one depth the drawing states outright.
CENTER_ARCH = [(16, 0), (16.5, 1), (17.5, 2), (18.5, 3), (19.5, 4), (20.5, 4),
               (21.5, 5), (22.5, 6), (23.5, 6), (24.5, 7), (26.5, 7), (27.5, 8),
               (36.5, 8), (37.5, 7), (39.5, 7), (40.5, 6), (41.5, 6), (42.5, 5),
               (43.5, 4), (44.5, 4), (45.5, 3), (46.5, 2), (47.5, 1), (48, 0)]


def center_or_mart(rib_repeat, crown=False):
    """The Pokemon Center and the Poke Mart: one building, two paint jobs.

    The drawing (64 x 64), from the top:

        rows  2- 8  the Center's raised crown (the Mart has none)
        rows  9-23  the flat top, ribbed front to back
        rows 24-37  the roof band with its emblem, drawn round
        rows 38-63  walls: fascia, glass, the door, the sign, the plinth

    The plan is an octagon: the front runs x 8-56, and 8-px chamfers turn the
    corners. The drawing says so itself - the plinth runs diagonally from
    (8, 63) to (0, 56) and the eave from (8, 38) to (0, 30), which is what a
    45-degree chamfer looks like at 45 degrees. The emblem is drawn round, so
    the band faces the camera square on: it pitches at 45 degrees, rising 7.

    Real depth: three collision rows, Z 16-64. The top grows backwards by rows
    of its own ribs, which run front to back and so repeat without a seam.
    """
    front, back = 64, 16
    plan = [(8, front), (56, front), (64, front - 8), (64, back + 8),
            (56, back), (8, back), (0, back + 8), (0, front - 8)]
    parts = [Frustum(
        "body", plan, wall_top=26,
        wall_side=Strip((38, 64), wrap=(8, 16)),
        band_rise=7,
        band_side=Strip((24, 38), wrap=(8, 16)),
        top=Strip((9, 24), repeat=rib_repeat, wrap=(8, 56)))]
    if crown:
        # on the flat top (y 33), from its front edge (z 57) back 16
        parts.append(Vault("crown", CENTER_ARCH, zf=57, zb=41, y0=33))
    return parts


# The walls, the chamfers, the band and the top as drawn; the corners above
# the chamfers are the band's sides, which the real depth moves.
CENTER_EXACT = [(8, 30, 56, 64), (0, 38, 64, 64), (12, 9, 52, 30)]
# The Center adds its crown, whole: both arches and the ribs between them.
CROWN_EXACT = CENTER_EXACT + [(16, 0, 48, 24, True)]

def oldale_house():
    """Oldale's two houses, cells 4..7 x 4..7 and 14..17 x 13..16.

    The drawing (64 x 64), from the top:

        rows  0- 9  the ridge's top, notched along its back edge
        rows 10-13  its front
        rows 14-33  five courses of tiles, period 4 down and 8 across
        rows 34-35  fascia
        rows 36-63  facade: posts, the window, the door

    One storey under a hipped roof, the same reading as Littleroot's lower
    roof. Real depth: the three collision rows, Z 16-64.
    """
    front_z, back_z = 64, 16
    overhang = 2
    columns = (8, 56)           # courses away from both verges (6 x 8)
    roof = HipRoof(
        "roof", 0, 64, zf=front_z + overhang, zb=back_z - overhang, y0=30,
        fascia=Strip((34, 36), wrap=columns),
        slope=Strip((30, 34), repeat=(14, 30), wrap=columns),
        teeth=(10, 14), cap=(0, 10), pitch=22.0, run=6,
        ridge_u=(0, 64), end_tile=Tile(8, 10, 18, 14))

    wall_top = 30
    post = Tile(2, 36, 9, 64, top=28)
    plaster = Tile(9, 36, 15, 64, top=28)
    base = Prism(
        "ground_floor", 2, 64,
        [(front_z, 0), (front_z, wall_top), (back_z, wall_top), (back_z, 0)],
        edges={0: Proj(36, 64), 2: plaster}, skip=(1, 3),
        caps=[Band(0, 28, plaster, front_z, front=post, back=post, z1=back_z),
              Band(28, wall_top + 1, Tile(9, 36, 15, 37, top=wall_top), front_z)])
    return [base, roof]


OLDALE_HOUSE_EXACT = [(2, 36, 64, 64), (8, 14, 56, 36)]


def briney_house():
    """Mr Briney's cottage on Route 104, cells 15..19 x 47..50 (80 x 64).

    The drawing, from the top:

        rows  0- 8  the ridge's top: thatch bundles, period 8 across
        rows  9-15  its front: a batten and a short course between dark lines
        rows 16-35  the thatch: period 8 down (rows 16-23 = 24-31) and across,
                    its eave course fringed at rows 32-35
        rows 36-38  fascia
        rows 39-47  the lattice under the eave (x 0-80)
        rows 48-63  the ground floor (x 1-80): posts, reed walls, the door

    One storey under a hipped roof, like Oldale's houses. Real depth: the
    three collision rows, Z 16-64. The verges (x 0-7 and 72-79) are drawn
    as edge bundles, so the courses are sampled from x 8-72.
    """
    front_z, back_z = 64, 16
    overhang = 2
    columns = (8, 72)
    roof = HipRoof(
        "roof", 0, 80, zf=front_z + overhang, zb=back_z - overhang, y0=27,
        fascia=Strip((36, 39), wrap=columns),
        slope=Strip((16, 36), repeat=(16, 24), wrap=columns),
        teeth=(9, 16), cap=(0, 9), pitch=22.0, run=6,
        ridge_u=(0, 80), end_tile=Tile(8, 9, 18, 16))

    floor_top, wall_top = 16, 27
    post = Tile(1, 48, 8, 64, top=floor_top)
    reed = Tile(8, 48, 18, 64, top=floor_top)
    lattice = Tile(8, 39, 16, 48, top=25)
    lattice_end = Tile(0, 39, 3, 48, top=25)
    ground_floor = Prism(
        "ground_floor", 1, 80,
        [(front_z, 0), (front_z, floor_top), (back_z, floor_top), (back_z, 0)],
        edges={0: Proj(48, 64), 2: reed}, skip=(1, 3),
        caps=[Band(0, floor_top, reed, front_z, front=post, back=post, z1=back_z)])
    upper = Prism(
        "lattice", 0, 80,
        [(front_z, floor_top), (front_z, wall_top), (back_z, wall_top), (back_z, floor_top)],
        edges={0: Proj(39, 48), 2: lattice}, skip=(1, 3),
        caps=[Band(floor_top, wall_top + 1, lattice, front_z, front=lattice_end,
                   back=lattice_end, z1=back_z)])
    return [ground_floor, upper, roof]


BRINEY_HOUSE_EXACT = [(1, 39, 80, 64), (0, 39, 1, 48), (8, 16, 72, 39)]


def flower_shop():
    """The Pretty Petal flower shop on Route 104, cells 3..8 x 15..18 (96 x 64).

    A flat roof of corrugated sheet, seen from above as the GBA sees every
    flat top. The drawing, from the top:

        rows  0- 3  the roof's back edge: an outline and three light rows
        rows  4-27  the sheet, every row the same (ribs period 4 across)
        rows 28-37  the roof slab's red front and the dark line under it
        rows 38-63  the facade (x 1-95): posts, the awning, windows, the door

    Real depth: the three collision rows, Z 16-64. The sheet is laid back
    to them with more of its own rows, the back edge closing it.
    """
    front, back = 64, 16
    wall = 64 - 38
    top = wall + (38 - 28)
    post = Tile(1, 38, 6, 64, top=wall)
    siding = Tile(72, 40, 80, 48, top=wall - 2)
    rim = Tile(9, 28, 17, 38, top=top)
    body = Prism(
        "ground_floor", 1, 95,
        [(front, 0), (front, wall), (back, wall), (back, 0)],
        edges={0: Proj(38, 64), 2: siding}, skip=(1, 3),
        caps=[Band(0, wall, siding, front, front=post, back=post, z1=back)])
    roof = Prism(
        "roof", 0, 96,
        [(front, wall), (front, top), (back, top), (back, wall)],
        edges={0: Proj(28, 38), 1: Strip((4, 28), repeat=(20, 28), tail=(0, 4)), 2: rim},
        skip=(3,), caps=[Band(wall, top + 1, rim, front)])
    return [body, roof]


FLOWER_SHOP_EXACT = [(0, 4, 96, 38), (1, 38, 95, 64)]

def kit_house(width):
    """The General tileset's red-roofed house, any width (in pixels).

    Built from a kit - roof caps, repeated middles, a facade of posts, windows
    and a door - so the same drawing comes 4 and 5 cells wide. The rows are
    the kit's, whatever the width (64 high):

        rows  0- 9  the ridge's top, period 8 across
        rows 10-15  its front
        rows 16-34  scale tiles: two 8-row courses and the eave's three rows
        rows 35-37  fascia
        rows 38-63  facade

    Real depth: three collision rows, Z 16-64. Courses are sampled between
    the drawn verges (x 0-2 and the last three columns), keeping the phase.
    """
    front_z, back_z = 64, 16
    overhang = 2
    columns = (8, width - 8)
    roof = HipRoof(
        "roof", 0, width, zf=front_z + overhang, zb=back_z - overhang, y0=28,
        fascia=Strip((35, 38), wrap=columns),
        slope=Strip((32, 35), repeat=(16, 32), wrap=columns),
        teeth=(10, 16), cap=(0, 10), pitch=22.0, run=6,
        ridge_u=(0, width), end_tile=Tile(8, 10, 18, 16))
    wall_top = 28
    post = Tile(2, 38, 8, 64, top=26)
    boards = Tile(9, 38, 15, 64, top=26)
    base = Prism(
        "ground_floor", 2, width - 2,
        [(front_z, 0), (front_z, wall_top), (back_z, wall_top), (back_z, 0)],
        edges={0: Proj(38, 64), 2: boards}, skip=(1, 3),
        caps=[Band(0, 26, boards, front_z, front=post, back=post, z1=back_z),
              Band(26, wall_top + 1, Tile(9, 38, 15, 39, top=wall_top), front_z)])
    return [base, roof]


def kit_house_exact(width):
    return [(2, 38, width - 2, 64), (8, 16, width - 8, 38)]

def gym():
    """The gym of Petalburg, Mauville, Mossdeep and Lavaridge (96 x 80).

        rows  1-40  a flat gravel roof, period 2 down and across
        rows 41-44  its edge: a light lip and the fascia
        rows 45-71  the facade either side of the porch
        rows 41-48  (x 40-71) the roof running on over the porch
        rows 49-53  the porch's own fascia, eight rows lower: it stands out 8
        rows 54-79  the porch: doors square on, and 8-px chamfers either side,
                    whose plinth runs diagonally from (40, 71) to (48, 79)

    Real depth: the four collision rows reach the top of the drawing, so the
    back wall stands at Z 2 and the gravel repeats back to it.
    """
    front, porch, back = 72, 80, 2
    wall_top, roof_top = 27, 31
    panel = Tile(72, 45, 88, 72, top=wall_top)      # a window bay, for sides
    corner = Tile(88, 45, 94, 72, top=wall_top)
    lip = Tile(8, 41, 16, 45, top=roof_top)
    body = Prism(
        "body", 2, 94,
        [(front, -1), (front, wall_top), (back, wall_top), (back, -1)],
        edges={0: Proj(45, 72), 2: panel}, skip=(1, 3),
        caps=[Band(-1, wall_top, panel, front, front=corner, back=corner, z1=back)])
    roof = Prism(
        "roof", 0, 96,
        [(front, wall_top), (front, roof_top), (back, roof_top), (back, wall_top)],
        edges={0: Proj(41, 45), 1: Strip((5, 41), repeat=(5, 7)), 2: lip},
        skip=(3,), caps=[Band(wall_top, roof_top + 1, lip, front)])
    porch_roof = Prism(
        "porch_roof", 40, 72,
        [(porch, wall_top), (porch, roof_top), (front, roof_top), (front, wall_top)],
        edges={0: Proj(49, 54)}, skip=(2, 3),
        caps=[Band(wall_top, roof_top + 1, lip, porch)])
    porch_walls = Walls("porch", [(40, front), (48, porch), (64, porch), (72, front)],
                        -1, wall_top)
    return [body, roof, porch_roof, porch_walls]


GYM_EXACT = [(2, 45, 40, 72), (72, 45, 94, 72), (3, 5, 93, 45), (40, 41, 72, 80)]

def flat_block(width, height, roof, cornice, facade_top, unit=None):
    """A flat-roofed block: parapeted roof, cornice, facade (any size).

    `roof` = (fixed, repeat, tail): the roof's rows as the drawing lays them
    from its front rim back, the course that repeats to the real depth, and
    the back parapet that closes it. `cornice` = (top, bottom) rows of the
    parapet's front; the facade runs from `facade_top` to the bottom. `unit`
    = (x0, x1, top, face, foot): a box on the roof whose top is drawn from
    row `top`, whose front from `face` to its foot on the roof at `foot`.

    Real depth: the collision starts one row below the drawing's top, so the
    back wall stands at Z 16 and the front at the drawing's foot.
    """
    front, back = height, 16
    wall = height - facade_top
    c0, c1 = cornice
    top = wall + (c1 - c0)
    fixed, repeat, tail = roof
    brick = Tile(8, facade_top, 16, facade_top + 16, top=wall)
    pilaster = Tile(0, facade_top, 8, facade_top + 16, top=wall)
    rim = Tile(8, c0, 16, c1, top=top)
    body = Prism(
        "body", 0, width,
        [(front, -1), (front, wall), (back, wall), (back, -1)],
        edges={0: Proj(facade_top, height), 2: brick}, skip=(1, 3),
        caps=[Band(-1, wall, brick, front, front=pilaster, back=pilaster, z1=back)])
    parts = [body]

    def slab(name, x0, x1, offset=0.0):
        return Prism(
            name, x0, x1,
            [(front, wall), (front, top), (back, top), (back, wall)],
            edges={0: Proj(c0, c1),
                   1: Strip(fixed, repeat=repeat, tail=tail,
                            repeat_offset=offset if offset else None), 2: rim},
            skip=(3,), caps=[Band(wall, top + 1, rim, front)],
            west=(x0 == 0), east=(x1 == width))

    if unit is None:
        parts.append(slab("roof", 0, width))
    else:
        ux0, ux1, t0, t1, t2 = unit
        # under and behind the unit the roof is laid from plain roof columns
        parts += [slab("roof_w", 0, ux0), slab("roof_u", ux0, ux1, offset=8 - ux0),
                  slab("roof_e", ux1, width)]
        zu = t2 + top
        side = Tile(ux0 + 8, t1, ux0 + 16, t2, top=top + (t2 - t1))
        parts.append(Prism(
            "unit", ux0, ux1,
            [(zu, top - 1), (zu, top + (t2 - t1)), (zu - (t1 - t0), top + (t2 - t1)),
             (zu - (t1 - t0), top - 1)],
            edges={0: Proj(t1, t2), 1: Proj(t0, t1), 2: side}, skip=(3,),
            caps=[Band(top - 1, top + (t2 - t1) + 1, side, zu)]))
    return parts


def stone_block(width, height, meta):
    """Rustboro's stone blocks (224..21f): the flat roof drawn rows 0-38, a
    course of 4 rows repeating, the back parapet rows 0-6; the parapet's front
    rows 39-47; the facade below, one row of windows per storey."""
    return flat_block(width, height, ((7, 39), (7, 11), (0, 7)), (39, 48), 48)


def olive_block(width, height, meta):
    """Rustboro's olive-roofed blocks (220..243): the roof drawn rows 0-39 in
    8-row courses, the cornice rows 40-47, the facade below. Most carry a
    ventilation unit on the right end of the roof (metatiles 222 223)."""
    unit = (width - 24, width - 4, 1, 16, 31) if meta.get("unit") else None
    return flat_block(width, height, ((8, 40), (8, 16), (0, 8)), (40, 48), 48, unit)


def flat_block_exact(width, height, first_roof_row):
    return [(0, first_roof_row, width, height)]

def flat_part(name, x0, x1, front, back, roof, cornice, facade_top,
              brick, rim, west=True, east=True):
    """One flat-roofed volume over [x0, x1): facade from `facade_top` to its
    foot at `front`, the parapet's front `cornice` above it, and the roof laid
    back to `back` by `roof` = (fixed, repeat, tail). `brick` and `rim` are
    the art rectangles its sides are dressed with."""
    wall = front - facade_top
    c0, c1 = cornice
    top = wall + (c1 - c0)
    fixed, repeat, tail = roof
    brick_t = Tile(*brick, top=wall)
    rim_t = Tile(*rim, top=top)
    body = Prism(
        name, x0, x1,
        [(front, 0), (front, wall), (back, wall), (back, 0)],
        edges={0: Proj(facade_top, front), 2: brick_t}, skip=(1, 3),
        caps=[Band(0, wall, brick_t, front)], west=west, east=east)
    slab = Prism(
        name + "_roof", x0, x1,
        [(front, wall), (front, top), (back, top), (back, wall)],
        edges={0: Proj(c0, c1), 1: Strip(fixed, repeat=repeat, tail=tail), 2: rim_t},
        skip=(3,), caps=[Band(wall, top + 1, rim_t, front)], west=west, east=east)
    return [body, slab]


def devon():
    """Devon Corporation (160 x 144): two wings and a tower between them.

        wings  x 0-47, 112-159: roof rows 2-31 (a 2-row lattice), parapet
               front rows 32-39, three storeys rows 40-127
        tower  x 48-111: roof rows 0-39 (8-row courses) with the company's
               emblem across its front rim, a double cornice rows 40-55,
               the facade and the arched doors rows 56-143 - it stands one
               row further south than the wings

    Real depth: the collision covers the drawing's top row, so the back wall
    stands at Z 0.
    """
    wing_roof = ((8, 32), (8, 10), (2, 8))
    parts = []
    parts += flat_part("wing_w", 0, 48, 128, 0, wing_roof, (32, 40), 40,
                       brick=(8, 40, 48, 72), rim=(8, 32, 48, 40), east=False)
    parts += flat_part("wing_e", 112, 160, 128, 0, wing_roof, (32, 40), 40,
                       brick=(112, 40, 152, 72), rim=(112, 32, 152, 40), west=False)
    parts += flat_part("tower", 48, 112, 144, 0, ((9, 40), (9, 17), (0, 9)), (40, 56), 56,
                       brick=(48, 56, 56, 88), rim=(56, 40, 64, 56))
    return parts


DEVON_EXACT = [(0, 8, 160, 128), (48, 128, 112, 144)]


def fountain():
    """Rustboro's fountain (48 x 48): an octagonal basin, its white rim and
    water drawn as one flat top 9 px up, the front wall and its chamfers
    below it, a bowl standing in the water and its jet.

    Drawn at its own depth, not stretched to the collision: a basin is as
    deep as it is wide, and the drawing says so.
    """
    h = 9
    front = 46
    top = [(9, front), (39, front), (47, front - 8), (47, 20), (37, 10), (11, 10), (1, 20),
           (1, front - 8)]
    walls = Walls("basin", [(1, front - 8), (9, front), (39, front), (47, front - 8)], -1, h)
    parts = [walls, Relief_top(top, h), Cylinder("bowl", 24, 31, 8, 7, h - 1, 14,
                                                 back_tile=Tile(16, 22, 32, 29)),
             Jet("jet", 21, 27, 31, 14, 26)]
    return parts


class Relief_top:
    """A flat polygon at height `h`, projected: the basin's rim and water."""

    def __init__(self, poly, h):
        self.poly, self.h = poly, h

    def emit(self, mesh):
        h = self.h
        mesh.poly([(x, h, z, x, z - h) for (x, z) in reversed(self.poly)], 1.0, "fountain.top")
        # the sides and back the drawing never shows: the front wall's stone
        n = len(self.poly)
        for i in range(2, n - 1):
            (xa, za), (xb, zb) = self.poly[i], self.poly[(i + 1) % n]
            quad = [(xa, -1, za), (xb, -1, zb), (xb, h, zb), (xa, h, za)]
            # projected: from the front it is the drawing, gaps included
            mesh.poly([q + (q[0], q[2] - q[1]) for q in quad], 0.7, "fountain.side~proj")


class Jet:
    """The fountain's jet: a standing plane through the bowl, projected."""

    def __init__(self, name, x0, x1, z, y0, y1):
        self.name, self.x0, self.x1, self.z, self.y0, self.y1 = name, x0, x1, z, y0, y1

    def emit(self, mesh):
        z = self.z
        quad = [(self.x0, self.y0, z), (self.x1, self.y0, z), (self.x1, self.y1, z),
                (self.x0, self.y1, z)]
        mesh.poly([(x, y, zz, x, zz - y) for (x, y, zz) in quad], 1.0, self.name + "~proj")


# ── Interiors ─────────────────────────────────────────────────────────────
#
# A room is cut into pieces, front first; see gen_voxel_buildings.interior_specs.
# Shapes are in pixels of the room's drawing; `height` is the rows of a piece's
# front, standing at its drawn foot; `base` lifts it onto another piece.

WALL_COLOURS = ("f6b473", "de7b31", "ffe6b4")   # the Center's orange wall


def piece(name, shape, height, base=0, fill=None, leave=(), side=None, solid=False,
          back=None, card=False, top=None, foot=None, walls=(), cells=(), against=None,
          claim=()):
    """`back`: the Z its top runs back to, where the cartridge's collision
    says it ends (or the wall it stands against); `against`: that wall's Z,
    where a top drawn deeper than the room in front of it stops, its back
    rows standing up the wall; `top`: the stretch of the room's
    drawing that depth is laid with, when its own drawn top is too thin to
    repeat; `card`: leaves, not a box; `foot`: the row a wall with a doorway
    stands on; `walls`: the side walls it carries, ((x, z), (x, z)[, height])
    - facing left of a->b - dressed with `side`."""
    return {"name": name, "shape": shape, "height": height, "base": base,
            "fill": fill, "leave": leave, "side": side, "solid": solid,
            "back": back, "card": card, "top": top, "foot": foot, "walls": walls,
            "cells": cells, "against": against, "claim": claim}


def own_shell(pieces):
    """Marks a room's shell - its walls, its doorways' recesses, a maze's
    blocks - as its own (`alone`): the generator stands a known piece
    wherever its cells are drawn again, and a side wall, which is drawn
    nowhere, was found wherever another room had the same floor down a
    column - walls standing loose in the middle of Rustboro's flats."""
    for pc in pieces:
        if pc["name"].split("_")[0] in ("wall", "side", "stairwell", "edge", "block",
                                         "partition", "divider"):
            pc["alone"] = True
    return pieces


# the Center's floor and its shadows, and the counter's cream and trim: what
# a stool, the table or a Poke Ball on the counter leaves round it
CENTER_FLOOR = ("cdc58b", "eedea4", "ffffc5")
CENTER_COUNTER = ("ffffc5", "e6e6b4", "de9c62", "ffe6b4", "de7b31")


def facet(name, a, b, height, walls=(), side=None, cells=()):
    """A chamfered corner: a wall along its foot, from a = (x, top, foot) to
    b in the drawing's pixels, carrying the side walls that run on from it
    (edge on to the GBA camera, drawn nowhere), `height` tall."""
    (xa, ta, fa), (xb, tb, fb) = a, b
    return {"name": name, "shape": [[(xa, ta), (xb, tb), (xb, fb), (xa, fa)]],
            "height": height, "facet": (a, b), "walls": walls, "side": side,
            "cells": cells}


# a stretch of the Center's back wall, full height and one panel wide, as the
# room looks with the furniture taken out: for the faces the drawing never shows
CENTER_WALL_SIDE = (64, 0, 80, 32)


def center_walls(front):
    """The Center's walls, 32 px: the back one, standing at row 32 - in two,
    west and east of x 128, so each half counts against the chunk it stands in
    - and its corners, chamfered at 45 degrees, from which the side walls run
    on to where the floor ends at `front`. The drawing has no side walls: they
    are edge on to its camera; from the console's they close the room."""
    return [
        facet("corner_w", (0, 15, 47), (16, -1, 31), 32, walls=[((0, front), (0, 47))],
              side=CENTER_WALL_SIDE, cells=[(0, front // 16 - 1)]),
        facet("corner_e", (208, -1, 31), (224, 15, 47), 32, walls=[((224, 47), (224, front))],
              side=CENTER_WALL_SIDE, cells=[(13, front // 16 - 1)]),
        piece("wall_e", [(128, 0, 208, 32)], 32, fill=16, side=CENTER_WALL_SIDE),
        piece("wall", [(16, 0, 128, 32)], 32, fill=16, side=CENTER_WALL_SIDE),
    ]


POKEMON_CENTER_1F = [
    # the Poke Balls on the counter's two arms
    piece("ball_w", [("ellipse", 72, 45.5, 7.6, 7.6)], 12, base=10, leave=CENTER_COUNTER),
    piece("ball_e", [("ellipse", 152, 45.5, 7.6, 7.6)], 12, base=10, leave=CENTER_COUNTER),
    # the counter: a bar across the front, an arm back to the wall each end
    piece("counter", [(64, 24, 80, 64), (144, 24, 161, 64), (64, 44, 161, 64)], 10, fill=1,
          back=48),
    # behind it, standing on the floor where the nurse stands
    piece("phone", [(126, 22, 146, 45)], 16, leave=WALL_COLOURS, back=32),
    piece("healer", [(80, 14, 112, 45)], 24, leave=WALL_COLOURS, back=32),
    piece("pc", [(158, 6, 178, 42)], 26, leave=WALL_COLOURS, back=32),
    piece("bookcase", [(32, 14, 65, 42)], 18, leave=WALL_COLOURS, back=32),
    piece("plant", [(14, 10, 34, 40)], 26, leave=WALL_COLOURS, card=True),
    piece("stool_1", [(16, 47, 32, 64)], 6, leave=CENTER_FLOOR),
    piece("stool_2", [(32, 47, 48, 64)], 6, leave=CENTER_FLOOR),
    piece("stool_3", [(160, 95, 176, 111)], 6, leave=CENTER_FLOOR),
    piece("stool_4", [(160, 111, 176, 127)], 6, leave=CENTER_FLOOR),
    piece("stool_5", [(176, 127, 192, 144)], 6, leave=CENTER_FLOOR),
    piece("stool_6", [(192, 127, 208, 144)], 6, leave=CENTER_FLOOR),
    piece("table", [(176, 95, 208, 128)], 8, leave=CENTER_FLOOR),
    piece("escalator", [(0, 78, 34, 123)], 8, leave=CENTER_FLOOR),
    # the walls: the back one, its chamfered corners, the two side walls
    # whose tops are the lines down the room's edges, the front corners
] + center_walls(144)



POKEMON_CENTER_2F = [
    # the three link terminals, in front of the counter, and the glass
    # tubes that stand behind them
    piece("terminal_1", [(48, 40, 64, 73)], 24, leave=WALL_COLOURS, back=48),
    piece("terminal_2", [(128, 54, 144, 81)], 20, back=64),
    piece("terminal_3", [(192, 54, 208, 81)], 20, back=64),
    piece("gate_1", [(80, 46, 96, 57)], 8),
    piece("gate_2", [(144, 46, 160, 57)], 8),
    piece("counter", [(0, 40, 48, 63), (64, 40, 80, 63), (96, 40, 128, 63),
                      (160, 40, 192, 63), (208, 40, 218, 63)], 9, fill=1, back=48),
    # glass tubes: as deep as they are wide, the rest of their drawing height
    piece("tube_1", [(48, 6, 64, 42)], 24, leave=WALL_COLOURS),
    piece("tube_2", [(128, 6, 144, 58)], 33, leave=WALL_COLOURS),
    piece("tube_3", [(192, 6, 208, 58)], 33, leave=WALL_COLOURS),
    piece("plant_1", [(80, 128, 96, 160)], 24, card=True),
    piece("plant_2", [(96, 128, 112, 160)], 24, card=True),
    piece("plant_3", [(144, 128, 160, 160)], 24, card=True),
    piece("plant_4", [(160, 128, 176, 160)], 24, card=True),
    # the five stools along the front are the ground floor's, tile for tile,
    # and stand here as they are (gen_voxel_buildings.reuse_pieces)
    piece("escalator", [(0, 86, 34, 122)], 8, leave=CENTER_FLOOR),
] + center_walls(160)


MART_WALL = ("ffffff", "8bd5de", "d5ded5", "b4b4a4", "83b4b4")
MART_WALL_SIDE = (32, 0, 48, 32)

MART = [
    # the till on the glass counter's arm; the counter, an L from the wall
    piece("till", [(32, 22, 48, 42)], 12, base=15, leave=MART_WALL),
    # a glass case, whose drawn top is two rows: its depth is laid with the
    # glass of the arm that runs back to the wall, which shows its top whole
    piece("counter", [(32, 24, 48, 80), (0, 58, 48, 80)], 15, fill=1, back=64,
          top=(34, 56, 46, 70)),
    # the shelves along the back wall, the plant, the three standing shelves
    piece("shelves_back", [(96, 14, 160, 42)], 20, leave=MART_WALL, back=32),
    piece("plant", [(160, 30, 172, 64)], 26, leave=MART_WALL, card=True),
    # the display stands: low, their front the band at their foot, their
    # three compartments of goods their top, as long as the three cells
    # their collision blocks
    piece("shelf_1", [(96, 62, 112, 111)], 14, back=64),
    piece("shelf_2", [(112, 62, 128, 111)], 14, back=64),
    piece("shelf_3", [(160, 54, 172, 111)], 14, back=64),
    # the walls as the Center's: 7-px corners, the side walls run on from them
    facet("corner_w", (0, 6, 40), (7, -1, 33), 34, walls=[((0, 128), (0, 40))],
          side=MART_WALL_SIDE, cells=[(0, 7)]),
    facet("corner_e", (169, -1, 33), (176, 6, 40), 34, walls=[((176, 40), (176, 128))],
          side=MART_WALL_SIDE, cells=[(10, 7)]),
    piece("wall", [(7, 0, 169, 32)], 32, fill=16, side=MART_WALL_SIDE),
]


def _replace(pieces, names, new):
    """A room like another but for some pieces: `new` stands where the first
    of `names` did, and the rest of them are gone."""
    out, done = [], False
    for pc in pieces:
        if pc["name"] in names:
            if not done:
                out += new
                done = True
            continue
        out.append(pc)
    return out


# Lavaridge's Center: a door to the hot spring in the back wall, a plant
# either side of it. Its counter, Poke Balls, machines, stools, table and
# escalator are the other Centers', tile for tile, and stand here as they are
# (gen_voxel_buildings.reuse_pieces), and so is the back wall east of the
# door; the plants and the rest of the walls are its own.
LAVARIDGE_CENTER_1F = [
    piece("plant_w", [(14, 10, 34, 40)], 26, leave=WALL_COLOURS),
    piece("plant_e", [(46, 10, 66, 40)], 26, leave=WALL_COLOURS),
] + [pc for pc in center_walls(144) if pc["name"] != "wall_e"]


# The front corners the GBA leaves black: the outside of a room whose front
# wall it never draws. In the round they are floor, not a hole in it.
CENTER_1F_OPEN = [[(0, 126), (18, 144), (0, 144)], [(224, 126), (206, 144), (224, 144)]]
CENTER_2F_OPEN = [[(0, 142), (18, 160), (0, 160)], [(224, 142), (206, 160), (224, 160)]]
MART_OPEN = [[(0, 118), (10, 128), (0, 128)], [(176, 118), (166, 128), (176, 128)]]

# ── Littleroot's two houses ───────────────────────────────────────────────
#
# Brendan's and May's, each a ground floor and a bedroom, the one house the
# other way round. The ground floor's back wall steps forward a cell where the
# stairs go up, and the stairs are a doorway in it: the flight is drawn inside
# the doorway as the GBA sees it from above, so it lies on the floor of a
# recess one cell deep, with the doorway's sides and back round it. Upstairs
# the same doorway leads down, drawn on the floor behind the wall's face.
#
# Furniture against the back wall stands on the wall's own row of collision,
# so what it is drawn with in front of the wall is all the depth it has; a
# desk drawn with more top than that stands its back rows up the wall.

HOUSE_WALL = ("d5d5b4", "b4b4a4", "ffffff")          # plaster, baseboard, trim
HOUSE_FLOOR = ("b4a44a", "947329", "ac8b39", "cdc55a")   # the planks
BLUE_RUG = ("6a94c5", "8bbdf6", "838394")               # and the chairs' shadows
PINK_RUG = ("d57bac", "838394")


def house_chair(name, back, seat, leave):
    """A kitchen chair seen from the side: its back, a narrow slab, and its
    seat, as deep as one another."""
    return [piece(name + "_back", [back], 9, leave=leave, solid=True),
            piece(name + "_seat", [seat], 5, leave=leave, solid=True)]


def stairwell(name, x0, x1, front, back, opening, side, crest):
    """The sides and back of a doorway's recess, x0..x1 from the wall's face
    at `front` back to `back`; `opening` is the doorway's height. `crest` is
    the row the wall's top is drawn on: nothing of the recess may rise above
    it at 45 degrees, so the sides step down where the doorway's height
    would."""
    low = min(opening, back - crest)
    mid = crest + opening
    if mid >= front or mid <= back:
        walls = [((x0, front), (x0, back), low), ((x1, back), (x1, front), low)]
    else:
        walls = [((x0, front), (x0, mid), opening), ((x0, mid), (x0, back), low),
                 ((x1, back), (x1, mid), low), ((x1, mid), (x1, front), opening)]
    walls.append(((x0, back), (x1, back), low))
    return piece(name, [], 32, side=side, walls=walls)


def brendan_1f():
    rug = BLUE_RUG
    return (house_chair("chair_nw", (33, 96, 37, 112), (37, 100, 47, 112), rug)
            + house_chair("chair_sw", (33, 112, 37, 128), (37, 116, 47, 128), rug)
            + house_chair("chair_ne", (91, 96, 95, 112), (81, 100, 91, 112), rug)
            + house_chair("chair_se", (91, 112, 95, 128), (81, 116, 91, 128), rug) + [
        piece("table", [(50, 96, 78, 128)], 8, leave=rug, solid=True),
        # the television on its stand, a set as deep as the cell it blocks,
        # and the low white cupboard beside it
        # its sides the casing: the dark grey the set is framed with, on its stand
        piece("tv", [(64, 61, 80, 88)], 20, back=72, top=(68, 62, 78, 63),
              side=(64, 68, 66, 88)),
        piece("cabinet", [(32, 65, 63, 88)], 9, back=72),
        # the kitchen along the back wall: fridge, sink, china cabinet
        piece("fridge", [(0, 18, 16, 48)], 24, leave=HOUSE_FLOOR, back=32),
        piece("tap", [(22, 24, 29, 29)], 5, base=8, leave=HOUSE_WALL[:2]),
        piece("sink", [(16, 29, 48, 41), (17, 41, 47, 48)], 8, back=32, top=(18, 38, 46, 39)),
        piece("dresser", [(49, 19, 72, 48)], 23, back=32),
        stairwell("stairwell", 128, 144, 48, 29, 19, (128, 29, 144, 34), 16),
        # the back wall, a cell forward east of the step; the doorway is cut
        # out of it and it stands on its foot
        piece("wall_e", [(112, 32, 113, 48), (113, 16, 176, 29), (113, 29, 128, 48),
                         (144, 29, 176, 48)], 32, fill=16, foot=48,
              side=(156, 16, 172, 48), walls=[((112, 32), (112, 48))]),
        piece("wall", [(0, 0, 113, 32), (113, 0, 117, 16)], 32, fill=16, foot=32,
              side=(72, 0, 80, 32)),
        piece("side_w", [], 32, side=(72, 0, 80, 32), walls=[((0, 144), (0, 32))]),
        piece("side_e", [], 32, side=(156, 16, 172, 48), walls=[((176, 48), (176, 144))]),
    ])


def may_1f():
    rug = PINK_RUG
    # the chairs' seats are Brendan's, pixel for pixel on another rug, and
    # stand here as they are; their backs are drawn over the rug
    return [pc for pc in (house_chair("chair_nw", (81, 96, 85, 112), (85, 100, 95, 112), rug)
                          + house_chair("chair_sw", (81, 112, 85, 128), (85, 116, 95, 128), rug)
                          + house_chair("chair_ne", (139, 96, 143, 112), (129, 100, 139, 112), rug)
                          + house_chair("chair_se", (139, 112, 143, 128), (129, 116, 139, 128), rug))
            if pc["name"].endswith("_back")] + [
        piece("table", [(98, 96, 126, 128)], 8, leave=rug, solid=True),
        # the television and its cabinet, the fridge, the tap and the sink
        # are Brendan's, pixel for pixel, and stand here as they are
        # (gen_voxel_buildings.reuse_pieces)
        piece("dresser", [(105, 19, 128, 48)], 23, back=32),
        stairwell("stairwell", 32, 48, 48, 29, 19, (32, 29, 48, 34), 16),
        piece("wall_w", [(63, 32, 64, 48), (0, 16, 63, 29), (0, 29, 32, 48),
                         (48, 29, 63, 48)], 32, fill=16, foot=48,
              side=(4, 16, 20, 48), walls=[((64, 48), (64, 32))]),
        piece("wall", [(63, 0, 176, 32), (59, 0, 63, 16)], 32, fill=16, foot=32,
              side=(96, 0, 104, 32)),
        piece("side_w", [], 32, side=(4, 16, 20, 48), walls=[((0, 144), (0, 48))]),
        piece("side_e", [], 32, side=(96, 0, 104, 32), walls=[((176, 32), (176, 144))]),
    ]


def brendan_2f():
    return [
        # the desk: the computer and the stereo on it, the chair before it
        piece("chair_back", [(1, 33, 5, 48)], 9, leave=HOUSE_FLOOR, solid=True),
        piece("chair_seat", [(5, 36, 14, 48)], 5, leave=HOUSE_FLOOR, solid=True),
        piece("pc", [(1, 9, 16, 30)], 16, base=9, leave=HOUSE_WALL, back=32),
        piece("stereo", [(18, 24, 30, 31)], 6, base=9, solid=True),
        piece("desk", [(0, 20, 32, 40)], 9, leave=HOUSE_FLOOR, against=32),
        # the games console and its pad, the television
        piece("console", [(51, 24, 64, 40)], 9, leave=HOUSE_WALL + HOUSE_FLOOR, back=32),
        piece("tv", [(64, 13, 80, 40)], 20, back=32, top=(68, 14, 78, 15),
              side=(64, 20, 66, 40)),
        # the bed, its head a board at the mattress's back
        piece("bed_head", [(12, 62, 36, 70)], 7, base=7, leave=HOUSE_FLOOR),
        piece("bed", [(12, 70, 36, 93)], 7, leave=HOUSE_FLOOR, solid=True, claim=[(12, 70, 36, 72)]),
        stairwell("stairwell", 112, 128, 32, 13, 19, (113, 14, 127, 26), 0),
        piece("wall", [(0, 0, 112, 32), (112, 0, 128, 13), (128, 0, 144, 32)], 32, fill=16,
              foot=32, side=(96, 0, 104, 32)),
        piece("side_w", [], 32, side=(96, 0, 104, 32), walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 32, side=(96, 0, 104, 32), walls=[((144, 32), (144, 128))]),
    ]


def may_2f():
    return [
        piece("chair_back", [(139, 33, 143, 48)], 9, leave=HOUSE_FLOOR, solid=True),
        piece("chair_seat", [(130, 36, 139, 48)], 5, leave=HOUSE_FLOOR, solid=True),
        piece("pc", [(128, 9, 143, 30)], 16, base=9, leave=HOUSE_WALL, back=32),
        piece("stereo", [(114, 24, 126, 31)], 6, base=9, solid=True),
        piece("desk", [(112, 20, 144, 40)], 9, leave=HOUSE_FLOOR, against=32),
        piece("console", [(83, 24, 98, 40)], 9, leave=HOUSE_WALL + HOUSE_FLOOR, back=32),
        # the television is Brendan's, tile for tile
        piece("bed_head", [(108, 62, 132, 70)], 7, base=7, leave=HOUSE_FLOOR),
        piece("bed", [(108, 70, 132, 93)], 7, leave=HOUSE_FLOOR, solid=True,
              claim=[(108, 70, 132, 72)]),
        stairwell("stairwell", 16, 32, 32, 13, 19, (17, 14, 31, 26), 0),
        piece("wall", [(0, 0, 16, 32), (16, 0, 32, 13), (32, 0, 144, 32)], 32, fill=16,
              foot=32, side=(40, 0, 48, 32)),
        piece("side_w", [], 32, side=(40, 0, 48, 32), walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 32, side=(40, 0, 48, 32), walls=[((144, 32), (144, 128))]),
    ]


# ── The two houses most of Hoenn lives in ─────────────────────────────────
#
# LAYOUT_HOUSE1 and LAYOUT_HOUSE2 (Oldale's two houses, and nineteen more
# maps'): a glass case and a chest of drawers or a kitchen along the back
# wall, a table and its chairs in the room, potted plants. Built like
# Littleroot's: furniture against the back wall stands on the wall's row of
# collision; what the second house repeats of the first tile for tile (the
# glass case, the plant in the corner) is the first's model
# (gen_voxel_buildings.reuse_pieces).

GENERIC_WALL = ("d5d5b4", "b4b4a4", "ffffff", "629c8b")   # plaster, trim, baseboard
GENERIC_FLOOR = ("ded552", "bdb431", "8b8b8b", "9c9410")  # the planks, their shade
GENERIC_RUG = ("ffcd8b", "f6f6a4")
GENERIC_TABLE = ("fff683", "bdac52")


def potted_plant(x, y):
    """The potted plant's outline, its crown's top-left at (x, y): the
    lines between the planks are drawn in the pot's own outline colour, so
    the shape follows the crown, the stem and the pot instead of a box."""
    return [(x, y, x + 16, y + 13), (x + 3, y + 13, x + 13, y + 14),
            (x + 5, y + 14, x + 11, y + 16), (x + 6, y + 16, x + 10, y + 19),
            (x + 3, y + 19, x + 13, y + 23), (x + 2, y + 23, x + 14, y + 30),
            (x + 4, y + 30, x + 5, y + 31), (x + 11, y + 30, x + 12, y + 31)]


def house1():
    rug = GENERIC_RUG
    return (house_chair("chair_n", (74, 64, 78, 80), (65, 68, 74, 80), rug)
            + house_chair("chair_s", (74, 80, 78, 96), (65, 84, 74, 96), rug) + [
        # the teapot on the table
        piece("teapot", [(49, 72, 59, 80)], 5, base=11, leave=GENERIC_TABLE),
        # what the teapot hides of the table top is the table's own pixels
        piece("table", [(34, 64, 62, 97)], 11, leave=rug, solid=True, fill=1, foot=97),
        piece("plant_se", potted_plant(144, 113), 31, leave=GENERIC_FLOOR, card=True),
        piece("plant_1", potted_plant(128, 17), 31, leave=GENERIC_WALL + GENERIC_FLOOR, card=True),
        piece("plant_2", potted_plant(144, 17), 31, leave=GENERIC_WALL + GENERIC_FLOOR, card=True),
        # the glass case and the chest of drawers against the back wall
        piece("case", [(0, 12, 32, 40)], 20, leave=GENERIC_WALL + GENERIC_FLOOR, back=32),
        piece("drawers", [(33, 11, 57, 41)], 21, leave=GENERIC_WALL + GENERIC_FLOOR, back=32),
        piece("wall", [(0, 0, 160, 32)], 32, fill=16, foot=32, side=(64, 0, 80, 32)),
        piece("side_w", [], 32, side=(64, 0, 80, 32), walls=[((0, 144), (0, 32))]),
        piece("side_e", [], 32, side=(64, 0, 80, 32), walls=[((160, 32), (160, 144))]),
    ])


def house2():
    fl = GENERIC_FLOOR
    # the east chairs' seats and the plant are the first house's, pixel for
    # pixel on another floor, and stand here as they are
    return (house_chair("chair_nw", (66, 64, 70, 80), (70, 68, 79, 80), fl)
            + house_chair("chair_sw", (66, 80, 70, 96), (70, 84, 79, 96), fl)
            + [piece("chair_ne_back", [(122, 64, 126, 80)], 9, leave=fl, solid=True),
               piece("chair_se_back", [(122, 80, 126, 96)], 9, leave=fl, solid=True)] + [
        piece("table", [(82, 64, 110, 96)], 10, leave=fl, solid=True),
        # the television on its glass-doored stand: Littleroot's set, whose
        # tile this is, on a stand a row shorter
        piece("tv", [(32, 13, 48, 39)], 19, leave=GENERIC_WALL + fl, back=32,
              top=(36, 14, 46, 15), side=(32, 20, 34, 39)),
        # the kitchen: a cupboard, the sink and hob, the fridge
        piece("tap", [(133, 16, 142, 25)], 8, base=8, leave=GENERIC_WALL[:2]),
        piece("cupboard", [(112, 17, 128, 40)], 16, leave=GENERIC_WALL + fl, back=32),
        piece("sink", [(128, 21, 160, 40)], 8, leave=GENERIC_WALL + fl, against=32),
        piece("fridge", [(160, 10, 176, 40)], 24, leave=GENERIC_WALL + fl, back=32),
        piece("wall", [(0, 0, 176, 32)], 32, fill=16, foot=32, side=(48, 0, 64, 32)),
        piece("side_w", [], 32, side=(48, 0, 64, 32), walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 32, side=(48, 0, 64, 32), walls=[((176, 32), (176, 128))]),
    ])


def house_with_bed():
    """LAYOUT_HOUSE_WITH_BED (Petalburg's second house, and two more maps'):
    the two houses' room with a bed in it, two chests and a bookcase along
    the back wall, a table and two chairs."""
    fl = GENERIC_FLOOR
    # the chairs and the chest of drawers are the second house's, pixel for
    # pixel, and stand here as they are (gen_voxel_buildings.reuse_pieces)
    return [
        piece("table", [(128, 64, 158, 96)], 10, leave=fl, solid=True),
        # the bed, its head a board at the mattress's back
        piece("bed_head", [(4, 48, 28, 56)], 7, base=7, leave=fl),
        piece("bed", [(4, 56, 28, 80)], 7, leave=fl, solid=True),
        # along the back wall: the green chest, the bookcase
        piece("chest", [(16, 15, 32, 40)], 17, leave=GENERIC_WALL + fl, back=32),
        piece("bookcase", [(128, 10, 160, 40)], 22, leave=GENERIC_WALL + fl, back=32),
        piece("wall", [(0, 0, 160, 32)], 32, fill=16, foot=32, side=(64, 0, 80, 32)),
        piece("side_w", [], 32, side=(64, 0, 80, 32), walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 32, side=(64, 0, 80, 32), walls=[((160, 32), (160, 128))]),
    ]


def petalburg_gym():
    """Petalburg's gym: nine rooms one under another in the black between
    them, each nine cells wide and eight deep under a back wall two cells
    tall, its doors and its plate drawn on the wall's face. Back walls only:
    the drawing has no side walls, and a pair made for every room (as the
    houses have, to close them from the console's camera) puts the layout's
    page past the console's 512x512 - four rooms' fit, and four closed rooms
    of nine would read as a fault."""
    side = (16, 208, 48, 240)       # a stretch of the second room's panelling
    return [piece("wall_%d" % room, [(0, y, 144, y + 32)], 32, fill=16, foot=y + 32, side=side)
            for room, y in ((room, room * 13 * 16) for room in range(9))]


# ── The other houses of the general indoor tileset ────────────────────────
#
# Rooms no one has modelled piece by piece yet: a back wall two cells tall,
# blocked all across, over a rectangle of floor. Their walls stand - the back
# one, and where the floor runs to the room's edges the two sides the drawing
# has no pixel of - and their furniture stays drawn on the floor until the
# room gets pieces of its own. Each: its layout, its size in cells, its floor,
# the wall's cell the sides are dressed with (bare panelling: the pair of
# metatiles the wall repeats most), and whether it has side walls.

PLAIN_ROOMS = [
    ("LAYOUT_HOUSE3", 10, 8, 0x229, 1, True),
    ("LAYOUT_DEWFORD_TOWN_HALL", 17, 9, 0x223, 3, True),
    ("LAYOUT_HOUSE4", 10, 9, 0x229, 3, True),
    ("LAYOUT_LILYCOVE_CITY_HOUSE2", 8, 8, 0x229, 3, True),
    ("LAYOUT_VERDANTURF_TOWN_WANDAS_HOUSE", 17, 8, 0x223, 5, True),
    ("LAYOUT_PACIFIDLOG_TOWN_HOUSE1", 10, 9, 0x3A3, 4, True),
    ("LAYOUT_PACIFIDLOG_TOWN_HOUSE2", 10, 9, 0x3A3, 5, True),
    ("LAYOUT_RUSTBORO_CITY_HOUSE", 12, 9, 0x32C, 3, True),
    ("LAYOUT_RUSTBORO_CITY_HOUSE1", 13, 8, 0x32C, 4, True),
    ("LAYOUT_RUSTBORO_CITY_CUTTERS_HOUSE", 11, 9, 0x32C, 4, True),
    ("LAYOUT_FORTREE_CITY_HOUSE1", 8, 6, 0x3D9, 2, True),
    ("LAYOUT_FORTREE_CITY_HOUSE2", 8, 6, 0x3D9, 1, True),
    ("LAYOUT_ROUTE116_TUNNELERS_REST_HOUSE", 10, 9, 0x229, 3, True),
    ("LAYOUT_ROUTE110_TRICK_HOUSE_ENTRANCE", 12, 8, 0x229, 1, True),
    ("LAYOUT_FORTREE_CITY_DECORATION_SHOP", 8, 6, 0x3D9, 1, False),
    ("LAYOUT_SOOTOPOLIS_CITY_LOTAD_AND_SEEDOT_HOUSE", 8, 7, 0x3FE, 2, True),
    ("LAYOUT_SOOTOPOLIS_CITY_HOUSE1", 8, 7, 0x3FE, 1, True),
    ("LAYOUT_SOOTOPOLIS_CITY_HOUSE2", 8, 7, 0x3FE, 4, True),
    ("LAYOUT_SOOTOPOLIS_CITY_HOUSE3", 8, 7, 0x3FE, 3, True),
    ("LAYOUT_MOSSDEEP_CITY_STEVENS_HOUSE", 11, 8, 0x223, 3, True),
]


def plain_room(width, height, plain_x, sides):
    w, h = width * 16, height * 16
    side = (plain_x * 16, 0, plain_x * 16 + 16, 32)
    pieces = [piece("wall", [(0, 0, w, 32)], 32, fill=16, foot=32, side=side)]
    if sides:
        pieces += [piece("side_w", [], 32, side=side, walls=[((0, h), (0, 32))]),
                   piece("side_e", [], 32, side=side, walls=[((w, 32), (w, h))])]
    return pieces


# ── Mr. Briney's house ────────────────────────────────────────────────────
#
# A room of tatami by the dock on Route 104: a chest of drawers, a small one
# and a glass case along the back wall, three jars in the corner, a low table.

BRINEY_FLOOR = ("c5ffac", "a4cd6a", "83ac4a", "7b7b83", "6a8b31", "414a6a")
BRINEY_WALL = ("d5b483", "ffffff", "d5c54a", "947329", "ac8b39", "629c8b")


def briney_room():
    fl = BRINEY_FLOOR
    side = (48, 0, 64, 32)
    return [
        piece("table", [(114, 67, 139, 94)], 10, leave=fl, solid=True),
        piece("jar_s", [(1, 65, 14, 80)], 9, leave=fl, solid=True),
        piece("jar_w", [(1, 49, 14, 64)], 9, leave=fl, solid=True),
        piece("jar_e", [(17, 49, 30, 64)], 9, leave=fl, solid=True),
        piece("drawers", [(17, 10, 45, 45)], 22, leave=BRINEY_WALL + fl, back=32),
        piece("chest", [(128, 16, 144, 45)], 16, leave=BRINEY_WALL + fl, back=32),
        piece("case", [(144, 12, 177, 45)], 20, leave=BRINEY_WALL + fl, back=32),
        piece("wall", [(0, 0, 192, 32)], 32, fill=16, foot=32, side=side),
        piece("side_w", [], 32, side=side, walls=[((0, 144), (0, 32))]),
        piece("side_e", [], 32, side=side, walls=[((192, 32), (192, 144))]),
    ]


# ── Rooms with a flight of stairs in the back wall ────────────────────────
#
# Rustboro's flats (and whatever else is built like them): the plain room's
# walls, with a doorway a cell wide in the back wall for every flight. Like
# Littleroot's: the flight is drawn inside the doorway as the GBA sees it from
# above, so it lies on the floor of a recess one cell deep, with the doorway's
# sides and back round it - a way out of the room, not a picture of stairs
# flat on the floor. Each: its layout, its size in cells, its floor, the
# wall's cell the sides are dressed with, and the doorways' columns - and, in
# Rustboro's second block of flats, where the wall that parts the room in two
# runs down from the back wall (its left edge), with the low wall the same
# three floors have along the front of the east half.

FLAT_FLOOR = ("d5d5b4", "f6f6a4", "b4b4a4", "8b8b8b", "ded552")

STAIR_ROOMS = [
    ("LAYOUT_RUSTBORO_CITY_FLAT1_1F", 14, 8, 0x32C, 5, (2,)),
    ("LAYOUT_RUSTBORO_CITY_FLAT1_2F", 14, 8, 0x32C, 5, (2,)),
    ("LAYOUT_RUSTBORO_CITY_FLAT2_1F", 14, 9, 0x32C, 1, (3,), 80),
    ("LAYOUT_RUSTBORO_CITY_FLAT2_2F", 14, 9, 0x32C, 9, (1, 3), 80),
    ("LAYOUT_RUSTBORO_CITY_FLAT2_3F", 14, 9, 0x32C, 3, (1,), 64),
    ("LAYOUT_LILYCOVE_CITY_COVE_LILY_MOTEL_1F", 12, 9, 0x229, 4, (2,)),
    ("LAYOUT_LILYCOVE_CITY_COVE_LILY_MOTEL_2F", 12, 9, 0x229, 4, (2,)),
    ("LAYOUT_ROUTE114_FOSSIL_MANIACS_HOUSE", 10, 8, 0x229, 2, (4,)),
    ("LAYOUT_ROUTE110_TRICK_HOUSE_END", 12, 8, 0x229, 6, (2, 10)),
    # the Devon Corporation's upper floors
    ("LAYOUT_RUSTBORO_CITY_DEVON_CORP_2F", 19, 9, 0x380, 4, (2, 14)),
    ("LAYOUT_RUSTBORO_CITY_DEVON_CORP_3F", 19, 9, 0x380, 4, (2,)),
]


def stair_room(width, height, plain_x, doors, partition=None):
    w, h = width * 16, height * 16
    side = (plain_x * 16, 0, plain_x * 16 + 16, 32)
    wall, pieces, x = [], [], 0
    if partition is not None:
        # seen from above: the white of their tops, and a face at the end
        pieces += [
            piece("divider", [(128, 90, 224, 127)], 27, solid=True, leave=FLAT_FLOOR),
            piece("partition", [(partition, 0, partition + 16, 127)], 29, solid=True),
        ]
    for door in sorted(doors):
        d = door * 16
        wall += [(x, 0, d, 32), (d, 0, d + 16, 13)]
        # the doorway: 19 pixels tall under the 13 of wall over it, its
        # sides the dark of the flight's own well
        pieces.append(stairwell("stairwell_%d" % door, d, d + 16, 32, 13, 19,
                                (d + 1, 14, d + 15, 26), 0))
        x = d + 16
    wall.append((x, 0, w, 32))
    return pieces + [
        piece("wall", wall, 32, fill=16, foot=32, side=side),
        piece("side_w", [], 32, side=side, walls=[((0, h), (0, 32))]),
        piece("side_e", [], 32, side=side, walls=[((w, 32), (w, h))]),
    ]


# ── Rustboro's Pokemon school ─────────────────────────────────────────────
#
# One classroom. Its back wall is drawn from 8 rows down the first cell (the
# black over it is nothing) to its foot 36 rows down, the blackboard and the
# windows on its face; the pillars' feet and the teacher's step, drawn a few
# rows further, stay on the floor.

def school_room():
    side = (176, 8, 192, 36)
    return [
        piece("wall", [(0, 8, 192, 36)], 28, fill=16, foot=36, side=side),
        piece("side_w", [], 28, side=side, walls=[((0, 176), (0, 36))]),
        piece("side_e", [], 28, side=side, walls=[((192, 36), (192, 176))]),
    ]


# ── The Pretty Petal flower shop ──────────────────────────────────────────
#
# Route 104's shop: planters of flowers and racks of potted plants about the
# floor, a table of pots and a shelf along the back wall. The counter, the
# stools and the vases stay drawn on the floor.

SHOP_FLOOR = ("eef6ff", "d5d5e6", "b4acb4")
SHOP_WALL = ("bdbd94", "e6e68b", "ffffbd", "629c62", "9cd59c", "4a7b41")


def flower_shop_room():
    fl = SHOP_FLOOR
    side = (48, 0, 64, 32)

    def planter(name, x, y0, y1):
        return piece(name, [(x, y0, x + 16, y1)], 10, leave=fl, solid=True)

    def rack(name, x):
        # from under the top plant's leaf tips, which close round two pixels
        # of floor each side, down to its feet
        return piece(name, [(x, 87, x + 16, 128)], 20, leave=fl, solid=True)

    return [
        planter("planter_sw", 0, 112, 144),
        planter("planter_w", 80, 96, 128), planter("planter_c", 128, 96, 128),
        planter("planter_e1", 192, 80, 128), planter("planter_e2", 208, 80, 128),
        rack("rack_1", 96), rack("rack_2", 112), rack("rack_e", 224),
        planter("planter_n1", 176, 48, 64), planter("planter_n2", 192, 48, 64),
        piece("shelf", [(112, 24, 152, 56)], 18, leave=SHOP_WALL + fl, back=42),
        piece("pots", [(162, 18, 209, 48)], 14, leave=SHOP_WALL + fl, back=32),
        piece("wall", [(0, 0, 240, 32)], 32, fill=16, foot=32, side=side),
        piece("side_w", [], 32, side=side, walls=[((0, 144), (0, 32))]),
        piece("side_e", [], 32, side=side, walls=[((240, 32), (240, 144))]),
    ]


# ── Dewford's gym ─────────────────────────────────────────────────────────
#
# A maze cut in rock two cells tall: every blocked cell is rock, its top or
# the two rows of its face, so each column's run of blocked cells is a block
# drawn from the run's first row to its foot. Runs side by side with the same
# rows are one rectangle (cells: x0, y0, x1, y1), front first. The leader's
# alcove and the statues, blocked too, are not rock and stay flat.

DEWFORD_GYM_BLOCKS = [
    (0, 0, 1, 28), (1, 18, 2, 28), (2, 23, 3, 28), (3, 24, 4, 28), (8, 23, 12, 28),
    (13, 23, 16, 28), (16, 19, 17, 28), (17, 0, 18, 28), (3, 17, 6, 22), (6, 15, 8, 22),
    (9, 15, 10, 21), (10, 16, 11, 21), (12, 16, 13, 19), (13, 9, 14, 19), (8, 15, 9, 18),
    (14, 13, 16, 18), (3, 11, 5, 16), (1, 12, 2, 15), (5, 7, 6, 14), (6, 6, 7, 14),
    (10, 9, 12, 14), (7, 9, 9, 13), (14, 7, 16, 12), (1, 6, 2, 10), (3, 7, 5, 10),
    (16, 0, 17, 9), (13, 0, 15, 6), (7, 0, 10, 5), (12, 0, 13, 5), (11, 0, 12, 4),
    (2, 0, 3, 3), (5, 0, 6, 3), (10, 0, 11, 3), (15, 0, 16, 3),
]
DEWFORD_GYM_FLOOR = [0x201, 0x202, 0x203, 0x205, 0x206, 0x209, 0x20A, 0x20B, 0x20D, 0x211,
                     0x212, 0x213, 0x215, 0x216, 0x218, 0x21A, 0x21B, 0x222, 0x223]


def dewford_gym():
    side = (128, 416, 144, 448)     # a stretch of the front wall's face
    # The rock the room is cut in, down its west and east edges, is a wall's
    # face each side and not a block: a block's art is its drawing three times
    # over (its hidden top and sides), and those two columns' would put the
    # layout's page past the console's 512x512.
    return [piece("block_%d" % i, [(x0 * 16, y0 * 16, x1 * 16, y1 * 16)], 32)
            for i, (x0, y0, x1, y1) in enumerate(DEWFORD_GYM_BLOCKS)
            if x0 not in (0, 17)] + [
        piece("edge_w", [], 32, side=side, walls=[((16, 448), (16, 32))]),
        piece("edge_e", [], 32, side=side, walls=[((272, 32), (272, 448))]),
    ]


# ── Professor Birch's lab ─────────────────────────────────────────────────
#
# Desks and a bookcase along the back wall, standing on its row of collision
# (so no deeper than the drawing puts them in front of it); in the room, the
# boxes, book stacks, bookcases and desks run back as far as the cells they
# block, and the machine is read column by column off its round drawing.

LAB_SHADOW = ("b4b4a4", "949494")      # the floor's shade, and its grid in it


def lab():
    sh = LAB_SHADOW
    return [
        piece("plant_s", [(48, 189, 64, 208)], 18, card=True),
        # the desk down the west side: its computer at the far end, the book
        # stacks against the wall, the chair
        # seen from behind: its back stands at the back of its seat
        piece("chair_sw_back", [(34, 160, 47, 167)], 7, base=4, leave=sh, solid=True),
        piece("chair_sw_seat", [(34, 167, 47, 176)], 4, leave=sh, solid=True),
        piece("books_sw1", [(1, 160, 16, 176)], 9, leave=sh, back=160),
        piece("books_sw2", [(1, 176, 16, 192)], 9, leave=sh, back=176),
        piece("pc_sw", [(16, 145, 32, 160)], 8, base=8, solid=True),
        piece("desk_sw", [(16, 160, 32, 192)], 8, leave=sh, back=160),
        # and its twin down the east side
        piece("plant_se", [(192, 141, 208, 161)], 18, card=True),
        piece("boxes_se", [(192, 164, 208, 192)], 20, leave=sh, back=176),
        piece("chair_se_back", [(161, 160, 165, 176)], 9, leave=sh, solid=True),
        piece("chair_se_seat", [(165, 164, 176, 176)], 5, leave=sh, solid=True),
        piece("pc_se", [(176, 145, 192, 160)], 8, base=8, solid=True),
        piece("desk_se", [(176, 160, 192, 192)], 8, leave=sh, back=160),
        # the machine, its dome, the two canisters beside it
        piece("dome", [("ellipse", 176, 103.5, 8.5, 7.5)], 5, base=10),
        piece("machine", [(160, 95, 192, 128)], 10, side=(168, 110, 184, 120)),
        piece("canisters", [(192, 109, 208, 128)], 16, leave=sh, back=112),
        # the two bookcases in the room, a box on each
        piece("box_w1", [(8, 82, 23, 96)], 6, base=21, solid=True),
        piece("box_w2", [(40, 82, 55, 96)], 6, base=21, solid=True),
        piece("bookcase_w1", [(0, 85, 32, 128)], 21, back=96),
        piece("bookcase_w2", [(32, 85, 64, 128)], 21, back=96),
        # boxes and book stacks in the north-east corner
        piece("boxes_ne1", [(144, 56, 160, 80)], 8, leave=sh),
        piece("boxes_ne2", [(160, 52, 176, 80)], 18, leave=sh, back=64),
        piece("books_ne1", [(177, 32, 193, 48)], 8, leave=sh, back=32),
        piece("books_ne2", [(193, 32, 208, 48)], 8, leave=sh, back=32),
        piece("books_ne3", [(193, 48, 208, 64)], 8, leave=sh, back=48),
        piece("chair_n_back", [(75, 48, 79, 64)], 9, leave=sh, solid=True),
        piece("chair_n_seat", [(64, 52, 75, 64)], 5, leave=sh, solid=True),
        piece("boxes_w", [(0, 41, 16, 64)], 7, leave=sh),
        # along the back wall
        piece("plant_nw", [(32, 29, 48, 48)], 18, card=True),
        piece("computer", [(49, 10, 72, 30)], 17, base=8, back=32),
        piece("desk_pc", [(48, 20, 80, 39)], 8, leave=sh, against=32),
        piece("book_red", [(117, 19, 128, 31)], 11, base=8, back=32),
        piece("book_open", [(134, 19, 147, 30)], 6, base=8, solid=True),
        piece("binder", [(147, 17, 158, 31)], 13, base=8, back=32),
        piece("desk_a", [(96, 20, 128, 39)], 8, leave=sh, against=32),
        piece("desk_b", [(128, 20, 160, 39)], 8, leave=sh, against=32),
        piece("bookcase_nw", [(0, 8, 32, 40)], 21, against=32),
        piece("wall", [(0, 0, 208, 32)], 32, fill=16, foot=32, side=(80, 0, 96, 32)),
        piece("side_w", [], 32, side=(80, 0, 96, 32), walls=[((0, 208), (0, 32))]),
        piece("side_e", [], 32, side=(80, 0, 96, 32), walls=[((208, 32), (208, 208))]),
    ]


# ── Rustboro's gym ────────────────────────────────────────────────────────
#
# A maze of low stone walls on a tiled floor: each block stands on the cells
# it blocks, its top drawn from 8 rows into the cell north of them and its
# front the last 10 rows of its own. The floor's shadows are floor. At the
# back, the gym's crenellated wall, one cell deeper at either end.

GYM_WALL_SIDE = (32, 7, 48, 32)
GYM_FLOOR = ("bdbdac",)             # the floor's shade round a statue's foot     # a stretch of the back wall without the emblem


def gym_block(name, rects):
    """A maze block: `rects` in cells (x0, y0, x1, y1), each drawn from 8 rows
    north of its cells to its foot."""
    return piece(name, [(x0 * 16, y0 * 16 - 8, x1 * 16, y1 * 16) for (x0, y0, x1, y1) in rects],
                 10, side=(56, 118, 72, 128))


def gym_statue(x):
    """A gym statue's outline, its cell's left edge at x: its pedestal's
    bottom corners are rounded off, the floor showing there."""
    return [(x, 272, x + 16, 302), (x + 1, 302, x + 15, 303), (x + 2, 303, x + 14, 304)]


def rustboro_gym():
    return [
        # the statues by the door: a ball on a pedestal. Every gym has them,
        # on its own floor: they stand in each as they are (reuse_pieces),
        # so they must not take the floor showing at their feet
        piece("statue_w_ball", [("ellipse", 40, 279, 7, 7)], 8, base=16),
        piece("statue_w", gym_statue(32), 16, back=288),
        piece("statue_e_ball", [("ellipse", 136, 279, 7, 7)], 8, base=16),
        piece("statue_e", gym_statue(128), 16, back=288),
        # the maze, front first
        gym_block("block_sw", [(2, 13, 4, 15), (2, 15, 5, 16)]),
        gym_block("block_s", [(6, 15, 8, 16)]),
        gym_block("block_w", [(4, 9, 5, 11), (2, 11, 5, 12)]),
        gym_block("block_c", [(6, 9, 8, 11)]),
        # the comb along the north and east, in three: a piece's art is its
        # whole rectangle, and one of 11 x 10 cells for this outline would
        # take a texture page of half a megabyte. Cut between columns, so no
        # column's run is split
        gym_block("block_e_arm", [(6, 12, 9, 13), (7, 13, 9, 14)]),
        gym_block("block_e", [(9, 6, 11, 16)]),
        gym_block("block_n", [(3, 6, 9, 8)]),
        gym_block("block_west", [(0, 6, 1, 16)]),
        # the back wall and its two deeper ends
        piece("wall_w", [(0, 0, 16, 48)], 25, fill=16, foot=48, side=GYM_WALL_SIDE),
        piece("wall_e", [(160, 0, 176, 48)], 25, fill=16, foot=48, side=GYM_WALL_SIDE),
        piece("wall", [(16, 0, 160, 32)], 25, fill=16, foot=32, side=GYM_WALL_SIDE),
        # the side walls in two each: one the room's length would need a
        # texture page too tall to share VRAM with the city's
        piece("side_w", [], 25, side=GYM_WALL_SIDE, walls=[((0, 176), (0, 48))]),
        piece("side_w2", [], 25, side=GYM_WALL_SIDE, walls=[((0, 320), (0, 176))]),
        piece("side_e", [], 25, side=GYM_WALL_SIDE, walls=[((176, 48), (176, 176))]),
        piece("side_e2", [], 25, side=GYM_WALL_SIDE, walls=[((176, 176), (176, 320))]),
    ]


SPECS = [
    {
        "name": "littleroot_house_w",
        "layout": "LAYOUT_LITTLEROOT_TOWN",
        "rect": (2, 4, 5, 5),
        "ground": [GRASS],
        "parts": lambda: littleroot_house(8),
        "exact": HOUSE_EXACT,
    },
    {
        "name": "littleroot_house_e",
        "layout": "LAYOUT_LITTLEROOT_TOWN",
        "rect": (13, 4, 5, 5),
        "ground": [GRASS],
        "parts": lambda: littleroot_house(64),
        "exact": HOUSE_EXACT,
    },
    {
        "name": "littleroot_lab",
        "layout": "LAYOUT_LITTLEROOT_TOWN",
        "rect": (3, 12, 7, 5),
        "ground": [GRASS],
        "parts": littleroot_lab,
        "exact": LAB_EXACT,
    },
    {
        # The canonical copy: Oldale paints its path into the top row.
        "name": "pokemon_center",
        "layout": "LAYOUT_PETALBURG_CITY",
        "rect": (19, 13, 4, 4),
        "match_rows": (1, 4),
        "ground": [GRASS],
        "parts": lambda: center_or_mart((9, 16), crown=True),
        "exact": CROWN_EXACT,
    },
    {
        # Oldale's copy has a tree's crown over its top-right corner.
        "name": "poke_mart",
        "layout": "LAYOUT_MAUVILLE_CITY",
        "rect": (22, 11, 4, 4),
        "match_rows": (1, 4),
        "ground": [GRASS],
        "parts": lambda: center_or_mart((12, 16)),
        "exact": CENTER_EXACT,
    },
    {
        "name": "oldale_house",
        "layout": "LAYOUT_OLDALE_TOWN",
        "rect": (4, 4, 4, 4),
        "ground": [GRASS],
        "parts": oldale_house,
        "exact": OLDALE_HOUSE_EXACT,
    },
    {
        "name": "briney_house",
        "layout": "LAYOUT_ROUTE104",
        "rect": (15, 47, 5, 4),
        "ground": [GRASS],
        "parts": briney_house,
        "exact": BRINEY_HOUSE_EXACT,
    },
    {
        "name": "flower_shop",
        "layout": "LAYOUT_ROUTE104",
        "rect": (3, 15, 6, 4),
        # the cobbled path round it
        "ground": [GRASS, 0x206, 0x207],
        "parts": flower_shop,
        "exact": FLOWER_SHOP_EXACT,
    },
    {
        "name": "kit_house_4",
        "layout": "LAYOUT_PETALBURG_CITY",
        "rect": (9, 16, 4, 4),
        "ground": [GRASS],
        "parts": lambda: kit_house(64),
        "exact": kit_house_exact(64),
    },
    {
        "name": "kit_house_5",
        "layout": "LAYOUT_PETALBURG_CITY",
        "rect": (5, 2, 5, 4),
        "ground": [GRASS],
        "parts": lambda: kit_house(80),
        "exact": kit_house_exact(80),
    },
    {
        "name": "gym",
        "layout": "LAYOUT_PETALBURG_CITY",
        "rect": (12, 4, 6, 5),
        # the bottom row is porch and the town's own ground
        "match_rows": (0, 4),
        "ground": [GRASS],
        "parts": gym,
        "exact": GYM_EXACT,
    },
    {
        # Petalburg's hedges. Not a building: a run of metatiles of any shape,
        # so each connected run becomes its own model, read column by column
        # off its drawing. The front, where a run ends to the south, is
        # drawn 11 rows tall.
        "name": "hedge",
        "components": {
            "secondary": "gTileset_Petalburg",
            "tiles": {0x23c, 0x23d, 0x23e, 0x244, 0x245, 0x246, 0x24c, 0x24d, 0x24e,
                      0x254, 0x255, 0x256, 0x264, 0x265, 0x266},
            "height": 11,
        },
        "ground": [GRASS],
    },
    {
        # Rustboro's stone blocks, any width and any number of storeys: every
        # one in the map is found by its corners and modelled from its own art.
        "name": "rustboro_stone",
        "kit": {"layout": "LAYOUT_RUSTBORO_CITY", "corner": 0x224, "top": {0x225},
                "end": {0x226}, "foot": 0x21c},
        "ground": [0x2BB, 0x2C3, GRASS],
        "parts": stone_block,
        "exact": lambda w, h, meta: flat_block_exact(w, h, 7),
    },
    {
        "name": "rustboro_olive",
        "kit": {"layout": "LAYOUT_RUSTBORO_CITY", "corner": 0x220, "top": {0x221, 0x222},
                "end": {0x223, 0x23F}, "foot": 0x240},
        "ground": [0x2BB, 0x2C3, GRASS],
        "parts": olive_block,
        "exact": lambda w, h, meta: flat_block_exact(w, h, 8),
    },
    {
        # The gym again: Rustboro's copy has its own roof and flanks.
        "name": "gym_rustboro",
        "layout": "LAYOUT_RUSTBORO_CITY",
        "rect": (24, 15, 6, 5),
        "ground": [0x2BB, 0x2C3, GRASS],
        "parts": gym,
        "exact": GYM_EXACT,
    },
    {
        # Rustboro's iron railings, read column by column like the hedges: a
        # railing along a row is all front, one down a column is a line on top
        # with its front where it ends. The bars keep their gaps (alpha).
        "name": "railing",
        "components": {
            "secondary": "gTileset_Rustboro",
            "tiles": {0x2A7, 0x2FC, 0x318, 0x319, 0x31A, 0x315, 0x31D, 0x320, 0x321,
                      0x32C, 0x32D, 0x2BE, 0x2BF, 0x2CD, 0x352, 0x2E9,
                      # the same railings over grass or a building's shade
                      0x2B7, 0x2C6, 0x2C7, 0x2D5, 0x2D7, 0x2DC, 0x2DE, 0x2DF,
                      0x2E6, 0x2E7, 0x2EC, 0x31B},
            "height": 12, "hull": 12, "bridge": 3, "block": 4,
            # drawn whole on the upper layer; the bottom one is the ground
            # with its grass edges, which the map paints under it
            "upper": True,
            # a railing down a column shows its bars on its sides: those of
            # the railing along a row
            "flank": 0x2A7,
        },
        "ground": [0x2BB, 0x2C3, GRASS],
    },
    {
        # The grey rock in the sea, found by its four tiles wherever they are
        # drawn (voxel_props.py): over the water round it, by a sandbank, by
        # a shore. A low dome, as deep as it is drawn tall below its crest.
        # The water round it stays the map's own, animated.
        "name": "sea_rock",
        "props": "sea_rock",
        "ground": [0x170],
        "rise": 1.0,
        "step": 2,
        # its foam on the water
        "ring": [(222, 230, 238)],
    },
    {
        # A boulder on the sand or the grass, a cell across (Route 106's beach,
        # Route 111's desert, the Safari Zone).
        "name": "sand_boulder",
        "props": "sand_boulder",
        "ground": [0x124],
        "rise": 1.0,
        "step": 2,
    },
    {
        # The brown stack in the sea: a peak, taller than it is deep.
        "name": "sea_stack",
        "props": "sea_stack",
        "ground": [0x170],
        "rise": 1.6,
        "step": 4,
        # its grey shadow on the water
        "ring": [(131, 131, 139)],
    },
    {
        "name": "devon_corporation",
        "layout": "LAYOUT_RUSTBORO_CITY",
        "rect": (7, 7, 10, 9),
        "ground": [0x2BB, 0x2C3, GRASS],
        "parts": devon,
        "exact": DEVON_EXACT,
    },
    {
        "name": "rustboro_fountain",
        "layout": "LAYOUT_RUSTBORO_CITY",
        "rect": (27, 38, 3, 3),
        "ground": [0x2BB, 0x2C3, GRASS],
        "parts": fountain,
        "exact": [(0, 0, 48, 48)],
    },
    {
        # Every town's Pokemon Center has this one ground floor.
        "name": "pc1f",
        "interior": {"layout": "LAYOUT_POKEMON_CENTER_1F", "ground": [0x202],
                     "pieces": POKEMON_CENTER_1F, "open": CENTER_1F_OPEN},
    },
    {
        "name": "pc2f",
        "interior": {"layout": "LAYOUT_POKEMON_CENTER_2F", "ground": [0x202],
                     "pieces": POKEMON_CENTER_2F, "open": CENTER_2F_OPEN},
    },
    {
        # Every Poke Mart but the department store has this one room.
        "name": "mart",
        "interior": {"layout": "LAYOUT_MART", "ground": [0x201], "pieces": MART,
                     "open": MART_OPEN,
                     "shade": [0x202, 0x204, 0x206, 0x208, 0x20A]},
    },
    {
        "name": "brendan_1f",
        "interior": {"layout": "LAYOUT_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F", "ground": [0x201],
                     "pieces": brendan_1f()},
    },
    {
        "name": "brendan_2f",
        "interior": {"layout": "LAYOUT_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F", "ground": [0x201],
                     "pieces": brendan_2f()},
    },
    {
        "name": "may_1f",
        "interior": {"layout": "LAYOUT_LITTLEROOT_TOWN_MAYS_HOUSE_1F", "ground": [0x201],
                     "pieces": may_1f()},
    },
    {
        "name": "may_2f",
        "interior": {"layout": "LAYOUT_LITTLEROOT_TOWN_MAYS_HOUSE_2F", "ground": [0x201],
                     "pieces": may_2f()},
    },
    {
        "name": "lab",
        "interior": {"layout": "LAYOUT_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB", "ground": [0x202],
                     "pieces": lab()},
    },
    {
        # after the starter: the boxes by the machine give way to a table.
        # The rest of the furniture is the lab's, tile for tile, and stands
        # here as it is, its back wall too; the side walls are its own.
        "name": "lab_table",
        "interior": {"layout": "LAYOUT_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB_WITH_TABLE",
                     "ground": [0x202],
                     "pieces": [piece("table", [(128, 64, 176, 89)], 8, leave=LAB_SHADOW,
                                      solid=True)]
                     + [pc for pc in lab() if pc["name"] in ("side_w", "side_e")]},
    },
    {
        "name": "lavaridge_pc1f",
        "interior": {"layout": "LAYOUT_LAVARIDGE_TOWN_POKEMON_CENTER_1F", "ground": [0x202],
                     "pieces": LAVARIDGE_CENTER_1F, "open": CENTER_1F_OPEN},
    },
    {
        # Oldale's first house, and eight more maps'
        "name": "house1",
        "interior": {"layout": "LAYOUT_HOUSE1", "ground": [0x223], "pieces": house1()},
    },
    {
        # Oldale's second house, and eleven more maps'
        "name": "house2",
        "interior": {"layout": "LAYOUT_HOUSE2", "ground": [0x223], "pieces": house2()},
    },
    {
        # Petalburg's second house, and two more maps'
        "name": "house_with_bed",
        "interior": {"layout": "LAYOUT_HOUSE_WITH_BED", "ground": [0x223], "pieces": own_shell(house_with_bed())},
    },
] + [
    {
        "name": layout[len("LAYOUT_"):].lower(),
        "interior": {"layout": layout, "ground": [ground],
                     "pieces": own_shell(plain_room(width, height, plain_x, sides))},
    }
    for layout, width, height, ground, plain_x, sides in PLAIN_ROOMS
] + [
    {
        # Mr. Briney's house, by the dock on Route 104
        "name": "briney_room",
        "interior": {"layout": "LAYOUT_ROUTE104_MR_BRINEYS_HOUSE", "ground": [0x229],
                     "pieces": own_shell(briney_room())},
    },
] + [
    {
        "name": layout[len("LAYOUT_"):].lower(),
        "interior": {"layout": layout, "ground": [ground],
                     "pieces": own_shell(stair_room(width, height, plain_x, doors, *extra))},
    }
    for layout, width, height, ground, plain_x, doors, *extra in STAIR_ROOMS
] + [
    {
        # Rustboro's Pokemon school
        "name": "school_room",
        "interior": {"layout": "LAYOUT_RUSTBORO_CITY_POKEMON_SCHOOL", "ground": [0x201],
                     "pieces": own_shell(school_room())},
    },
    {
        # The Pretty Petal flower shop on Route 104
        "name": "flower_shop_room",
        "interior": {"layout": "LAYOUT_ROUTE104_PRETTY_PETAL_FLOWER_SHOP", "ground": [0x201],
                     "pieces": own_shell(flower_shop_room())},
    },
    {
        # Rustboro's gym: Roxanne's maze. Before the other gyms: a room stands
        # only the pieces of the rooms modelled before it (reuse_pieces), and
        # the statues every gym has by its door are modelled here
        "name": "rustboro_gym",
        "interior": {"layout": "LAYOUT_RUSTBORO_CITY_GYM", "ground": [0x201],
                     "shade": [0x202, 0x203, 0x204, 0x216, 0x22f, 0x237],
                     "pieces": rustboro_gym()},
    },
    {
        # Dewford's gym: Brawly's maze
        "name": "dewford_gym",
        "interior": {"layout": "LAYOUT_DEWFORD_TOWN_GYM", "ground": [0x210],
                     "shade": DEWFORD_GYM_FLOOR, "pieces": own_shell(dewford_gym())},
    },
    {
        # Petalburg's gym: Norman's rooms
        "name": "petalburg_gym",
        "interior": {"layout": "LAYOUT_PETALBURG_CITY_GYM", "ground": [0x201, 0x22B],
                     "shade": [0x209, 0x212, 0x213, 0x214, 0x22A, 0x232,
                               0x216, 0x22C],
                     "pieces": own_shell(petalburg_gym())},
    },
]
