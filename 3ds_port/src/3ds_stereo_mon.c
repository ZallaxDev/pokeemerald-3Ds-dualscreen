/*
 * The stereoscopic Pokémon viewer, native side: the planes file, the texture
 * the planes are composed into, and the drawing (3ds_stereo_mon.h).
 *
 * A Pokémon is a stack of flat cut-outs, one per depth plane, like the
 * layered sprites the 3DS's own 2D games use: each plane is drawn shifted
 * sideways for each eye in proportion to its depth (front projection, which
 * is what the Sprite Stereo Lab previews). The stack never turns: turned,
 * each plane would slide by its depth and the slices show at the edges,
 * and the cards would lose their whole-pixel scale.
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "3ds_platform.h"
#include "3ds_video.h"
#include "3ds_input.h"
#include "3ds_log.h"
#include "3ds_stereo_mon.h"

/*
 * One texture in linear memory with a 64x64 tile per frame and plane, two
 * tiles across once there are more than eight, at most 128x512 (128 KiB):
 * the linear heap has a few hundred KiB spare once the field is up. Both
 * frames of the picture fit with up to eight planes; with more, only the
 * first frame is kept and the animation rests. Fewer planes are fewer tiles
 * and a smaller texture. Made once, at the largest size it will need, and
 * kept: the emulator's texture cache keys surfaces by address, and a fresh
 * texture over memory a freed one used was read with the old one's layout.
 */
#define DIM CTR_STEREO_MON_DIM
#define TEX_MAX_TILES 32
/* Up to five frames of four groups: 32 tiles, four across. */
#define TEX_MAX_W (4 * DIM)
#define TEX_MAX_H 512
/* Tiles composed per frame, at most. */
#define TILES_PER_FRAME 6
/* Screen pixels per sprite pixel, and where the sprite's middle sits. */
#define SCALE 3.0f
#define CENTRE_X (CTR_GAME_WIDTH / 2)
#define CENTRE_Y 130.0f
/* Total stack depth in sprite pixels, independent of the exported plane count. */
#define DEPTH_SPAN 14.4f /* Original ten-plane span, independent of plane count. */
/* The plane that lies on the screen: most of the body behind it, the
 * nearest planes a little in front. */
#define PIVOT_FRACTION 0.62f
/* Parallax at the slider's top and depth 100%, in screen pixels per sprite
 * pixel of depth: the whole stack of ten planes spans about ten pixels. */
#define PARALLAX 0.7f
/* The animation: the second frame for a moment, every so often. */
#define ANIM_PERIOD 150
#define ANIM_HOLD 14
#define DEPTH_KEY "stereo_depth"
#define DEPTH_STEP 25
#define DEPTH_MAX 300
/* How many planes the file's are merged into (PLANES_KEY): fewer are
 * thicker slices, fewer tiles and less VRAM, the same depth overall. */
#define PLANES_KEY "stereo_planes"
#define GROUPS_MIN 2
/* Pixels of the planes behind carried on each card (CtrStereoMon_ComposeTile). */
/* Four planes 4.8 sprite pixels apart shift up to about three sprite pixels
 * between the front and the back at 100%: the far cards go on that far. */
#define SKIRT 4

/* A planes file: its header and index, resident. The viewer's own (the
 * Pokémon's fronts) and the battle's other pictures (the backs file). */
typedef struct
{
    const char *path;
    uint8_t index[CTR_STEREO_MON_HEADER_BYTES + CTR_STEREO_MON_INDEX_BYTES * CTR_STEREO_MON_MAX_COUNT];
    uint32_t indexBytes, fileSize;
    CtrStereoMonHeader header;
    bool loaded, tried;
} PlanesFile;
static PlanesFile sFront = {.path = CTR_STEREO_MON_FILE}, sBacks = {.path = CTR_STEREO_BACKS_FILE};
/* The viewer's shorthands for its own file. */
#define sIndex sFront.index
#define sIndexBytes sFront.indexBytes
#define sHeader sFront.header
#define sLoaded sFront.loaded
/* One Pokémon's planes, read on show, and the header they go with. */
static uint8_t sPlanes[CTR_STEREO_MON_MAX_FRAMES * DIM * DIM / 2];
static CtrStereoMonHeader sShown;
/* Where DrawPlanes put each plane for the eye being drawn. */
static float sPlaneX[CTR_STEREO_MON_MAX_PLANES], sPlaneY, sPlaneScale;

static C3D_Tex sTex;
static bool sTexReady, sTexAllocated;
/* The texture's layout for what is shown: tiles across, and how many of the
 * picture's frames it holds. */
static unsigned sTexCols, sTexFrames, sTexGroups;
static Tex3DS_SubTexture sTiles[CTR_STEREO_MON_MAX_FRAMES * CTR_STEREO_MON_MAX_PLANES];

/* What is shown: the picture is copied here and composed inside the frame,
 * tile by tile (sComposed of sToCompose done). */
static bool sActive;
static unsigned sComposed, sToCompose;
static uint16_t sId;
static unsigned sFrames;
static uint16_t sPicture[CTR_STEREO_MON_MAX_FRAMES][DIM * DIM];
static char sName[CTR_STEREO_MON_NAME];

/* The view: depth in percent and the number of planes (kept in the
 * settings), the animation. */
static int sDepth = -1;
static int sGroups = -1;
static bool sAnimate = true;
static uint32_t sTick;

static bool LoadFile(PlanesFile *pf)
{
    FILE *f;
    long size;

    if (pf->tried) return pf->loaded;
    pf->tried = true;
    f = CtrFs_OpenAsset(pf->path);
    if (f == NULL)
    {
        CtrLog_Write(CTR_LOG_VIDEO, "stereo: no %s", pf->path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    pf->indexBytes = (uint32_t)fread(pf->index, 1, sizeof(pf->index), f);
    fclose(f);
    if (size > 0 && CtrStereoMon_ParseHeader(pf->index, pf->indexBytes, &pf->header) && pf->header.count > 0
        && pf->header.count <= CTR_STEREO_MON_MAX_COUNT)
    {
        pf->fileSize = (uint32_t)size;
        pf->loaded = true;
        CtrLog_Write(CTR_LOG_VIDEO, "stereo: %s: %u entries, %u planes, up to %u frames (%ld bytes)", pf->path,
                     pf->header.count, pf->header.planes, pf->header.frames, size);
    }
    else
        CtrLog_Write(CTR_LOG_ERROR, "stereo: %s is not a planes file (%ld bytes)", pf->path, size);
    return pf->loaded;
}

bool CtrStereoMon_Load(void) { return LoadFile(&sFront); }

/* The gallery's order: the fronts, then the backs file by kind. */
static uint16_t sGallery_[2 * CTR_STEREO_MON_MAX_COUNT];
static unsigned sGalleryCount;
static bool sGalleryBuilt;

static void GalleryBuild(void)
{
    static const uint16_t kinds[] = {CTR_STEREO_ID_BACK, CTR_STEREO_ID_TRAINER, CTR_STEREO_ID_PLAYER};

    if (sGalleryBuilt) return;
    sGalleryBuilt = true;
    sGalleryCount = 0;
    if (!LoadFile(&sFront)) return;
    for (unsigned i = 0; i < sHeader.count; ++i) sGallery_[sGalleryCount++] = CtrStereoMon_NationalAt(i);
    if (!LoadFile(&sBacks)) return;
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned i = 0; i < sBacks.header.count && sGalleryCount < 2 * CTR_STEREO_MON_MAX_COUNT; ++i)
        {
            uint16_t id = 0;

            if (CtrStereoMon_IndexEntry(sBacks.index, sBacks.indexBytes, &sBacks.header, i, &id, NULL)
                && (id & 0xC000) == kinds[k])
                sGallery_[sGalleryCount++] = id;
        }
}

unsigned CtrStereoMon_GalleryCount(void)
{
    GalleryBuild();
    return sGalleryCount;
}

uint16_t CtrStereoMon_GalleryIdAt(unsigned index)
{
    GalleryBuild();
    return index < sGalleryCount ? sGallery_[index] : 0;
}

int CtrStereoMon_GalleryIndexOf(uint16_t id)
{
    GalleryBuild();
    for (unsigned i = 0; i < sGalleryCount; ++i)
        if (sGallery_[i] == id) return (int)i;
    return -1;
}

uint16_t CtrStereoMon_Counterpart(uint16_t id)
{
    if ((id & 0xC000) == 0) return CtrStereoMon_HasBack(CTR_STEREO_ID_BACK | id) ? (CTR_STEREO_ID_BACK | id) : 0;
    if ((id & 0xC000) == CTR_STEREO_ID_BACK) return CtrStereoMon_Has(id & 0x3FFF) ? (id & 0x3FFF) : 0;
    return 0;
}

/* The file an id's planes are in, and the id's index there (or -1). */
static PlanesFile *FileOf(uint16_t id, int *index)
{
    PlanesFile *pf = (id & 0xC000) ? &sBacks : &sFront;

    *index = LoadFile(pf) ? CtrStereoMon_Find(pf->index, pf->indexBytes, &pf->header, id) : -1;
    return pf;
}

unsigned CtrStereoMon_Count(void) { return sLoaded ? sHeader.count : 0; }

uint16_t CtrStereoMon_NationalAt(unsigned index)
{
    uint16_t national = 0;

    if (sLoaded) CtrStereoMon_IndexEntry(sIndex, sIndexBytes, &sHeader, index, &national, NULL);
    return national;
}

bool CtrStereoMon_Has(uint16_t national)
{
    return sLoaded && CtrStereoMon_Find(sIndex, sIndexBytes, &sHeader, national) >= 0;
}

const CtrStereoMonHeader *CtrStereoMon_Header(void) { return sLoaded ? &sHeader : NULL; }
const CtrStereoMonHeader *CtrStereoMon_BacksHeader(void) { return LoadFile(&sBacks) ? &sBacks.header : NULL; }

/* One entry's planes from a file into `out` (`size` bytes), with its frames. */
static bool ReadPlanesInto(PlanesFile *pf, unsigned index, uint8_t *out, uint32_t size, unsigned *frames)
{
    uint32_t offset, bytes;
    unsigned entryFrames = CtrStereoMon_EntryFrames(pf->index, pf->indexBytes, &pf->header, index);
    FILE *f;
    bool ok;

    bytes = CtrStereoMon_FramesBytes(&pf->header, entryFrames);
    if (entryFrames == 0 || !CtrStereoMon_IndexEntry(pf->index, pf->indexBytes, &pf->header, index, NULL, &offset)
        || offset > pf->fileSize || (uint64_t)offset + bytes > pf->fileSize || bytes > size)
        return false;
    f = CtrFs_OpenAsset(pf->path);
    if (f == NULL) return false;
    ok = fseek(f, (long)offset, SEEK_SET) == 0 && fread(out, 1, bytes, f) == bytes;
    fclose(f);
    if (ok && frames) *frames = entryFrames;
    return ok;
}

bool CtrStereoMon_PlanesFor(uint16_t national, uint8_t *out, uint32_t size)
{
    int index;

    if (!CtrStereoMon_Load()) return false;
    index = CtrStereoMon_Find(sIndex, sIndexBytes, &sHeader, national);
    return index >= 0 && ReadPlanesInto(&sFront, (unsigned)index, out, size, NULL);
}

bool CtrStereoMon_HasBack(uint16_t id)
{
    return LoadFile(&sBacks) && CtrStereoMon_Find(sBacks.index, sBacks.indexBytes, &sBacks.header, id) >= 0;
}

bool CtrStereoMon_BackPlanesFor(uint16_t id, uint8_t *out, uint32_t size, unsigned *frames)
{
    int index;

    if (!LoadFile(&sBacks)) return false;
    index = CtrStereoMon_Find(sBacks.index, sBacks.indexBytes, &sBacks.header, id);
    return index >= 0 && ReadPlanesInto(&sBacks, (unsigned)index, out, size, frames);
}

bool CtrStereoMon_Show(uint16_t national, const uint16_t *picture, unsigned frames, const char *name)
{
    return CtrStereoMon_ShowId(national, picture, frames, name);
}

bool CtrStereoMon_ShowId(uint16_t id, const uint16_t *picture, unsigned frames, const char *name)
{
    int index;
    PlanesFile *pf = FileOf(id, &index);
    unsigned entryFrames = 0;

    if (!CtrStereoMon_Load() || picture == NULL || frames == 0) return false;
    if (index < 0 || !ReadPlanesInto(pf, (unsigned)index, sPlanes, sizeof(sPlanes), &entryFrames))
    {
        CtrLog_Write(CTR_LOG_ERROR, "stereo: id %04x has no readable planes", id);
        return false;
    }
    if (frames > CTR_STEREO_MON_MAX_FRAMES) frames = CTR_STEREO_MON_MAX_FRAMES;
    memcpy(sPicture, picture, frames * sizeof(sPicture[0]));
    sFrames = frames;
    sId = id;
    sShown = pf->header;
    sShown.frames = (uint8_t)entryFrames;
    snprintf(sName, sizeof(sName), "%s", name ? name : "");
    if (sDepth < 0)
    {
        sDepth = CtrSettings_GetInt(DEPTH_KEY, 100);
        if (sDepth < 0 || sDepth > DEPTH_MAX) sDepth = 100;
    }
    if (sGroups < 0)
    {
        sGroups = CtrSettings_GetInt(PLANES_KEY, sHeader.planes);
        if (sGroups < GROUPS_MIN || sGroups > sHeader.planes) sGroups = sHeader.planes;
    }
    if (!sActive) sTick = 0;
    sActive = true;
    /* Composed afresh on the next frame (TextureReady). */
    sTexReady = false;
    return true;
}

/* The planes merged into another number of groups: composed again. */
static void SetGroups(int groups)
{
    if (groups < GROUPS_MIN) groups = GROUPS_MIN;
    if (groups > (int)sShown.planes) groups = (int)sShown.planes;
    if (groups == sGroups) return;
    sGroups = groups;
    CtrSettings_SetInt(PLANES_KEY, sGroups);
    sTexReady = false;
}

/* The planes drawn with: the setting, capped by the picture's own. */
static unsigned Groups(void)
{
    if (sGroups > (int)sShown.planes) return sShown.planes;
    return (unsigned)sGroups;
}

void CtrStereoMon_Hide(void) { sActive = false; }
bool CtrStereoMon_Active(void) { return sActive; }

void CtrStereoMon_Press(unsigned keys)
{
    if (!sActive) return;
    if (keys & CTR_KEY_UP) sDepth = sDepth + DEPTH_STEP > DEPTH_MAX ? DEPTH_MAX : sDepth + DEPTH_STEP;
    if (keys & CTR_KEY_DOWN) sDepth = sDepth < DEPTH_STEP ? 0 : sDepth - DEPTH_STEP;
    if (keys & (CTR_KEY_UP | CTR_KEY_DOWN)) CtrSettings_SetInt(DEPTH_KEY, sDepth);
    if (keys & CTR_KEY_L) SetGroups(sGroups - 1);
    if (keys & CTR_KEY_R) SetGroups(sGroups + 1);
    if (keys & CTR_KEY_X) sAnimate = !sAnimate;
}

/* ── The texture ─────────────────────────────────────────────────────── */

/* The texture for the Pokémon shown with the planes chosen: its layout set
 * and its tiles queued for composing whenever either changes (sTexReady
 * false), its memory made once; false without the memory for it. */
static bool TextureReady(void)
{
    unsigned groups = Groups(), frames = sFrames, tiles, cols;

    if (frames * groups > TEX_MAX_TILES) frames = 1;
    tiles = frames * groups;
    cols = (tiles + TEX_MAX_H / DIM - 1) / (TEX_MAX_H / DIM);
    if (cols < 1) cols = 1;
    if (sTexReady) return true;
    if (!sTexAllocated)
    {
        if (!C3D_TexInit(&sTex, TEX_MAX_W, TEX_MAX_H, GPU_RGBA5551))
        {
            CtrLog_Write(CTR_LOG_ERROR, "stereo: no linear memory for a %ux%u texture (free=%lu)",
                         TEX_MAX_W, TEX_MAX_H, (unsigned long)linearSpaceFree());
            return false;
        }
        C3D_TexSetFilter(&sTex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&sTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        sTexAllocated = true;
        CtrLog_Write(CTR_LOG_VIDEO, "stereo: a %ux%u texture (linear free=%lu)", TEX_MAX_W, TEX_MAX_H,
                     (unsigned long)linearSpaceFree());
    }
    /* Every texel written, so that nothing of what the memory held is read. */
    memset(sTex.data, 0, TEX_MAX_W * TEX_MAX_H * 2);
    C3D_TexFlush(&sTex);
    sTexCols = cols;
    sTexFrames = frames;
    sTexGroups = groups;
    for (unsigned tile = 0; tile < tiles; ++tile)
    {
        unsigned x = (tile % cols) * DIM, y = (tile / cols) * DIM;

        sTiles[tile] = (Tex3DS_SubTexture){DIM, DIM, x / (float)TEX_MAX_W, 1.0f - y / (float)TEX_MAX_H,
                                           (x + DIM) / (float)TEX_MAX_W, 1.0f - (y + DIM) / (float)TEX_MAX_H};
    }
    sTexReady = true;
    sComposed = 0;
    sToCompose = tiles;
    CtrLog_Write(CTR_LOG_VIDEO, "stereo: %u planes, %u frame(s), %u tiles %u across", groups, frames, tiles, cols);
    return true;
}

/* The next tiles into the texture; a Pokémon is drawn only once all its
 * tiles are there. */
static void ComposeSome(void)
{
    unsigned groups = Groups();

    for (unsigned n = 0; n < TILES_PER_FRAME && sComposed < sToCompose; ++n, ++sComposed)
        CtrStereoMon_ComposeTile(sPicture[sComposed / groups], sPlanes, &sShown, sComposed / groups, groups,
                                 sComposed % groups, sTex.data, TEX_MAX_W,
                                 (sComposed % sTexCols) * DIM, (sComposed / sTexCols) * DIM, SKIRT);
    C3D_TexFlush(&sTex);
}

/* Between showings the texture stays allocated (see above); its layout is
 * set again on the next. */
void CtrStereoMon_Idle(void)
{
    sTexReady = false;
}

/* ── Text: a 3x5 font, drawn as rectangles ──────────────────────────── */

#define GLYPH_PIXEL 2
#define GLYPH_ADVANCE (4 * GLYPH_PIXEL)

static const char *const sGlyphs[] = {
    "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
    "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111",
    "010101111101101", "110101110101110", "111100100100111", "110101101101110", "111100111100111",
    "111100111100100", "111100101101111", "101101111101101", "111010010010111", "001001001101111",
    "101101110101101", "100100100100111", "101111111101101", "110101101101101", "111101101101111",
    "111101111100100", "111101101111001", "110101110101101", "111100111001111", "111010010010010",
    "101101101101111", "101101101101010", "101101111111101", "101101010101101", "101101010010010",
    "111001010100111",
    /* . - : < > ^ v % / */
    "000000000000010", "000000111000000", "000010000010000", "001010100010001", "100010001010100",
    "010101000000000", "000000000101010", "101001010100101", "001001010100100",
};

static int GlyphOf(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + c - 'A';
    if (c >= 'a' && c <= 'z') return 10 + c - 'a';
    switch (c)
    {
    case '.': return 36;
    case '-': return 37;
    case ':': return 38;
    case '<': return 39;
    case '>': return 40;
    case '^': return 41;
    case 'v': return 42;
    case '%': return 43;
    case '/': return 44;
    default: return -1;
    }
}

static void DrawTextSized(float x, float y, const char *text, uint32_t colour, int px)
{
    for (; *text; ++text, x += 4 * px)
    {
        int g = *text == 'v' ? 42 : GlyphOf(*text);

        if (g < 0) continue;
        for (unsigned p = 0; p < 15; ++p)
            if (sGlyphs[g][p] == '1')
                C2D_DrawRectSolid(x + (p % 3) * px, y + (p / 3) * px, 0, px, px, colour);
    }
}

static void DrawText(float x, float y, const char *text, uint32_t colour)
{
    DrawTextSized(x, y, text, colour, GLYPH_PIXEL);
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

/* The stack of planes for one eye at `scale` screen pixels per sprite
 * pixel, `tint` over every plane or NULL. */
static void DrawPlanes(float sign, float slider, unsigned frame, const C2D_ImageTint *tint, float scale)
{
    const unsigned groups = Groups();
    const float pivot = PIVOT_FRACTION * (groups - 1);
    /* Thicker slices stand further apart: the stack keeps its depth. */
    const float spacing = DEPTH_SPAN / (float)(groups - 1);
    const int percent = sDepth;
    const float depth = slider * percent / 100.0f * PARALLAX;


    if (!(sTexReady && sToCompose && sComposed == sToCompose)) return;
    for (unsigned plane = 0; plane < groups; ++plane)
    {
        /* The plane's depth, towards the viewer, in sprite pixels. */
        float z = (plane - pivot) * spacing;
        float x = CENTRE_X - DIM / 2 * scale + sign * depth * z;
        float y = CENTRE_Y - DIM / 2 * scale;
        C2D_Image image = {&sTex, &sTiles[(frame < sTexFrames ? frame : 0) * groups + plane]};

        sPlaneX[plane] = x;
        sPlaneY = y;
        sPlaneScale = scale;
        C2D_DrawImageAt(image, x, y, 0, tint, scale, scale);
    }
}


static void DrawEye(C3D_RenderTarget *target, float sign, float slider, unsigned frame)
{
    const unsigned groups = Groups();
    char line[64];

    C2D_TargetClear(target, C2D_Color32(10, 12, 18, 255));
    C2D_SceneBegin(target);
    C2D_ViewReset();
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
    /* A quiet backdrop at the screen's depth, and a floor the stack stands on. */
    C2D_DrawRectangle(0, 0, 0, CTR_GAME_WIDTH, CTR_GAME_HEIGHT,
                      C2D_Color32(22, 26, 40, 255), C2D_Color32(22, 26, 40, 255),
                      C2D_Color32(8, 9, 14, 255), C2D_Color32(8, 9, 14, 255));
    C2D_DrawRectangle(0, CENTRE_Y + DIM * SCALE / 2 - 6, 0, CTR_GAME_WIDTH, 12,
                      C2D_Color32(40, 46, 70, 0), C2D_Color32(40, 46, 70, 0),
                      C2D_Color32(40, 46, 70, 160), C2D_Color32(40, 46, 70, 160));
    DrawPlanes(sign, slider, frame, NULL, SCALE);
    if ((sId & 0xC000) == 0) snprintf(line, sizeof(line), "NO.%03u %s", sId, sName);
    else snprintf(line, sizeof(line), "%s", sName);
    DrawText(8, 6, line, C2D_Color32(235, 235, 240, 255));
    snprintf(line, sizeof(line), "%u/%u", (unsigned)(CtrStereoMon_GalleryIndexOf(sId) + 1), CtrStereoMon_GalleryCount());
    DrawText(CTR_GAME_WIDTH - 8 - GLYPH_ADVANCE * (int)strlen(line), 6, line, C2D_Color32(160, 165, 180, 255));
    snprintf(line, sizeof(line), "<> MON   ^v DEPTH %u%%   L/R PLANES %u", (unsigned)sDepth, groups);
    DrawText(8, CTR_GAME_HEIGHT - 10 - 10 * GLYPH_PIXEL, line, C2D_Color32(160, 165, 180, 255));
    DrawText(8, CTR_GAME_HEIGHT - 6 - 5 * GLYPH_PIXEL, "X ANIM   B BACK", C2D_Color32(160, 165, 180, 255));
#ifdef CTR_STEREO_DEBUG
    snprintf(line, sizeof(line), "T%u C%u/%u F%u G%u X%u", sTexReady, sComposed, sToCompose, sFrames, groups, sTexCols);
    DrawText(CTR_GAME_WIDTH - 8 - GLYPH_ADVANCE * (int)strlen(line), CTR_GAME_HEIGHT - 6 - 5 * GLYPH_PIXEL, line,
             C2D_Color32(255, 200, 120, 255));
#endif
    C2D_Flush();
}

void CtrStereoMon_Draw(void *leftTarget, void *rightTarget, float slider)
{
    unsigned frame;

    if (!sActive) return;
    if ((!sTexReady || sComposed < sToCompose) && TextureReady()) ComposeSome();
    ++sTick;
    frame = sAnimate && sFrames > 1 && sTick % ANIM_PERIOD < ANIM_HOLD ? 1 : 0;
    if (slider < 0.0f) slider = 0.0f;
    if (slider > 1.0f) slider = 1.0f;
    /* The left eye sees what is nearer than the screen shifted right. */
    DrawEye((C3D_RenderTarget *)leftTarget, rightTarget ? 1.0f : 0.0f, slider, frame);
    if (rightTarget)
        DrawEye((C3D_RenderTarget *)rightTarget, -1.0f, slider, frame);
}
