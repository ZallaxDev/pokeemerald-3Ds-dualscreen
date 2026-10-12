# Buildings modelled from their own drawing

`scripts/gen_voxel_buildings.py` turns every building described in
`scripts/voxel_building_specs.py` into a 3D model and writes them to
`voxel/buildings.bin` (an RGBA5551 texture in PICA order plus triangles in the
exact `VoxelVertex` format). On the console, `src/voxel/voxel_building.c`
copies the triangles into the chunk of the model's top-left cell and draws
flat ground under its cells.

## The principle

Emerald's art is a fixed oblique projection: a point (X, Y, Z) in pixels lands
at `u = X, v = Z − Y`, which is what an orthographic camera sees at 45°. So:

1. **Every texel is one pixel** seen at 45° square to its face: 1:1 on walls
   and floors, `cos b + sin b` along a roof slope of pitch `b`.
   `density_check` verifies it triangle by triangle. Nothing is stretched.
2. **The front is the drawing**: the generator renders the model at 45° and
   requires a pixel-exact match in the spec's `exact` zones (facade, eave,
   upper floor…). If it does not match, the build fails.
3. **Real depth**: the drawing is flatter than the building (a house with 4
   rows of collision drawn in 5 rows of art would be 2 cells deep). The model
   takes the real footprint and fills the missing depth with **more courses
   of the same tiles** or more of the same plaster column — never stretching.
4. **Sides and backs from the art itself**: hip roofs with courses laid
   parallel to each eave; side walls with the clean plaster column and the
   facade's corner posts.
5. **No ground**: a pixel belongs to the building if its upper layer is opaque
   or its lower-layer subtile is not one of the ground subtiles around it.
   Everything else is transparent and the cell gets the ground metatile.

## Modelling a new building

1. Find one copy in a layout and its rectangle of cells.
2. Read the art's bands row by row (see the docstring of
   `voxel_building_specs.py` for the Littleroot house): facade, eave (fascia),
   roof courses and their period, ridge teeth and cap, clean columns
   (`ROOF_COLUMNS`) without edge boards.
3. Describe the parts: `Prism` with `Proj` for walls at their drawn depth,
   `HipRoof` for roofs, `Strip` for coursed faces, `Tile` for plaster and posts.
4. Declare the `exact` zones and run:

   ```
   python scripts/gen_voxel_buildings.py --preview build/buildings
   ```

   It writes `*_ortho.png` (art | 45° render | differences) and perspective
   views from several cameras (`game`, `left`, `right`, `yaw35`, `yaw-50`,
   `high`) framed like the voxel renderer.
5. The generator finds every appearance of the same drawing in every layout
   with the same tilesets, so a building is modelled once.
6. Check the ground under it: the generator's `ground:` lines, then `gba`
   and `under` on the town (see "Voxelizing a town: the ground").

## Reuse across Hoenn

- `match_rows` says which rows of the drawing must repeat cell for cell. If
  they all come from the primary tileset, the building matches in **any**
  town sharing that primary, whatever its secondary. That is how the Pokémon
  Center and the Mart (two specs) cover their 20 appearances.
- The spec points at a **canonical** copy (Petalburg's Center, Mauville's
  Mart): other towns paint things of their own in the top row (the path in
  Oldale, a tree crown, a cliff in Lilycove and Verdanturf, a sign in
  Lavaridge).
- Those cells are not lost: where the model has no geometry, the generator
  creates a **ground patch** from what the map paints there minus the
  building's pixels and lays it flat on the ground with the model. If the
  model does occupy the cell, the log says so.
- The ground under each placement is the most common walkable metatile around
  it in that map, which its atlas always contains.
- `--town LAYOUT_X` renders the whole layout with every model and the same
  rules as the console.

## Interiors: a tile modelled once is modelled everywhere

The rule for every room, now and in the future:

1. **Before modelling a room**, run the generator: its log lists, with
   `reuse: <piece> stands in <room> at x,y`, every piece already modelled
   in another room that this one repeats. A piece is reused, as it is, same
   model, no copy:
   - where **every cell of its rectangle draws exactly the same pixels**
     (whatever the tileset or metatile number); or, failing that,
   - for a thing standing in the room (not a wall), where **its own pixels
     are drawn again, pixel for pixel, on whatever floor**: the gyms' statue
     is modelled once in Rustboro and stands in all eight gyms. There it
     stands "bare" (a twin model without its first room's floor marks) and
     the room's own paint round it is laid as patches. Same-tile matches
     are placed first: two pieces drawn alike can stand at different depths.
   Shape a piece so that it does not claim floor pixels (the statue's
   rounded foot corners), or it will only match on its own room's floor.
   Anything like this (statues, plants, chairs, machines) must never be
   lifted again by hand in another map.
2. **The room's spec models only what is new.** Anything reused is left out:
   the generator treats its pixels as taken, and a piece of the spec that
   is left with none fails with `claims no pixel`, the signal that it was a
   duplicate and has to go.
3. Order counts: a piece belongs to the first room in `SPECS` that models
   it. Later rooms, and every indoor room nobody has modelled, take it.
   In a room with no spec, the known furniture stands and the rest of the
   drawing stays flat, laid over the floor as patches.
4. The proof is the same: `room ... 0 pixel(s) differ` for every room,
   reused pieces included.

This way Pokemon Center 2F uses the 1F stools, May's house uses Brendan's
fridge, sink, TVs, cabinet and chair seats (on her pink rug), the lab with the table uses the whole lab minus the
table, Lavaridge uses the Center's counter and machines, and House2 uses
House1's glass case, plant and east chair seats. Reuse also covers some rooms nobody has
modelled yet (Lanette's house, the League, Steven's house...).

Details that apply to every piece:

- A card (plant) is textured only with its own pixels, never with the floor
  behind its pot.
- What a piece hides of a mark (a rug under a chair) is drawn under it,
  when the row reaches the same mark on both sides; otherwise the floor
  shows through. Not behind a card: that floor can be seen.
- A piece's side (`side`) samples its own pixels, not the fill that the
  wall behind uses to complete itself.

## Free-form objects (hedges, walls)

A spec with `components` does not describe a building but a set of metatiles
(Petalburg's hedges). Every connected run of those metatiles, in every layout
of that tileset, becomes its own model:

- The art comes only from the run's cells, and the ground is separated
  **pixel by pixel** (foliage and grass share a subtile on the hedge's flank).
- `Relief` reads it column by column: in each column the drawing shows the
  object's top and, at its south end, its front of `height` rows. The top goes
  at that height and the front is vertical, both projected, so the drawing is
  reproduced exactly. Flanks and back are dressed with a piece of the drawn
  front.
- Cells of its rectangle that are not part of the run (the surrounding house)
  are marked with height 255 and the console does not claim them.
- A run is only placed where it was found: a short one also appears inside
  every long one.

Rustboro's railings add three rules:

- A long run is cut into 4×4-cell blocks, one model per repeated block. A
  block carries on into the next one: its top runs on past its seam with
  `height` rows of the next block's drawing, and no front or back stands at
  the seam. Without this, a railing running down a column stood up as a
  slab every four cells.
- `upper`: the railing is drawn whole on the upper layer. Only that layer is
  modelled, and the map paints the bottom layer under it (the ground with
  its grass edges). Otherwise those edges rose as pale slivers.
- `flank`: a railing running down a column is drawn as a line (its top). Its
  long sides carry the bars of the railing that runs along a row (metatile
  2A7), gaps included, instead of the dark line projected. That band is
  appended below the art and is never read as drawing.
- The bars stand as one plane down the middle of the line, not one on each
  side: a box with bars on both faces read as a wall with windows. Either
  way the plane is edge-on at 45 degrees.
- No flank stands where a block goes on into the block east or west of it.
- The tile list must hold every variant of the railing: the same railing
  over grass or a building's shade is another metatile.

Shadows: the lighting casts a model from one height per cell. A cell the
solid covers less than FOOTPRINT_FULL/256 of (a railing's line, a hedge's
end) also carries its footprint, 16×16 bits, and casts only from those
pixels. As a box, a railing darkened its whole cell and cast a block. The
ground patches under a model are lit like the terrain, so a shadow falls
across them too; the model keeps the shading it was drawn with.

Texture pages are packed bottom-left on a skyline, not on shelves. Two
pages of 512 KiB do not fit the 768 KiB VRAM block together: the page of a
room next to Rustboro (512 KiB) must stay at 256 KiB or less, or crossing
the door leaves one of them waiting. So a long, thin piece (a side wall the
room's full length) is split.

A page is a power of two wide but only as tall as the rows it uses, in
whole tiles of 8: the GPU samples it as a texture the next power of two
tall, and the rows past the page are neither stored nor given VRAM (no
face reads them). Each width is tried, tallest drawing first and largest
first, and the fewest texels win. Rounded up to 512×512, the Battle
Frontier's two halves took 512 KiB each, and no splitting could bring
either to 256 KiB: West's faces read about 120,000 texels and East's about
150,000, more than a 512×256 page holds. At their own height they take
512×352 and 512×384, 736 KiB together. `voxel_engine_view.py check` adds
up the pages of connected maps.

## Projected surfaces: smooth shapes that stay exact

A face laid with the drawing projected (`u = x, v = z - y`, tag `~proj`) is
the drawing at 45 degrees wherever it stands, and the console tests alpha,
so a transparent texel cuts it. A round or thin thing then only has to
*cover* its outline at 45 degrees; it does not have to be cut to it column by
column, which is what left steps and fins on `HeightSolid` flanks:

- `ProjDome`: a smooth dome over an ellipse (Slateport's bush, 60 triangles;
  the market's fruit, a prop with `"shape": "dome"`, 60 for 231).
- `ProjLathe`: a turned thing from a (y, radius) profile on an elliptical
  plan (the harbour bollard, 130 for 160 with a fin; the market's urn, a
  prop with `"shape": "lathe"`, 88 for 227).
- `ProjHull`: a boat - outside, then a deck along its sheer, or open with
  its rim, inside, floor and boxes standing in it (the sailboats' hulls,
  now exact; the rowboat, 272 smooth triangles for 682 in steps).
- Cards: a thing that is thin in the drawing's own plane is one flat face:
  a sailboat's sails and mast (`SailCards`, the hull's `HeightSolid` told by
  `mask` that their pixels are not its own), an awning's or a stall's legs,
  the fan club's door posts.

Fit the shape's parameters to the outline by search (`ortho_check` on a
grid of values: the smallest shape with no pixel missing). The game's camera
never turns (yaw 0, pitch 34-46), so what matters is how the shape reads from
the south, high or low; its far side is hardly seen.

What such a face shows from the game's camera is the drawing as the 45-degree
view saw it at that place: right where the face stands where the drawing's
thing stands, a ghost where it does not. A wall that paints what stands in
front of it (a post, a porch) needs a skin of its own wall over that part
(the fan club's `Canopy`).

## Finding faults: voxel_defect_scan.py

The proofs only see the 45-degree view; every face it does not see is
invented, and that is where the faults are. `scripts/voxel_defect_scan.py
LAYOUT` measures every triangle of every model placed in a layout and
flags, from the game's camera, texels smeared along a face (`stretch`), the
ground round a model painted on an upright face (`ground`), faces seen
through (`holes`), shapes built in stair steps (`steps`) and thin parts
built as boxes (`thin`). It writes a report per model and tag, and renders
views with every flagged face tinted (`--view name:cx,cz,pitch,yaw,dist`).
Run it on a town after modelling it, then look at what it tints; then
`voxel_engine_view.py view` shows the same place as the console builds it.

## Finding faults: the map against its drawing

`scripts/voxel_engine_view.py gba LAYOUT OUT.png` draws the scene the console
builds (its own C over romfs/voxel/*.bin) in the GBA's projection, a texel a
pixel, unlit, and compares it with the map's drawing, at the console's 5 bits
a channel. Every model is proved alone; this proves what the map does round
them. Each differing pixel is told by what shows there: a hole (nothing), the
ground (the terrain's atlas: a patch or a ground laid wrong) or a model (its
real depth past its drawing, a lifted roof - or a model out of place). Cells
with trees are left out. What it found in Slateport, and the spec keys that
fixed it:

- `ground_fixed`: the ground under a placement was the commonest walkable
  metatile round it, which put a pier under boats moored by it, the path
  under the battle tent and the sea under two bollards (it is now chosen by
  what the model's cells show: see the ground under the models). `True`
  takes the spec's own ground, a dict `{layout: metatile}` one for a map.
- `patch_own`, `patch_cells`, `keep`: what the drawing paints in a model's
  cells that no part of the model draws (the lawn and a house's shadow
  either side of its facade, a curb's lawn, a path's edge in the fan club's
  last row) is lost unless it is the map's: `keep` leaves only the model's
  own pixels, and the patches lay the rest as the map paints it.
- `clip_art`: projected shapes (ProjDome, ProjLathe, ProjHull, cards) standing
  past their drawing read, on the console, the next model's texels on the
  page; the generator cuts them to the drawing's opaque box.
- `voxel_cells.NOT_POSTS`: a blocked cell open on three sides with no sign
  event is taken for a post and stands up as a sign. Slateport's curb ends,
  a bollard and the market's goods were; they are listed as never posts.

## Finding faults: the ground under the models

The GBA's view sees none of the ground the console lays under a model, and the
game's camera does: a patch lies a hair over it, so a line a pixel wide of
whatever is under it shows along the patch's edges, and a lifted roof shows the
floor beneath. `scripts/voxel_engine_view.py under LAYOUT` looks from a game
camera every few cells and matches every pixel of a model's cell's terrain it
sees against the drawing round that point; a colour the drawing has nowhere
near counts against the cell. The floor the drawing hides (a stall's, under its
roof) counts too, so the list is read by eye. In Slateport it found the sea
under the harbour's curbs and under the market's west fence, and grass under
the market's gate where the path runs on beneath it:

- the ring's commonest metatile is never water for a model that stands on
  land (no water among its spec's grounds, as a boat's has): the sea along a
  pier, the rocky edge along a shore, were the commonest round a curb or a
  fence run.
- `ground_fixed` gives the spec's own ground only in a map that paints that
  metatile somewhere: the console's atlas holds only the metatiles its maps
  use. The grey fence (General) takes its own bottom layer, the same in
  every one of its metatiles and drawn whole by the grass 0x001 (0x004 has the
  same bottom layer and flowers over it); Slateport's curbs the pier's bricks.
- `cell_ground_at`: `cell_ground` for one placement, `{(layout, x, y): {(i,
  j): metatile}}`, laid as patches (the market's gate over its path, in the
  shade its row of posts draws).

`voxel_engine_view.py view` (and voxel_relief_lines' renders) interpolate
each vertex's shade across its triangle, as the console's GPU interpolates the
vertex colour; with one shade a triangle, a cast shadow was a dark block on
the paving and a bollard's cell a light square, neither of them on the
console.

### The ground chosen, and what the generator reports

The console lays one ground metatile under every cell a placement owns, and
the first placement that owns a cell lays it there (`ModelCell`). The
generator chooses it (`placement_ground`) from the metatiles the map paints
round the placement (not water, for what stands on land) and the spec's own
grounds the map paints, kept to those the map lays beside themselves across
and down if any (a material that tiles, not a path's grassy edge, which would
stripe the floor):

1. the one that draws most of what the map shows where the model's cells
   have no patch, the model draws nothing and no face of it stands in front
   in the GBA's projection: there the ground shows whole, in the GBA's view
   too (the grass under a fence's bars, not the tall grass round it);
2. then what the game's camera sees of it: the patches' edge pixels it draws
   as the map does (the grass between a curb's slabs) and the ground round
   the model, 16 pixels of edge for each cell of it.

`ground_fixed` still overrides it (in a map that paints that metatile). Then
the generator prints, for each placement, the cells its ground still is not:

    ground: battle_factory in LAYOUT_... at 4,30 on 0x170: 0,8 bare 128 (0x05d), ...

- `bare N`: N pixels (24 or more) where the ground shows and the map draws
  something else. `gba` sees them. A cell of another material than the rest
  (the ship's bottom row on the shallows' edge) wants `patch_cells`; a
  model on two materials, the one its front shows (`ground_fixed`: the Battle
  Factory's platform, not the sea of its back corners). A drawn shadow (a
  stall's darker paving) is left: the console casts its own.
- `patched N`: most of the patch's pixels, N of them, colours the ground has
  none of. The patch hides the ground in the GBA's view; the game's camera
  sees a line of it along the patch's edges (`under`): `ground_fixed` to the
  patch's material (the curbs' brick).

What neither sees: the floor a lifted roof hides in the drawing (the path on
under the market's gate). `under` finds it; `cell_ground_at` lays the floor
that runs on under it.

### Voxelizing a town: the ground

1. Generate (`--output`) and read the `ground:` lines of the town.
2. `voxel_engine_view.py gba LAYOUT OUT.png`: every `ground` cell is a bare
   cell of the report, or a patch wrong.
3. `voxel_engine_view.py under LAYOUT --out PREFIX`: PREFIX_under.png tints
   red the cells whose ground the game's camera sees against a drawing of
   other colours. Look at the strongest with `view` before fixing: under a
   stall's roof the paving is right, though the drawing shows its goods.
4. Fix with the spec keys above, regenerate, and compare `gba` and `under`
   before and after on every map whose grounds changed: a spec reaches every
   copy of its drawing in Hoenn.

Measured when the choice came in (2026-10-12), against the commonest metatile
round each placement: 129 placements in 29 maps changed. In the GBA's view
the Battle Frontier East went from 17 082 px of ground unlike its drawing to
7 476, Route 108 from 1 553 to 330, Lilycove from 5 251 to 4 429, and no map
got worse (four cells did: two in Lavaridge by 24 px, two in Route 109 by
8). In the game's camera (`under`) the Frontier East went from 317 410 px to
244 656, Route 108 from 12 196 to 979, Rustboro from 21 527 to 14 798, Route
121 from 6 492 to 3 168; Faraway Island lost 285 px (a curb's corner on the
snow it rims), Verdanturf 108, Mt Chimney 28, Southern Island 6, Slateport 1.

## Memory

The shared texture lives in VRAM (uploaded through the atlas staging buffer,
so at most 512×256). The generator fails if it is exceeded.

## Cost on Old 3DS

Littleroot house: 614 triangles (1,842 vertices, 43 KiB). Shared texture
256×128 (64 KiB of linear memory) for every model.
