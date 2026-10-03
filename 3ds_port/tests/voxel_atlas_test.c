/* The real atlas composer against the per-texel reference it replaced, on
 * random tiles, palettes, flips and layers: every texel, and the black-filler
 * verdict, must come out the same. */
#include <assert.h>
#include <stdio.h>
#include "../src/voxel/voxel_atlas.c"

#include "../src/3ds_video_decode.c"

/*
 * voxel_atlas.c's world and asset dependencies: a fake tileset pair for the
 * whole-atlas tests below, whose tiles, metatiles, palettes and referenced
 * ids are the arrays further down.
 */
unsigned char g_PortLogActive;
void Port_Log_Printf(const char *fmt, ...) { (void)fmt; }
const void *Port_ResolveAssetPointer(const void *p) { return p; }
static const int kPrimary = 1, kSecondary = 2;
static bool sDerived;
static const struct MapLayout sDerivedLayout = {
    .primaryTileset = (const struct Tileset *)&kPrimary,
    .secondaryTileset = (const struct Tileset *)&kSecondary
};
bool VoxelBuildings_Variant(unsigned i, unsigned *layout, unsigned *metatile, unsigned *quarters)
{
    if (!sDerived || i >= VOXEL_VARIANTS) return false;
    *layout = 1; *metatile = i; *quarters = 1; return true;
}
const struct MapLayout *Port_GetMapLayoutById(u16 id) { return id == 1 ? &sDerivedLayout : NULL; }
unsigned VoxelRelief_CutCount(void) { return sDerived ? VOXEL_CUTS : 0; }
bool VoxelRelief_CutVariant(unsigned i, unsigned *layout, unsigned *metatile, const uint8_t **rows)
{
    static const uint8_t cut[32] = {1};
    if (!sDerived || i >= VOXEL_CUTS) return false;
    *layout = 1; *metatile = i; *rows = cut; return true;
}
static uint8_t sPrimaryTiles[512 * 32], sSecondaryTiles[512 * 32];
static uint16_t sPrimaryPalettes[16 * 16], sSecondaryPalettes[16 * 16];
static uint16_t sMetatiles[1024 * 8];
static uint8_t sUsed[1024];
void VoxelWorld_MarkUsedMetatiles(const void *p, const void *s, uint8_t *used)
{
    if (p == &kPrimary && s == &kSecondary)
        memcpy(used, sUsed, sizeof(sUsed));
}
bool Voxel_LoadTiles(const void *t, uint8_t *d, uint32_t n)
{
    if (t != &kPrimary && t != &kSecondary)
        return false;
    memcpy(d, t == &kPrimary ? sPrimaryTiles : sSecondaryTiles, n < 512 * 32 ? n : 512 * 32);
    return true;
}
const uint16_t *Voxel_ResolveMetatiles(const void *t)
{
    return t == &kPrimary ? sMetatiles : t == &kSecondary ? sMetatiles + 512 * 8 : NULL;
}
const uint16_t *Voxel_ResolvePalettes(const void *t)
{
    return t == &kPrimary ? sPrimaryPalettes : t == &kSecondary ? sSecondaryPalettes : NULL;
}
unsigned Voxel_MetatileCount(const void *t, unsigned limit) { (void)t; return limit; }

/* ── The composer as it was, texel by texel ─────────────────────────────── */

static void ReferenceCompose(const struct AtlasSource *src, const uint16_t *entries,
                             uint16_t *dest, unsigned slot)
{
    unsigned baseX = (slot % VOXEL_ATLAS_COLUMNS) * VOXEL_ATLAS_SLOT;
    unsigned baseY = (slot / VOXEL_ATLAS_COLUMNS) * VOXEL_ATLAS_SLOT;

    for (unsigned layer = 0; layer < 2; ++layer)
        for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
        {
            unsigned dstX = (quadrant % 2) * SUBTILE_PX, dstY = (quadrant / 2) * SUBTILE_PX;
            uint16_t entry = entries[layer * 4 + quadrant];
            unsigned tileId = entry & 0x3FF, paletteId = (entry >> 12) & 0xF;
            bool flipX = (entry >> 10) & 1, flipY = (entry >> 11) & 1;
            const uint8_t *tiles = tileId < TILES_PER_TILESET ? src->primaryTiles : src->secondaryTiles;
            unsigned localTile = tileId < TILES_PER_TILESET ? tileId : tileId - TILES_PER_TILESET;

            if (tiles == NULL)
                continue;
            for (unsigned py = 0; py < SUBTILE_PX; ++py)
                for (unsigned px = 0; px < SUBTILE_PX; ++px)
                {
                    unsigned srcX = flipX ? (SUBTILE_PX - 1 - px) : px;
                    unsigned srcY = flipY ? (SUBTILE_PX - 1 - py) : py;
                    uint8_t packed = tiles[localTile * 32 + srcY * 4 + srcX / 2];
                    unsigned colorIdx = (srcX & 1) ? (packed >> 4) : (packed & 0xF);
                    uint16_t bgr15;

                    if (layer != 0 && colorIdx == 0)
                        continue;
                    if (layer == 0 && colorIdx == 0)
                        bgr15 = src->primaryPalettes != NULL ? src->primaryPalettes[0] : 0;
                    else
                        bgr15 = LookupColor(src, paletteId, colorIdx);
                    dest[CtrVideo_Texel(baseX + dstX + px, baseY + dstY + py, VOXEL_ATLAS_W)] =
                        VoxelGrade_RGBA5551(bgr15);
                }
        }
}

static bool ReferenceBlack(const struct AtlasSource *src, const uint16_t *entries)
{
    for (unsigned layer = 0; layer < 2; ++layer)
        for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
        {
            uint16_t entry = entries[layer * 4 + quadrant];
            unsigned tileId = entry & 0x3FF, paletteId = (entry >> 12) & 0xF;
            const uint8_t *tiles = tileId < TILES_PER_TILESET ? src->primaryTiles : src->secondaryTiles;
            unsigned local = tileId < TILES_PER_TILESET ? tileId : tileId - TILES_PER_TILESET;

            if (tiles == NULL)
                return false;
            for (unsigned b = 0; b < 32; ++b)
                for (unsigned half = 0; half < 2; ++half)
                {
                    unsigned colorIdx = half ? (tiles[local * 32 + b] >> 4) : (tiles[local * 32 + b] & 0xF);
                    uint16_t bgr15;

                    if (colorIdx == 0)
                    {
                        if (layer != 0)
                            continue;
                        bgr15 = src->primaryPalettes != NULL ? src->primaryPalettes[0] : 0;
                    }
                    else
                        bgr15 = LookupColor(src, paletteId, colorIdx);
                    if ((bgr15 & 0x7FFF) != 0)
                        return false;
                }
        }
    return true;
}

static uint32_t sSeed = 12345;
static unsigned Random(void)
{
    sSeed = sSeed * 1103515245u + 12345u;
    return sSeed >> 8;
}

static uint16_t sNew[VOXEL_ATLAS_PIXELS], sOld[VOXEL_ATLAS_PIXELS];

static void Fill(unsigned blackEvery)
{
    for (unsigned i = 0; i < sizeof(sPrimaryTiles); ++i)
    {
        sPrimaryTiles[i] = (uint8_t)Random();
        sSecondaryTiles[i] = (uint8_t)Random();
    }
    /* A few all-zero tiles, which only draw the backdrop and layer 1 keys out. */
    memset(sPrimaryTiles + 7 * 32, 0, 32);
    memset(sSecondaryTiles + 9 * 32, 0, 32);
    for (unsigned i = 0; i < 16 * 16; ++i)
    {
        sPrimaryPalettes[i] = (uint16_t)(Random() & 0xFFFF);
        sSecondaryPalettes[i] = (uint16_t)(Random() & 0xFFFF);
        if (blackEvery && i % blackEvery != 1)
        {
            sPrimaryPalettes[i] &= 0x8000;
            sSecondaryPalettes[i] &= 0x8000;
        }
    }
}

static void Run(struct AtlasSource *src, unsigned trials, unsigned *blacks)
{
    BuildColorTable(src);
    InitMorton();
    for (unsigned t = 0; t < trials; ++t)
    {
        uint16_t entries[8];
        unsigned slot = Random() % VOXEL_SOLID_BASE;
        bool black;

        for (unsigned e = 0; e < 8; ++e)
            entries[e] = (uint16_t)(Random() & 0xFFFF);
        if (t % 5 == 0)
            entries[Random() % 8] = (uint16_t)((Random() & 0xF000) | 7); /* blank tile */
        memset(sNew, 0x5A, sizeof(sNew));
        memset(sOld, 0x5A, sizeof(sOld));
        black = ComposeMetatile(src, entries, sNew, slot, 0, NULL);
        ReferenceCompose(src, entries, sOld, slot);
        assert(memcmp(sNew, sOld, sizeof(sNew)) == 0);
        assert(black == ReferenceBlack(src, entries));
        *blacks += black;
    }
}

/* ── Whole atlases: composed a slice at a time, exactly as in one go ────── */

static uint16_t sOneGo[VOXEL_ATLAS_PIXELS], sSliced[VOXEL_ATLAS_PIXELS];

static void Sliced(const VoxelMapInstance *inst, const VoxelAtlasMap *base, VoxelAtlasMap *out,
                   unsigned *added)
{
    static VoxelAtlasJob job;
    unsigned steps = 0;

    memset(sSliced, 0x5A, sizeof(sSliced));
    if (!VoxelAtlas_JobBegin(&job, inst, sSliced, base))
    {
        assert(base != NULL && job.ok && !job.active);
        *out = *base;
        *added = 0;
        return;
    }
    while (!VoxelAtlas_JobStep(&job, 1 + Random() % 9))
        ++steps;
    assert(steps > 20 && job.pageReady && job.ok);
    *out = job.map;
    *added = job.added;
    VoxelAtlas_JobCancel(&job);
}

static void WholeAtlases(void)
{
    VoxelMapInstance inst;
    VoxelAtlasMap oneGo, sliced, base;
    unsigned added;

    memset(&inst, 0, sizeof(inst));
    inst.primaryTileset = &kPrimary;
    inst.secondaryTileset = &kSecondary;
    Fill(0);
    for (unsigned i = 0; i < sizeof(sMetatiles) / sizeof(sMetatiles[0]); ++i)
        sMetatiles[i] = (uint16_t)(Random() & 0xFFFF);

    /* A fresh atlas of a map that references about a third of the ids. */
    for (unsigned m = 0; m < 1024; ++m)
        sUsed[m] = Random() % 3 == 0;
    memset(sOneGo, 0x5A, sizeof(sOneGo));
    assert(VoxelAtlas_Build(&inst, sOneGo, &oneGo, false));
    Sliced(&inst, NULL, &sliced, &added);
    assert(memcmp(sOneGo, sSliced, sizeof(sOneGo)) == 0);
    assert(memcmp(&oneGo, &sliced, sizeof(oneGo)) == 0);
    assert(added == oneGo.used && !oneGo.overflowed);

    /* The same atlas extended with more ids: nothing it had moves. */
    base = oneGo;
    for (unsigned m = 0; m < 1024; ++m)
        sUsed[m] = sUsed[m] || Random() % 4 == 0;
    assert(VoxelAtlas_Build(&inst, sOneGo, &oneGo, true));
    Sliced(&inst, &base, &sliced, &added);
    assert(memcmp(sOneGo, sSliced, sizeof(sOneGo)) == 0);
    assert(memcmp(&oneGo, &sliced, sizeof(oneGo)) == 0);
    assert(added == oneGo.used - base.used && added > 0);
    for (unsigned m = 0; m < 1024; ++m)
        assert(base.slotOf[m] == 0 || oneGo.slotOf[m] == base.slotOf[m]);

    /* Every real id: three pages, and no valid material becomes absent. */
    base = oneGo;
    memset(sUsed, 1, sizeof(sUsed));
    Sliced(&inst, &base, &sliced, &added);
    assert(!sliced.overflowed && sliced.used == 1024);
    for (unsigned m = 0; m < 1024; ++m)
        assert(sliced.slotOf[m] != 0 && sliced.slotOf[m] != VOXEL_SLOT_ABSENT);
    /* And extended again with nothing new, it adds nothing. */
    base = sliced;
    Sliced(&inst, &base, &sliced, &added);
    assert(added == 0 && memcmp(&base, &sliced, sizeof(base)) == 0);
}

static void PagedAtlases(void)
{
    static const unsigned sizes[] = {504, 505, 1024};
    static VoxelAtlasJob job;
    static uint16_t dest[VOXEL_ATLAS_PIXELS + 16];
    VoxelMapInstance inst = {0};
    struct AtlasSource src = {0};
    inst.primaryTileset = &kPrimary;
    inst.secondaryTileset = &kSecondary;
    Fill(0);
    for (unsigned i = 0; i < sizeof(sMetatiles) / sizeof(sMetatiles[0]); ++i)
        sMetatiles[i] = (uint16_t)Random();
    for (unsigned n = 0; n < sizeof(sizes) / sizeof(sizes[0]); ++n)
    {
        memset(sUsed, 0, sizeof(sUsed));
        memset(sUsed, 1, sizes[n]);
        for (unsigned i = VOXEL_ATLAS_PIXELS; i < VOXEL_ATLAS_PIXELS + 16; ++i)
            dest[i] = 0xCAFE;
        assert(VoxelAtlas_JobBegin(&job, &inst, dest, NULL));
        unsigned pages = 0;
        do
        {
            while (!VoxelAtlas_JobStep(&job, 7)) {}
            assert(job.ok && job.pageReady && job.map.used == sizes[n]);
            memset(sOld, 0, sizeof(sOld));
            FillSolidSlots(sOld);
            /* Job releases decoded tiles on its last page; fixture data is
             * independently available for the per-texel reference. */
            src.primaryTiles = sPrimaryTiles;
            src.secondaryTiles = sSecondaryTiles;
            src.primaryPalettes = sPrimaryPalettes;
            src.secondaryPalettes = sSecondaryPalettes;
            for (unsigned m = 0; m < sizes[n]; ++m)
            {
                unsigned slot = job.map.slotOf[m] - 1u;
                assert(job.map.slotOf[m] != VOXEL_SLOT_ABSENT);
                if (slot / VOXEL_ATLAS_MAX_SLOTS == job.page)
                    ReferenceCompose(&src, sMetatiles + m * 8u, sOld, slot % VOXEL_ATLAS_MAX_SLOTS);
            }
            assert(memcmp(dest, sOld, sizeof(sOld)) == 0);
            for (unsigned i = VOXEL_ATLAS_PIXELS; i < VOXEL_ATLAS_PIXELS + 16; ++i)
                assert(dest[i] == 0xCAFE);
            ++pages;
        } while (VoxelAtlas_JobNextPage(&job));
        assert(pages == (sizes[n] + 503u) / 504u && !job.active);
    }
}

/* Repeated extension requests must not decode or overwrite an unchanged atlas. */
static void UnchangedExtension(void)
{
    VoxelMapInstance inst = {.primaryTileset = &kPrimary, .secondaryTileset = &kSecondary};
    VoxelAtlasMap map;
    static VoxelAtlasJob job;
    memset(sUsed, 0, sizeof(sUsed));
    sUsed[1] = 2;
    Fill(0);
    assert(VoxelAtlas_Build(&inst, sOneGo, &map, false));
    memcpy(sSliced, sOneGo, sizeof(sOneGo));
    assert(!VoxelAtlas_JobBegin(&job, &inst, sSliced, &map));
    assert(!job.active && job.primaryTiles == NULL && job.secondaryTiles == NULL);
    assert(memcmp(sOneGo, sSliced, sizeof(sOneGo)) == 0);
    assert(VoxelAtlas_Build(&inst, sSliced, &map, true));
    assert(memcmp(sOneGo, sSliced, sizeof(sOneGo)) == 0);
    /* A newly required id must still schedule its extension. */
    sUsed[2] = 1;
    assert(VoxelAtlas_JobBegin(&job, &inst, sSliced, &map));
    while (!VoxelAtlas_JobStep(&job, 17)) {}
    assert(job.ok && job.map.slotOf[2] != 0 && job.map.slotOf[1] == map.slotOf[1]);
    /* Pending slots also require work, never the unchanged fast path. */
    map = job.map; map.slotOf[2] = VOXEL_SLOT_PENDING;
    assert(VoxelAtlas_JobBegin(&job, &inst, sSliced, &map));
    VoxelAtlas_JobCancel(&job);
}

static void AnimatedTiles(void)
{
    VoxelMapInstance inst = {0};
    VoxelAtlasMap map;
    uint8_t live[1024 * 32], dirty[128] = {0};
    struct AtlasSource src = {0};
    unsigned slot1, slot2;

    inst.primaryTileset = &kPrimary;
    inst.secondaryTileset = &kSecondary;
    memset(sUsed, 0, sizeof(sUsed));
    memset(sMetatiles, 0, sizeof(sMetatiles));
    sUsed[1] = sUsed[2] = 1;
    for (unsigned t = 0; t < 4; ++t)
    {
        sMetatiles[1 * 8 + t] = 432;
        sMetatiles[2 * 8 + t] = 10;
    }
    Fill(0);
    assert(VoxelAtlas_Build(&inst, sOneGo, &map, false));
    slot1 = map.slotOf[1] - 1;
    slot2 = map.slotOf[2] - 1;
    assert(slot1 != slot2);
    memcpy(sSliced, sOneGo, sizeof(sOneGo));
    memcpy(live, sPrimaryTiles, sizeof(sPrimaryTiles));
    memcpy(live + sizeof(sPrimaryTiles), sSecondaryTiles, sizeof(sSecondaryTiles));
    memset(live + 432 * 32, 0x11, 32);
    dirty[432 >> 3] = 1u << (432 & 7);
    assert(VoxelAtlas_RefreshAnimated(&inst, &map, sOneGo, live, dirty) == 1);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
        {
            unsigned offset = CtrVideo_Texel((slot2 % VOXEL_ATLAS_COLUMNS) * 16 + x,
                                               (slot2 / VOXEL_ATLAS_COLUMNS) * 16 + y,
                                               VOXEL_ATLAS_W);
            assert(sOneGo[offset] == sSliced[offset]);
        }
    src.primaryTiles = live;
    src.secondaryTiles = live + sizeof(sPrimaryTiles);
    src.primaryPalettes = sPrimaryPalettes;
    src.secondaryPalettes = sSecondaryPalettes;
    src.primaryMetatiles = sMetatiles;
    src.secondaryMetatiles = sMetatiles + 512 * 8;
    memset(sOld, 0, sizeof(sOld));
    ReferenceCompose(&src, sMetatiles + 8, sOld, slot1);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
        {
            unsigned offset = CtrVideo_Texel((slot1 % VOXEL_ATLAS_COLUMNS) * 16 + x,
                                               (slot1 / VOXEL_ATLAS_COLUMNS) * 16 + y,
                                               VOXEL_ATLAS_W);
            assert(sOneGo[offset] == sOld[offset]);
        }
    memset(dirty, 0, sizeof(dirty));
    assert(VoxelAtlas_RefreshAnimated(&inst, &map, sOneGo, live, dirty) == 0);

    /* The rows of slots a refresh wrote - all of the page a changed frame has
     * to upload: three animated metatiles in three rows, and every texel of
     * each inside its row's contiguous bytes. */
    {
        const unsigned animated[3] = {5, 40, 66};
        unsigned rows = 0, expected = 0;

        memset(sUsed, 0, sizeof(sUsed));
        memset(sMetatiles, 0, sizeof(sMetatiles));
        for (unsigned id = 1; id <= 70; ++id)
        {
            sUsed[id] = 1;
            for (unsigned t = 0; t < 4; ++t)
                sMetatiles[id * 8 + t] = (id == 5 || id == 40 || id == 66) ? 432 : 10;
        }
        Fill(0);
        assert(VoxelAtlas_Build(&inst, sOneGo, &map, false));
        for (unsigned i = 0; i < 3; ++i)
            expected |= 1u << (((map.slotOf[animated[i]] - 1u) % VOXEL_ATLAS_MAX_SLOTS) / VOXEL_ATLAS_COLUMNS);
        assert(expected == 7u); /* slots 4, 39, 65: rows 0, 1, 2 */
        memcpy(live, sPrimaryTiles, sizeof(sPrimaryTiles));
        memcpy(live + sizeof(sPrimaryTiles), sSecondaryTiles, sizeof(sSecondaryTiles));
        memset(live + 432 * 32, 0x11, 32);
        memset(dirty, 0, sizeof(dirty));
        dirty[432 >> 3] = 1u << (432 & 7);
        assert(VoxelAtlas_RefreshAnimatedPageRows(&inst, &map, 0, sOneGo, live, dirty, &rows) == 3);
        assert(rows == expected);
        for (unsigned i = 0; i < 3; ++i)
        {
            unsigned slot = (map.slotOf[animated[i]] - 1u) % VOXEL_ATLAS_MAX_SLOTS;
            unsigned row = slot / VOXEL_ATLAS_COLUMNS;

            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x)
                {
                    unsigned bytes = 2u * CtrVideo_Texel((slot % VOXEL_ATLAS_COLUMNS) * 16 + x,
                                                         row * 16 + y, VOXEL_ATLAS_W);

                    assert(bytes >= row * VOXEL_ATLAS_ROW_BYTES
                        && bytes < (row + 1u) * VOXEL_ATLAS_ROW_BYTES);
                }
        }
        /* The same slots redrawn alone, a couple at a time: each one's two runs
         * are exactly what the page holds after the full refresh. */
        {
            static VoxelAnimSlot out[2];
            unsigned cursor = 0, seen = 0, calls = 0;

            while (cursor < VOXEL_METATILE_IDS)
            {
                unsigned n = VoxelAtlas_AnimatedSlots(&inst, &map, 0, live, dirty, &cursor, out, 2);

                assert(n <= 2 && ++calls < 100);
                for (unsigned i = 0; i < n; ++i, ++seen)
                {
                    unsigned c = out[i].slot % VOXEL_ATLAS_COLUMNS, r = out[i].slot / VOXEL_ATLAS_COLUMNS;
                    unsigned b0 = 2u * r * (VOXEL_ATLAS_W / 8u) + 2u * c;

                    assert(memcmp(out[i].pixels, sOneGo + b0 * 64u, 256) == 0);
                    assert(memcmp(out[i].pixels + 128, sOneGo + (b0 + VOXEL_ATLAS_W / 8u) * 64u, 256) == 0);
                }
            }
            assert(seen == 3 && calls >= 2);
        }
        memset(dirty, 0, sizeof(dirty));
        assert(VoxelAtlas_RefreshAnimatedPageRows(&inst, &map, 0, sOneGo, live, dirty, &rows) == 0
            && rows == 0);
    }
}

/* All real ids plus building/cut variants: fourth page and live animation. */
static void DerivedPages(void)
{
    VoxelMapInstance inst = {.primaryTileset = &kPrimary, .secondaryTileset = &kSecondary};
    static VoxelAtlasJob job;
    uint8_t live[1024 * 32], dirty[128] = {0};
    sDerived = true;
    sVariantsResolved = sCutsResolved = false;
    Fill(0);
    memset(sPrimaryTiles, 0, 32); /* transparent upper layer */
    memset(sMetatiles, 0, sizeof(sMetatiles));
    for (unsigned m = 0; m < 1024; ++m)
        for (unsigned q = 0; q < 4; ++q) sMetatiles[m * 8 + q] = 432;
    memset(sUsed, 1, sizeof(sUsed));
    sUsed[1023] = 2; /* Visible material precedes all prefetch ids. */
    memcpy(live, sPrimaryTiles, sizeof(sPrimaryTiles));
    memcpy(live + sizeof(sPrimaryTiles), sSecondaryTiles, sizeof(sSecondaryTiles));
    memset(live + 432 * 32, 0x11, 32);
    dirty[432 >> 3] = 1u << (432 & 7);
    assert(VoxelAtlas_JobBegin(&job, &inst, sOneGo, NULL));
    unsigned pages = 0;
    do
    {
        while (!VoxelAtlas_JobStep(&job, 17)) {}
        assert(job.ok && !job.map.overflowed && job.map.used == VOXEL_METATILE_IDS);
        assert(job.map.slotOf[1023] == 1);
        unsigned expected = job.map.used - job.page * VOXEL_SOLID_BASE;
        if (expected > VOXEL_SOLID_BASE) expected = VOXEL_SOLID_BASE;
        assert(VoxelAtlas_RefreshAnimatedPage(&inst, &job.map, job.page, sOneGo, live, dirty) == expected);
        struct AtlasSource src = {.primaryTiles = live, .secondaryTiles = live + sizeof(sPrimaryTiles),
            .primaryPalettes = sPrimaryPalettes, .secondaryPalettes = sSecondaryPalettes};
        memset(sOld, 0, sizeof(sOld)); FillSolidSlots(sOld);
        for (unsigned m = 0; m < VOXEL_METATILE_IDS; ++m)
        {
            unsigned slot = job.map.slotOf[m] - 1u;
            assert(slot / VOXEL_ATLAS_MAX_SLOTS < VOXEL_ATLAS_PAGES);
            if (slot / VOXEL_ATLAS_MAX_SLOTS != job.page) continue;
            unsigned local = slot % VOXEL_ATLAS_MAX_SLOTS;
            unsigned base = m < 1024 ? m : m < VOXEL_CUT_FIRST ? m - 1024 : m - VOXEL_CUT_FIRST;
            ReferenceCompose(&src, sMetatiles + base * 8, sOld, local);
            if (m >= VOXEL_CUT_FIRST)
                sOld[CtrVideo_Texel(local % VOXEL_ATLAS_COLUMNS * 16,
                                     local / VOXEL_ATLAS_COLUMNS * 16, VOXEL_ATLAS_W)] &= (uint16_t)~1u;
        }
        assert(memcmp(sOneGo, sOld, sizeof(sOld)) == 0);
        ++pages;
    } while (VoxelAtlas_JobNextPage(&job));
    assert(pages == 4 && !job.active);
    sDerived = false;
}

bool Voxel_LoadTilesStep(const void *t, uint8_t *d, uint32_t n, VoxelTileLoad *load, unsigned bytes)
{
    (void)bytes;
    load->ok = Voxel_LoadTiles(t, d, n);
    load->done = load->initialized = true;
    return true;
}

int main(void)
{
    struct AtlasSource src;
    unsigned blacks = 0;

    memset(&src, 0, sizeof(src));
    src.primaryTiles = sPrimaryTiles;
    src.secondaryTiles = sSecondaryTiles;
    src.primaryPalettes = sPrimaryPalettes;
    src.secondaryPalettes = sSecondaryPalettes;

    Fill(0);
    Run(&src, 2000, &blacks);
    /* Mostly black, then all black: the verdict is exercised both ways. */
    Fill(16);
    Run(&src, 2000, &blacks);
    Fill(1);
    Run(&src, 2000, &blacks);
    assert(blacks != 0);
    /* Missing secondary tiles and palettes. */
    src.secondaryTiles = NULL;
    src.secondaryPalettes = NULL;
    Run(&src, 2000, &blacks);
    src.primaryPalettes = NULL;
    Run(&src, 1000, &blacks);
    WholeAtlases();
    AnimatedTiles();
    PagedAtlases();
    VoxelGrade_Init();
    WholeAtlases();
    AnimatedTiles();
    PagedAtlases();
    DerivedPages();
    UnchangedExtension();
    printf("PASS voxel atlas: block composer equals per-texel reference (%u black), "
           "sliced atlases equal one-go ones, fresh and extended; unchanged extensions skipped, selective animation, four pages with variants and visible priority\n", blacks);
    return 0;
}
