/*
 * Terrain relief read off the drawing: cliffs, their stairs, rocks at sea.
 *
 * voxel/relief.bin is written by scripts/gen_voxel_relief.py, which
 * explains the model: every point (u, v) of the map's art is lifted to a
 * height h and stands at (u, h, v + h), which the GBA's 45-degree view sees as
 * the drawing itself. The file holds, for each lifted cell, its heights on a
 * 4-pixel lattice (5 x 5 points, in pixels, signed; a drawn mountain's in
 * units of 2 pixels, so that it can stand taller than a byte).
 *
 * Everything standing on a lifted cell is lifted with it: the same (0, h, h)
 * displacement applied to the terrain is applied to the trees, structures,
 * buildings and billboards on it, so they stand where the drawing shows them.
 *
 * A map as a whole stands at its base: the level its ground is at in the
 * world (Fortree up on the plateau Route 119 climbs to), everything on it
 * raised by it, straight up. The cells' heights are over the base; only
 * VoxelRelief_LiftAt, a height in the world, counts it in.
 */
#ifndef CTR_VOXEL_RELIEF_H
#define CTR_VOXEL_RELIEF_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_world.h"

#define VOXEL_RELIEF_SIDE 5  /* lattice points per cell side */

bool VoxelRelief_Init(void);
void VoxelRelief_Shutdown(void);

/* The level a map's ground stands at in the world, tiles (0 for a map the
 * file does not name). */
float VoxelRelief_Base(const VoxelMapInstance *inst);

/* The 25 heights (pixels, over the base) of a world cell, or NULL when it is
 * not lifted. */
const int16_t *VoxelRelief_Cell(const VoxelMapInstance *inst, int x, int y);

/* True for a map whose relief was read off its drawing: every blocked cell
 * of rock is a relief cell, and no structure stands on one. */
bool VoxelRelief_IsDrawn(const VoxelMapInstance *inst);

/* True when the cell's lattice is not level: rock or stairs, drawn by the
 * relief itself and not by any structure. */
bool VoxelRelief_IsSlope(const int16_t *grid);

/* The 25 depths (pixels) the cell's points are moved south by: its heights
 * again, (u, h, v + h), the drawing seen at 45 degrees. NULL when the cell
 * is not lifted. */
const int16_t *VoxelRelief_Depth(const VoxelMapInstance *inst, int x, int y);

/* Height in tiles over the base at the centre of a cell (0 when not lifted). */
float VoxelRelief_CellLift(const VoxelMapInstance *inst, int x, int y);

/* Its depth, in tiles, at the centre of a cell. */
float VoxelRelief_CellShift(const VoxelMapInstance *inst, int x, int y);

/* Height in tiles in the world at a world position, interpolated on the
 * lattice: the base of the map there and the relief over it. */
float VoxelRelief_LiftAt(float worldX, float worldZ);

/* Depth in tiles at a world position, interpolated on the lattice. */
float VoxelRelief_ShiftAt(float worldX, float worldZ);

/*
 * A drawn map's surface as it stands on its base, for the sun (the lighting's
 * rays): its height in tiles over the base at a world position, 0 off any
 * drawn map; the highest of it over a cell; the highest of the map.
 */
float VoxelRelief_SurfaceAt(float worldX, float worldZ);
float VoxelRelief_SurfaceTop(const VoxelMapInstance *inst, int x, int z);
float VoxelRelief_DrawnTop(const VoxelMapInstance *inst);

/*
 * Cut tiles: a cell of rock drawn over the ground behind it (the sea round a
 * ridge's end), whose lattice would lift that ground with the rock. It is
 * drawn twice - its whole tile flat at its foot, and its relief textured with
 * the variant that has the background clear (bit set in `rows`, a row of 16
 * pixels each, two bytes little endian). Returns the variant, -1 for a cell
 * that is not cut; `foot` in tiles over the base. `ground` is the metatile
 * of the ground it is drawn over (-1: none), which runs on under the rock
 * beside it: never drawn at 45 degrees, it is what shows through the clear
 * background from above, between the rock's outline and its hidden wall.
 *
 * VOXEL_RELIEF_NO_VARIANT is no cut: a cell under which the ground behind a
 * terrace's rim (`ground`) runs on, flat at `foot`, where the step down to
 * it leaves the view open from above.
 */
#define VOXEL_RELIEF_NO_VARIANT 0xFFFF
/*
 * `wall` (-1: none) says the cell has cliffs: wherever its west, east or
 * south edge stands over its neighbour's - a tile of rock reaching its own
 * edge in its own shape above ground further down than the tile goes - a
 * wall closes the step. West and east it is the rock's own edge column
 * drawn down; south, the mountain's face, this metatile
 * (VOXEL_RELIEF_NO_FACE: the map has none, no south cliff).
 */
#define VOXEL_RELIEF_NO_FACE 0xFFFE
int VoxelRelief_Cut(const VoxelMapInstance *inst, int x, int y, float *foot, int *ground,
                    int *wall);
unsigned VoxelRelief_CutCount(void);
bool VoxelRelief_CutVariant(unsigned i, unsigned *layout, unsigned *metatile,
                            const uint8_t **rows);

#endif
