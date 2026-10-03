/* Real lighting/mesh code against a small mutable world, no SDK or GPU. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "voxel_lighting.h"
#include "voxel_atlas.h"
#include "voxel_regions.h"
#include "voxel_building.h"

static VoxelMapInstance sMaps[2] = {
    {.layoutId = 1, .width = 4, .height = 16},
    {.layoutId = 2, .originX = 4, .width = 12, .height = 16}
};
/* A modelled building three tiles tall, across the seam of two maps. */
#define HOUSE_TOP 3.0f
static bool sHousePresent = true, sTreePresent;
/* A railing along a row: layout 2's cell (8, 10), world (12, 10), its line
 * the cell's two southern pixel rows, 0.75 tall. */
static bool sRailPresent;
static const uint16_t sRailMask[16] = {[14] = 0xFFFF, [15] = 0xFFFF};
static int sOffsetX, sOffsetZ;

unsigned VoxelWorld_InstanceCount(void) { return 2; }
const VoxelMapInstance *VoxelWorld_Instance(unsigned i) { return i < 2 ? &sMaps[i] : NULL; }
const VoxelMapInstance *VoxelWorld_GetInstanceAt(int x, int z)
{
    for (unsigned i = 0; i < 2; ++i)
        if (x >= sMaps[i].originX && x < sMaps[i].originX + sMaps[i].width
         && z >= sMaps[i].originY && z < sMaps[i].originY + sMaps[i].height)
            return &sMaps[i];
    return NULL;
}
bool VoxelWorld_UsesTreeSprites(const VoxelMapInstance *inst) { return inst != NULL; }
int VoxelWorld_GetMetatileId(int x, int z)
{
    int lx = x - sOffsetX, lz = z - sOffsetZ;
    if (VoxelWorld_GetInstanceAt(x, z) == NULL) return -1;
    if (sHousePresent && lx >= 4 && lx < 6 && lz >= 4 && lz < 6) return 2;
    if (sTreePresent && lx >= 8 && lx < 10 && lz >= 4 && lz < 6)
    {
        const int parts[] = {0x1D4, 0x1D5, 0x1DC, 0x1DD};
        return parts[(lz - 4) * 2 + lx - 8];
    }
    return 1;
}
unsigned VoxelRegions_RoleAt(unsigned layoutId, int x, int z)
{
    (void)layoutId; (void)x; (void)z;
    return VOXEL_ROLE_FLOOR;
}
/* The house stands in layout 2's first two columns, rows 4-5 - world 4-5. */
bool VoxelBuildings_CellAt(const VoxelMapInstance *inst, int x, int y,
                           int *groundMetatile, float *top)
{
    int lx = x - inst->originX, ly = y - inst->originY;

    if (sRailPresent && inst->layoutId == 2 && lx == 8 && ly == 10)
    {
        if (groundMetatile != NULL) *groundMetatile = 1;
        if (top != NULL) *top = 0.75f;
        return true;
    }
    if (!sHousePresent || inst->layoutId != 2 || lx < 0 || lx >= 2 || ly < 4 || ly >= 6)
        return false;
    if (groundMetatile != NULL) *groundMetatile = 1;
    if (top != NULL) *top = HOUSE_TOP;
    return true;
}
const uint16_t *VoxelBuildings_Footprint(const VoxelMapInstance *inst, int x, int y)
{
    if (sRailPresent && inst->layoutId == 2 && x - inst->originX == 8 && y - inst->originY == 10)
        return sRailMask;
    return NULL;
}
/* Raised to disable the ray ceiling, for the equivalence oracle. */
static float sCeilingOverride;
extern bool gVoxelLightingStepEveryPoint;
float VoxelBuildings_MaxTop(void)
{
    if (sCeilingOverride > 0.0f)
        return sCeilingOverride;
    return sHousePresent ? HOUSE_TOP : 0.0f;
}
float VoxelBuildings_LayoutTop(const VoxelMapInstance *inst)
{
    (void)inst;
    return VoxelBuildings_MaxTop();
}
VoxelVisualShape VoxelWorld_ClassifyTile(int x, int z)
{
    if (VoxelWorld_GetInstanceAt(x, z) == NULL) return VOXEL_SHAPE_VOID;
    if (x - sOffsetX == 2 && z - sOffsetZ == 2) return VOXEL_SHAPE_WATER;
    return VOXEL_SHAPE_FLAT;
}
uint32_t VoxelWorld_BlockHash(int x0, int z0, int x1, int z1)
{
    uint32_t hash = 2166136261u;
    for (int z = z0; z < z1; ++z)
        for (int x = x0; x < x1; ++x)
            hash = (hash ^ (uint32_t)VoxelWorld_GetMetatileId(x, z)) * 16777619u;
    return hash;
}
int VoxelWorld_BorderMetatile(int x, int z) { (void)x; (void)z; return -1; }
void VoxelWorld_GetPlayerWorldCoords(float *x, float *z) { *x = *z = 0; }
void VoxelAtlas_SlotUV(unsigned slot, float *u0, float *v0, float *u1, float *v1)
{
    *u0 = (slot % 32) / 32.0f; *u1 = *u0 + 1.0f / 32;
    *v0 = 1.0f - (slot / 32) / 16.0f; *v1 = *v0 - 1.0f / 16;
}
void VoxelAtlas_SolidUV(VoxelSolidColor color, float *u0, float *v0, float *u1, float *v1)
{
    VoxelAtlas_SlotUV((unsigned)color, u0, v0, u1, v1);
}

static void Ground(VoxelBuilder *builder, int x, int z)
{
    VoxelMesh_Top(builder, (float)x, (float)z, 0, 0, 0, 1, 1, 0, 1);
}

int main(void)
{
    VoxelVertex storage[2048];
    VoxelBuilder builder;
    VoxelBuilder_Init(&builder, storage, 2048);
    builder.lighting = true;
    VoxelMesh_BeginWindow(0, 0, 8, 8);
    Ground(&builder, 3, 3);
#if CTR_VOXEL_LIGHTING
    /* The sun is in the northwest: a building across a map/chunk boundary
     * casts southeast, but not northwest or onto its own roof. Shadows must
     * work before the caster's chunk has been meshed. */
    assert(VoxelLighting_Sample(6.5f, 0, 6.5f) < 0.8f);
    assert(VoxelLighting_Sample(3.5f, 0, 3.5f) == 1.0f);
    assert(VoxelLighting_Sample(4.5f, 3, 4.5f) == 1.0f);
    assert(VoxelLighting_Sample(3.9f, 0, 4.5f) > 0.9f);
    assert(VoxelLighting_Sample(3.9f, 0, 4.5f) < 1.0f); /* AO on the sunny side */
    /* Faces: the sun's, from the northwest - south and east in the ambient. */
    assert(VoxelLighting_Face(0, 1, 0) == 1.0f);
    assert(VoxelLighting_Face(0, 0, 1) == VOXEL_AMBIENT);
    assert(VoxelLighting_Face(1, 0, 0) == VOXEL_AMBIENT);
    assert(VoxelLighting_Face(-1, 0, 0) > 0.9f && VoxelLighting_Face(0, 0, -1) < 0.9f);
    /* Compare a cache-cold oracle with a densely reused sample grid, including
     * nearby but unequal coordinates sharing buckets and different heights. */
    float reference[3][25][25];
    for (int h = 0; h < 3; ++h)
        for (int z = 0; z < 25; ++z)
            for (int x = 0; x < 25; ++x)
            {
                VoxelLighting_Reset();
                reference[h][z][x] = VoxelLighting_Sample(x * 0.31f - 1, h * 0.4f, z * 0.31f - 1);
            }
    VoxelLighting_Reset();
    for (int pass = 0; pass < 2; ++pass)
        for (int h = 0; h < 3; ++h)
            for (int z = 0; z < 25; ++z)
                for (int x = 0; x < 25; ++x)
                    assert(reference[h][z][x] == VoxelLighting_Sample(x * 0.31f - 1, h * 0.4f, z * 0.31f - 1));
    /* Adjacent half-tile ground lattices should reuse their exact samples. */
    VoxelLighting_Reset();
    unsigned latticeStart = VoxelLighting_Rays();
    for (int pass = 0; pass < 2; ++pass)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                for (int dz = 0; dz < 3; ++dz)
                    for (int dx = 0; dx < 3; ++dx)
                        (void)VoxelLighting_Sample(x + dx * 0.5f, 0, z + dz * 0.5f);
    assert(VoxelLighting_Rays() - latticeStart <= 1200);
    printf("Lighting ground lattice: %u rays for 1089 unique points, 4608 queries\n",
           VoxelLighting_Rays() - latticeStart);
    /* Rays stop once they climb past the tallest caster on screen. With the
     * ceiling lifted out of reach every ray runs its full length, and must
     * reach the same answers. */
    sCeilingOverride = 200.0f;
    for (int h = 0; h < 3; ++h)
        for (int z = 0; z < 25; ++z)
            for (int x = 0; x < 25; ++x)
            {
                VoxelLighting_Reset();
                assert(reference[h][z][x] == VoxelLighting_Sample(x * 0.31f - 1, h * 0.4f, z * 0.31f - 1));
            }
    sCeilingOverride = 0.0f;
    VoxelLighting_Reset();
    /* Rays jump from cell to cell instead of visiting every point: the same
     * answers as the point-by-point march, over the whole reference grid. */
    gVoxelLightingStepEveryPoint = true;
    for (int h = 0; h < 3; ++h)
        for (int z = 0; z < 25; ++z)
            for (int x = 0; x < 25; ++x)
            {
                VoxelLighting_Reset();
                assert(reference[h][z][x] == VoxelLighting_Sample(x * 0.31f - 1, h * 0.4f, z * 0.31f - 1));
            }
    sTreePresent = true;
    for (int pass = 0; pass < 2; ++pass)
        for (int h = 0; h < 3; ++h)
            for (int z = 0; z < 40; ++z)
                for (int x = 0; x < 50; ++x)
                {
                    float got;

                    gVoxelLightingStepEveryPoint = false;
                    VoxelLighting_Reset();
                    got = VoxelLighting_Sample(x * 0.27f - 1, h * 0.37f, z * 0.19f - 1);
                    gVoxelLightingStepEveryPoint = true;
                    VoxelLighting_Reset();
                    assert(got == VoxelLighting_Sample(x * 0.27f - 1, h * 0.37f, z * 0.19f - 1));
                }
    sTreePresent = false;
    gVoxelLightingStepEveryPoint = false;
    VoxelLighting_Reset();
    assert(builder.count == 24 && builder.dropped == 0);
    for (unsigned i = 0; i < builder.count; ++i)
    {
        assert(storage[i].shade >= 0.60f && storage[i].shade <= 1.0f);
        assert(storage[i].u >= 0 && storage[i].u <= 1);
        assert(storage[i].v >= 0 && storage[i].v <= 1);
    }

    /* The receiving chunk's signature changes when a caster in its neighbour
     * changes, even though none of the receiver's own tiles changed. */
    uint32_t hash = VoxelLighting_Hash(0, 0, 4, 4);
    uint32_t local = VoxelWorld_BlockHash(0, 0, 4, 4);
    sHousePresent = false;
    VoxelLighting_Reset();
    assert(local == VoxelWorld_BlockHash(0, 0, 4, 4));
    assert(hash != VoxelLighting_Hash(0, 0, 4, 4));
    assert(VoxelLighting_Sample(6.5f, 0, 6.5f) == 1.0f);
    sHousePresent = true;
    VoxelLighting_Reset();

    /* Rebase into negative world coordinates: cached map-local geometry and
     * dependency hashes must remain the same. */
    VoxelVertex before[24];
    memcpy(before, storage, sizeof(before));
    sOffsetX = -20; sOffsetZ = -30;
    for (unsigned i = 0; i < 2; ++i)
    {
        sMaps[i].originX += sOffsetX;
        sMaps[i].originY += sOffsetZ;
    }
    assert(hash == VoxelLighting_Hash(-20, -30, -16, -26));
    /* The world moved: the renderer resets the caches on every rebase. */
    VoxelLighting_Reset();
    VoxelBuilder_Init(&builder, storage, 2048);
    builder.lighting = true;
    VoxelBuilder_SetOrigin(&builder, sOffsetX, sOffsetZ);
    VoxelMesh_BeginWindow(-20, -30, -12, -22);
    Ground(&builder, -17, -27);
    assert(builder.count == 24);
    for (unsigned i = 0; i < 24; ++i)
    {
        assert(fabsf(storage[i].x - before[i].x) < 0.00001f);
        assert(fabsf(storage[i].z - before[i].z) < 0.00001f);
        assert(fabsf(storage[i].shade - before[i].shade) < 0.00001f);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        sMaps[i].originX -= sOffsetX;
        sMaps[i].originY -= sOffsetZ;
    }
    sOffsetX = sOffsetZ = 0;
    VoxelLighting_Reset();

    /* A different meshing window cannot change a seam's lighting. */
    VoxelBuilder_Init(&builder, storage, 2048);
    builder.lighting = true;
    VoxelMesh_BeginWindow(3, 3, 4, 4);
    Ground(&builder, 3, 3);
    assert(memcmp(before, storage, sizeof(before)) == 0);

    /* Low capacity falls back to the original quad instead of losing ground. */
    VoxelBuilder_Init(&builder, storage, 6);
    builder.lighting = true;
    Ground(&builder, 3, 3);
    assert(builder.count == 6 && builder.dropped == 0);

    /* Round tree proxies shade the floor southeast of the crown's centre and
     * disappear with the live tiles. */
    sTreePresent = true;
    VoxelLighting_Reset();
    assert(VoxelLighting_Sample(9.74f, 0, 5.13f) < 0.8f);
    sTreePresent = false;
    VoxelLighting_Reset();
    assert(VoxelLighting_Sample(9.74f, 0, 5.13f) == 1.0f);

    /* Contact shadows clipped at a water/land corner lie on each surface:
     * water is flush with the ground (00485c4fe), so both at the same
     * height; empty space and building footprints receive no triangles. */
    VoxelBuilder_Init(&builder, storage, VOXEL_CONTACT_VERTICES);
    VoxelLighting_Contact(&builder, 3.05f, 3.0f);
    assert(builder.count != 0 && builder.count <= VOXEL_CONTACT_VERTICES && builder.dropped == 0);
    bool water = false, land = false;
    for (unsigned i = 0; i < builder.count; ++i)
    {
        assert(isfinite(storage[i].x) && isfinite(storage[i].z));
        assert(fabsf(storage[i].y - 0.012f) < 0.00001f);
        if (storage[i].x >= 2.0f && storage[i].x <= 3.0f && storage[i].z >= 2.0f && storage[i].z <= 3.0f)
            water = true;
        else
            land = true;
    }
    assert(water && land);
    VoxelBuilder_Init(&builder, storage, VOXEL_CONTACT_VERTICES);
    VoxelLighting_Contact(&builder, 5, 5);
    VoxelLighting_Contact(&builder, -5, -5);
    assert(builder.count == 0);

    /* Sweep sub-tile positions to exercise clipping and the reserved bound. */
    for (int z = 0; z <= 20; ++z)
        for (int x = 0; x <= 20; ++x)
        {
            VoxelBuilder_Init(&builder, storage, VOXEL_CONTACT_VERTICES);
            VoxelLighting_Contact(&builder, 7.0f + x * 0.05f, 7.0f + z * 0.05f);
            assert(builder.count <= VOXEL_CONTACT_VERTICES && builder.dropped == 0);
        }

    /* A railing casts from its line, not from its cell: the ground just
     * south-east of the line is in its shadow, the rest of its own cell -
     * north of the line - in the sun. */
    sRailPresent = true;
    VoxelLighting_Reset();
    assert(VoxelLighting_Sample(12.9f, 0, 11.1f) < 0.8f);
    assert(VoxelLighting_Sample(12.9f, 0, 10.5f) > 0.9f);
    sRailPresent = false;
    VoxelLighting_Reset();

    sMaps[1].indoor = true;
    VoxelLighting_Reset();
    assert(VoxelLighting_Sample(3.5f, 0, 3.5f) == 1.0f);
    VoxelBuilder_Init(&builder, storage, VOXEL_CONTACT_VERTICES);
    VoxelLighting_Contact(&builder, 7, 7);
    assert(builder.count == 0);
    puts("PASS lighting on: sample cache equivalence, ray ceiling, cell skipping, sun, AO, neighbour invalidation, rebase, seams, capacity, live trees, contact clipping, railing footprints, interiors");
#else
    assert(builder.count == 6 && builder.dropped == 0);
    for (unsigned i = 0; i < builder.count; ++i) assert(storage[i].shade == 1.0f);
    puts("PASS lighting off: original quad and colours");
#endif
    return 0;
}
