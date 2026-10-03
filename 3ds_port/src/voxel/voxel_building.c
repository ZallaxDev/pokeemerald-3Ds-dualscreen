/*
 * Reader and emitter for voxel/buildings.bin (game data). See voxel_building.h;
 * scripts/gen_voxel_buildings.py writes the file and documents the models.
 *
 * Layout (little endian):
 *   "VXB7", u16 pages, models, pageModels, placements, heightBytes, masks,
 *   u32 vertices, u16 variants, u16 0
 *   pages       x 8:  u16 w, h; u32 file offset of its RGBA5551 texels
 *   models      x 16: u8 w, h; u16 ground; u32 firstVertex, vertexCount, heights
 *   pageModels  x 8:  u16 model, page; i16 ox, oy (pixels)
 *   placements  x 16: u16 layout, pageModel, x, y, ground, extraCount;
 *                     u32 extraFirst (sorted by layout)
 *   heightBytes       one byte per model cell, pixels; 255 = not the model's
 *   padding to 2, u16 footprint per model cell: the index of its mask, or
 *                     0xFFFF for a cell cast as a box
 *   masks x 32        16 u16 rows; bit x of row z: the solid stands over that
 *                     pixel of the cell (a railing's line, not its cell)
 *   quarters          u8 per height byte: of its own metatile's upper layer,
 *                     what the model stands for (bit 2 * row + column)
 *   padding to 2, variants x 6: u16 layout, metatile; u8 quarters; u8 0 -
 *                     the ground variants (voxel_atlas.h)
 *   padding to 4, vertices x 24: float x, y, z, u, v, shade (tiles, relative
 *                     to the top-left cell; u, v in pixels of the model's own
 *                     drawing, or of its page for a placement's ground patches)
 *   the pages' texels, read only when a map on screen needs them
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Host tests define PORT_LOG away rather than link the console's logger. */
#ifndef PORT_LOG
#include "port_log.h"
#endif

#include "voxel_atlas.h"
#include "voxel_building.h"
#include "voxel_file.h"
#include "voxel_grade.h"
#include "voxel_lighting.h"
#include "voxel_relief.h"

#ifndef VOXEL_BUILDINGS_PATH
#define VOXEL_BUILDINGS_PATH "voxel/buildings.bin"
#endif

typedef struct
{
    uint16_t w, h;
    uint32_t offset;
} BuildingPage;

typedef struct
{
    uint8_t w, h;
    uint16_t ground;
    uint32_t firstVertex, vertexCount, heights;
} BuildingModel;

typedef struct
{
    uint16_t model, page;
    int16_t ox, oy;
} BuildingPageModel;

typedef struct
{
    uint16_t layout, pageModel, x, y, ground, extraCount;
    uint32_t extraFirst;
} BuildingPlacement;

static BuildingPage *sPages;
static unsigned sPageCount;
static BuildingModel *sModels;
static unsigned sModelCount;
static BuildingPageModel *sPageModels;
static unsigned sPageModelCount;
static BuildingPlacement *sPlacements;
static unsigned sPlacementCount;
static uint8_t *sHeights;
static uint16_t *sFootprints;   /* one per height byte */
static uint16_t *sMasks;        /* 16 rows each */
static unsigned sMaskCount;
static VoxelVertex *sVertices;
static unsigned sVertexCount;
static uint8_t *sQuarters;      /* one per height byte */
static uint8_t *sVariants;      /* 6 bytes each */
static unsigned sVariantCount;
/* A placement whose cells keep their own metatile (gen_voxel_buildings.py
 * OWN_GROUND). */
#define OWN_GROUND 0xFFFFu
static float sMaxTop;
/* Kept open for the page reads, which come one slice at a time: opening the
 * file again for every slice cost a RomFS path lookup per slice. */
static FILE *sPageFile;
/* The placements last looked up: a build or a shadow ray asks about every
 * cell it touches, nearly always of the layout it asked about last. */
static int sLastLayout = -1;
static unsigned sLastFirst, sLastCount;

static unsigned U16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static uint32_t U32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static bool ReadRows(FILE *file, unsigned count, unsigned size, uint8_t *row,
                     bool (*take)(unsigned, const uint8_t *))
{
    for (unsigned i = 0; i < count; ++i)
        if (fread(row, 1, size, file) != size || !take(i, row))
            return false;
    return true;
}

static unsigned sHeightBytes;

static bool TakePage(unsigned i, const uint8_t *r)
{
    sPages[i].w = (uint16_t)U16(r);
    sPages[i].h = (uint16_t)U16(r + 2);
    sPages[i].offset = U32(r + 4);
    return true;
}

static bool TakeModel(unsigned i, const uint8_t *r)
{
    BuildingModel *m = &sModels[i];
    m->w = r[0];
    m->h = r[1];
    m->ground = (uint16_t)U16(r + 2);
    m->firstVertex = U32(r + 4);
    m->vertexCount = U32(r + 8);
    m->heights = U32(r + 12);
    return m->firstVertex + m->vertexCount <= sVertexCount
        && m->heights + (unsigned)m->w * m->h <= sHeightBytes;
}

static bool TakePageModel(unsigned i, const uint8_t *r)
{
    BuildingPageModel *pm = &sPageModels[i];
    pm->model = (uint16_t)U16(r);
    pm->page = (uint16_t)U16(r + 2);
    pm->ox = (int16_t)U16(r + 4);
    pm->oy = (int16_t)U16(r + 6);
    return pm->model < sModelCount && pm->page < sPageCount;
}

static bool TakePlacement(unsigned i, const uint8_t *r)
{
    BuildingPlacement *p = &sPlacements[i];
    p->layout = (uint16_t)U16(r);
    p->pageModel = (uint16_t)U16(r + 2);
    p->x = (uint16_t)U16(r + 4);
    p->y = (uint16_t)U16(r + 6);
    p->ground = (uint16_t)U16(r + 8);
    p->extraCount = (uint16_t)U16(r + 10);
    p->extraFirst = U32(r + 12);
    return p->pageModel < sPageModelCount
        && p->extraFirst + p->extraCount <= sVertexCount;
}

bool VoxelBuildings_Init(void)
{
    uint8_t header[24], row[16];
    long offset;
    FILE *file;
    bool ok = false;

    VoxelBuildings_Shutdown();
    file = VoxelFile_Open(VOXEL_BUILDINGS_PATH);
    if (file == NULL)
    {
        PORT_LOG("[VIDEO] VOXEL buildings: %s absent, houses stay extruded\n",
                VOXEL_BUILDINGS_PATH);
        return false;
    }
    if (fread(header, 1, sizeof(header), file) != sizeof(header)
     || memcmp(header, "VXB7", 4) != 0)
        goto done;
    sPageCount = U16(header + 4);
    sModelCount = U16(header + 6);
    sPageModelCount = U16(header + 8);
    sPlacementCount = U16(header + 10);
    sHeightBytes = U16(header + 12);
    sMaskCount = U16(header + 14);
    sVertexCount = U32(header + 16);
    sVariantCount = U16(header + 20);

    sPages = malloc(sPageCount * sizeof(*sPages) + 1);
    sModels = malloc(sModelCount * sizeof(*sModels) + 1);
    sPageModels = malloc(sPageModelCount * sizeof(*sPageModels) + 1);
    sPlacements = malloc(sPlacementCount * sizeof(*sPlacements) + 1);
    sHeights = malloc(sHeightBytes + 1);
    sFootprints = malloc(sHeightBytes * sizeof(uint16_t) + 1);
    sMasks = malloc(sMaskCount * 16 * sizeof(uint16_t) + 1);
    sVertices = malloc(sVertexCount * sizeof(VoxelVertex) + 1);
    sQuarters = malloc(sHeightBytes + 1);
    sVariants = malloc(sVariantCount * 6u + 1);
    if (!sPages || !sModels || !sPageModels || !sPlacements || !sHeights || !sVertices
     || !sFootprints || !sMasks || !sQuarters || !sVariants)
        goto done;
    if (!ReadRows(file, sPageCount, 8, row, TakePage)
     || !ReadRows(file, sModelCount, 16, row, TakeModel)
     || !ReadRows(file, sPageModelCount, 8, row, TakePageModel)
     || !ReadRows(file, sPlacementCount, 16, row, TakePlacement))
        goto done;
    if (fread(sHeights, 1, sHeightBytes, file) != sHeightBytes)
        goto done;
    if ((sHeightBytes & 1) != 0 && fseek(file, 1, SEEK_CUR) != 0)
        goto done;
    /* Little-endian u16s, the console's own order. */
    if (fread(sFootprints, sizeof(uint16_t), sHeightBytes, file) != sHeightBytes
     || fread(sMasks, 16 * sizeof(uint16_t), sMaskCount, file) != sMaskCount
     || fread(sQuarters, 1, sHeightBytes, file) != sHeightBytes
     || ((sHeightBytes & 1) != 0 && fseek(file, 1, SEEK_CUR) != 0)
     || fread(sVariants, 6, sVariantCount, file) != sVariantCount)
        goto done;
    for (unsigned i = 0; i < sHeightBytes; ++i)
        if (sFootprints[i] != 0xFFFF && sFootprints[i] >= sMaskCount)
            goto done;
    offset = ftell(file);
    if (offset < 0 || fseek(file, (4 - (offset & 3)) & 3, SEEK_CUR) != 0)
        goto done;
    /* The file's vertex record is VoxelVertex exactly: six little-endian
     * floats, which is also the console's layout. */
    if (fread(sVertices, sizeof(VoxelVertex), sVertexCount, file) != sVertexCount)
        goto done;
    sMaxTop = 0.0f;
    for (unsigned i = 0; i < sHeightBytes; ++i)
        if (sHeights[i] != 0xFF && sHeights[i] / 16.0f > sMaxTop)
            sMaxTop = sHeights[i] / 16.0f;
    ok = true;

done:
    fclose(file);
    if (!ok)
    {
        PORT_LOG("[ERROR] VOXEL buildings: %s is truncated or malformed\n",
                VOXEL_BUILDINGS_PATH);
        VoxelBuildings_Shutdown();
        return false;
    }
    PORT_LOG("[VIDEO] VOXEL buildings: %u models on %u pages, %u placements, %u vertices\n",
            sModelCount, sPageCount, sPlacementCount, sVertexCount);
    return true;
}

void VoxelBuildings_Shutdown(void)
{
    free(sPages);
    free(sModels);
    free(sPageModels);
    free(sPlacements);
    free(sHeights);
    free(sFootprints);
    free(sMasks);
    free(sVertices);
    free(sQuarters);
    free(sVariants);
    sQuarters = NULL;
    sVariants = NULL;
    sVariantCount = 0;
    sPages = NULL;
    sModels = NULL;
    sPageModels = NULL;
    sPlacements = NULL;
    sHeights = NULL;
    sFootprints = NULL;
    sMasks = NULL;
    sMaskCount = 0;
    sVertices = NULL;
    sPageCount = sModelCount = sPageModelCount = sPlacementCount = sVertexCount = 0;
    sLastLayout = -1;
    sMaxTop = 0.0f;
    if (sPageFile != NULL)
    {
        fclose(sPageFile);
        sPageFile = NULL;
    }
}

float VoxelBuildings_MaxTop(void)
{
    return sMaxTop;
}

bool VoxelBuildings_PageSize(unsigned page, unsigned *width, unsigned *height)
{
    if (page >= sPageCount)
        return false;
    *width = sPages[page].w;
    *height = sPages[page].h;
    return true;
}

bool VoxelBuildings_ReadPage(unsigned page, unsigned first, unsigned count, uint16_t *dest)
{
    if (page >= sPageCount || first + count > (unsigned)sPages[page].w * sPages[page].h)
        return false;
    if (sPageFile == NULL)
    {
        sPageFile = VoxelFile_Open(VOXEL_BUILDINGS_PATH);
        if (sPageFile == NULL)
            return false;
        /* Slices go straight into the caller's buffer, never through stdio's. */
        setvbuf(sPageFile, NULL, _IONBF, 0);
    }
    if (fseek(sPageFile, (long)(sPages[page].offset + first * sizeof(uint16_t)), SEEK_SET) != 0
     || fread(dest, sizeof(uint16_t), count, sPageFile) != count)
        return false;
    /* On the reading thread, off the render thread's frame. */
    VoxelGrade_Texels(dest, count);
    return true;
}

/* The placements of one layout, which the generator sorted by layout. */
static const BuildingPlacement *LayoutPlacements(const VoxelMapInstance *inst, unsigned *count)
{
    unsigned lo = 0, hi = sPlacementCount, n = 0;

    *count = 0;
    if (inst == NULL || sPlacementCount == 0)
        return NULL;
    if (inst->layoutId != sLastLayout)
    {
        while (lo < hi)
        {
            unsigned mid = (lo + hi) / 2;

            if (sPlacements[mid].layout < (unsigned)inst->layoutId)
                lo = mid + 1;
            else
                hi = mid;
        }
        while (lo + n < sPlacementCount && sPlacements[lo + n].layout == (unsigned)inst->layoutId)
            ++n;
        sLastLayout = inst->layoutId;
        sLastFirst = lo;
        sLastCount = n;
    }
    *count = sLastCount;
    return sLastCount ? &sPlacements[sLastFirst] : NULL;
}

/* A few layouts' answers kept: the lighting asks once per map on screen at
 * every reset of its caches. */
#define LAYOUT_TOPS 8u

float VoxelBuildings_LayoutTop(const VoxelMapInstance *inst)
{
    static int sTopLayout[LAYOUT_TOPS]; /* layout + 1; 0 is empty */
    static float sTop[LAYOUT_TOPS];
    static unsigned sTopNext;
    unsigned count;
    const BuildingPlacement *p;
    float top = 0.0f;

    if (inst == NULL || sModels == NULL)
        return 0.0f;
    for (unsigned i = 0; i < LAYOUT_TOPS; ++i)
        if (sTopLayout[i] == inst->layoutId + 1)
            return sTop[i];
    p = LayoutPlacements(inst, &count);
    for (unsigned i = 0; i < count; ++i)
    {
        const BuildingModel *m = &sModels[sPageModels[p[i].pageModel].model];

        for (unsigned k = 0; k < (unsigned)m->w * m->h; ++k)
            if (sHeights[m->heights + k] != 0xFF && sHeights[m->heights + k] / 16.0f > top)
                top = sHeights[m->heights + k] / 16.0f;
    }
    sTopLayout[sTopNext] = inst->layoutId + 1;
    sTop[sTopNext] = top;
    sTopNext = (sTopNext + 1) % LAYOUT_TOPS;
    return top;
}

int VoxelBuildings_PageOf(const VoxelMapInstance *inst)
{
    unsigned count;
    const BuildingPlacement *p = LayoutPlacements(inst, &count);

    return p ? (int)sPageModels[p[0].pageModel].page : -1;
}

/* The model cell over world cell (x, y): the index of its height byte, or
 * -1. The first placement that owns the cell answers. */
static long ModelCell(const VoxelMapInstance *inst, int x, int y, unsigned *placement)
{
    unsigned count;
    const BuildingPlacement *p = LayoutPlacements(inst, &count);
    int lx, ly;

    if (p == NULL)
        return -1;
    lx = x - inst->originX;
    ly = y - inst->originY;
    for (unsigned i = 0; i < count; ++i)
    {
        const BuildingModel *m = &sModels[sPageModels[p[i].pageModel].model];
        int cx = lx - p[i].x, cy = ly - p[i].y;
        unsigned k;

        if (cx < 0 || cy < 0 || cx >= m->w || cy >= m->h)
            continue;
        /* A hedge's rectangle holds the house it runs round. */
        k = m->heights + (unsigned)cy * m->w + (unsigned)cx;
        if (sHeights[k] == 0xFF)
            continue;
        if (placement != NULL)
            *placement = i;
        return (long)k;
    }
    return -1;
}

bool VoxelBuildings_CellAt(const VoxelMapInstance *inst, int x, int y,
                           int *groundMetatile, float *top)
{
    unsigned i, count;
    long k = ModelCell(inst, x, y, &i);

    if (k < 0)
        return false;
    if (groundMetatile != NULL)
    {
        *groundMetatile = LayoutPlacements(inst, &count)[i].ground;
        if (*groundMetatile == (int)OWN_GROUND)
        {
            /* the cell's own drawing, less what the model stands for */
            unsigned own = (unsigned)VoxelWorld_GetMetatileId(x, y);

            *groundMetatile = (int)own;
            for (unsigned v = 0; sQuarters[k] != 0 && v < sVariantCount && v < VOXEL_VARIANTS; ++v)
                if (U16(sVariants + 6u * v + 2) == own && sVariants[6u * v + 4] == sQuarters[k])
                {
                    *groundMetatile = (int)(VOXEL_METATILE_REAL + v);
                    break;
                }
        }
    }
    if (top != NULL)
        *top = sHeights[k] / 16.0f;
    return true;
}

bool VoxelBuildings_Variant(unsigned i, unsigned *layout, unsigned *metatile, unsigned *quarters)
{
    if (i >= sVariantCount)
        return false;
    *layout = U16(sVariants + 6u * i);
    *metatile = U16(sVariants + 6u * i + 2);
    *quarters = sVariants[6u * i + 4];
    return true;
}

const uint16_t *VoxelBuildings_Footprint(const VoxelMapInstance *inst, int x, int y)
{
    long k = ModelCell(inst, x, y, NULL);

    if (k < 0 || sFootprints[k] == 0xFFFF)
        return NULL;
    return &sMasks[(unsigned)sFootprints[k] * 16u];
}

bool VoxelBuildings_EmitSome(VoxelBuilder *builder, const VoxelMapInstance *inst,
                             int x0, int y0, int x1, int y1, VoxelBuildingCursor *cursor,
                             unsigned triangles)
{
    unsigned count;
    const BuildingPlacement *p = LayoutPlacements(inst, &count);

    for (; p != NULL && cursor->placement < count; ++cursor->placement, cursor->part = 0,
                                                     cursor->vertex = 0)
    {
        unsigned i = cursor->placement;
        const BuildingPageModel *pm = &sPageModels[p[i].pageModel];
        const BuildingModel *m = &sModels[pm->model];
        const BuildingPage *page = &sPages[pm->page];
        float wx = (float)(inst->originX + p[i].x);
        float wz = (float)(inst->originY + p[i].y);
        float su = 1.0f / page->w, sv = 1.0f / page->h;
        /* The model, then this placement's own ground patches: the model's
         * uv are pixels of its drawing, placed on the page at (ox, oy). */
        const uint32_t first[2] = { m->firstVertex, p[i].extraFirst };
        const uint32_t total[2] = { m->vertexCount, p[i].extraCount };
        const float ox[2] = { (float)pm->ox, 0.0f }, oy[2] = { (float)pm->oy, 0.0f };

        if (inst->originX + p[i].x < x0 || inst->originX + p[i].x >= x1
         || inst->originY + p[i].y < y0 || inst->originY + p[i].y >= y1)
            continue;
        /* On a lifted plateau the building stands on it: the lift of the
         * cell under its door, the bottom-left of its rectangle. */
        builder->lift = VoxelRelief_CellLift(inst, inst->originX + p[i].x,
                                             inst->originY + p[i].y + m->h - 1);
        builder->shift = VoxelRelief_CellShift(inst, inst->originX + p[i].x,
                                             inst->originY + p[i].y + m->h - 1);
        for (; cursor->part < 2; ++cursor->part, cursor->vertex = 0)
        {
            unsigned r = cursor->part;
            const VoxelVertex *v = &sVertices[first[r]];

            /* The ground patches are ground: a quad a cell, lit like the
             * terrain they lie on, so a shadow falls across them too. The
             * model keeps the shading it was drawn with. */
            unsigned step = r == 1 ? 6u : 3u;

            for (; cursor->vertex + step - 1 < total[r]; cursor->vertex += step)
            {
                uint32_t k = cursor->vertex;
                VoxelVertex t[6];

                if (triangles < step / 3u)
                {
                    builder->lift = 0.0f;
                    builder->shift = 0.0f;
                    return false;
                }
                triangles -= step / 3u;

                for (unsigned j = 0; j < step; ++j)
                {
                    t[j] = v[k + j];
                    t[j].x += wx;
                    t[j].z += wz;
                    t[j].u = (ox[r] + t[j].u) * su;
                    t[j].v = 1.0f - (oy[r] + t[j].v) * sv;
                }
                /* a patch is written a, b, c, a, c, d */
                if (step == 6)
                    VoxelBuilder_Quad(builder, &t[0], &t[1], &t[2], &t[5]);
#if CTR_VOXEL_LIGHTING
                else if (builder->lighting)
                {
                    /* Lit by the same sun as the terrain, not by the side
                     * the drawing was made for. */
                    VoxelLighting_ModelTri(builder, &t[0], &t[1], &t[2], t[0].shade);
                }
#endif
                else
                    VoxelBuilder_Tri(builder, &t[0], &t[1], &t[2]);
            }
        }
        builder->lift = 0.0f;
        builder->shift = 0.0f;
    }
    return true;
}

void VoxelBuildings_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                                 int x0, int y0, int x1, int y1)
{
    VoxelBuildingCursor cursor = { 0, 0, 0 };

    VoxelBuildings_EmitSome(builder, inst, x0, y0, x1, y1, &cursor, UINT32_MAX);
}
