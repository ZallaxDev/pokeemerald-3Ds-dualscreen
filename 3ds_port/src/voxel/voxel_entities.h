/*
 * Player and NPC billboards for the voxel overworld.
 *
 * Object events are drawn as camera-facing quads sampled from a sprite atlas
 * that this module decodes out of the game's own logical OBJ VRAM, so the
 * sprites are whatever the game is actually showing this frame - animation,
 * palette fades and all - with no second asset path.
 *
 * Like the rest of the voxel modules below ctr_voxel.c, nothing here touches
 * the GPU: the atlas is written into a caller-owned pixel buffer and the
 * billboards into a caller-owned vertex array.
 *
 * Derived from the object-event handling of the MIT-licensed voxel_renderer.c
 * of pokeemerald-multiplatform; see NOTICE.md.
 */
#ifndef CTR_VOXEL_ENTITIES_H
#define CTR_VOXEL_ENTITIES_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_camera.h"
#include "voxel_mesh_builder.h"

/* 16 slots of 64x64, one per gObjectEvents[], in a 256x256 RGBA5551 texture. */
#define VOXEL_SPRITE_ATLAS_DIM  256u
#define VOXEL_SPRITE_SLOT_DIM   64u
#define VOXEL_SPRITE_COLUMNS    (VOXEL_SPRITE_ATLAS_DIM / VOXEL_SPRITE_SLOT_DIM)
#define VOXEL_SPRITE_SLOTS      16u
#define VOXEL_SPRITE_PIXELS     (VOXEL_SPRITE_ATLAS_DIM * VOXEL_SPRITE_ATLAS_DIM)
#define VOXEL_CAST_SHADOW_VERTICES 6u
#define VOXEL_REFLECTION_VERTICES 6u

/* Forgets every cached slot, so the next update re-decodes from scratch. */
void VoxelEntities_Reset(void);

/*
 * The player's world position with sub-tile interpolation applied, which is
 * what the camera has to follow: VoxelWorld_GetPlayerWorldCoords reports whole
 * tiles, and a follow camera fed a target that jumps a tile at a time lurches
 * once per step no matter how smooth its damping is.
 */
void VoxelEntities_GetPlayerWorldPos(float *worldX, float *worldZ);
/* First vertex of the emitted player quad, or -1 when invisible/not emitted. */
int VoxelEntities_PlayerVertexFirst(void);

/*
 * Re-decodes the slots whose sprite changed and appends one billboard per
 * visible object event to the builder. `atlas` is VOXEL_SPRITE_PIXELS
 * uint16_t in PICA order. Optional `shadows` receives each outdoor object's
 * cast shadow: one quad (VOXEL_CAST_SHADOW_VERTICES), alpha in `shade`.
 * Returns the number of slots re-decoded this frame.
 */
unsigned VoxelEntities_Emit(VoxelBuilder *builder, uint16_t *atlas,
                            const VoxelCamera *camera, VoxelBuilder *shadows,
                            VoxelBuilder *reflections);

#endif
