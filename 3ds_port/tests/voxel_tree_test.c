/* Exercise the real mesh emitters with a tiny world crossing four chunks. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "voxel_tree.h"
#include "voxel_atlas.h"
#include "voxel_regions.h"

static VoxelMapInstance sMap = {.originX = 7, .originY = 7, .width = 2, .height = 2};
static bool sGeneral = true;
static int sTiles[] = {0x1D6, 0x1D7, 0x1E6, 0x1E7};
static VoxelAtlasMap sAtlas;
static VoxelVertex sWhole[1024], sParts[1024];

bool VoxelWorld_UsesTreeSprites(const VoxelMapInstance *inst)
{
    return inst == &sMap && sGeneral;
}

const VoxelMapInstance *VoxelWorld_Instance(unsigned index)
{
    return index == 0 ? &sMap : NULL;
}

const VoxelMapInstance *VoxelWorld_GetInstanceAt(int x, int y)
{
    return x >= sMap.originX && x < sMap.originX + 2
        && y >= sMap.originY && y < sMap.originY + 2 ? &sMap : NULL;
}

int VoxelWorld_GetMetatileId(int x, int y)
{
    assert(VoxelWorld_GetInstanceAt(x, y) != NULL);
    return sTiles[(y - sMap.originY) * 2 + x - sMap.originX];
}

VoxelVisualShape VoxelWorld_ClassifyTile(int x, int y)
{
    return VoxelWorld_GetInstanceAt(x, y) ? VOXEL_SHAPE_FLAT : VOXEL_SHAPE_VOID;
}

int VoxelWorld_BorderMetatile(int x, int y)
{
    const int pattern[] = {0x1D4, 0x1D5, 0x1DC, 0x1DD};
    return pattern[((x - sMap.originX) & 1) + 2 * ((y - sMap.originY) & 1)];
}

void VoxelWorld_GetPlayerWorldCoords(float *x, float *z) { *x = *z = 0; }

unsigned VoxelRegions_RoleAt(unsigned layoutId, int x, int y)
{
    (void)layoutId; (void)x; (void)y;
    return VOXEL_ROLE_TREE;
}

void VoxelAtlas_SlotUV(unsigned slot, float *u0, float *v0, float *u1, float *v1)
{
    *u0 = (slot % 32) / 32.0f;
    *v0 = 1.0f - (slot / 32) / 16.0f;
    *u1 = *u0 + 1.0f / 32;
    *v1 = *v0 - 1.0f / 16;
}

void VoxelAtlas_SolidUV(VoxelSolidColor color, float *u0, float *v0, float *u1, float *v1)
{
    VoxelAtlas_SlotUV((unsigned)color, u0, v0, u1, v1);
}

static void Init(VoxelBuilder *b, VoxelVertex *v)
{
    VoxelBuilder_Init(b, v, 1024);
    VoxelBuilder_SetAtlas(b, &sAtlas);
    VoxelBuilder_SetOrigin(b, sMap.originX, sMap.originY);
    VoxelMesh_BeginWindow(sMap.originX - 2, sMap.originY - 2,
                          sMap.originX + 4, sMap.originY + 4);
}

int main(void)
{
    VoxelBuilder whole, parts;
    unsigned ground = 0, crown = 0;

    Init(&whole, sWhole);
    VoxelMesh_EmitInstance(&whole, &sMap, 7, 7, 9, 9);
    assert(whole.count == 0); /* no original ground artwork or extruded region */
    VoxelTree_EmitInstance(&whole, &sMap, 7, 7, 9, 9);
    assert(whole.count == 48 && whole.dropped == 0);
    for (unsigned i = 0; i < whole.count; ++i)
    {
        const VoxelVertex *v = &whole.vertices[i];
        assert(isfinite(v->x) && isfinite(v->y) && isfinite(v->z));
        assert(v->x >= 0 && v->x <= 2 && v->v >= 0 && v->v <= 1);
        if (v->y == 0)
        {
            ++ground;
            assert(v->u >= 0.5f && v->u <= 1 && v->z >= 0 && v->z <= 2);
        }
        else
        {
            ++crown;
            assert(v->u >= 0 && v->u <= 0.5f);
        }
    }
    assert(ground == 24 && crown == 24);
    assert(sWhole[6].z < sWhole[8].z && sWhole[6].y > sWhole[8].y);

    Init(&parts, sParts);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            VoxelTree_EmitInstance(&parts, &sMap, x * 8, y * 8, (x + 1) * 8, (y + 1) * 8);
    assert(parts.count == whole.count);
    assert(memcmp(sWhole, sParts, whole.count * sizeof(VoxelVertex)) == 0);

    /* Streaming emits one cell per slice; preserve vertices and order. */
    Init(&parts, sParts);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            VoxelTree_EmitInstance(&parts, &sMap, x + 7, y + 7, x + 8, y + 8);
    assert(parts.count == whole.count);
    assert(memcmp(sWhole, sParts, whole.count * sizeof(VoxelVertex)) == 0);

    /* A map crossing changes the origin, not the cached local mesh. */
    sMap.originX = -3;
    sMap.originY = -5;
    Init(&parts, sParts);
    VoxelTree_EmitInstance(&parts, &sMap, -3, -5, -1, -3);
    assert(parts.count == whole.count);
    for (unsigned i = 0; i < whole.count; ++i)
    {
        assert(fabsf(sWhole[i].x - sParts[i].x) < 0.00001f);
        assert(fabsf(sWhole[i].y - sParts[i].y) < 0.00001f);
        assert(fabsf(sWhole[i].z - sParts[i].z) < 0.00001f);
    }

    /* Border excludes every mapped cell, including at negative coordinates. */
    Init(&parts, sParts);
    VoxelMesh_BeginWindow(-5, -7, 1, 1);
    VoxelMesh_EmitBorder(&parts, -5, -7, 1, 1);
    assert(parts.count == 0); /* no raised belt under the replacement */
    VoxelTree_EmitBorder(&parts, -5, -7, 1, 1);
    assert(parts.count == (6 * 8 - 4) * 12 && parts.dropped == 0);

    unsigned borderCount = parts.count;
    memcpy(sWhole, sParts, borderCount * sizeof(VoxelVertex));
    Init(&parts, sParts);
    for (int y = -7; y < 1; ++y)
        for (int x = -5; x < 1; ++x)
            VoxelTree_EmitBorder(&parts, x, y, x + 1, y + 1);
    assert(parts.count == borderCount);
    assert(memcmp(sWhole, sParts, borderCount * sizeof(VoxelVertex)) == 0);

    sGeneral = false;
    Init(&parts, sParts);
    VoxelTree_EmitInstance(&parts, &sMap, -3, -5, -1, -3);
    VoxelTree_EmitBorder(&parts, -5, -7, 1, 1);
    assert(parts.count == 0);
    assert(VoxelTree_Part(0x026) == -1); /* secret base tree */
    assert(VoxelTree_Part(0x1D9) == -1); /* rock */

    /* Grass fringes lose the old canopy and never acquire region walls. */
    sGeneral = true;
    sTiles[0] = 0x1CE; sTiles[1] = 0x1CF;
    sTiles[2] = 0x1C6; sTiles[3] = 0x1C7;
    sAtlas.slotOf[0x001] = 1;
    sAtlas.slotOf[0x00D] = 2;
    Init(&parts, sParts);
    VoxelMesh_EmitInstance(&parts, &sMap, -3, -5, -1, -3);
    VoxelTree_EmitInstance(&parts, &sMap, -3, -5, -1, -3);
    assert(parts.count == 24 && parts.uncovered == 0);
    for (unsigned i = 0; i < parts.count; ++i)
        assert(parts.vertices[i].y == 0);

    /* Small trees: the canopy top keeps its ground; each trunk cell - edge of
     * a wood or inside it - lays the trunk and stands the whole crown on it. */
    sTiles[0] = 0x00E; sTiles[1] = 0x00F;
    sTiles[2] = 0x016; sTiles[3] = 0x0C7;
    assert(VoxelTree_GroundMetatile(0x00E) == 0x001);
    assert(VoxelTree_Part(0x016) == VOXEL_TREE_SMALL);
    assert(VoxelTree_Part(0x1EC) == 2 && VoxelTree_Part(0x1ED) == 3);
    Init(&parts, sParts);
    VoxelMesh_EmitInstance(&parts, &sMap, -3, -5, -1, -3);
    assert(parts.count == 12 && parts.uncovered == 0); /* two grass tops */
    for (unsigned i = 0; i < parts.count; ++i)
        assert(parts.vertices[i].y == 0 && parts.vertices[i].z <= 1);
    parts.count = 0;
    VoxelTree_EmitInstance(&parts, &sMap, -3, -5, -1, -3);
    assert(parts.count == 24 && parts.dropped == 0);
    for (unsigned i = 0; i < parts.count; ++i)
    {
        const VoxelVertex *v = &parts.vertices[i];
        assert(v->u >= 0.5f && v->u <= 1 && v->v >= 0 && v->v <= 0.5f);
        if (v->y == 0)
            assert(v->u >= 0.75f && v->v >= 0.25f && v->z >= 1 && v->z <= 2);
        else
            assert(v->u <= 0.75f);
    }
    /* Leaning north from its trunk into the cell above, as tall as it is long. */
    assert(parts.vertices[6].z < parts.vertices[8].z && parts.vertices[6].y > parts.vertices[8].y);
    assert(parts.vertices[6].z < 1 && fabsf(parts.vertices[6].y - 1.432089f) < 0.0001f);

    puts("PASS voxel trees: ground/crown, chunk seams, map origins, border, tileset scope, grass fringes, small trees");
    return 0;
}
