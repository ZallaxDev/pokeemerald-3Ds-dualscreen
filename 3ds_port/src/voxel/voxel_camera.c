/*
 * Follow camera for the voxel overworld. See voxel_camera.h and NOTICE.md.
 */

#include <math.h>

#include "3ds_platform.h"
#include "voxel_camera.h"
#include "voxel_world.h"

#define VOXEL_DEG_TO_RAD (3.14159265358979323846f / 180.0f)
#define VOXEL_FOLLOW 0.15f

void VoxelCamera_Init(VoxelCamera *cam)
{
    cam->x = 0.0f;
    cam->y = 8.0f;
    cam->z = 0.0f;
    cam->targetX = 0.0f;
    cam->targetY = 0.0f;
    cam->targetZ = 0.0f;
    cam->pitch = 40.0f;
    cam->yaw = 0.0f;
    cam->distance = 9.0f;
    cam->fov = 35.0f;
    cam->ground = 0.0f;
}

void VoxelCamera_SetGround(VoxelCamera *cam, float ground, int snap)
{
    cam->ground = snap ? ground : cam->ground + (ground - cam->ground) * VOXEL_FOLLOW;
}

/* Places the eye for the current target, pitch, yaw and distance. */
static void Place(VoxelCamera *cam)
{
    float pitchRad = cam->pitch * VOXEL_DEG_TO_RAD;
    float yawRad = cam->yaw * VOXEL_DEG_TO_RAD;

    /* The eye sits south of the player (+Z) and above it, looking north. */
    cam->x = cam->targetX + sinf(yawRad) * cam->distance;
    cam->y = cam->ground + tanf(pitchRad) * cam->distance;
    cam->z = cam->targetZ + cosf(yawRad) * cam->distance;
    cam->targetY = cam->ground;
}

/* Wider maps are framed from further away, up to a fixed ceiling. The pitch
 * and the zoom are the player's (bottom-screen options; 40 degrees, 100%). */
static void AdaptDistance(VoxelCamera *cam)
{
    int mapW = 0, mapH = 0;
    float scale;

    VoxelWorld_GetMapDimensions(&mapW, &mapH);
    scale = (float)(mapW > mapH ? mapW : mapH) * 0.2f;
    if (scale > 5.0f)
        scale = 5.0f;
    cam->distance = (8.0f + scale) * 100.0f / (float)CtrSettings_VoxelZoom();
    cam->pitch = (float)CtrSettings_VoxelPitch();
}

void VoxelCamera_Snap(VoxelCamera *cam, float playerWorldX, float playerWorldZ)
{
    cam->targetX = playerWorldX;
    cam->targetZ = playerWorldZ;
    AdaptDistance(cam);
    Place(cam);
}

void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldZ)
{
    cam->targetX += (playerWorldX - cam->targetX) * VOXEL_FOLLOW;
    cam->targetZ += (playerWorldZ - cam->targetZ) * VOXEL_FOLLOW;
    AdaptDistance(cam);
    Place(cam);
}

void VoxelCamera_Shift(VoxelCamera *cam, float dx, float dz)
{
    cam->targetX += dx;
    cam->targetZ += dz;
    Place(cam);
}

void VoxelCamera_Frame(VoxelCamera *cam, float targetX, float targetZ, float ground,
                       float pitch, float distance)
{
    cam->targetX = targetX;
    cam->targetZ = targetZ;
    cam->ground = ground;
    cam->pitch = pitch;
    cam->distance = distance;
    Place(cam);
}
