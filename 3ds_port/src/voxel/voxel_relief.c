/*
 * Reader for voxel/relief.bin (game data); see voxel_relief.h and the generator,
 * scripts/gen_voxel_relief.py.
 *
 * Layout (little endian):
 *   "VXL4", u16 layouts, u16 side (5)
 *   layouts x 14: u16 layout id, u16 cells, u16 width, u16 height (bit 15:
 *                 drawn; bit 14: heights in units of 2 pixels), u32 offset,
 *                 s16 base (pixels: the level the whole map stands at)
 *   cells x (2 + 25): u8 x, u8 y, int8 heights[25] (row major, over the base)
 *
 * then the cut tiles (see VoxelRelief_Cut): u16 variants, u16 cells;
 *   variants x 36: u16 layout drawing the tileset, u16 metatile, u16 rows[16]
 *                  (bit set: background)
 *   cells x 12:    u16 layout, u8 x, u8 y, u16 variant, s16 foot (pixels),
 *                  u16 metatile of the ground behind it (0xFFFF: none),
 *                  u16 metatile its cliff walls are drawn with (0xFFFF: none)
 *   and last u32 the table's offset, "CUTS".
 *
 * A point's depth is its height ((u, h, v + h)), so the file does not carry
 * it. The heights are decoded to pixels at load: a drawn mountain can stand
 * taller than a signed byte of pixels.
 */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Host tests define PORT_LOG away rather than link the console's logger. */
#ifndef PORT_LOG
#include "port_log.h"
#endif

#include "voxel_relief.h"
#include "voxel_file.h"

#ifndef VOXEL_RELIEF_PATH
#define VOXEL_RELIEF_PATH "voxel/relief.bin"
#endif
#define GRID (VOXEL_RELIEF_SIDE * VOXEL_RELIEF_SIDE)
#define CELL_BYTES (2 + GRID)
#define ROW_BYTES 14

/*
 * Cells are searched, not indexed: every map with a ledge has a few dozen
 * lifted cells, and a dense index of each map's every cell cost more heap
 * than all of them. The generator writes them row by row, x within y.
 */
typedef struct
{
    uint16_t layoutId, width, height, count;
    int16_t base;          /* pixels */
    bool drawn;            /* the relief is all of this map's terrain */
    const uint8_t *cells;  /* into sBlob: x, y and the stored heights */
    int16_t *heights;      /* GRID per cell, pixels */
    /* A drawn map's surface where it stands, for the sun to be stopped by:
     * its height (pixels) every lattice step of world X and Z, not of the
     * drawing's rows - a point of the drawing lies as far south as it is
     * high, so the table runs SURFACE_SPAN cells past the map's south edge. */
    int8_t *surface;       /* in units of 2 pixels */
    uint16_t surfaceW, surfaceH;
    float top;             /* its highest point, tiles */
} ReliefLayout;

#define SURFACE_SPAN 6
static int Find(const ReliefLayout *l, const VoxelMapInstance *inst, int x, int y);
static bool BuildSurface(ReliefLayout *l);

static uint8_t *sBlob;
static ReliefLayout *sLayouts;
static unsigned sLayoutCount;
/* The layouts by id (their entry + 1, 0 for none): every height, lift and
 * shadow-ray step asks which layout a map is, and a scan of them all was the
 * bulk of what a step on a mountain cost. */
static uint16_t *sLayoutIndex;
static unsigned sLayoutIndexSize;
static const uint8_t *sCutVariants, *sCutCells;
static unsigned sCutVariantCount, sCutCellCount;
#define CUT_VARIANT_BYTES 36
#define CUT_CELL_BYTES 12

/* floorf as a conversion and one correction: the library call it replaces was
 * made at every step of every shadow ray over a mountain. */
static inline int FloorI(float v)
{
    int i = (int)v;

    return (float)i > v ? i - 1 : i;
}

static unsigned U16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static uint32_t U32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

bool VoxelRelief_Init(void)
{
    FILE *file;
    long size;

    VoxelRelief_Shutdown();
    file = VoxelFile_Open(VOXEL_RELIEF_PATH);
    if (file == NULL)
    {
        PORT_LOG("[VIDEO] VOXEL relief: %s absent, terrain stays level\n", VOXEL_RELIEF_PATH);
        return false;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    sBlob = size > 8 ? malloc((size_t)size) : NULL;
    if (sBlob == NULL || fread(sBlob, 1, (size_t)size, file) != (size_t)size
     || memcmp(sBlob, "VXL4", 4) != 0 || U16(sBlob + 6) != VOXEL_RELIEF_SIDE
     || memcmp(sBlob + size - 4, "CUTS", 4) != 0)
        goto fail;
    {
        uint32_t at = U32(sBlob + size - 8);

        if ((long)at + 4 > size - 8)
            goto fail;
        sCutVariantCount = U16(sBlob + at);
        sCutCellCount = U16(sBlob + at + 2);
        sCutVariants = sBlob + at + 4;
        sCutCells = sCutVariants + CUT_VARIANT_BYTES * sCutVariantCount;
        if ((long)(sCutCells - sBlob) + (long)(CUT_CELL_BYTES * sCutCellCount) > size - 8)
            goto fail;
    }
    sLayoutCount = U16(sBlob + 4);
    if (8 + (unsigned long)ROW_BYTES * sLayoutCount > (unsigned long)size)
        goto fail;
    sLayouts = calloc(sLayoutCount ? sLayoutCount : 1, sizeof(*sLayouts));
    if (sLayouts == NULL)
        goto fail;
    for (unsigned i = 0; i < sLayoutCount; ++i)
    {
        const uint8_t *row = sBlob + 8 + ROW_BYTES * i;
        ReliefLayout *l = &sLayouts[i];
        unsigned cells = U16(row + 2);
        uint32_t offset = U32(row + 8);

        l->layoutId = (uint16_t)U16(row);
        l->base = (int16_t)U16(row + 12);
        l->width = (uint16_t)U16(row + 4);
        unsigned unit = (U16(row + 6) & 0x4000) ? 2 : 1;

        l->height = (uint16_t)(U16(row + 6) & 0x3FFF);
        l->drawn = (U16(row + 6) & 0x8000) != 0;
        l->cells = sBlob + offset;
        l->count = (uint16_t)cells;
        if (offset + (uint32_t)cells * CELL_BYTES > (uint32_t)size)
            goto fail;
        for (unsigned k = 1; k < cells; ++k)
        {
            const uint8_t *a = l->cells + (k - 1) * CELL_BYTES, *c = a + CELL_BYTES;
            if (a[1] > c[1] || (a[1] == c[1] && a[0] >= c[0]))
                goto fail;  /* not in row order: the search would miss cells */
        }
        l->heights = malloc((size_t)(cells ? cells : 1) * GRID * sizeof(int16_t));
        if (l->heights == NULL)
            goto fail;
        for (unsigned k = 0; k < cells; ++k)
            for (unsigned g = 0; g < GRID; ++g)
                l->heights[k * GRID + g] =
                    (int16_t)((int8_t)l->cells[k * CELL_BYTES + 2 + g] * (int)unit);
    }
    fclose(file);
    for (unsigned i = 0; i < sLayoutCount; ++i)
        if (sLayouts[i].drawn && !BuildSurface(&sLayouts[i]))
        {
    PORT_LOG("[ERROR] VOXEL relief: no memory for a drawn map's surface\n");
            VoxelRelief_Shutdown();
            return false;
        }
    {
        unsigned most = 0;

        for (unsigned i = 0; i < sLayoutCount; ++i)
            if (sLayouts[i].layoutId > most)
                most = sLayouts[i].layoutId;
        sLayoutIndex = calloc((size_t)most + 1u, sizeof(*sLayoutIndex));
        if (sLayoutIndex != NULL)
        {
            sLayoutIndexSize = most + 1u;
            /* backwards: the first of two layouts of one id answers, as the scan did */
            for (unsigned i = sLayoutCount; i-- > 0;)
                sLayoutIndex[sLayouts[i].layoutId] = (uint16_t)(i + 1u);
        }
    }
    PORT_LOG("[VIDEO] VOXEL relief: %u layouts\n", sLayoutCount);
    return true;

fail:
    fclose(file);
    PORT_LOG("[ERROR] VOXEL relief: %s is truncated or malformed\n", VOXEL_RELIEF_PATH);
    VoxelRelief_Shutdown();
    return false;
}

void VoxelRelief_Shutdown(void)
{
    for (unsigned i = 0; sLayouts != NULL && i < sLayoutCount; ++i)
    {
        free(sLayouts[i].surface);
        free(sLayouts[i].heights);
    }
    free(sLayouts);
    free(sLayoutIndex);
    sLayoutIndex = NULL;
    sLayoutIndexSize = 0;
    free(sBlob);
    sLayouts = NULL;
    sBlob = NULL;
    sLayoutCount = 0;
    sCutVariants = sCutCells = NULL;
    sCutVariantCount = sCutCellCount = 0;
}

unsigned VoxelRelief_CutCount(void)
{
    return sCutVariantCount;
}

bool VoxelRelief_CutVariant(unsigned i, unsigned *layout, unsigned *metatile,
                            const uint8_t **rows)
{
    const uint8_t *v;

    if (i >= sCutVariantCount)
        return false;
    v = sCutVariants + CUT_VARIANT_BYTES * i;
    *layout = U16(v);
    *metatile = U16(v + 2);
    *rows = v + 4;
    return true;
}

int VoxelRelief_Cut(const VoxelMapInstance *inst, int x, int y, float *foot, int *ground,
                    int *wall)
{
    unsigned lo = 0, hi = sCutCellCount, key;

    if (inst == NULL || sCutCells == NULL)
        return -1;
    x -= inst->originX;
    y -= inst->originY;
    if (x < 0 || y < 0 || x > 255 || y > 255)
        return -1;
    key = ((unsigned)inst->layoutId << 16) | ((unsigned)x << 8) | (unsigned)y;
    while (lo < hi)
    {
        unsigned mid = (lo + hi) / 2;
        const uint8_t *c = sCutCells + CUT_CELL_BYTES * mid;
        unsigned at = (U16(c) << 16) | ((unsigned)c[2] << 8) | c[3];

        if (at == key)
        {
            *foot = (int16_t)U16(c + 6) / 16.0f;
            *ground = U16(c + 8) == 0xFFFFu ? -1 : (int)U16(c + 8);
            *wall = U16(c + 10) == 0xFFFFu ? -1 : (int)U16(c + 10);
            return (int)U16(c + 4);
        }
        if (at < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

static const ReliefLayout *LayoutOf(const VoxelMapInstance *inst)
{
    if (inst == NULL)
        return NULL;
    if (sLayoutIndex != NULL)
    {
        unsigned k = (unsigned)inst->layoutId < sLayoutIndexSize ? sLayoutIndex[inst->layoutId] : 0u;

        return k != 0 ? &sLayouts[k - 1u] : NULL;
    }
    for (unsigned i = 0; i < sLayoutCount; ++i)
        if (sLayouts[i].layoutId == (unsigned)inst->layoutId)
            return &sLayouts[i];
    return NULL;
}

float VoxelRelief_Base(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);
    return l != NULL ? l->base / 16.0f : 0.0f;
}

const int16_t *VoxelRelief_Cell(const VoxelMapInstance *inst, int x, int y)
{
    const ReliefLayout *l = LayoutOf(inst);
    int k = Find(l, inst, x, y);
    return k >= 0 ? l->heights + (unsigned)k * GRID : NULL;
}

const int16_t *VoxelRelief_Depth(const VoxelMapInstance *inst, int x, int y)
{
    return VoxelRelief_Cell(inst, x, y);
}

static int Find(const ReliefLayout *l, const VoxelMapInstance *inst, int x, int y)
{
    unsigned lo = 0, hi, key;

    if (l == NULL)
        return -1;
    x -= inst->originX;
    y -= inst->originY;
    if (x < 0 || y < 0 || x >= l->width || y >= l->height)
        return -1;
    key = ((unsigned)y << 8) | (unsigned)x;
    hi = l->count;
    while (lo < hi)
    {
        unsigned mid = (lo + hi) / 2;
        const uint8_t *c = l->cells + mid * CELL_BYTES;
        unsigned at = ((unsigned)c[1] << 8) | c[0];

        if (at == key)
            return (int)mid;
        if (at < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

bool VoxelRelief_IsDrawn(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);
    return l != NULL && l->drawn;
}

bool VoxelRelief_IsSlope(const int16_t *grid)
{
    for (unsigned i = 1; grid != NULL && i < VOXEL_RELIEF_SIDE * VOXEL_RELIEF_SIDE; ++i)
        if (grid[i] != grid[0])
            return true;
    return false;
}

float VoxelRelief_CellLift(const VoxelMapInstance *inst, int x, int y)
{
    const int16_t *g = VoxelRelief_Cell(inst, x, y);
    return g ? g[2 * VOXEL_RELIEF_SIDE + 2] / 16.0f : 0.0f;
}

float VoxelRelief_CellShift(const VoxelMapInstance *inst, int x, int y)
{
    const int16_t *g = VoxelRelief_Depth(inst, x, y);
    return g ? g[2 * VOXEL_RELIEF_SIDE + 2] / 16.0f : 0.0f;
}

static float Sample(const int16_t *g, float worldX, float worldZ);

float VoxelRelief_LiftAt(float worldX, float worldZ)
{
    int x = FloorI(worldX), y = FloorI(worldZ);
    const VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(x, y);

    return VoxelRelief_Base(inst) + Sample(VoxelRelief_Cell(inst, x, y), worldX, worldZ);
}

float VoxelRelief_ShiftAt(float worldX, float worldZ)
{
    int x = FloorI(worldX), y = FloorI(worldZ);
    return Sample(VoxelRelief_Depth(VoxelWorld_GetInstanceAt(x, y), x, y), worldX, worldZ);
}

static float Sample(const int16_t *g, float worldX, float worldZ)
{
    int x = FloorI(worldX), y = FloorI(worldZ);
    float fx, fy, a, b;
    int i, j;

    if (g == NULL)
        return 0.0f;
    fx = (worldX - x) * (VOXEL_RELIEF_SIDE - 1);
    fy = (worldZ - y) * (VOXEL_RELIEF_SIDE - 1);
    i = (int)fx;
    j = (int)fy;
    if (i > VOXEL_RELIEF_SIDE - 2) i = VOXEL_RELIEF_SIDE - 2;
    if (j > VOXEL_RELIEF_SIDE - 2) j = VOXEL_RELIEF_SIDE - 2;
    fx -= i;
    fy -= j;
    a = g[j * VOXEL_RELIEF_SIDE + i] * (1 - fx) + g[j * VOXEL_RELIEF_SIDE + i + 1] * fx;
    b = g[(j + 1) * VOXEL_RELIEF_SIDE + i] * (1 - fx) + g[(j + 1) * VOXEL_RELIEF_SIDE + i + 1] * fx;
    return (a * (1 - fy) + b * fy) / 16.0f;
}

/* A lattice point of a layout's drawing: its height and its depth, pixels. */
static void Point(const ReliefLayout *l, unsigned i, unsigned j, int *h, int *d)
{
    const unsigned n = VOXEL_RELIEF_SIDE - 1;
    unsigned x = i / n, y = j / n, a, b;
    int k;
    VoxelMapInstance inst;

    if (x >= l->width) x = l->width - 1u;
    if (y >= l->height) y = l->height - 1u;
    a = i - x * n;
    b = j - y * n;
    memset(&inst, 0, sizeof(inst));
    inst.layoutId = l->layoutId;
    k = Find(l, &inst, (int)x, (int)y);
    *h = k >= 0 ? l->heights[(unsigned)k * GRID + b * VOXEL_RELIEF_SIDE + a] : 0;
    *d = *h;
}

/* Each column of the drawing, walked down its rows, runs south through the
 * world as v + d: the height at every world lattice row it passes is read off
 * between the two points either side of it (the drawing never folds over). */
static bool BuildSurface(ReliefLayout *l)
{
    const unsigned n = VOXEL_RELIEF_SIDE - 1, step = 16 / n;
    unsigned w = l->width * n + 1u, rows = (l->height + SURFACE_SPAN) * n + 1u;
    int top = 0;

    l->surface = calloc((size_t)w * rows, 1);
    if (l->surface == NULL)
        return false;
    l->surfaceW = (uint16_t)w;
    l->surfaceH = (uint16_t)rows;
    for (unsigned i = 0; i < w; ++i)
    {
        int h0, d0;
        unsigned k = 0;

        Point(l, i, 0, &h0, &d0);
        for (unsigned j = 1; j <= l->height * n; ++j)
        {
            int h1, d1;
            float z0 = (float)((j - 1) * step) + d0, z1;

            Point(l, i, j, &h1, &d1);
            z1 = (float)(j * step) + d1;
            for (; k < rows && (float)(k * step) <= z1; ++k)
            {
                float t = z1 > z0 ? ((float)(k * step) - z0) / (z1 - z0) : 1.0f;
                int v;

                if (t < 0.0f) t = 0.0f;
                v = (int)(h0 + (h1 - h0) * t + 0.5f);
                l->surface[k * w + i] = (int8_t)(v >= 0 ? (v + 1) / 2 : -((1 - v) / 2));
                if (v > top) top = v;
            }
            h0 = h1;
            d0 = d1;
        }
    }
    l->top = top / 16.0f;
    return true;
}

static float SurfacePoint(const ReliefLayout *l, int i, int k)
{
    if (i < 0 || k < 0 || i >= l->surfaceW || k >= l->surfaceH)
        return 0.0f;
    return 2.0f * l->surface[k * l->surfaceW + i];
}

float VoxelRelief_SurfaceAt(float worldX, float worldZ)
{
    const int n = VOXEL_RELIEF_SIDE - 1;
    int x = FloorI(worldX), z = FloorI(worldZ);
    const VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(x, z);
    const ReliefLayout *l = LayoutOf(inst);
    float fx, fz;
    int i, k;

    if (l == NULL || l->surface == NULL)
        return 0.0f;
    fx = (worldX - inst->originX) * n;
    fz = (worldZ - inst->originY) * n;
    i = FloorI(fx);
    k = FloorI(fz);
    fx -= i;
    fz -= k;
    return ((SurfacePoint(l, i, k) * (1 - fx) + SurfacePoint(l, i + 1, k) * fx) * (1 - fz)
          + (SurfacePoint(l, i, k + 1) * (1 - fx) + SurfacePoint(l, i + 1, k + 1) * fx) * fz)
           / 16.0f;
}

float VoxelRelief_SurfaceTop(const VoxelMapInstance *inst, int x, int z)
{
    const ReliefLayout *l = LayoutOf(inst);
    const int n = VOXEL_RELIEF_SIDE - 1;
    float top = 0.0f;

    if (l == NULL || l->surface == NULL)
        return 0.0f;
    x -= inst->originX;
    z -= inst->originY;
    for (int k = z * n; k <= z * n + n; ++k)
        for (int i = x * n; i <= x * n + n; ++i)
            if (SurfacePoint(l, i, k) > top)
                top = SurfacePoint(l, i, k);
    return top / 16.0f;
}

float VoxelRelief_DrawnTop(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);
    return l != NULL && l->surface != NULL ? l->top : 0.0f;
}
