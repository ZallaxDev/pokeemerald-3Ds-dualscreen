#ifndef CTR_BATTLE_HUD_H
#define CTR_BATTLE_HUD_H

/*
 * The 2D battle's info boxes at the top screen's own resolution
 * (3ds_battle_hud.c), and the art of its text box.
 *
 * The battle scene is magnified 1.4 times; the info boxes, drawn by the GBA
 * as sprites, are drawn instead from what they show - name, level, HP, bars,
 * status - in smooth art made for the screen (scripts/gen_battle_hud.py) and
 * the game's own font at 1:1. battle_interface.c tells this module what each
 * box shows as it draws it into its sprites, so both change on the same
 * frame; the compositor (3ds_video.c) then draws each box's picture where its
 * sprite is, and leaves the sprite's own tiles out.
 *
 * SDK-free: battle_interface.c and 3ds_video.c both include it.
 */
#include <stdbool.h>
#include <stdint.h>

/* One box a battler can have: singles, doubles and the Safari Zone's. */
enum
{
    CTR_HUD_SINGLE_OPPONENT,
    CTR_HUD_SINGLE_PLAYER,
    CTR_HUD_DOUBLE_OPPONENT,
    CTR_HUD_DOUBLE_PLAYER,
    CTR_HUD_SAFARI,
};

/* What the status pill shows (UpdateStatusIconInHealthbox's order). */
enum
{
    CTR_HUD_STATUS_NONE,
    CTR_HUD_STATUS_PSN,
    CTR_HUD_STATUS_PAR,
    CTR_HUD_STATUS_SLP,
    CTR_HUD_STATUS_FRZ,
    CTR_HUD_STATUS_BRN,
};

#define CTR_HUD_BOXES 4
/* A box's picture: its sprite's top-left corner is the picture's (0, 0). */
#define CTR_HUD_W 160
#define CTR_HUD_H 64

typedef struct
{
    /* The OAM tiles of the box's two halves and of its bar, [first, first + count). */
    uint16_t boxTile, boxTiles, barTile, barTiles;
    /* RGBA8 (0xRRGGBBAA), CTR_HUD_W x CTR_HUD_H, rows top to bottom; a new
     * version whenever it changes. Unfaded: the palette fade the box's
     * sprites are under is `fade` (0..1) towards `fadeColour` (0xRRGGBB00),
     * for the compositor to tint the picture with as it draws it. */
    const uint32_t *pixels;
    uint32_t version;
    float fade;
    uint32_t fadeColour;
    bool used;
} CtrHudBox;

/* ---- Told by battle_interface.c (PLATFORM_3DS), by healthbox sprite. ---- */
void CtrBattleHud_BoxCreated(unsigned char healthboxSpriteId, unsigned char battler, unsigned char kind);
void CtrBattleHud_SetName(unsigned char healthboxSpriteId, const unsigned char *name, unsigned char gender);
void CtrBattleHud_SetLevel(unsigned char healthboxSpriteId, unsigned char level);
void CtrBattleHud_SetHp(unsigned char healthboxSpriteId, short value, bool max);
/* HP: pixels 0..48 and colour 0 green, 1 yellow, 2 red; EXP: pixels 0..64. */
void CtrBattleHud_SetBar(unsigned char healthboxSpriteId, bool exp, unsigned char pixels, unsigned char colour);
void CtrBattleHud_SetStatus(unsigned char healthboxSpriteId, unsigned char status);
void CtrBattleHud_SetBall(unsigned char healthboxSpriteId, bool shown);
void CtrBattleHud_SetSafariBalls(unsigned char healthboxSpriteId, unsigned char balls);
/* battle_interface.c: a healthbox element's 8x8 tile (GetHealthboxElementGfxPtr). */
const unsigned char *CtrBattleHud_ElementGfx(unsigned char elementId);

/* ---- For the compositor. ---- */
/* Whether the boxes are drawn in HD at all (the art loaded). */
bool CtrBattleHud_Ready(void);
/* The art read and its labels traced, once, as the first battle makes its boxes. */
void CtrBattleHud_Prepare(void);
/* Brings every box's picture up to date (a palette fade in its fade fields); returns
 * the table of CTR_HUD_BOXES boxes. */
const CtrHudBox *CtrBattleHud_Boxes(void);
/* The text box's art (gen_battle_hud.py ID_BOX_*): x, y on the screen. */
const uint32_t *CtrBattleHud_BoxArt(bool middle, int *x, int *y, int *w, int *h);
/* The player's trainer moved down `lift` GBA pixels with the sliding text box; 0 lets it go. */
void CtrBattleHud_LiftPlayerTrainer(int lift);
/* Forgets every box (no battle on screen). */
void CtrBattleHud_Clear(void);

#endif
