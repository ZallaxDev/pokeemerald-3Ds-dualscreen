# Trees of the General tileset

Original art for the voxel mode's trees, drawn for Pokémon Emerald 3Ds Dual Screen:

- `tree_trunk.png`: 32×32, opaque trunk and ground, on a 2×2-cell footprint.
- `tree_crown.png`: 32×36 RGBA, crown on a transparent background.
- `tree_small_trunk.png`: 16×16, trunk of the small tree on a single cell.
- `tree_small_crown.png`: 16×32 RGBA, crown of the small tree on a
  transparent background.
- `tree_2_small_trunk.png`, `tree_2_small_crown.png`: the same pair for
  Dewford Town's small tree, trunk on sand.

`scripts/gen_voxel_trees.py` packs them, unscaled, into
`voxel/trees.rgba5551`: a 128×128 RGBA5551 texture in PICA200 order (large
crown at 0,0; large trunk at 32,0; small crown at 32,32; small trunk at
48,32; Dewford's small crown at 64,0 and its trunk at 80,0). It is an engine file and ships inside the 3DSX. The build regenerates
it whenever a source changes; it needs Pillow like the port's other graphics
tools.

The crown is a fixed plane tilted 50° from the ground, high end to the north.
The tilt and the anchor point are in `src/voxel/voxel_tree.c`. The plane's
base sits at −0.10 cells: the transparent margin sinks into the ground and the
visible leaves overlap the trunk. The renderer uses alpha test: transparent
pixels write neither colour nor depth.

They replace the regular metatiles `1D4–1D7`, `1DC–1DF` and `1E4–1E7` of the
**General** tileset, also in its repeated border. The tops `1C6–1C7` and
`1CE–1CF` get their background grass back. Special trees keep their art. Each
cell emits its own part of the trunk and the crown, so a tree can cross a
chunk or map boundary without being duplicated or cut when rebuilt.

The small tree works the same way: the trunk cell (`016–017`, forest edge;
`0C6–0C7`, interior, where the next crown overlaps the trunk; `1F4–1F5`)
becomes the new trunk and raises the whole crown above it, a 1×2-cell plane at
50°, base at −0.10 and 0.825 cells from the north edge. The upper cells lose
the old crown and get their background back: `00E–00F` and `040` grass, `01D`
ledge edge, `025` tall grass, `02D` reflective water, `035`/`193` water, `0CE`
rock. `1EC–1ED` are the bottom row of a large tree. The secondary tilesets'
variants keep their art.

Dewford Town's small tree (its secondary tileset, so the ids only mean a tree
there) is replaced the same way with its own art: the trunk cells `23A` (on
the sand) and `243` (inside the wood, where the next crown overlaps it, also
the repeated border) become the trunk and carry the whole crown; the crown top
`239` gets the sand `124` back.
