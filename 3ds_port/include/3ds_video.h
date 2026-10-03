#ifndef CTR_VIDEO_H
#define CTR_VIDEO_H
#include <stdbool.h>
#include <stdint.h>

/* Reserve a voxel transfer (at most a split and a copy) before touching its
 * destination. False means retain the completed CPU job for the next frame. */
bool CtrVideo_TryVoxelUpload(void);
/* How many more uploads TryVoxelUpload would grant this frame. */
unsigned CtrVideo_VoxelUploadsLeft(void);

#define CTR_GAME_WIDTH 400
#define CTR_GAME_HEIGHT 240

/*
 * A GBA stage keeps the native pixel grid: the 240x160 picture sits 1:1 in the
 * middle of the 400x240 screen, and the compositor fills the space around it
 * from the picture's own art (see DrawStageBg in 3ds_video.c).
 */
#define CTR_STAGE_X ((CTR_GAME_WIDTH - 240) / 2)
#define CTR_STAGE_Y ((CTR_GAME_HEIGHT - 160) / 2)
/* The battle scene: centred across, resting on the bottom edge. */
#define CTR_BATTLE_X CTR_STAGE_X
#define CTR_BATTLE_Y (CTR_GAME_HEIGHT - 160)
/*
 * The battle scene itself is magnified: the middle of its bottom edge (GBA
 * 120,112) sits on screen (200,192), on top of the 1:1 text box. At 1.5 the
 * tile grid still lands on whole screen pixels.
 */
#define CTR_BATTLE_ZOOM 1.4f

typedef struct
{
    const uint8_t *vram;
    const uint16_t *palette;
    const uint16_t *oam;
    const uint16_t *regs;
} CtrVideoMemory;

typedef struct
{
    uint32_t frames, tiles, uploads, sprites, errors, stereo;
    uint16_t display;
    float fps, cpuMs, gpuMs, waitMs;
} CtrVideoStats;

bool CtrVideo_Init(void);
void CtrVideo_Shutdown(void);
void CtrVideo_Bind(CtrVideoMemory memory);
void CtrVideo_Present(void);
/* Keep showing the last top-screen frame instead of the game's screen. */
void CtrVideo_HoldTop(bool hold);
/* OAM entries belonging to field weather, tagged while BuildOamBuffer sorts sprites. */
/*
 * The true y of the sprite that wrote OAM entries [first, end). OAM keeps 8
 * bits of it, which a 160-line GBA screen never confuses, but the 240-line
 * field view does: a sprite just below it reads as one just above.
 */
void CtrVideo_ClearOamAnchors(void);
void CtrVideo_SetOamAnchor(unsigned first, unsigned end, int y);
void CtrVideo_ClearVoxelWeatherOam(void);
void CtrVideo_MarkVoxelWeatherOam(unsigned first, unsigned end);
void CtrVideo_NotifyTilesetAnimWrite(const void *dest, unsigned bytes);
const uint8_t *CtrVideo_GetBgVram(void);
/* Frees the 2D compositor's 3D depth planes before the next frame, for the
 * overworld when it cannot place an atlas. */
void CtrVideo_RequestPlaneRelease(void);
/*
 * Whether the frames that follow are a GBA stage: a screen composed as one
 * 240x160 picture (intro, title, credits), shown 1:1 in the middle of the top
 * screen with the space around it filled from its own art. See docs/ARCHITECTURE.md.
 */
void CtrVideo_SetStage(bool stage);
/* Whether a field move's banner is up: BG0 then wraps across the whole
 * screen instead of being the text band (src/field_effect.c). */
void CtrVideo_SetFieldBanner(bool banner);
/*
 * Which GBA screen shown centred the frames that follow are, if any: 1:1 at
 * the stage position, with the layers that wrap on the GBA and sprites
 * reaching into the margins. Some screens also carry their background out to
 * the edges of the top screen (3ds_video.c, sCentredFills).
 */
enum
{
    CTR_CENTRED_NONE,
    /* The fly map, the Town Map. */
    CTR_CENTRED_PLAIN,
    /* The title menu and the professor's speech. */
    CTR_CENTRED_MAIN_MENU,
    CTR_CENTRED_NAMING,
    CTR_CENTRED_CLOCK,
    /* The choice of starter from the professor's bag. */
    CTR_CENTRED_STARTER,
    /*
     * The PokéNav, composed as the others but shown on the bottom screen: its
     * 240x240 area left of the button column, its header on the top edge,
     * its help bar on the bottom one and the rest in the middle (NavBand).
     * The top screen keeps the frame it last showed meanwhile.
     */
    CTR_CENTRED_POKENAV,
    /*
     * The PC's boxes, also on the bottom screen: the GBA screen 1:1 in the
     * middle of the 240x240 area, its scrolling background carried on above
     * and below it.
     */
    CTR_CENTRED_STORAGE,
    /* A Pokémon's summary, on the bottom screen the same way. */
    CTR_CENTRED_SUMMARY,
    /*
     * The bag: opened from the field, in the 240x240 area left of the column,
     * the picture in its middle; opened from anything else (a battle, a
     * shop, the PC, giving an item) over the whole bottom screen. Its striped
     * backdrop is carried on around it.
     */
    CTR_CENTRED_BAG,
    CTR_CENTRED_BAG_WHOLE,
    /* The Pokédex, opened from the field: left of the column as the bag. */
    CTR_CENTRED_POKEDEX,
    /*
     * The party menu: opened from the field (the start menu, an item used or
     * given from the bag) left of the column, the picture in its middle;
     * from a battle, a contest or a facility over the whole bottom screen.
     * Its olive frame is carried on around it.
     */
    CTR_CENTRED_PARTY,
    CTR_CENTRED_PARTY_WHOLE,
    CTR_CENTRED_SCREENS
};
void CtrVideo_SetCentred(unsigned screen);
/*
 * The window edges an HBlank DMA writes line by line (the PokéNav's glow
 * behind its chosen option, its condition graph): values holds WIN0H for each
 * line, or WIN0H and WIN1H with both; NULL turns it off. Only the PokéNav's
 * screens are composed with them.
 */
void CtrVideo_SetLineWindow(const uint16_t *values, unsigned lines, bool both);
/* Whether the last frame drew a game screen into the bottom screen: the
 * PokéNav or the bag into its left area, or one over all of it. */
bool CtrVideo_BottomInUse(void);
/* Whether it drew the whole bottom screen: the PC's boxes, a summary, the bag
 * opened from anywhere but the field. */
bool CtrVideo_BottomWhole(void);
/*
 * The PokéNav's line of the picture a tap on line y of the bottom screen
 * lands on: its header stays on the top edge, its help bar on the bottom
 * edge and its body in between (3ds_video.c, NavBand). -1 between them.
 */
int CtrVideo_BottomPictureY(int y);
/*
 * Whether the frames that follow are the battle scene: the 240x160 picture 1:1
 * at (CTR_BATTLE_X, CTR_BATTLE_Y), so that its text box lies on the bottom
 * edge of the top screen, the text box stretched to the full width and the
 * scene above and beside it carried on from its own layers. See docs/ARCHITECTURE.md.
 */
void CtrVideo_SetBattle(bool battle);
/*
 * Per-scanline values of background scroll registers for the next frame: reg
 * is the offset from BG0HOFS, wide means two registers per line (32-bit DMA),
 * values holds one unit per line. NULL values turns it off.
 */
void CtrVideo_SetLineScroll(unsigned reg, bool wide, const void *values, unsigned lines);
/*
 * Whether the frames that follow show a battle transition: GBA geometry
 * (compat/ctr_gba_transition.h), composed over the field - 2D or voxel - at
 * the battle scene's scale. See 3ds_video.c, RenderTransition.
 */
void CtrVideo_SetTransition(bool transition);
/*
 * The registers from BG0HOFS to BLDY (CTR_LINE_REG_FIRST, CTR_LINE_REGS of
 * them) as each of the GBA's 160 lines of a transition's frame shows them:
 * lines x CTR_LINE_REGS values, NULL when there is no transition.
 */
#define CTR_GBA_LINES 160
#define CTR_LINE_REG_FIRST 0x10
#define CTR_LINE_REGS 35
void CtrVideo_SetLineRegisters(const uint16_t *regs, unsigned lines);
/* Sprites the game places on the screen rather than on the map: a battle
 * transition's, drawn with it (src/sprite.c). */
void CtrVideo_ClearScreenOam(void);
void CtrVideo_MarkScreenOam(unsigned first, unsigned end);
const CtrVideoStats *CtrVideo_GetStats(void);
void CtrScene_Init(void);
void CtrScene_Update(void);
unsigned CtrScene_Mode(void);
void CtrCursor_Init(const void *tiles, const uint16_t *palette);
void CtrCursor_Update(bool visible);
void CtrCursor_GetPosition(int *x, int *y);

/* Pure, SDK-free address/format helpers, also used by host regression tests. */
uint32_t CtrVideo_TextMapOffset(unsigned x, unsigned y, unsigned size);
uint32_t CtrVideo_Texel(unsigned x, unsigned y, unsigned width);
uint32_t CtrVideo_RGBA8(uint16_t color, bool opaque);
uint16_t CtrVideo_RGBA5551(uint16_t color);
unsigned CtrVideo_ObjTile(unsigned base, unsigned x, unsigned y,
                          unsigned width, bool color256, bool mapping1d);
int32_t CtrVideo_AffineReference(uint32_t value);
#endif
