/*
 * Buildings modelled from their own drawing.
 *
 * voxel/buildings.bin is written by scripts/gen_voxel_buildings.py,
 * which reads every building off its art (voxel_building.py explains how) and
 * refuses to write a model that does not reproduce that art pixel for pixel
 * in the GBA's projection. The console does no modelling: it copies each
 * placed model's triangles into the chunk that holds its top-left cell, draws
 * the plain ground under every cell the model covers, and keeps the region
 * table's generic extrusion away from those cells.
 *
 * The models carry their own texture - the building's art with the ground
 * made transparent, paged by map - so they are drawn in their own
 * alpha-tested pass.
 */
#ifndef CTR_VOXEL_BUILDING_H
#define CTR_VOXEL_BUILDING_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_mesh_builder.h"
#include "voxel_world.h"

/* Reads the file. False, with everything left empty, if it is absent. */
bool VoxelBuildings_Init(void);
void VoxelBuildings_Shutdown(void);

/*
 * Textures are paged by map: every layout's models are drawn from one page,
 * which the renderer loads when a map on screen needs it. -1 when the layout
 * places nothing.
 */
int VoxelBuildings_PageOf(const VoxelMapInstance *inst);
bool VoxelBuildings_PageSize(unsigned page, unsigned *width, unsigned *height);
/* Reads `count` of a page's RGBA5551 texels (PICA order) from texel `first`
 * into `dest`. */
bool VoxelBuildings_ReadPage(unsigned page, unsigned first, unsigned count, uint16_t *dest);

/* Tallest cell of any model, in tiles: a bound for shadow rays. */
float VoxelBuildings_MaxTop(void);
/* The tallest of the map's own models, in tiles over its base (0 for none). */
float VoxelBuildings_LayoutTop(const VoxelMapInstance *inst);

/*
 * World cell (x, y) of `inst`, when a model covers it: returns true and gives
 * the ground metatile to draw flat there and the model's tallest point over
 * the cell, in tiles (the lighting pass casts the shadow from it). Whatever
 * the map paints of its own in those cells arrives as the placement's ground
 * patches, drawn with the model - or, for a model found by its tiles (a rock
 * in the sea), the ground is the cell's own metatile, drawn from the atlas
 * so that its animation goes on: less the quarters the model stands for, a
 * ground variant (an atlas id past the real ones, voxel_atlas.h).
 */
bool VoxelBuildings_CellAt(const VoxelMapInstance *inst, int x, int y,
                           int *groundMetatile, float *top);

/*
 * The cell's footprint when the model covers only part of it (a railing's
 * line): 16 rows, bit x of row z set where its solid stands over that pixel.
 * NULL for a cell the model covers as a box, or does not cover.
 */
const uint16_t *VoxelBuildings_Footprint(const VoxelMapInstance *inst, int x, int y);

/*
 * Ground variant `i`: a layout, a metatile of the tileset that layout draws
 * it from, and the quarters of its upper layer (bit 2 * row + column) left
 * out. False past the last.
 */
bool VoxelBuildings_Variant(unsigned i, unsigned *layout, unsigned *metatile, unsigned *quarters);

/* Appends every model whose top-left cell lies in [x0,x1) x [y0,y1), with
 * its placement's ground patches. */
void VoxelBuildings_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                                 int x0, int y0, int x1, int y1);

/*
 * The same, at most `triangles` triangles at a time: a city's large models
 * are thousands of lit triangles, too many for one slice of a frame. Start
 * from a zeroed cursor and call again until it returns true; the triangles
 * come out in exactly EmitInstance's order.
 */
typedef struct
{
    unsigned placement, part;
    uint32_t vertex;
} VoxelBuildingCursor;

bool VoxelBuildings_EmitSome(VoxelBuilder *builder, const VoxelMapInstance *inst,
                             int x0, int y0, int x1, int y1, VoxelBuildingCursor *cursor,
                             unsigned triangles);

#endif
