/* Integration against the generated, ROM-derived building pack. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
enum
{
    FAIL_NONE,
    FAIL_CALLOC,
    FAIL_MALLOC,
    FAIL_OPEN
};
static int sFailure;
static unsigned sLiveBlocks, sLiveFiles;
static void *TrackedMalloc(size_t size)
{
    if (sFailure == FAIL_MALLOC)
        return NULL;
    void *p = malloc(size);
    if (p)
        ++sLiveBlocks;
    return p;
}
static void *TrackedCalloc(size_t count, size_t size)
{
    if (sFailure == FAIL_CALLOC)
        return NULL;
    void *p = calloc(count, size);
    if (p)
        ++sLiveBlocks;
    return p;
}
static void *TrackedRealloc(void *p, size_t size)
{
    int wasNull = p == NULL;
    void *q = realloc(p, size);
    if (wasNull && q)
        ++sLiveBlocks;
    return q;
}
static void TrackedFree(void *p)
{
    if (p)
    {
        assert(sLiveBlocks);
        --sLiveBlocks;
    }
    free(p);
}
static FILE *TrackedOpen(const char *name, const char *mode)
{
    if (sFailure == FAIL_OPEN)
        return NULL;
    FILE *f = fopen(name, mode);
    if (f)
        ++sLiveFiles;
    return f;
}
static int TrackedClose(FILE *f)
{
    assert(sLiveFiles);
    --sLiveFiles;
    return fclose(f);
}
#define malloc TrackedMalloc
#define calloc TrackedCalloc
#define realloc TrackedRealloc
#define free TrackedFree
#define fopen TrackedOpen
#define fclose TrackedClose
#define VOXEL_HOST_FILES
#define PORT_LOG(...) ((void)0)
#include "../src/voxel/voxel_building.c"
float VoxelRelief_Base(const VoxelMapInstance *m)
{
    (void)m;
    return 0.5f;
}
float VoxelRelief_CellLift(const VoxelMapInstance *m, int x, int y)
{
    (void)m;
    (void)x;
    (void)y;
    return 0.25f;
}
float VoxelRelief_CellShift(const VoxelMapInstance *m, int x, int y)
{
    (void)m;
    (void)x;
    (void)y;
    return -0.125f;
}
void VoxelBuilder_Tri(VoxelBuilder *b, const VoxelVertex *a, const VoxelVertex *c,
                      const VoxelVertex *d)
{
    const VoxelVertex *v[3] = {a, c, d};
    assert(b->count + 3 <= b->capacity);
    for (unsigned i = 0; i < 3; ++i)
    {
        VoxelVertex t = *v[i];
        t.x -= b->originX;
        t.z += b->shift - b->originZ;
        t.y += b->base + b->lift;
        b->vertices[b->count++] = t;
    }
}
static void CheckSurfaceAlignment(unsigned model)
{
    const BuildingModel *m = &sModels[model];
    const struct NightModel *night = &sNightModels[model];
    for (unsigned sample = 0; sample < night->count; sample += 3)
    {
        const VoxelVertex *light = &sNightVertices[night->first + sample];
        bool matched = false;
        for (unsigned k = 0; k + 2 < m->vertexCount; k += 3)
        {
            const VoxelVertex *v = &sVertices[m->firstVertex + k];
            if (fabsf(v[0].z + 1.0f / 512.0f - light->z) > 0.0001f ||
                fabsf(v[0].z - v[1].z) > 0.001f || fabsf(v[0].z - v[2].z) > 0.001f)
                continue;
            float u1 = v[1].u - v[0].u, u2 = v[2].u - v[0].u;
            float v1 = v[1].v - v[0].v, v2 = v[2].v - v[0].v, det = u1 * v2 - u2 * v1;
            if (fabsf(det) < 0.001f)
                continue;
            float u = light->u - v[0].u, w = light->v - v[0].v;
            float b = (u * v2 - u2 * w) / det, c = (u1 * w - u * v1) / det;
            if (b < -0.0001f || c < -0.0001f || b + c > 1.0001f)
                continue;
            float y = v[0].y + b * (v[1].y - v[0].y) + c * (v[2].y - v[0].y);
            if (fabsf(light->y - y) < 0.0001f)
            {
                matched = true;
                break;
            }
        }
        assert(matched);
    }
}

static bool LightCovers(unsigned model, float u, float v)
{
    const struct NightModel *n = &sNightModels[model];
    for (unsigned k = 0; k + 2 < n->count; k += 3)
    {
        const VoxelVertex *p = &sNightVertices[n->first + k];
        float u1 = p[1].u - p[0].u, u2 = p[2].u - p[0].u;
        float v1 = p[1].v - p[0].v, v2 = p[2].v - p[0].v, det = u1 * v2 - u2 * v1;
        if (fabsf(det) < 0.0001f)
            continue;
        float du = u - p[0].u, dv = v - p[0].v;
        float b = (du * v2 - u2 * dv) / det, c = (u1 * dv - du * v1) / det;
        if (b >= 0 && c >= 0 && b + c <= 1.0001f)
            return true;
    }
    return false;
}

static void CheckEmissionEdges(void)
{
    unsigned chosen = 0;
    while (chosen < sPlacementCount &&
           !sNightModels[sPageModels[sPlacements[chosen].pageModel].model].count)
        ++chosen;
    assert(chosen < sPlacementCount);
    BuildingPlacement saved = sPlacements[0];
    unsigned count = sPlacementCount;
    sPlacements[0] = sPlacements[chosen];
    sPlacementCount = 1;
    sLastLayout = -1;
    const BuildingPlacement *p = &sPlacements[0];
    const BuildingModel *model = &sModels[sPageModels[p->pageModel].model];
    VoxelMapInstance map = {
        .layoutId = p->layout, .originX = -7, .originY = -9, .width = 128, .height = 128};
    int x = map.originX + p->x, z = map.originY + p->y, w = model->w, h = model->h;
    const int edges[][4] = {
        {x + w, z, x + w + 1, z + h}, {x - 1, z, x, z + h}, {x, z + h, x + w, z + h + 1},
        {x, z - 1, x + w, z},         {x, z, x, z + h},     {x, z, x + w, z}};
    VoxelVertex vertices[32768];
    VoxelBuilder builder = {.vertices = vertices, .capacity = 32768};
    for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i)
    {
        builder.count = 0;
        VoxelBuildings_EmitNight(&builder, &map, edges[i][0], edges[i][1], edges[i][2],
                                 edges[i][3]);
        assert(builder.count == 0);
    }
    VoxelBuildings_EmitNight(&builder, &map, x, z, x + w, z + h);
    assert(builder.count > 0);
    sPlacements[0] = saved;
    sPlacementCount = count;
    sLastLayout = -1;
}

static void CheckPartialInitialization(void)
{
    ClearNightWindows();
    unsigned baseline = sLiveBlocks;
    for (int failure = FAIL_CALLOC; failure <= FAIL_OPEN; ++failure)
    {
        sFailure = failure;
        BuildNightWindows();
        assert(!sNightModels && !sNightVertices && sNightCount == 0);
        assert(sLiveBlocks == baseline && sLiveFiles == 0);
    }
    sFailure = FAIL_NONE;
}

int main(int argc, char **argv)
{
    assert(VoxelBuildings_Init());
    for (unsigned model = 0; model < sModelCount; ++model)
        CheckSurfaceAlignment(model);
    unsigned facilityDoors = 0;
    for (unsigned i = 0; i < sWindowRectCount; ++i)
    {
        const uint16_t *r = sWindowRects + i * 5;
        if (sModels[r[0]].w == 4 && sModels[r[0]].h == 4 && r[1] == 17 && r[2] == 45 && r[3] == 31)
        {
            assert(LightCovers(r[0], 24, 59.5f));
            assert(!LightCovers(r[0], 16.5f, 50));
            ++facilityDoors;
        }
        if (sModels[r[0]].w == 6 && sModels[r[0]].h == 5 && r[1] == 49 && r[2] == 62 && r[3] == 55)
        {
            assert(LightCovers(r[0], 52, 75.5f));
            assert(LightCovers(r[0], 60, 75.5f));
            assert(!LightCovers(r[0], 56, 67.5f));
            ++facilityDoors;
        }
    }
    assert(facilityDoors >= 4);
    assert(sWindowRectCount > 0 && sNightCount > 0 && sNightCount < NIGHT_TEMPLATE_VERTICES);
    static VoxelVertex out[32768];
    VoxelBuilder b = {.vertices = out, .capacity = 32768};
    bool found = false;
    for (unsigned i = 0; i < sPlacementCount; ++i)
    {
        const BuildingPlacement *p = &sPlacements[i];
        const struct NightModel *n = &sNightModels[sPageModels[p->pageModel].model];
        if (!n->count)
            continue;
        VoxelMapInstance m = {.layoutId = p->layout, .width = 128, .height = 128};
        VoxelBuildings_EmitNight(&b, &m, 0, 0, 128, 128);
        assert(b.count > 0);
        for (unsigned k = 0; k < b.count; ++k)
            assert(isfinite(out[k].x) && isfinite(out[k].y) && isfinite(out[k].z));
        m.indoor = true;
        b.count = 0;
        VoxelBuildings_EmitNight(&b, &m, 0, 0, 128, 128);
        assert(b.count == 0);
        found = true;
        break;
    }
    assert(found);
    CheckEmissionEdges();
    printf("PASS night building pack: %u glass rectangles, %u cached vertices, outdoor emission, "
           "indoor exclusion\n",
           sWindowRectCount, sNightCount);
    if (argc == 2)
    {
        FILE *source = VoxelFile_Open(VOXEL_BUILDINGS_PATH);
        assert(source);
        uint64_t fingerprint = UINT64_C(14695981039346656037);
        int byte;
        while ((byte = fgetc(source)) != EOF)
            fingerprint = (fingerprint ^ (unsigned)byte) * UINT64_C(1099511628211);
        assert(!ferror(source));
        assert(fclose(source) == 0);
        FILE *dump = fopen(argv[1], "wb");
        assert(dump);
        assert(fwrite("NXM1", 4, 1, dump) == 1);
        assert(fwrite(&fingerprint, 8, 1, dump) == 1);
        assert(fwrite(&sModelCount, 4, 1, dump) == 1);
        for (unsigned model = 0; model < sModelCount; ++model)
        {
            unsigned count = sNightModels[model].count;
            assert(fwrite(&count, 4, 1, dump) == 1);
            assert(fwrite(sNightVertices + sNightModels[model].first, sizeof(VoxelVertex), count,
                          dump) == count);
        }
        assert(fclose(dump) == 0);
    }
    CheckPartialInitialization();
    VoxelBuildings_Shutdown();
    assert(sLiveBlocks == 0 && sLiveFiles == 0);
}
