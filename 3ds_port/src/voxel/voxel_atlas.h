/*
 * Metatile atlas for the voxel overworld.
 *
 * Packed 512x256 pages per tileset pair, with 504 metatiles and eight solid
 * slots per page. Pages are allocated lazily from a shared VRAM budget. The
 * id-to-slot table preserves assignments when a map needs more materials.
 * Composition matches the reference's two layers of four 8x8 subtiles;
 * see NOTICE.md for the reference renderer's MIT attribution.
 *
 * The destination is written in PICA200 texture order, so the caller hands in
 * the raw pixel buffer of a C3D_Tex and nothing here needs libctru.
 */
#ifndef CTR_VOXEL_ATLAS_H
#define CTR_VOXEL_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_world.h"

#define VOXEL_ATLAS_W       512u
#define VOXEL_ATLAS_H       256u
#define VOXEL_ATLAS_SLOT    16u
#define VOXEL_ATLAS_COLUMNS (VOXEL_ATLAS_W / VOXEL_ATLAS_SLOT)
#define VOXEL_ATLAS_ROWS    (VOXEL_ATLAS_H / VOXEL_ATLAS_SLOT)
#define VOXEL_ATLAS_MAX_SLOTS (VOXEL_ATLAS_COLUMNS * VOXEL_ATLAS_ROWS)
#define VOXEL_ATLAS_PIXELS  (VOXEL_ATLAS_W * VOXEL_ATLAS_H)

/*
 * The reference draws furniture legs, table sides and television bodies as
 * untextured `glColor3f` geometry. This shader always samples texture0, so
 * rather than add a per-vertex colour - eight more bytes on every vertex, in a
 * build already short of linear memory - the last few atlas slots hold those
 * flat colours. A "solid" face is then an ordinary textured face whose UVs
 * point at one of them.
 */
typedef enum
{
    VOXEL_SOLID_WOOD = 0,   /* table sides */
    VOXEL_SOLID_WOOD_DARK,  /* table legs */
    VOXEL_SOLID_GREY,       /* television stand */
    VOXEL_SOLID_BLACK,      /* television body */
    VOXEL_SOLID_SHEET,      /* bedding */
    VOXEL_SOLID_COUNT
} VoxelSolidColor;

#define VOXEL_SOLID_SLOTS 8u
#define VOXEL_SOLID_BASE  (VOXEL_ATLAS_MAX_SLOTS - VOXEL_SOLID_SLOTS)

/* Texture coordinates of one of the flat colours. */
void VoxelAtlas_SolidUV(VoxelSolidColor color, float *u0, float *v0, float *u1, float *v1);

/*
 * One entry per metatile id. 0 means this atlas has never seen the id, which
 * is the renderer's cue that a script placed something new and the atlas needs
 * rebuilding once. VOXEL_SLOT_ABSENT means it was seen and has no pixels.
 * VOXEL_SLOT_PENDING requests a retry; other entries are page*512 + slot + 1.
 */
#define VOXEL_METATILE_REAL 1024u
/*
 * After the real ids, the ground variants (voxel_building.h): a metatile less
 * the quarters of its upper layer a model stands for - the water round a rock
 * in the sea, without the rock. Id VOXEL_METATILE_REAL + i is variant i of
 * buildings.bin; it is composed, extended and animated like any other id.
 */
#define VOXEL_VARIANTS 128u
/*
 * Then the cut tiles (voxel_relief.h): a rock's tile with the ground drawn
 * behind it clear, for its relief to stand over the ground drawn flat. Id
 * VOXEL_CUT_FIRST + i is cut variant i of relief.bin.
 */
#define VOXEL_CUTS 512u
#define VOXEL_CUT_FIRST (VOXEL_METATILE_REAL + VOXEL_VARIANTS)
#define VOXEL_METATILE_IDS (VOXEL_CUT_FIRST + VOXEL_CUTS)
#define VOXEL_SLOT_ABSENT  0xFFFFu
#define VOXEL_SLOT_PENDING 0xFFFEu
#define VOXEL_ATLAS_PAGES ((VOXEL_METATILE_IDS + VOXEL_SOLID_BASE - 1u) / VOXEL_SOLID_BASE)

typedef struct VoxelAtlasMap
{
    uint16_t slotOf[VOXEL_METATILE_IDS];
    uint8_t tileMask[VOXEL_ATLAS_PAGES][128]; /* source tiles referenced per page */
    unsigned used;      /* slots actually composed */
    bool overflowed;    /* the map referenced more ids than the atlas holds */
} VoxelAtlasMap;

/*
 * Synchronous convenience for page zero only. Fills `dest` (VOXEL_ATLAS_PIXELS
 * uint16_t, RGBA5551, PICA order) and `map` with assignments for all pages.
 * Use the job API to compose every page. Returns false if nothing could be composed, in which case `dest` is
 * left cleared.
 *
 * With `extend`, `map` is an atlas already in use that has met ids it was not
 * built with: every id keeps its slot and is redrawn there, and only the new
 * ones take free slots - so nothing drawn from the old atlas goes stale.
 * If every requested material is already resolved, succeeds with `dest`
 * untouched: the caller retains the atlas pixels already in use.
 */
bool VoxelAtlas_Build(const VoxelMapInstance *inst, uint16_t *dest, VoxelAtlasMap *map,
                      bool extend);

/*
 * The same composition spread over several calls, so that no frame pays for
 * all of it. Begin marks the ids and clears `dest`; each Step decodes at most 512 bytes of one
 * tileset or composes up to `metatiles` metatiles, and returns true once the
 * current page is ready. Upload it, wait for completion, then call NextPage.
 * Publish `map` only with the last page (active == false), if `ok` is true. With `base`, the job extends
 * that table (see VoxelAtlas_Build) rather than starting an empty one.
 *
 * The tileset pair is read afresh at every step; the caller must not change
 * `dest` in between, and cancels the job if the pair stops being wanted.
 */
typedef struct
{
    bool active, extend, ok;
    const void *primary, *secondary;
    int mapGroup, mapNum;
    uint16_t *dest;
    VoxelAtlasMap map;
    uint8_t used[VOXEL_METATILE_IDS];
    uint8_t *primaryTiles, *secondaryTiles;
    bool primaryLoaded, secondaryLoaded;
    VoxelTileLoad tileLoad;
    unsigned phase, next, priority;
    unsigned added;     /* ids given a slot of their own by this job */
    unsigned page;      /* current staging page; slotOf encodes page*512+slot+1 */
    bool pageReady;     /* CPU must not touch dest until its GPU upload retires */
} VoxelAtlasJob;

/* False with job->ok means base already covers the request: no buffers/upload. */
bool VoxelAtlas_JobBegin(VoxelAtlasJob *job, const VoxelMapInstance *inst, uint16_t *dest,
                         const VoxelAtlasMap *base);
bool VoxelAtlas_JobStep(VoxelAtlasJob *job, unsigned metatiles);
void VoxelAtlas_JobCancel(VoxelAtlasJob *job);
unsigned VoxelAtlas_PageCount(const VoxelAtlasMap *map);
/* Call only after the preceding page's upload has completed. */
bool VoxelAtlas_JobNextPage(VoxelAtlasJob *job);
/* Recompose only slots whose source 8x8 tiles were changed in BG_VRAM.
 * dest contains the complete prior atlas and remains valid for unaffected ids. */
unsigned VoxelAtlas_RefreshAnimated(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                    uint16_t *dest, const uint8_t *liveTiles,
                                    const uint8_t dirtyTiles[128]);
unsigned VoxelAtlas_RefreshAnimatedPage(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                       unsigned page, uint16_t *dest, const uint8_t *liveTiles,
                                       const uint8_t dirtyTiles[128]);
/*
 * The same, also giving the rows of slots it wrote: bit r of `*rows` is the
 * slots 32r..32r+31 of the page (VOXEL_ATLAS_ROWS of them). A row of slots is
 * VOXEL_ATLAS_ROW_BYTES of the page's memory, contiguous (the PICA's 8x8 blocks
 * run along the page, so a row of 16-pixel slots is two block rows), which is
 * all of the page a changed animation frame needs uploading.
 */
#define VOXEL_ATLAS_ROW_BYTES (VOXEL_ATLAS_SLOT * VOXEL_ATLAS_W * 2u)
unsigned VoxelAtlas_RefreshAnimatedPageRows(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                            unsigned page, uint16_t *dest, const uint8_t *liveTiles,
                                            const uint8_t dirtyTiles[128], unsigned *rows);

/* Does this metatile of the instance's tileset pair draw nothing but black -
 * an interior's outside? Known once the pair's atlas has been built. */
bool VoxelAtlas_IsVoid(const VoxelMapInstance *inst, int metatile);

/*
 * One animated slot, redrawn alone: `pixels` is the 16x16 slot in the PICA's
 * order, which is the slot's two runs of 256 bytes in its page, one after the
 * other (blocks (2c,2r),(2c+1,2r) at block index 128r+2c, then the same one
 * block row down, 64 blocks further; a block is 128 bytes).
 */
typedef struct
{
    uint16_t pixels[VOXEL_ATLAS_SLOT * VOXEL_ATLAS_SLOT];
    unsigned slot; /* within the page */
    unsigned pad[3]; /* the next one starts on 16 bytes, as the GPU copy wants */
} VoxelAnimSlot;
unsigned VoxelAtlas_AnimatedSlots(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                  unsigned page, const uint8_t *liveTiles,
                                  const uint8_t work[128], unsigned *cursor,
                                  VoxelAnimSlot *out, unsigned max);

/* Texture coordinates of a packed slot, in the PICA convention where v=1 is
 * the first row in memory. U includes page*2, stripped by the chunk packer. */
void VoxelAtlas_SlotUV(unsigned slot, float *u0, float *v0, float *u1, float *v1);

#endif
