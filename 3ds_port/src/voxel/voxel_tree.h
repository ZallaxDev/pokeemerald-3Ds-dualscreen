/* General tileset trees: ground trunk and a north-leaning cutout crown. */
#ifndef CTR_VOXEL_TREE_H
#define CTR_VOXEL_TREE_H

#include "voxel_mesh_builder.h"

#define VOXEL_TREE_TEXTURE_DIM 128u
#define VOXEL_TREE_TEXTURE_PATH "romfs:/voxel/trees.rgba5551"

/* A small tree is one cell: its trunk, with the whole crown standing on it. */
#define VOXEL_TREE_SMALL 4

/* General IDs, plus the secondary tileset's own trees (Dewford's small one):
 * -1 for other art, the 2x2 quadrant (row*2+col) of a large tree, or
 * VOXEL_TREE_SMALL. secondaryTileset is the map's, as in VoxelMapInstance. */
int VoxelTree_Part(const void *secondaryTileset, int metatileId);
/* Remove the old canopy from the cell above a tree, leaving its ground. */
int VoxelTree_GroundMetatile(const void *secondaryTileset, int metatileId);

/* Appended after the ordinary terrain; these vertices use the tree texture. */
void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int x0, int y0, int x1, int y1);
void VoxelTree_EmitBorder(VoxelBuilder *builder, int x0, int y0, int x1, int y1);

#endif
