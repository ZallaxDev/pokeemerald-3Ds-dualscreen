#include <stddef.h>
#include "voxel_tree.h"
#include "voxel_lighting.h"
#include "voxel_relief.h"

int VoxelTree_Part(int metatileId)
{
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

int VoxelTree_GroundMetatile(int metatileId)
{
    switch (metatileId)
    {
    case 0x1C6: case 0x1C7: return 0x00D; /* tall grass without canopy */
    case 0x1CE: case 0x1CF: return 0x001; /* ordinary grass */
    /* Flowers stand up as cards (EmitGrass): the grass they grow in. */
    case VOXEL_FLOWER_METATILE: return 0x001;
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
 * and sunk the same way as the large one, standing on its one-cell trunk. */
static void EmitSmallCell(VoxelBuilder *builder, int x, int y)
{
    float wx = (float)x, wz = (float)y;
    const float rise = 1.532089f, run = 1.285575f;
    const float baseHeight = -0.10f;
    /* 0.15 further forward than half the large tree's: any less and the
     * leaves stand through the ground behind the trunk. */
    float baseZ = wz + 0.825f;

    VoxelMesh_Top(builder, wx, wz, 0.0f, 0.0f,
                  48.0f / VOXEL_TREE_TEXTURE_DIM, 0.5f, 1.0f, 0.25f, 1.0f);
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, 0.5f,  0.5f, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, 0.75f, 0.5f, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       0.75f, 0.0f, 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       0.5f,  0.0f, 1.0f});
    builder->rounded = false;
}

static void EmitCell(VoxelBuilder *builder, int x, int y, int part)
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
        EmitSmallCell(builder, x, y);
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

/*
 * Grass: the ground keeps its drawing and rows of tufts stand on it, cards
 * leaning north like a crown's so the camera meets them face on. Tall grass
 * is half a tile tall: a walker's card stands on the middle of its cell,
 * between the rows, and the row in front hides its feet as the grass does on
 * the GBA. Long grass is a tile tall and hides them to the waist. Every other
 * row mirrored, so a field does not read as columns. Flowers stand the same
 * way, on plain grass (VoxelTree_GroundMetatile): the drawing's sway is the
 * tileset's animation, which a card cut from a picture of its own cannot
 * show, so the wind moves them instead - the top of every card carries a
 * negative shade, which is what voxel.v.pica sways.
 */
static void EmitGrass(VoxelBuilder *builder, int x, int y, VoxelGrass kind)
{
    /* The tufts in the tree texture (gen_voxel_trees.py), 16 pixels wide:
     * where each is, and how many rows of pixels tall. */
    static const struct
    {
        unsigned char x, y, rows;
    } tufts[] = {
        [VOXEL_GRASS_TALL] = {0, 44, 10},
        [VOXEL_GRASS_LONG] = {16, 44, 16},
        [VOXEL_GRASS_ASH] = {48, 50, 10},
        [VOXEL_GRASS_FLOWER] = {0, 54, 10},
    };
    const float u0 = (float)tufts[kind].x / VOXEL_TREE_TEXTURE_DIM;
    const float u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    const float v0 = 1.0f - (float)tufts[kind].y / VOXEL_TREE_TEXTURE_DIM;
    const float v1 = v0 - (float)tufts[kind].rows / VOXEL_TREE_TEXTURE_DIM;
    /* The card at 60 degrees from the ground (sin, cos), its foot a little
     * under it so no gap shows below the blades. */
    const float length = tufts[kind].rows / 16.0f;
    const float rise = 0.866025f * length, run = 0.5f * length, baseHeight = -0.04f;
    float wx = (float)x, wz = (float)y;
#if CTR_VOXEL_LIGHTING
    /* Lit as one object, like a sign: a sample a corner for two cards a cell
     * of a whole field was most of a chunk's build. */
    float constant = builder->lightingConstant;

    if (builder->lighting && constant < 0.0f)
        builder->lightingConstant = VoxelLighting_Sample(wx + 0.5f, builder->lift + 0.25f, wz + 0.5f);
#endif
    builder->rounded = true;
    for (int row = 0; row < VOXEL_GRASS_TUFT_ROWS; ++row)
    {
        float baseZ = wz + 0.45f + row * 0.5f;
        bool mirror = ((x + y + row) & 1) != 0;
        float ua = mirror ? u1 : u0, ub = mirror ? u0 : u1;

        VoxelBuilder_Quad(builder,
            &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, ua, v0, -1.0f},
            &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, ub, v0, -1.0f},
            &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       ub, v1, 1.0f},
            &(VoxelVertex){wx,        baseHeight,        baseZ,       ua, v1, 1.0f});
    }
    builder->rounded = false;
#if CTR_VOXEL_LIGHTING
    builder->lightingConstant = constant;
#endif
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
            int part = VoxelTree_Part(VoxelWorld_GetMetatileId(x, y));
            VoxelGrass grass = part >= 0 ? VOXEL_GRASS_NONE : VoxelWorld_Grass(x, y);

            if (part >= 0 || grass != VOXEL_GRASS_NONE)
            {
                builder->lift = VoxelRelief_CellLift(inst, x, y);
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                if (part >= 0)
                    EmitCell(builder, x, y, part);
                else
                    EmitGrass(builder, x, y, grass);
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
        }
}

void VoxelTree_EmitBorder(VoxelBuilder *builder, int x0, int y0, int x1, int y1)
{
    if (!VoxelWorld_UsesTreeSprites(VoxelWorld_Instance(0)))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int part;
            if (VoxelWorld_GetInstanceAt(x, y) != NULL)
                continue;
            part = VoxelTree_Part(VoxelWorld_BorderMetatile(x, y));
            if (part >= 0)
                EmitCell(builder, x, y, part);
        }
}
