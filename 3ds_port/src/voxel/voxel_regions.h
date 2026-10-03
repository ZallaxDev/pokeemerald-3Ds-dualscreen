/*
 * What each cell of a layout is, solved for every layout on the host.
 *
 * scripts/gen_voxel_regions.py classifies every cell of every layout once and
 * this reads the answer back. The renderer asks one question of it: is this
 * cell a signpost (voxel_sign.c). It raises nothing; terrain stands up only
 * where it was modelled or read off its drawing.
 */
#ifndef CTR_VOXEL_REGIONS_H
#define CTR_VOXEL_REGIONS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * WHAT A CELL IS.
 *
 * The roles scripts/voxel_cells.py assigns to every cell of every layout on
 * the host; scripts/gen_voxel_regions.py parses these lines rather than
 * keeping a second copy of them.
 *
 * The whole vocabulary is kept so that the host tools and this header agree
 * on the numbers; the renderer only tests for VOXEL_ROLE_SIGNPOST.
 */
#define VOXEL_ROLE_FLOOR 0
#define VOXEL_ROLE_WATER 1
#define VOXEL_ROLE_LEDGE 2
#define VOXEL_ROLE_STAIR 3
#define VOXEL_ROLE_WALL  4  /* a building: there is a door in it */
#define VOXEL_ROLE_TREE  5
#define VOXEL_ROLE_PROP  6
#define VOXEL_ROLE_SHELF 7  /* blocked with nothing walkable beside it */
#define VOXEL_ROLE_FENCE 8  /* a thin line standing on one ground */
#define VOXEL_ROLE_CLIFF 9
#define VOXEL_ROLE_SIGNPOST 10 /* exterior lone sign; deliberately no region */
#define VOXEL_ROLE_COUNT 11

bool VoxelRegions_Init(void);
void VoxelRegions_Shutdown(void);

/*
 * What this cell is. Coordinates are local to the layout, not world tiles.
 * Returns VOXEL_ROLE_FLOOR for a cell the table does not cover.
 */
unsigned VoxelRegions_RoleAt(unsigned layoutId, int localX, int localY);

#endif
