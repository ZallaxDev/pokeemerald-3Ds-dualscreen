/* Pixel-masked geometry for the reference renderer's exterior signpost. */
#ifndef CTR_VOXEL_SIGN_H
#define CTR_VOXEL_SIGN_H

#include <stdint.h>

#include "voxel_mesh_builder.h"
#include "voxel_world.h"

/* One bit per 1/16-cell pixel. Bit 0 is the leftmost pixel. Row 0 is top.
 * A sign with a head (a lamp's lantern drawn in the cell north of it) is 32
 * rows tall: the first `headRows` are the head's, textured from its own
 * cell's drawing at headU0/headV0. */
#define VOXEL_SIGN_MAX_ROWS 32
typedef struct
{
    uint16_t opaque[VOXEL_SIGN_MAX_ROWS];
    uint8_t width;
    uint8_t height;
    uint8_t headRows;
    /* Normalized atlas coordinate of pixel (0,0), and atlas dimensions. */
    float u0, v0;
    float du, dv;
    float headU0, headV0;
} VoxelSignMask;

#define VOXEL_SIGN_PINNED_DEPTH 2

/*
 * Emits the signpost as opaque per-pixel voxels. `depthPixels` is in the same
 * pixel units as PINNED_DEPTH.signpost (2). No camera rotation is applied.
 */
void VoxelSign_Emit(VoxelBuilder *builder, float wx, float wz,
                    float baseY, const VoxelSignMask *mask,
                    unsigned depthPixels, float shade);

/* Loads the mask and emits donor ground and the sign. True means the caller
 * must skip its ordinary tile emission, including the original ground art. */
bool VoxelSign_EmitCell(VoxelBuilder *builder, const VoxelMapInstance *inst,
                        int x, int y);

/* The same sign without its ground, for a cell whose ground is drawn by its
 * relief (with the art of the cell south of it, as VoxelSign_EmitCell's). */
bool VoxelSign_EmitStanding(VoxelBuilder *builder, const VoxelMapInstance *inst, int x, int y);

/* A signpost cell with a mask: its drawing is the sign, not its ground. */
bool VoxelSign_IsCell(const VoxelMapInstance *inst, int x, int y);

/* For the sun: how high the sign in cell (x, y) stands, in tiles (0 when the
 * cell has none), and whether a world point is inside its drawing. */
float VoxelSign_CasterTop(const VoxelMapInstance *inst, int x, int y);
bool VoxelSign_Occludes(const VoxelMapInstance *inst, int x, int y,
                        float px, float py, float pz);

/* A cell holding the head of the sign south of it: true with the metatile its
 * ground is drawn with, the head itself standing on the sign. */
bool VoxelSign_HeadGround(const VoxelMapInstance *inst, int x, int y, int *metatile);

/* Releases the sidecar cache and permits a later ROMFS reload. */
/* Reads the masks now rather than at the first sign built: 16 KiB of RomFS
 * read inside a frame was a 60 ms stall the first time a town came on screen. */
void VoxelSign_Init(void);
void VoxelSign_Shutdown(void);

void VoxelSign_EmitNight(VoxelBuilder *builder, const VoxelMapInstance *inst,
                        int x0, int y0, int x1, int y1);

#endif
