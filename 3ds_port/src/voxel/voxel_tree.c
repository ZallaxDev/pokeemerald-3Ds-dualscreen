#include <stddef.h>
#include "voxel_tree.h"
#include "voxel_relief.h"

struct Tileset;
extern const struct Tileset gTileset_Dewford;

/* Where gen_voxel_trees.py packs each tree, in texels. */
#define TEX_U(px) ((float)(px) / VOXEL_TREE_TEXTURE_DIM)
#define TEX_V(py) (1.0f - (float)(py) / VOXEL_TREE_TEXTURE_DIM)

/* Ids from 0x200 are the secondary tileset's, the same number meaning a
 * different drawing on every map. */
#define SECONDARY_ID 0x200

int VoxelTree_Part(const void *secondaryTileset, int metatileId)
{
    if (metatileId >= SECONDARY_ID)
    {
        /* Dewford's small tree: the trunk on the sand (23A), and inside the
         * wood (243), where the next crown overlaps it. */
        if (secondaryTileset == &gTileset_Dewford
         && (metatileId == 0x23A || metatileId == 0x243))
            return VOXEL_TREE_SMALL;
        return -1;
    }
    switch (metatileId)
    {
    case 0x1D4: case 0x1D6: return 0; /* upper left, including forest edge */
    case 0x1D5: case 0x1D7: return 1;
    case 0x1DC: case 0x1DE: case 0x1E4: case 0x1E6: return 2;
    case 0x1DD: case 0x1DF: case 0x1E5: case 0x1E7: return 3;
    /* A large tree's lower row under a small tree's canopy top. */
    case 0x1EC: return 2;
    case 0x1ED: return 3;
    /* Small trees: the edge of a wood, and inside it where the next crown
     * overlaps the trunk. 1F4-1F5 also carry a neighbour's leaves. */
    case 0x016: case 0x017: case 0x0C6: case 0x0C7:
    case 0x1F4: case 0x1F5: return VOXEL_TREE_SMALL;
    default: return -1;
    }
}

int VoxelTree_GroundMetatile(const void *secondaryTileset, int metatileId)
{
    if (metatileId >= SECONDARY_ID)
    {
        /* Dewford's crown top over the sand. */
        if (secondaryTileset == &gTileset_Dewford && metatileId == 0x239)
            return 0x124;
        return metatileId;
    }
    switch (metatileId)
    {
    case 0x1C6: case 0x1C7: return 0x00D; /* tall grass without canopy */
    case 0x1CE: case 0x1CF: return 0x001; /* ordinary grass */
    /* A small tree's canopy top, over whatever it stood in front of. The
     * fence feet under 040 have no metatile of their own: grass. */
    case 0x00E: case 0x00F: case 0x040: return 0x001;
    case 0x01D: return 0x002; /* ledge edge */
    case 0x025: return 0x00D; /* tall grass */
    case 0x02D: return 0x0A1; /* reflective water */
    case 0x035: case 0x193: return 0x170; /* calm water */
    case 0x0CE: return 0x091; /* rock wall, sand base */
    default: return metatileId;
    }
}

/* The small crown is 16:32: width 1, length 2 tiles, at the same 50 degrees
 * and sunk the same way as the large one, standing on its one-cell trunk.
 * Dewford's own small tree has its art at 64,0, the General one at 32,32. */
static void EmitSmallCell(VoxelBuilder *builder, int x, int y, bool dewford)
{
    int cx = dewford ? 64 : 32, cy = dewford ? 0 : 32;
    int tx = cx + 16, ty = cy;
    float wx = (float)x, wz = (float)y;
    const float rise = 1.532089f, run = 1.285575f;
    const float baseHeight = -0.10f;
    /* 0.15 further forward than half the large tree's: any less and the
     * leaves stand through the ground behind the trunk. Dewford's another
     * 0.15, so its crown stands over the shadow drawn on its sand. */
    float baseZ = wz + (dewford ? 0.975f : 0.825f);

    VoxelMesh_Top(builder, wx, wz, 0.0f, 0.0f,
                  TEX_U(tx), TEX_V(ty), TEX_U(tx + 16), TEX_V(ty + 16), 1.0f);
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, TEX_U(cx),      TEX_V(cy),      1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, TEX_U(cx + 16), TEX_V(cy),      1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       TEX_U(cx + 16), TEX_V(cy + 32), 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       TEX_U(cx),      TEX_V(cy + 32), 1.0f});
    builder->rounded = false;
}

static void EmitCell(VoxelBuilder *builder, int x, int y, int part, int metatileId)
{
    int col = part & 1, row = part >> 1;
    float wx = (float)x, wz = (float)y;
    float u0 = (32.0f + col * 16.0f) / VOXEL_TREE_TEXTURE_DIM;
    float u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v0 = 1.0f - row * 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v1 = v0 - 16.0f / VOXEL_TREE_TEXTURE_DIM;
    /* The crown keeps its 32:36 aspect ratio: width 2, length 2.25 tiles.
     * sin/cos(50 degrees), fixed in the world rather than camera billboarding.
     * Lower the transparent bottom margin into the ground so the visible
     * leaves overlap the trunk instead of exposing a horizontal cut. */
    const float rise = 1.723600f, run = 1.446272f;
    const float baseHeight = -0.10f;
    float baseZ = wz - row + 1.35f;
    float top = 1.0f - row * 0.5f, bottom = top - 0.5f;

    if (part == VOXEL_TREE_SMALL)
    {
        EmitSmallCell(builder, x, y, metatileId >= SECONDARY_ID);
        return;
    }
    VoxelMesh_Top(builder, wx, wz, 0.0f, 0.0f, u0, v0, u1, v1, 1.0f);

    u0 = col * 16.0f / VOXEL_TREE_TEXTURE_DIM;
    u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    v0 = 1.0f - row * 18.0f / VOXEL_TREE_TEXTURE_DIM;
    v1 = v0 - 18.0f / VOXEL_TREE_TEXTURE_DIM;
    /* A crown card: lit as the rounded crown it stands for. */
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + top * rise,    baseZ - top * run,    u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + top * rise,    baseZ - top * run,    u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + bottom * rise, baseZ - bottom * run, u1, v1, 1.0f},
        &(VoxelVertex){wx,        baseHeight + bottom * rise, baseZ - bottom * run, u0, v1, 1.0f});
    builder->rounded = false;
}

void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int x0, int y0, int x1, int y1)
{
    if (!VoxelWorld_UsesTreeSprites(inst))
        return;
    if (x0 < inst->originX) x0 = inst->originX;
    if (y0 < inst->originY) y0 = inst->originY;
    if (x1 > inst->originX + inst->width) x1 = inst->originX + inst->width;
    if (y1 > inst->originY + inst->height) y1 = inst->originY + inst->height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int metatileId = VoxelWorld_GetMetatileId(x, y);
            int part = VoxelTree_Part(inst->secondaryTileset, metatileId);
            if (part >= 0)
            {
                builder->lift = VoxelRelief_CellLift(inst, x, y);
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                EmitCell(builder, x, y, part, metatileId);
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
        }
}

void VoxelTree_EmitBorder(VoxelBuilder *builder, int x0, int y0, int x1, int y1)
{
    const VoxelMapInstance *current = VoxelWorld_Instance(0);

    if (!VoxelWorld_UsesTreeSprites(current))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int metatileId, part;
            if (VoxelWorld_GetInstanceAt(x, y) != NULL)
                continue;
            metatileId = VoxelWorld_BorderMetatile(x, y);
            part = VoxelTree_Part(current->secondaryTileset, metatileId);
            if (part >= 0)
                EmitCell(builder, x, y, part, metatileId);
        }
}
