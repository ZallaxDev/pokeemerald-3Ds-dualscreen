/*
 * The 3D battle: the voxel world drawn as a battle's scenery.
 *
 * The battle's own picture - its Pokemon, the healthboxes, the text box,
 * the move animations - stays the game's, composed over the world where the
 * GBA draws its scenery on BG3 (3ds_video.c, RenderBattleWorld). This module
 * is the game's side of it: where in the map the battle stands, and where
 * the battlers' feet are on the screen.
 *
 * Like voxel_world.h it carries no game types: ctr_voxel.c and the 2D
 * compositor, which own the GPU, include it.
 */
#ifndef CTR_VOXEL_BATTLE_H
#define CTR_VOXEL_BATTLE_H

#include <stdbool.h>

/*
 * The battle camera: lower and closer than the field's, so that the ground
 * the battlers stand on fills the scene, but never so low that the view
 * reaches the horizon (at 30 degrees the top of the screen meets the ground
 * some 16 tiles away, inside what the world builds anyway). The field of
 * view is the field camera's (voxel_camera.c).
 */
#define VOXEL_BATTLE_PITCH 30.0f
#define VOXEL_BATTLE_DISTANCE 7.0f
#define VOXEL_BATTLE_FOV 35.0f

/* The game is in a battle (gMain.inBattle): from the battle's first frame
 * to its last, through the bag and the party screen it opens. */
bool VoxelBattle_GameInBattle(void);
/* A link battle, whose intro shows its VS frame on BG1 and BG2. */
bool VoxelBattle_IsLink(void);

/*
 * Chooses where the battle stands: the point of the world the battle camera
 * looks at (world tiles) and the height of the ground there. Searched near
 * the player for the spot whose ground under the two battlers is open and
 * of the kind the battle is about (the grass a wild Pokemon came out of,
 * the water of a surfing or fishing battle, the floor of a room), level,
 * with nothing tall standing in front of either battler and nothing outside
 * the map on screen.
 *
 * Begin once per battle, with the instances built (VoxelWorld_BuildInstances);
 * then Step once a frame, reading that many more cells of the world and then
 * scoring that many more candidate spots, until it returns true with the
 * answer.
 */
void VoxelBattle_BeginStage(void);
bool VoxelBattle_StepStage(unsigned cells, unsigned candidates, float *targetX, float *targetZ,
                           float *ground);

/* A contact shadow under a battler or a trainer, in GBA coordinates: its
 * centre and its radii. */
typedef struct
{
    float x, y, rx, ry;
} VoxelBattleShadow;

/* The battlers on screen whose feet touch the ground and that the game gives
 * no shadow of its own (a floating Pokemon has one). Returns how many. */
unsigned VoxelBattle_Shadows(VoxelBattleShadow *out, unsigned max);

#endif
