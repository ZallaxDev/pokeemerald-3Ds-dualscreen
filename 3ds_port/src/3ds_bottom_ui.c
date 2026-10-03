/*
 * The bottom screen, game side: what it shows and what a touch does.
 *
 * It replaces the START menu. A column of buttons on the right holds every
 * entry the menu had, plus the Hoenn map: MAP (the default), POKéMON, BAG,
 * the trainer card, POKéDEX, POKéNAV, SAVE and OPTION. The 240x240 area on
 * the left shows the chosen one; everything happens here while the top screen
 * keeps the world. The PokéNav is the game's own, run as it is: the
 * compositor draws its screens into that area (CtrVideo_BottomInUse) and a
 * tap on them becomes the buttons the PokéNav reads.
 *
 * Map, trainer card, Pokédex, summary, save and options are drawn and run
 * here directly. Using an item, switching mons, giving items or field moves
 * need the game's own logic, so the game's party menu or bag runs *hidden*:
 * the top screen holds its last frame (CtrVideo_HoldTop), the menu is driven
 * by button presses fed through Platform_GetKeyInput, and what it shows (its
 * submenu entries, messages, yes/no questions) is mirrored here as buttons.
 * The player never sees a cursor move; the game still decides everything.
 *
 * Nothing here is new artwork. Every panel is built from the game's own
 * graphics, decoded from the same RomFS files the game loads: the party menu
 * background and slot tilemaps, the battle text box frames, the Hoenn region
 * map, the trainer card, mon/item/type/status icons, front pictures, the bag
 * sprite, the window frames and the game's fonts. Texts are the game's
 * strings where it has them.
 *
 * Cost model, chosen for an Old 3DS whose frame the top screen already fills:
 *   - every frame: a snapshot of the few values on screen (a memcmp of a few
 *     hundred bytes) and the touch state; no drawing;
 *   - when the snapshot changes: the canvas is recomposed by the CPU from
 *     pre-rendered backgrounds (a memcpy) plus the live parts, and copied to
 *     the framebuffer;
 *   - RomFS is never read inside a redraw: icons and pictures are fetched one
 *     per frame beforehand, everything else is decoded once at boot;
 *   - icon animation: only the icon rectangles are restored and redrawn.
 * No GPU time, no VRAM, no linear memory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "3ds_locale.h"
#include "main.h"
#include "money.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_main.h"
#include "battle_message.h"
#include "contest_util.h"
#include "data.h"
#include "event_data.h"
#include "fieldmap.h"
#include "fonts.h"
#include "graphics.h"
#include "item.h"
#include "item_icon.h"
#include "item_menu.h"
#include "menu.h"
#include "new_game.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_summary_screen.h"
#include "pokenav.h"
#include "region_map.h"
#include "save.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "util.h"
#include "constants/items.h"
#include "constants/map_types.h"
#include "constants/party_menu.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/trainers.h"
#include "port_platform.h"

#include "3ds_data.h"
#include "3ds_bottom.h"
#include "3ds_input.h"
#include "3ds_log.h"
#include "3ds_platform.h"
#include "3ds_video.h"

/* Exported by the game under PLATFORM_3DS, or not exported by its headers. */
void CB2_BagMenuRun(void);
u8 CtrPartyMenu_GetActions(const u8 **names, u8 max);
const u8 *CtrPartyMenu_GetMessage(void);
u8 CtrBagMenu_GetActions(const u8 **names, u8 max, u8 *columns);
const u8 *CtrBagMenu_GetMessage(void);
bool8 CtrMenu_YesNoOpen(void);
void CtrStartMenu_Request(u8 action);
bool8 CtrStartMenu_Pending(void);
bool8 CtrStartMenu_Available(void);
bool8 CtrStartMenu_Busy(void);
bool8 CtrPokenav_IsOpen(void);
u32 CtrPokenav_Screen(bool8 *ready);
int CtrPokenavMenu_Options(int *cursor);
void CtrPokenavMenu_Rows(int *yStart, int *deltaY);
bool8 CtrPokenavList_View(u8 *x, u8 *y, u8 *width, u16 *top, u16 *selected, u16 *shown, u16 *count);
u8 CtrPokenavMatchCall_Input(u16 *cursor, u16 *count);
bool8 CtrPokenavRibbons_Summary(u16 *selected, u16 *normal, u16 *gift, u16 *giftStart, bool8 *expanded);
bool8 CtrRegionMap_Cursor(s16 *x, s16 *y, bool8 *zoomed, bool8 *moving);
bool8 CtrMonMarkings_Menu(s8 *cursor, s16 *x, s16 *y);
bool8 CtrPokenavCondition_Marking(void);
void CtrPokenavMenu_SetCursor(int cursor);
void CtrPokenavList_SetSelected(u16 selected);
void CtrPokenavMatchCall_SetOption(u16 cursor);
void CtrMonMarkings_SetCursor(s8 cursor);
void SetPokemonCryStereo(u32 val);
extern const struct PokedexEntry gPokedexEntries[];

/* start_menu.c's MENU_ACTION_* (the enum is private to that file). */
enum { START_POKEDEX, START_POKEMON, START_BAG, START_POKENAV, START_NONE = 0xFF };

#define W CTR_BOTTOM_WIDTH
#define H CTR_BOTTOM_HEIGHT
/* The content area left of the button column. */
#define CW 240
#define COL_X CW

/* ------------------------------------------------------------------------ */
/* Canvas                                                                   */
/* ------------------------------------------------------------------------ */

/*
 * The canvas, in framebuffer layout, is what gets copied to the screen. The
 * big static pictures behind each view (party menu background, Hoenn map,
 * trainer card) are decoded once into caches of the same layout, so a redraw
 * starts with a memcpy instead of re-decoding tens of thousands of pixels.
 * Views draw in content coordinates; sOX moves them (battle menus centre the
 * 240-wide views in the whole screen).
 */
static u16 sCanvas[W * H] __attribute__((aligned(32)));
static u16 *sDst = sCanvas;
static int sOX;

enum { CACHE_MENU, CACHE_WIDE, CACHE_MAP, CACHE_CARD, CACHE_COUNT };
static u16 *sCache[CACHE_COUNT];
static int sCardCacheKey = -1;

static void CopyCache(int which)
{
    if (sCache[which])
        memcpy(sCanvas, sCache[which], sizeof(sCanvas));
    else
        memset(sCanvas, 0, sizeof(sCanvas));
}

static inline void Put(int x, int y, u16 c)
{
    x += sOX;
    if ((unsigned)x >= W || (unsigned)y >= H)
        return;
    sDst[x * H + (H - 1 - y)] = c;
}

static void FillRect(int x, int y, int w, int h, u16 c)
{
    int x0 = x + sOX, x1 = x0 + w, y0 = y, y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    for (int cx = x0; cx < x1; ++cx)
    {
        u16 *p = sDst + cx * H + (H - y1);
        for (int n = y1 - y0; n > 0; --n)
            *p++ = c;
    }
}

static u16 Rgb565(u16 bgr)
{
    u16 r = bgr & 31, g = (bgr >> 5) & 31, b = (bgr >> 10) & 31;
    return (r << 11) | (((g << 1) | (g >> 4)) << 5) | b;
}

/* The same colour at 55% brightness: how a pressed or chosen button looks. */
static u16 Darker(u16 c)
{
    u16 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return ((r * 9 / 16) << 11) | ((g * 9 / 16) << 5) | (b * 9 / 16);
}

typedef struct { u16 c[16]; } Pal;

static void ToPals(Pal *dst, const u16 *src, int count)
{
    for (int p = 0; p < count; ++p)
        for (int i = 0; i < 16; ++i)
            dst[p].c[i] = Rgb565(src[p * 16 + i]);
}

static void DarkPal(Pal *dst, const Pal *src)
{
    for (int i = 0; i < 16; ++i)
        dst->c[i] = Darker(src->c[i]);
}

/*
 * A 4bpp 8x8 tile; colour 0 is transparent, as on the GBA. This is where a
 * redraw spends its time, so a tile fully on screen is written straight into
 * its columns: pixel (x, y) is canvas[x * H + H - 1 - y], so one step right
 * is +H and one step down is -1.
 */
static void DrawTile(const u8 *tile, int x, int y, const u16 *pal, bool8 hflip, bool8 vflip)
{
    int sx0 = x + sOX;

    if (sx0 >= W || y >= H || sx0 + 8 <= 0 || y + 8 <= 0)
        return;
    if (sx0 >= 0 && y >= 0 && sx0 + 8 <= W && y + 8 <= H)
    {
        u16 *origin = sDst + sx0 * H + (H - 1 - y);
        for (int py = 0; py < 8; ++py)
        {
            const u8 *row = tile + (vflip ? 7 - py : py) * 4;
            u32 bits = row[0] | (row[1] << 8) | (row[2] << 16) | ((u32)row[3] << 24);
            u16 *p = origin - py;

            if (!bits)
                continue;
            for (int px = 0; px < 8; ++px, bits >>= 4)
            {
                u8 v = bits & 15;
                if (v)
                    p[(hflip ? 7 - px : px) * H] = pal[v];
            }
        }
        return;
    }
    for (int py = 0; py < 8; ++py)
    {
        const u8 *row = tile + (vflip ? 7 - py : py) * 4;
        for (int px = 0; px < 8; ++px)
        {
            int sx = hflip ? 7 - px : px;
            u8 v = (row[sx >> 1] >> ((sx & 1) * 4)) & 15;
            if (v)
                Put(x + px, y + py, pal[v]);
        }
    }
}

/* The single colour of a tile with no transparent or differing pixel, or -1. */
static int SolidTileColor(const u8 *tile)
{
    u8 v = tile[0] & 15;

    if (!v)
        return -1;
    for (int i = 0; i < 32; ++i)
        if (tile[i] != (v | (v << 4)))
            return -1;
    return v;
}

/* A sprite in the GBA's one-dimensional tile layout. */
static void DrawSprite(const u8 *tiles, int wt, int ht, int x, int y, const u16 *pal)
{
    for (int ty = 0; ty < ht; ++ty)
        for (int tx = 0; tx < wt; ++tx)
            DrawTile(tiles + (ty * wt + tx) * 32, x + tx * 8, y + ty * 8, pal, FALSE, FALSE);
}

/* A text background tilemap entry: tile, flips and palette bank. */
static void DrawMapEntry(const u8 *tiles, u32 tileCount, u16 e, int x, int y, const Pal *pals)
{
    u32 tile = e & 0x3FF;

    if (tiles && tile < tileCount)
        DrawTile(tiles + tile * 32, x, y, pals[e >> 12].c, (e >> 10) & 1, (e >> 11) & 1);
}

static void DrawTypeIcon(u8 type, int x, int y);

/* ------------------------------------------------------------------------ */
/* Resources                                                                */
/* ------------------------------------------------------------------------ */

static void *ReadRomfs(const char *path, u32 *outSize)
{
    char full[128];
    void *data;

    snprintf(full, sizeof(full), "graphics/%s", path);
    data = CtrData_Load(full, outSize);
    if (!data)
        CtrLog_Write(CTR_LOG_ERROR, "bottom: %s missing", full);
    return data;
}

/*
 * LZ77 data, whether a game symbol (an asset stub, resolved from RomFS) or a
 * buffer read here. The size comes from the stream's own header.
 */
static void *Unlz(const void *src, u32 *outSize)
{
    const u8 *res = src ? Port_ResolveAssetPointer(src) : NULL;
    u32 header, size;
    void *dst;

    if (!res)
        return NULL;
    header = res[0] | (res[1] << 8) | (res[2] << 16) | ((u32)res[3] << 24);
    size = header >> 8;
    if ((header & 0xFF) != 0x10 || size == 0 || size > 0x10000)
        return NULL;
    dst = malloc(size);
    if (!dst)
        return NULL;
    LZ77UnCompWram((const u32 *)res, dst);
    if (outSize)
        *outSize = size;
    return dst;
}

static void *UnlzFile(const char *path, u32 *outSize)
{
    void *packed = ReadRomfs(path, NULL);
    void *data = Unlz(packed, outSize);

    free(packed);
    return data;
}

static bool8 PalFile(const char *path, Pal *dst, int count, bool8 compressed)
{
    u32 size = 0;
    u16 *raw = compressed ? UnlzFile(path, &size) : ReadRomfs(path, &size);

    if (!raw)
        return FALSE;
    if ((int)(size / 32) < count)
        count = size / 32;
    ToPals(dst, raw, count);
    free(raw);
    return TRUE;
}

/* An uncompressed game palette; the pointer may be an asset stub. */
static void PalSymbol(const void *src, Pal *dst, int count)
{
    const u16 *res = src ? Port_ResolveAssetPointer(src) : NULL;

    if (res)
        ToPals(dst, res, count);
}

/* The screens of the button column, top to bottom. */
enum { SCR_MAP, SCR_POKEMON, SCR_BAG, SCR_CARD, SCR_POKEDEX, SCR_POKENAV, SCR_SAVE, SCR_OPTION, SCR_COUNT };

/*
 * The game's six options, then the port's own: the voxel overworld, off by
 * default and kept in settings.txt rather than in the save (3ds_settings.c).
 * Only a build with the voxel renderer has that row.
 */
enum { OPT_TEXT_SPEED, OPT_BATTLE_SCENE, OPT_BATTLE_STYLE, OPT_SOUND, OPT_BUTTON_MODE, OPT_FRAME,
       OPT_VOXEL, OPT_VOXEL_PITCH, OPT_VOXEL_ZOOM, OPTION_ROWS };
#if CTR_VOXEL_ENABLED
#define OPTION_SHOWN OPTION_ROWS
#else
#define OPTION_SHOWN OPT_VOXEL
#endif

typedef struct
{
    u8 *tiles;
    u8 size;        /* 3 for a 24x24 item icon, 4 for 32x32 */
    Pal pal;
} Icon;

static struct
{
    bool8 ready;
    Pal text;                        /* the message box text palette */
    /* Party menu. */
    u8 *partyTiles;
    u32 partyTileCount;
    u16 partyRaw[16 * 11];
    Pal partyPal[11];
    u8 *slotMain, *slotMainNoHp, *slotWide, *slotWideNoHp, *slotWideEmpty;
    u8 *ballTiles;
    Pal ballPal;
    /* Battle text box frames, and their darkened copies. */
    u8 *boxTiles;
    u32 boxTileCount;
    Pal boxPal[2], boxPalDark[2];
    /* Icons. */
    Icon column[SCR_COUNT];
    u8 *typeTiles;
    Pal typePal[3];
    u8 *statusTiles;
    Pal statusPal;
    Pal monIconPal[3];
    /* Region map. */
    u8 *mapTiles;
    u32 mapTileCount;
    u8 *mapMap;
    u16 mapPal[32];
    u8 *playerIcon[2];
    Pal playerIconPal[2];
    u8 *cursorTiles;
    Pal cursorPal;
    /* Trainer card. */
    u8 *cardTiles;
    u32 cardTileCount;
    u16 *cardFront, *cardBg;
    Pal cardPal[5][3];
    Pal cardFemaleBg, badgePal, starPal;
    u8 *badgeTiles;
    u8 *trainerPic[2];
    Pal trainerPicPal[2];
    u8 *bagTiles[2];
    Pal bagPal;
} sRes;

static void LoadItemIcon(Icon *icon, const void *tiles, const void *pal)
{
    u16 *raw = Unlz(pal, NULL);

    icon->tiles = Unlz(tiles, NULL);
    icon->size = 3;
    if (raw)
    {
        ToPals(&icon->pal, raw, 1);
        free(raw);
    }
}

/* Both players' trainer picture and bag, so nothing is decoded in a frame. */
static void LoadGenderResources(void)
{
    for (u8 gender = MALE; gender <= FEMALE; ++gender)
    {
        u16 pic = gFacilityClassToPicIndex[gender == FEMALE ? FACILITY_CLASS_MAY : FACILITY_CLASS_BRENDAN];
        u16 *pal = Unlz(gTrainerFrontPicPaletteTable[pic].data, NULL);
        u32 size = 0;
        u8 *all = Unlz(gender == FEMALE ? gBagFemaleTiles : gBagMaleTiles, &size);

        sRes.trainerPic[gender] = Unlz(gTrainerFrontPicTable[pic].data, NULL);
        if (pal)
        {
            ToPals(&sRes.trainerPicPal[gender], pal, 1);
            free(pal);
        }
        /* Only the first of the six frames, the closed bag, is shown. */
        if (all && size >= 64 * 32 && (sRes.bagTiles[gender] = malloc(64 * 32)) != NULL)
            memcpy(sRes.bagTiles[gender], all, 64 * 32);
        free(all);
    }
    {
        u16 *pal = Unlz(gBagPalette, NULL);
        if (pal)
        {
            ToPals(&sRes.bagPal, pal, 1);
            free(pal);
        }
    }
}

static void LoadResources(void)
{
    static const char *const cardPals[5] = {
        "trainer_card/green.gbapal", "trainer_card/bronze.gbapal", "trainer_card/copper.gbapal",
        "trainer_card/silver.gbapal", "trainer_card/gold.gbapal",
    };
    u32 size;

    PalSymbol(GetOverworldTextboxPalettePtr(), &sRes.text, 1);

    sRes.partyTiles = UnlzFile("party_menu/bg.4bpp.lz", &size);
    sRes.partyTileCount = size / 32;
    {
        u16 *raw = UnlzFile("party_menu/bg.gbapal.lz", &size);
        if (raw)
        {
            memcpy(sRes.partyRaw, raw, size < sizeof(sRes.partyRaw) ? size : sizeof(sRes.partyRaw));
            free(raw);
        }
        ToPals(sRes.partyPal, sRes.partyRaw, 11);
    }
    sRes.slotMain = ReadRomfs("party_menu/slot_main.bin", NULL);
    sRes.slotMainNoHp = ReadRomfs("party_menu/slot_main_no_hp.bin", NULL);
    sRes.slotWide = ReadRomfs("party_menu/slot_wide.bin", NULL);
    sRes.slotWideNoHp = ReadRomfs("party_menu/slot_wide_no_hp.bin", NULL);
    sRes.slotWideEmpty = ReadRomfs("party_menu/slot_wide_empty.bin", NULL);
    sRes.ballTiles = UnlzFile("party_menu/pokeball_small.4bpp.lz", NULL);
    PalFile("party_menu/pokeball.gbapal.lz", &sRes.ballPal, 1, TRUE);

    sRes.boxTiles = UnlzFile("battle_interface/textbox.4bpp.lz", &size);
    sRes.boxTileCount = size / 32;
    PalFile("battle_interface/textbox.gbapal.lz", sRes.boxPal, 2, TRUE);
    DarkPal(&sRes.boxPalDark[0], &sRes.boxPal[0]);
    DarkPal(&sRes.boxPalDark[1], &sRes.boxPal[1]);

    /* The column's icons: the game's own item icons, and the PokéNav's. */
    LoadItemIcon(&sRes.column[SCR_MAP], gItemIcon_TownMap, gItemIconPalette_TownMap);
    LoadItemIcon(&sRes.column[SCR_POKEMON], gItemIcon_PokeBall, gItemIconPalette_PokeBall);
    LoadItemIcon(&sRes.column[SCR_BAG], gItemIcon_BerryPouch, gItemIconPalette_BerryPouch);
    LoadItemIcon(&sRes.column[SCR_CARD], gItemIcon_ContestPass, gItemIconPalette_ContestPass);
    LoadItemIcon(&sRes.column[SCR_POKEDEX], gItemIcon_FameChecker, gItemIconPalette_FameChecker);
    LoadItemIcon(&sRes.column[SCR_SAVE], GetItemIconPicOrPalette(ITEM_LETTER, 0), GetItemIconPicOrPalette(ITEM_LETTER, 1));
    LoadItemIcon(&sRes.column[SCR_OPTION], gItemIcon_TeachyTV, gItemIconPalette_TeachyTV);
    sRes.column[SCR_POKENAV].tiles = UnlzFile("pokenav/nav_icon.4bpp.lz", NULL);
    sRes.column[SCR_POKENAV].size = 4;
    PalFile("pokenav/nav_icon.gbapal", &sRes.column[SCR_POKENAV].pal, 1, FALSE);

    sRes.typeTiles = UnlzFile("types/move_types.4bpp.lz", NULL);
    PalFile("types/move_types.gbapal.lz", sRes.typePal, 3, TRUE);
    sRes.statusTiles = UnlzFile("interface/status_icons.4bpp.lz", NULL);
    PalFile("interface/status_icons.gbapal.lz", &sRes.statusPal, 1, TRUE);
    for (int i = 0; i < 3; ++i)
        PalSymbol(gMonIconPaletteTable[i].data, &sRes.monIconPal[i], 1);

    sRes.mapTiles = UnlzFile("pokenav/region_map/map.8bpp.lz", &size);
    sRes.mapTileCount = size / 64;
    sRes.mapMap = UnlzFile("pokenav/region_map/map.bin.lz", NULL);
    {
        u16 *raw = ReadRomfs("pokenav/region_map/map.gbapal", &size);
        if (raw)
        {
            for (u32 i = 0; i < 32 && i < size / 2; ++i)
                sRes.mapPal[i] = Rgb565(raw[i]);
            free(raw);
        }
    }
    sRes.playerIcon[MALE] = ReadRomfs("pokenav/region_map/brendan_icon.4bpp", NULL);
    sRes.playerIcon[FEMALE] = ReadRomfs("pokenav/region_map/may_icon.4bpp", NULL);
    PalFile("pokenav/region_map/brendan_icon.gbapal", &sRes.playerIconPal[MALE], 1, FALSE);
    PalFile("pokenav/region_map/may_icon.gbapal", &sRes.playerIconPal[FEMALE], 1, FALSE);
    sRes.cursorTiles = UnlzFile("pokenav/region_map/cursor_small.4bpp.lz", NULL);
    PalFile("pokenav/region_map/cursor.gbapal", &sRes.cursorPal, 1, FALSE);

    sRes.cardTiles = UnlzFile("trainer_card/tiles.4bpp.lz", &size);
    sRes.cardTileCount = size / 32;
    sRes.cardFront = UnlzFile("trainer_card/front.bin.lz", NULL);
    sRes.cardBg = UnlzFile("trainer_card/bg.bin.lz", NULL);
    for (int i = 0; i < 5; ++i)
        PalFile(cardPals[i], sRes.cardPal[i], 3, FALSE);
    PalFile("trainer_card/female_bg.gbapal", &sRes.cardFemaleBg, 1, FALSE);
    PalFile("trainer_card/badges.gbapal", &sRes.badgePal, 1, FALSE);
    PalFile("trainer_card/star.gbapal", &sRes.starPal, 1, FALSE);
    sRes.badgeTiles = UnlzFile("trainer_card/badges.4bpp.lz", NULL);

    LoadGenderResources();

    for (int i = 0; i < CACHE_COUNT; ++i)
        sCache[i] = malloc(sizeof(sCanvas));
    sRes.ready = sRes.partyTiles && sRes.boxTiles && sRes.slotMain && sRes.slotWide && sRes.slotWideEmpty
              && sCache[CACHE_MENU] && sCache[CACHE_WIDE];
    if (!sRes.ready)
        CtrLog_Write(CTR_LOG_ERROR, "bottom: party menu or text box graphics missing, screen stays off");
}

/*
 * Icons are read from RomFS, which on hardware is the slowest thing a frame
 * can do. Drawing only ever uses what is already cached; Prefetch loads at
 * most one missing icon per frame, and the screen is redrawn once they are in.
 */
static bool8 sIconBudget;
static u32 sIconClock;

/* Mon icons: two 32x32 frames each, a few species at a time. */
#define MON_ICON_SLOTS 12
static struct
{
    u16 key;
    u32 age;
    u8 tiles[1024];
} sMonIcons[MON_ICON_SLOTS];

static const u8 *MonIcon(u16 iconSpecies, bool8 deoxysForm)
{
    u16 key = iconSpecies | (deoxysForm ? 0x8000 : 0);
    int victim = 0;
    const u8 *src;

    for (int i = 0; i < MON_ICON_SLOTS; ++i)
    {
        if (sMonIcons[i].key == key && key != 0)
        {
            sMonIcons[i].age = ++sIconClock;
            return sMonIcons[i].tiles;
        }
        if (sMonIcons[i].age < sMonIcons[victim].age)
            victim = i;
    }
    if (iconSpecies >= SPECIES_EGG + 28 || iconSpecies == SPECIES_NONE || !sIconBudget)
        return NULL;
    sIconBudget = FALSE;
    src = Port_ResolveAssetPointer(gMonIconTable[iconSpecies]);
    if (!src)
        return NULL;
    /* Deoxys keeps its alternate form in the same file, 0x400 in. */
    memcpy(sMonIcons[victim].tiles, src + (deoxysForm ? 0x400 : 0), 1024);
    sMonIcons[victim].key = key;
    sMonIcons[victim].age = ++sIconClock;
    return sMonIcons[victim].tiles;
}

/* Item icons: 24x24 with their own palette. */
#define ITEM_ICON_SLOTS 24
static struct
{
    u16 item;
    u32 age;
    bool8 valid, loaded;
    u8 tiles[9 * 32];
    Pal pal;
} sItemIcons[ITEM_ICON_SLOTS];

static int ItemIcon(u16 item)
{
    int victim = 0;
    u32 size = 0;
    u8 *tiles;
    u16 *pal;

    for (int i = 0; i < ITEM_ICON_SLOTS; ++i)
    {
        if (sItemIcons[i].loaded && sItemIcons[i].item == item)
        {
            sItemIcons[i].age = ++sIconClock;
            return sItemIcons[i].valid ? i : -1;
        }
        if (sItemIcons[i].age < sItemIcons[victim].age)
            victim = i;
    }
    if (!sIconBudget)
        return -1;
    sIconBudget = FALSE;
    tiles = Unlz(GetItemIconPicOrPalette(item, 0), &size);
    pal = Unlz(GetItemIconPicOrPalette(item, 1), NULL);
    sItemIcons[victim].valid = tiles && pal && size >= sizeof(sItemIcons[victim].tiles);
    if (sItemIcons[victim].valid)
    {
        memcpy(sItemIcons[victim].tiles, tiles, sizeof(sItemIcons[victim].tiles));
        ToPals(&sItemIcons[victim].pal, pal, 1);
    }
    free(tiles);
    free(pal);
    sItemIcons[victim].item = item;
    sItemIcons[victim].loaded = TRUE;
    sItemIcons[victim].age = ++sIconClock;
    return sItemIcons[victim].valid ? victim : -1;
}

static void DrawItemIcon(u16 item, int x, int y)
{
    int slot = ItemIcon(item);

    if (slot >= 0)
        DrawSprite(sItemIcons[slot].tiles, 3, 3, x, y, sItemIcons[slot].pal.c);
}

/* Front pictures for the Pokédex: 64x64, two at a time. */
static struct
{
    u16 species;
    u32 age;
    bool8 valid;
    u8 tiles[2048];
    Pal pal;
} sPics[2];

static int FrontPic(u16 species)
{
    int victim = sPics[0].age <= sPics[1].age ? 0 : 1;
    u32 size = 0;
    u8 *tiles;
    u16 *pal;

    for (int i = 0; i < 2; ++i)
        if (sPics[i].species == species && species)
        {
            sPics[i].age = ++sIconClock;
            return sPics[i].valid ? i : -1;
        }
    if (!sIconBudget || species == SPECIES_NONE || species >= NUM_SPECIES)
        return -1;
    sIconBudget = FALSE;
    tiles = Unlz(gMonFrontPicTable[species].data, &size);
    pal = Unlz(gMonPaletteTable[species].data, NULL);
    sPics[victim].valid = tiles && pal && size >= sizeof(sPics[victim].tiles);
    if (sPics[victim].valid)
    {
        memcpy(sPics[victim].tiles, tiles, sizeof(sPics[victim].tiles));
        ToPals(&sPics[victim].pal, pal, 1);
    }
    free(tiles);
    free(pal);
    sPics[victim].species = species;
    sPics[victim].age = ++sIconClock;
    return sPics[victim].valid ? victim : -1;
}

/* ------------------------------------------------------------------------ */
/* Text                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct
{
    const u16 *glyphs;
    const u8 *widths;
    u8 height, lineHeight;
} Font;

static Font sSmall, sNormal;

static const u16 *ResolveFontBase(const u16 *glyphs)
{
    const void *resolved = Port_ResolveFontPointer(glyphs);

    if (resolved == glyphs)
        resolved = Port_ResolveAssetPointer(glyphs);
    return resolved;
}

/* The glyph payloads live in the asset cache; resolve them per redraw. */
static void ResolveFonts(void)
{
    sSmall.glyphs = ResolveFontBase(gFontSmallLatinGlyphs);
    sSmall.widths = gFontSmallLatinGlyphWidths;
    sSmall.height = 13;
    sSmall.lineHeight = 13;
    sNormal.glyphs = ResolveFontBase(gFontNormalLatinGlyphs);
    sNormal.widths = gFontNormalLatinGlyphWidths;
    sNormal.height = 16;
    sNormal.lineHeight = 16;
}

/* Walks a game string: returns the next glyph, 0xFFFE for a line break or
 * 0xFFFF at the end. */
static u16 NextGlyph(const u8 **str)
{
    for (;;)
    {
        u8 c = *(*str)++;

        switch (c)
        {
        case EOS:
            --*str;
            return 0xFFFF;
        case CHAR_NEWLINE:
        case CHAR_PROMPT_SCROLL:
        case CHAR_PROMPT_CLEAR:
            return 0xFFFE;
        case EXT_CTRL_CODE_BEGIN:
            if (**str == EOS)
                return 0xFFFF;
            *str += GetExtCtrlCodeLength(**str);
            break;
        case PLACEHOLDER_BEGIN:
        case CHAR_DYNAMIC:
        case CHAR_KEYPAD_ICON:
            if (**str != EOS)
                ++*str;
            break;
        case CHAR_EXTRA_SYMBOL:
            if (**str == EOS)
                return 0xFFFF;
            return 0x100 | *(*str)++;
        default:
            return c;
        }
    }
}

static int StrWidth(const Font *font, const u8 *str)
{
    int width = 0, best = 0;

    if (!str)
        return 0;
    for (u16 g; (g = NextGlyph(&str)) != 0xFFFF;)
    {
        if (g == 0xFFFE)
        {
            if (width > best) best = width;
            width = 0;
            continue;
        }
        width += font->widths[g];
    }
    return width > best ? width : best;
}

static int DrawGlyph(const Font *font, u16 glyph, int x, int y, u16 fg, u16 shadow)
{
    const u16 *base = font->glyphs + glyph * 0x20;
    int width = font->widths[glyph];
    int sx = x + sOX;
    bool8 inside = sx >= 0 && y >= 0 && sx + width <= W && y + font->height <= H;
    u16 *origin = sDst + (inside ? sx * H + (H - 1 - y) : 0);

    if (width > 16)
        width = 16;
    for (int row = 0; row < font->height; ++row)
    {
        for (int half = 0; half < 2 && half * 8 < width; ++half)
        {
            u16 bits = base[(row >= 8 ? 0x10 : 0) + half * 8 + (row & 7)];

            if (!bits)
                continue;
            for (int k = 0; k < 8 && half * 8 + k < width; ++k)
            {
                u8 byte = k < 4 ? bits >> 8 : bits & 0xFF;
                u8 v = (byte >> (6 - 2 * (k & 3))) & 3;
                int px = half * 8 + k;

                if (v != 1 && v != 2)
                    continue;
                if (inside)
                    origin[px * H - row] = v == 1 ? fg : shadow;
                else
                    Put(x + px, y + row, v == 1 ? fg : shadow);
            }
        }
    }
    return font->widths[glyph];
}

/* Draws a game string; returns the x where it ended. */
static int DrawStr(const Font *font, const u8 *str, int x, int y, u16 fg, u16 shadow)
{
    int left = x;

    if (!font->glyphs || !str)
        return x;
    for (u16 g; (g = NextGlyph(&str)) != 0xFFFF;)
    {
        if (g == 0xFFFE)
        {
            x = left;
            y += font->lineHeight;
            continue;
        }
        x += DrawGlyph(font, g, x, y, fg, shadow);
    }
    return x;
}

static void DrawStrRight(const Font *font, const u8 *str, int right, int y, u16 fg, u16 shadow)
{
    DrawStr(font, str, right - StrWidth(font, str), y, fg, shadow);
}

static void DrawStrCentered(const Font *font, const u8 *str, int cx, int y, u16 fg, u16 shadow)
{
    DrawStr(font, str, cx - StrWidth(font, str) / 2, y, fg, shadow);
}

/* Latin labels for the few words the game has no standalone string for. */
static const u8 *Ascii(const char *text)
{
    static u8 ring[8][40];
    static u8 next;
    u8 *out = ring[next++ & 7];
    int n = 0;

    for (; *text && n < 39; ++text)
    {
        char c = *text;
        u8 v;

        if (c >= 'A' && c <= 'Z') v = CHAR_A + (c - 'A');
        else if (c >= 'a' && c <= 'z') v = CHAR_a + (c - 'a');
        else if (c >= '0' && c <= '9') v = CHAR_0 + (c - '0');
        else if (c == '/') v = CHAR_SLASH;
        else if (c == '-') v = CHAR_HYPHEN;
        else if (c == '.') v = CHAR_PERIOD;
        else if (c == ':') v = CHAR_COLON;
        else if (c == '!') v = CHAR_EXCL_MARK;
        else if (c == '?') v = CHAR_QUESTION_MARK;
        else if (c == '\'') v = CHAR_SGL_QUOTE_RIGHT;
        else if (c == '*') v = CHAR_e_ACUTE; /* POK*MON */
        else v = CHAR_SPACE;
        out[n++] = v;
    }
    out[n] = EOS;
    return out;
}

static const u8 *Number(u32 value, int digits, enum StringConvertMode mode)
{
    static u8 ring[8][12];
    static u8 next;
    u8 *out = ring[next++ & 7];

    ConvertIntToDecimalStringN(out, value, mode, digits);
    return out;
}

/* Text colours from the message box palette. */
#define TXT(i) (sRes.text.c[(i)])
#define TXT_WHITE TXT(TEXT_COLOR_WHITE)
#define TXT_DARK TXT(TEXT_COLOR_DARK_GRAY)
#define TXT_LIGHT TXT(TEXT_COLOR_LIGHT_GRAY)
#define TXT_RED TXT(TEXT_COLOR_RED)
#define TXT_LRED TXT(TEXT_COLOR_LIGHT_RED)
#define TXT_BLUE TXT(TEXT_COLOR_BLUE)
#define TXT_LBLUE TXT(TEXT_COLOR_LIGHT_BLUE)

/* Labels on a normal button, and on one shown darker (chosen or pressed). */
#define LABEL_FG(on) ((on) ? TXT_WHITE : TXT_DARK)
#define LABEL_SH(on) ((on) ? TXT_DARK : TXT_LIGHT)

/* ------------------------------------------------------------------------ */
/* Frames and backgrounds                                                   */
/* ------------------------------------------------------------------------ */

enum { BOX_MENU, BOX_MESSAGE };

/*
 * The battle text box's two frames, stretched in whole tiles: the white menu
 * box for buttons and lists, the teal message box for what the game says.
 * A dark box is the same frame at lower brightness: chosen or pressed.
 */
static void DrawBoxEx(int kind, int x, int y, int wt, int ht, bool8 dark)
{
    static const u8 menu[3][3] = {{0x12, 0x13, 0x14}, {0x15, 0x16, 0x17}, {0x18, 0x19, 0x1A}};
    static const u8 message[3][5] = {
        {0x03, 0x04, 0x05, 0x06, 0x07},
        {0x08, 0x09, 0x0A, 0x0B, 0x0C},
        {0x0D, 0x0E, 0x0F, 0x10, 0x11},
    };
    /* Caps are one tile wide on the menu box, two on the message box. */
    int cap = kind == BOX_MENU ? 1 : 2;
    u16 bank = kind == BOX_MENU ? 0x1000 : 0;
    u8 center = kind == BOX_MENU ? menu[1][1] : message[1][2];
    int solid = center < sRes.boxTileCount ? SolidTileColor(sRes.boxTiles + center * 32) : -1;
    const Pal *pals = dark ? sRes.boxPalDark : sRes.boxPal;

    /* The interior is one flat colour: fill it, draw only the frame. */
    if (solid >= 0 && wt > 2 * cap && ht > 2)
        FillRect(x + cap * 8, y + 8, (wt - 2 * cap) * 8, (ht - 2) * 8, pals[bank >> 12].c[solid]);
    for (int ty = 0; ty < ht; ++ty)
    {
        int row = ty == 0 ? 0 : ty == ht - 1 ? 2 : 1;
        for (int tx = 0; tx < wt; ++tx)
        {
            bool8 inside = row == 1 && tx >= cap && tx < wt - cap;
            u8 tile;

            if (inside && solid >= 0)
                continue;
            if (kind == BOX_MENU)
                tile = menu[row][tx == 0 ? 0 : tx == wt - 1 ? 2 : 1];
            else
                tile = message[row][tx == 0 ? 0 : tx == 1 ? 1 : tx == wt - 2 ? 3 : tx == wt - 1 ? 4 : 2];
            DrawMapEntry(sRes.boxTiles, sRes.boxTileCount, bank | tile, x + tx * 8, y + ty * 8, pals);
        }
    }
}

static void DrawBox(int kind, int x, int y, int wt, int ht)
{
    DrawBoxEx(kind, x, y, wt, ht, FALSE);
}

/*
 * The party menu background, framed on all four sides: the GBA screen leaves
 * its right edge open because the picture runs off it, here the left border
 * is mirrored instead.
 */
static void DrawPartyBackground(int cols, int rows)
{
    FillRect(0, 0, cols * 8, rows * 8, sRes.partyPal[0].c[0]);
    for (int ty = 0; ty < rows; ++ty)
    {
        for (int tx = 0; tx < cols; ++tx)
        {
            int edge = tx < cols / 2 ? tx : cols - 1 - tx;
            u16 flip = tx < cols / 2 ? 0 : 0x400, t;

            if (edge < 2) t = 0x002, flip = 0;
            else if (edge > 2) t = ty == 0 ? 0x005 : ty == rows - 1 ? 0x008 : 0x00E, flip = 0;
            else if (ty == 0) t = 0x004;
            else if (ty == 1) t = 0x006;
            else if (ty == rows - 2) t = 0x009;
            else if (ty == rows - 1) t = 0x007;
            else t = 0x00E;
            DrawMapEntry(sRes.partyTiles, sRes.partyTileCount, 0x1000 | flip | t, tx * 8, ty * 8, sRes.partyPal);
        }
    }
}

/* The column's background: the party menu's olive frame colour. */
static void DrawColumnBackground(void)
{
    for (int ty = 0; ty < H / 8; ++ty)
        for (int tx = COL_X / 8; tx < W / 8; ++tx)
            DrawMapEntry(sRes.partyTiles, sRes.partyTileCount, 0x1002, tx * 8, ty * 8, sRes.partyPal);
}

/* ------------------------------------------------------------------------ */
/* View state                                                               */
/* ------------------------------------------------------------------------ */

enum
{
    MODE_OFF,
    MODE_FIELD,
    MODE_PARTY_MENU,
    MODE_BAG_MENU,
    /* The PokéNav, drawn by the compositor left of the column. */
    MODE_POKENAV,
    MODE_BATTLE_INFO,
    MODE_BATTLE_ACTION,
    MODE_BATTLE_MOVE,
    MODE_BATTLE_TARGET,
};

/* What the lower panel of the party and bag views offers. */
enum
{
    PANEL_NONE,
    PANEL_HINT,       /* the menu waits for a mon or an item: CANCEL */
    PANEL_ACTIONS,    /* the game's submenu, as buttons */
    PANEL_MESSAGE,    /* a message: tap to go on */
    PANEL_YESNO,      /* a question */
    PANEL_QUANTITY,   /* how many: up, down, OK, CANCEL */
};

#define BAG_ROWS 5
#define BAG_ROW_H 24
#define DEX_ROWS 8
#define MAX_MENU_ITEMS 8

typedef struct
{
    u16 species;      /* SPECIES_NONE: empty slot */
    u16 iconSpecies;
    u8 deoxys, isEgg, level, gender, ailment, fainted;
    u16 hp, maxHp;
    u8 nick[POKEMON_NAME_LENGTH + 2];
} MonView;

typedef struct
{
    u8 present, side, level, ailment;
    u16 hp, maxHp, iconSpecies;
    u8 deoxys, gender;
    u8 nick[POKEMON_NAME_LENGTH + 2];
} BattlerView;

/* Everything a redraw reads. Zeroed before every snapshot: memcmp-safe. */
typedef struct
{
    u8 mode, screen, pressed, gender, inBattle;
    u8 enabled;                       /* bit per column screen */
    /* Party. */
    s8 partyCursor;
    MonView party[PARTY_SIZE];
    /* The lower panel: a game menu, a message or a question. */
    u8 panel, menuCount, menuCols, menuCursor;
    const u8 *menuNames[MAX_MENU_ITEMS];
    const u8 *message;
    /* Summary. */
    s8 summary;
    u16 stats[6], moves[MAX_MON_MOVES];
    u8 pp[MAX_MON_MOVES], maxPp[MAX_MON_MOVES], nature, ability, types[2];
    u16 heldItem;
    /* Region map. */
    u8 mapsec, cursorX, cursorY, pickMapsec, pickX, pickY;
    /* Bag. */
    u8 pocket, keyPocket;
    s16 bagCursor;
    u16 bagScroll, bagCount;
    u16 items[BAG_ROWS], qty[BAG_ROWS];
    u16 descItem;
    /* Trainer card. */
    u8 name[PLAYER_NAME_LENGTH + 1];
    u8 hasDex, stars, badges, minutes;
    u16 id, dex, hours;
    u32 money;
    /* Pokédex. */
    u8 national;
    u16 dexScroll, dexCount, dexSeen, dexOwn, dexDetail;
    u16 dexNum[DEX_ROWS];
    u8 dexFlags[DEX_ROWS];           /* 1 seen, 2 caught */
    /* Save and options. */
    u8 saveStep, canSave;
    u8 options[OPTION_ROWS];
    /* Battle. */
    u8 isDouble, safari, cursor, battler;
    BattlerView battlers[MAX_BATTLERS_COUNT];
    struct ChooseMoveStruct moves4;
    u8 text[96];
} ViewState;

static ViewState sState, sShown;
static bool8 sForceRedraw = TRUE;
static bool8 sInGame;
static u8 sScreen = SCR_MAP;
static u8 sAnimFrame;

/* Per screen state, kept while another screen is shown. */
static u8 sBagPocket;
static u16 sBagScroll;
static s16 sBagTapped = -1;
static s8 sPartyTapped = -1;
static s8 sSummary = -1;
static u8 sPickMapsec = MAPSEC_NONE, sPickX, sPickY;
static u16 sDexScroll, sDexDetail;
static u8 sSaveStep;
static u8 sSaveMessage[96];

enum { SAVE_ASK, SAVE_OVERWRITE, SAVE_DONE };

/* Learned once: the party menu's running callback (static in party_menu.c). */
static MainCallback sPartyMenuCallback;

/* ------------------------------------------------------------------------ */
/* Hit zones                                                                */
/* ------------------------------------------------------------------------ */

enum
{
    HIT_NONE = 0xFF,
    HIT_COLUMN = 0x10,     /* + screen */
    HIT_SLOT = 0x20,       /* + party slot */
    HIT_CANCEL = 0x30,
    HIT_OK,
    HIT_YES,
    HIT_NO,
    HIT_PREV,
    HIT_NEXT,
    HIT_BACK,
    HIT_UP,
    HIT_DOWN,
    HIT_PANEL,             /* a message: anywhere on it */
    HIT_POCKET = 0x40,     /* + pocket */
    HIT_ROW = 0x50,        /* + visible list row */
    HIT_ACTION = 0x60,     /* + action cursor */
    HIT_MOVE = 0x70,       /* + move slot */
    HIT_TARGET_LEFT = 0x80,
    HIT_TARGET_RIGHT,
    HIT_TARGET_OK,
    HIT_MAP = 0x90,
    HIT_MENU = 0xB0,       /* + game menu entry */
    HIT_OPTION = 0xC0,     /* + option row; +HIT_OPTION_BACK for the left arrow */
};
/* More than there are option rows, so a row and a left arrow never share an id. */
#define HIT_OPTION_BACK 16

typedef struct { s16 x, y, w, h; u8 id; } Hit;
static Hit sHits[64];
static u8 sHitCount;

static void AddHit(int x, int y, int w, int h, u8 id)
{
    if (sHitCount < ARRAY_COUNT(sHits))
        sHits[sHitCount++] = (Hit){x + sOX, y, w, h, id};
}

static u8 HitTest(int x, int y)
{
    for (int i = sHitCount - 1; i >= 0; --i)
        if (x >= sHits[i].x && y >= sHits[i].y && x < sHits[i].x + sHits[i].w && y < sHits[i].y + sHits[i].h)
            return sHits[i].id;
    return HIT_NONE;
}

static void DrawButton(int x, int y, int wt, int ht, bool8 on, u8 id)
{
    DrawBoxEx(BOX_MENU, x, y, wt, ht, on);
    AddHit(x, y, wt * 8, ht * 8, id);
}

static void DrawLabelButtonFont(const Font *font, int x, int y, int wt, int ht, const u8 *label, bool8 on,
                                bool8 enabled, u8 id)
{
    DrawBoxEx(BOX_MENU, x, y, wt, ht, on);
    DrawStrCentered(font, label, x + wt * 4, y + ht * 4 - font->height / 2, enabled ? LABEL_FG(on) : TXT_LIGHT,
                    enabled ? LABEL_SH(on) : TXT_WHITE);
    if (enabled)
        AddHit(x, y, wt * 8, ht * 8, id);
}

static void DrawLabelButton(int x, int y, int wt, int ht, const u8 *label, bool8 on, bool8 enabled, u8 id)
{
    DrawLabelButtonFont(&sNormal, x, y, wt, ht, label, on, enabled, id);
}

/* ------------------------------------------------------------------------ */
/* Animated icons                                                           */
/* ------------------------------------------------------------------------ */

typedef struct { s16 x, y; u16 iconSpecies; u8 deoxys, still; } AnimIcon;
static AnimIcon sAnim[8];
static u8 sAnimCount;
/* What was under each animated icon, to redraw its frames in place. */
static u16 sUnder[8][32 * 32];

static void AddMonIcon(u16 iconSpecies, bool8 deoxys, int x, int y, bool8 still)
{
    if (sAnimCount < ARRAY_COUNT(sAnim) && iconSpecies != SPECIES_NONE)
        sAnim[sAnimCount++] = (AnimIcon){x + sOX, y, iconSpecies, deoxys, still};
}

static void IconRect(const AnimIcon *icon, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = icon->x < 0 ? 0 : icon->x;
    *y0 = icon->y < 0 ? 0 : icon->y;
    *x1 = icon->x + 32 > W ? W : icon->x + 32;
    *y1 = icon->y + 32 > H ? H : icon->y + 32;
}

/* Icons store screen coordinates: draw them untranslated. */
static void DrawIconFrame(const AnimIcon *icon)
{
    const u8 *tiles = MonIcon(icon->iconSpecies, icon->deoxys);
    u8 pal = gMonIconPaletteIndices[icon->iconSpecies];
    int ox = sOX;

    sOX = 0;
    if (tiles && pal < 3)
        DrawSprite(tiles + (icon->still ? 0 : sAnimFrame) * 512, 4, 4, icon->x, icon->y, sRes.monIconPal[pal].c);
    sOX = ox;
}

static void DrawAnimIcons(void)
{
    for (int i = 0; i < sAnimCount; ++i)
    {
        int x0, y0, x1, y1;

        IconRect(&sAnim[i], &x0, &y0, &x1, &y1);
        for (int x = x0; x < x1; ++x)
            memcpy(sUnder[i] + (x - x0) * 32, sCanvas + x * H + (H - y1), (y1 - y0) * sizeof(u16));
        DrawIconFrame(&sAnim[i]);
    }
}

/* Icon frames only: restore each icon's rectangle and redraw it. */
static void AnimateIcons(void)
{
    for (int i = 0; i < sAnimCount; ++i)
    {
        int x0, y0, x1, y1;

        IconRect(&sAnim[i], &x0, &y0, &x1, &y1);
        if (sAnim[i].still || x0 >= x1 || y0 >= y1)
            continue;
        for (int x = x0; x < x1; ++x)
            memcpy(sCanvas + x * H + (H - y1), sUnder[i] + (x - x0) * 32, (y1 - y0) * sizeof(u16));
        DrawIconFrame(&sAnim[i]);
        CtrBottom_BlitRect(sCanvas, x0, y0, x1, y1);
    }
}

/* ------------------------------------------------------------------------ */
/* Game state                                                               */
/* ------------------------------------------------------------------------ */

static bool8 PartyMenuReady(void)
{
    return FuncIsActiveTask(Task_HandleChooseMonInput) && !gPaletteFade.active;
}

static bool8 BagMenuReady(void)
{
    if (gMain.callback2 != CB2_BagMenuRun || gBagMenu == NULL || gPaletteFade.active)
        return FALSE;
    for (int i = 0; i < ITEMWIN_COUNT; ++i)
        if (gBagMenu->windowIds[i] != WINDOW_NONE)
            return FALSE;
    return gBagMenu->toSwapPos == 0xFF; /* NOT_SWAPPING in item_menu.c */
}

static u16 BagMenuIndex(void)
{
    return gBagPosition.scrollPosition[gBagPosition.pocket] + gBagPosition.cursorPosition[gBagPosition.pocket];
}

/* The player stands in the field with nothing else going on. */
static bool8 FieldIdle(void)
{
    return gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled()
        && gPlayerAvatar.tileTransitionState == T_NOT_MOVING && !gPaletteFade.active && !CtrStartMenu_Busy();
}

static u8 EnabledScreens(void)
{
    u8 mask = (1 << SCR_MAP) | (1 << SCR_BAG) | (1 << SCR_CARD) | (1 << SCR_OPTION);

    if (FlagGet(FLAG_SYS_POKEMON_GET))
        mask |= 1 << SCR_POKEMON;
    if (FlagGet(FLAG_SYS_POKEDEX_GET))
        mask |= 1 << SCR_POKEDEX;
    if (FlagGet(FLAG_SYS_POKENAV_GET) && CtrStartMenu_Available())
        mask |= 1 << SCR_POKENAV;
    if (CtrStartMenu_Available())
        mask |= 1 << SCR_SAVE;
    return mask;
}

/*
 * What the battle controllers wait for. Their input handlers call in here
 * every frame they run (CtrBattleMenu_*Input); last frame's call is what the
 * bottom screen shows.
 */
enum { ASK_NONE, ASK_ACTION, ASK_MOVE, ASK_TARGET };

typedef struct
{
    u8 kind, battler;
} BattleAsk;

static BattleAsk sAsk, sAsked;
static u8 sBattleTap = 0xFF;   /* a tap for the controller to take */
static bool8 sMoveCancel;      /* the move menu's cursor is on CANCEL */

static u8 CurrentMode(void)
{
    if (gMain.callback2 == CB2_Overworld)
        sInGame = TRUE;
    if (!sInGame || !gSaveBlock1Ptr || !gSaveBlock2Ptr)
        return MODE_OFF;
    if (CtrPokenav_IsOpen())
        return MODE_POKENAV;
    if (FuncIsActiveTask(Task_HandleChooseMonInput))
        sPartyMenuCallback = gMain.callback2;
    if (sPartyMenuCallback && gMain.callback2 == sPartyMenuCallback)
        return MODE_PARTY_MENU;
    if (gMain.callback2 == CB2_BagMenuRun && gBagMenu)
        return MODE_BAG_MENU;
    if (gMain.inBattle)
    {
        if (gMain.callback2 == BattleMainCB2 && !gPaletteFade.active)
        {
            if (sAsked.kind == ASK_ACTION) return MODE_BATTLE_ACTION;
            if (sAsked.kind == ASK_MOVE) return MODE_BATTLE_MOVE;
            if (sAsked.kind == ASK_TARGET) return MODE_BATTLE_TARGET;
        }
        return MODE_BATTLE_INFO;
    }
    return MODE_FIELD;
}

/* ------------------------------------------------------------------------ */
/* Hidden sessions: the game's party menu or bag running under the world    */
/* ------------------------------------------------------------------------ */

static struct
{
    bool8 active, entered, battle;
    u16 frames, away;
} sSession;

static void BeginSession(bool8 battle)
{
    if (!sSession.active)
        CtrLog_Write(CTR_LOG_VIDEO, "bottom screen: hidden menu session (%s)", battle ? "battle" : "field");
    sSession.active = TRUE;
    sSession.entered = FALSE;
    sSession.battle = battle;
    sSession.frames = sSession.away = 0;
}

/*
 * Whether the top screen holds its frame. A session holds from the first
 * press that leads to the menu (the fade out is not shown) through the menu
 * and back until the fade in is over. A screen the session did not expect -
 * an evolution, the move to forget, the fly map - is shown after a moment.
 */
static bool8 UpdateSession(u8 mode, bool8 planRunning)
{
    bool8 inMenu = mode == MODE_PARTY_MENU || mode == MODE_BAG_MENU || mode == MODE_POKENAV;
    bool8 home = gMain.callback2 == CB2_Overworld || gMain.callback2 == BattleMainCB2;

    if (inMenu && !sSession.active)
        BeginSession(gMain.inBattle);
    if (!sSession.active)
        return FALSE;
    ++sSession.frames;
    if (inMenu)
    {
        sSession.entered = TRUE;
        sSession.away = 0;
        return TRUE;
    }
    if (home)
    {
        sSession.away = 0;
        if ((sSession.entered || (!planRunning && sSession.frames > 30)) && !gPaletteFade.active)
        {
            sSession.active = FALSE;
            CtrLog_Write(CTR_LOG_VIDEO, "bottom screen: hidden menu session over");
            return FALSE;
        }
        return TRUE;
    }
    /* A menu's setup and the map reload pass through here too: brief. */
    return ++sSession.away < 40;
}

/* Hidden menus need not wait on their fades and slow text. */
static void FastForward(void)
{
    for (int i = 0; i < 6; ++i)
    {
        if (gPaletteFade.active)
            UpdatePaletteFade();
        RunTextPrinters();
    }
}

/* ------------------------------------------------------------------------ */
/* The battle menus                                                         */
/* ------------------------------------------------------------------------ */

/*
 * The action, move and target menus are the bottom screen's: the controllers
 * do not draw theirs (the top keeps its message box) and read their keys
 * through these, which move the game's cursor in the bottom screen's order
 * and turn a tap into the key that confirms it. What each choice does stays
 * the controller's own code.
 */
bool8 CtrBattleMenu_Active(void)
{
    return sRes.ready && !(gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED | BATTLE_TYPE_WALLY_TUTORIAL));
}

void CtrBattleMenu_Begin(void)
{
    sBattleTap = HIT_NONE;
    sMoveCancel = FALSE;
}

/* "What will X do?", in the message box: the one thing left on top. */
void CtrBattleMenu_ShowPrompt(void)
{
    /* The printer reads it while it prints: a copy of its own. */
    static u8 prompt[64];
    int i;

    for (i = 0; i < (int)sizeof(prompt) - 1 && gDisplayedStringBattle[i] != EOS; ++i)
        prompt[i] = gDisplayedStringBattle[i];
    prompt[i] = EOS;
    BattlePutTextOnWindow(prompt, B_WIN_MSG);
}

static u8 TakeBattleTap(u8 kind)
{
    u8 tap = sBattleTap;

    sBattleTap = HIT_NONE;
    sAsk.kind = kind;
    sAsk.battler = gActiveBattler;
    return tap;
}

void CtrBattleMenu_ActionInput(u8 *cursor, bool8 safari)
{
    u8 tap = TakeBattleTap(ASK_ACTION), next = *cursor;
    u16 dpad = gMain.newKeys & DPAD_ANY;

    /* FIGHT on top; BAG, POKéMON and RUN in a row under it. */
    gMain.newKeys &= ~DPAD_ANY;
    if (dpad & DPAD_UP)
        next = 0;
    else if ((dpad & DPAD_DOWN) && next == 0)
        next = 2;
    else if ((dpad & DPAD_LEFT) && next > 1)
        --next;
    else if ((dpad & DPAD_RIGHT) && next != 0 && next < 3)
        ++next;
    if (next != *cursor)
    {
        PlaySE(SE_SELECT);
        *cursor = next;
    }
    if (tap >= HIT_ACTION && tap < HIT_ACTION + 4)
    {
        *cursor = tap - HIT_ACTION;
        gMain.newKeys |= A_BUTTON;
    }
    /* BAG and POKéMON open menus that run hidden. */
    if ((gMain.newKeys & A_BUTTON) && !safari && (*cursor == 1 || *cursor == 2))
        BeginSession(TRUE);
}

void CtrBattleMenu_MoveInput(u8 *cursor, const u16 *moves)
{
    u8 tap = TakeBattleTap(ASK_MOVE), next = *cursor, count = 0;
    u16 dpad = gMain.newKeys & DPAD_ANY;
    bool8 cancel = sMoveCancel;

    for (int i = 0; i < MAX_MON_MOVES; ++i)
        if (moves[i] != MOVE_NONE)
            ++count;
    /* The moves two by two, CANCEL under them. No reordering (SELECT). */
    gMain.newKeys &= ~(DPAD_ANY | SELECT_BUTTON);
    if (cancel)
    {
        if (dpad & DPAD_UP)
            cancel = FALSE;
    }
    else if (dpad & DPAD_UP)
    {
        if (next & 2)
            next ^= 2;
    }
    else if (dpad & DPAD_DOWN)
    {
        if (!(next & 2) && (next ^ 2) < count)
            next ^= 2;
        else
            cancel = TRUE;
    }
    else if (dpad & DPAD_LEFT)
    {
        if (next & 1)
            next ^= 1;
    }
    else if (dpad & DPAD_RIGHT)
    {
        if (!(next & 1) && (next ^ 1) < count)
            next ^= 1;
    }
    if (next != *cursor || cancel != sMoveCancel)
        PlaySE(SE_SELECT);
    *cursor = next;
    sMoveCancel = cancel;
    if (tap >= HIT_MOVE && tap < HIT_MOVE + MAX_MON_MOVES && moves[tap - HIT_MOVE] != MOVE_NONE)
    {
        *cursor = tap - HIT_MOVE;
        sMoveCancel = FALSE;
        gMain.newKeys |= A_BUTTON;
    }
    else if (tap == HIT_CANCEL)
        gMain.newKeys |= B_BUTTON;
    /* A on CANCEL is B. */
    if (sMoveCancel && (gMain.newKeys & A_BUTTON))
        gMain.newKeys = (gMain.newKeys & ~A_BUTTON) | B_BUTTON;
}

void CtrBattleMenu_TargetInput(void)
{
    u8 tap = TakeBattleTap(ASK_TARGET);

    if (tap == HIT_TARGET_LEFT) gMain.newKeys |= DPAD_LEFT;
    else if (tap == HIT_TARGET_RIGHT) gMain.newKeys |= DPAD_RIGHT;
    else if (tap == HIT_TARGET_OK) gMain.newKeys |= A_BUTTON;
    else if (tap == HIT_CANCEL) gMain.newKeys |= B_BUTTON;
}

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                */
/* ------------------------------------------------------------------------ */

static void SnapshotMon(MonView *view, struct Pokemon *mon)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);

    if (species == SPECIES_NONE)
        return;
    view->species = species;
    view->isEgg = GetMonData(mon, MON_DATA_IS_EGG);
    view->iconSpecies = view->isEgg ? SPECIES_EGG : GetIconSpecies(species, GetMonData(mon, MON_DATA_PERSONALITY));
    view->deoxys = species == SPECIES_DEOXYS;
    view->level = GetMonData(mon, MON_DATA_LEVEL);
    view->hp = GetMonData(mon, MON_DATA_HP);
    view->maxHp = GetMonData(mon, MON_DATA_MAX_HP);
    view->ailment = view->isEgg ? AILMENT_NONE : GetMonAilment(mon);
    view->fainted = !view->isEgg && view->hp == 0;
    GetMonNickname(mon, view->nick);
    view->gender = GetMonGender(mon);
    /* The party menu leaves the symbol off a Nidoran still named after its
     * species: the name already says it. */
    if ((species == SPECIES_NIDORAN_M || species == SPECIES_NIDORAN_F)
     && StringCompare(view->nick, gSpeciesNames[species]) == 0)
        view->gender = MON_GENDERLESS;
}

static void SnapshotParty(ViewState *s)
{
    for (int i = 0; i < PARTY_SIZE; ++i)
        SnapshotMon(&s->party[i], &gPlayerParty[i]);
}

static void SnapshotSummary(ViewState *s, u8 slot)
{
    struct Pokemon *mon = &gPlayerParty[slot];
    static const u8 stats[6] = {MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPATK, MON_DATA_SPDEF,
                                MON_DATA_SPEED};
    u8 bonuses = GetMonData(mon, MON_DATA_PP_BONUSES);

    s->summary = slot;
    for (int i = 0; i < 6; ++i)
        s->stats[i] = GetMonData(mon, stats[i]);
    for (int i = 0; i < MAX_MON_MOVES; ++i)
    {
        s->moves[i] = GetMonData(mon, MON_DATA_MOVE1 + i);
        s->pp[i] = GetMonData(mon, MON_DATA_PP1 + i);
        s->maxPp[i] = s->moves[i] ? CalculatePPWithBonus(s->moves[i], bonuses, i) : 0;
    }
    s->nature = GetNature(mon);
    s->ability = GetAbilityBySpecies(s->party[slot].species, GetMonData(mon, MON_DATA_ABILITY_NUM));
    s->types[0] = gSpeciesInfo[s->party[slot].species].types[0];
    s->types[1] = gSpeciesInfo[s->party[slot].species].types[1];
    s->heldItem = GetMonData(mon, MON_DATA_HELD_ITEM);
}

static u8 PlayerRegionPosition(u8 *outX, u8 *outY)
{
    /* region_map.c's InitMapBasedOnPlayerLocation, without its UI state. */
    const struct MapHeader *header = &gMapHeader;
    u16 mapWidth, mapHeight, x, y, scale;
    u8 mapsec;

    switch (GetMapTypeByGroupAndId(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum))
    {
    case MAP_TYPE_UNDERGROUND:
    case MAP_TYPE_UNKNOWN:
        if (gMapHeader.allowEscaping)
        {
            header = Overworld_GetMapHeaderByGroupAndId(gSaveBlock1Ptr->escapeWarp.mapGroup,
                                                        gSaveBlock1Ptr->escapeWarp.mapNum);
            x = gSaveBlock1Ptr->escapeWarp.x;
            y = gSaveBlock1Ptr->escapeWarp.y;
            mapsec = header->regionMapSectionId;
        }
        else
        {
            mapsec = gMapHeader.regionMapSectionId;
            header = NULL;
            x = y = 1;
        }
        break;
    case MAP_TYPE_SECRET_BASE:
        header = Overworld_GetMapHeaderByGroupAndId(gSaveBlock1Ptr->dynamicWarp.mapGroup,
                                                    gSaveBlock1Ptr->dynamicWarp.mapNum);
        x = gSaveBlock1Ptr->dynamicWarp.x;
        y = gSaveBlock1Ptr->dynamicWarp.y;
        mapsec = header->regionMapSectionId;
        break;
    case MAP_TYPE_INDOOR:
    {
        const struct WarpData *warp = gMapHeader.regionMapSectionId != MAPSEC_DYNAMIC
                                    ? &gSaveBlock1Ptr->escapeWarp : &gSaveBlock1Ptr->dynamicWarp;
        header = Overworld_GetMapHeaderByGroupAndId(warp->mapGroup, warp->mapNum);
        mapsec = gMapHeader.regionMapSectionId != MAPSEC_DYNAMIC ? gMapHeader.regionMapSectionId
                                                                 : header->regionMapSectionId;
        x = warp->x;
        y = warp->y;
        break;
    }
    default:
        mapsec = gMapHeader.regionMapSectionId;
        x = gSaveBlock1Ptr->pos.x;
        y = gSaveBlock1Ptr->pos.y;
        break;
    }
    if (mapsec >= MAPSEC_NONE)
        return MAPSEC_NONE;
    mapWidth = header && header->mapLayout ? header->mapLayout->width : 1;
    mapHeight = header && header->mapLayout ? header->mapLayout->height : 1;
    if (gRegionMapEntries[mapsec].width && gRegionMapEntries[mapsec].height)
    {
        scale = mapWidth / gRegionMapEntries[mapsec].width;
        x /= scale ? scale : 1;
        if (x >= gRegionMapEntries[mapsec].width) x = gRegionMapEntries[mapsec].width - 1;
        scale = mapHeight / gRegionMapEntries[mapsec].height;
        y /= scale ? scale : 1;
        if (y >= gRegionMapEntries[mapsec].height) y = gRegionMapEntries[mapsec].height - 1;
    }
    else
    {
        x = y = 0;
    }
    /* 1 and 2 are region_map.c's MAPCURSOR_X_MIN / MAPCURSOR_Y_MIN. */
    *outX = gRegionMapEntries[mapsec].x + x + 1;
    *outY = gRegionMapEntries[mapsec].y + y + 2;
    return mapsec;
}

static u16 PocketCount(u8 pocket)
{
    u16 n = 0;

    for (u16 i = 0; i < gBagPockets[pocket].capacity; ++i)
        if (gBagPockets[pocket].itemSlots[i].itemId != ITEM_NONE)
            n = i + 1;
    return n;
}

static void SnapshotBag(ViewState *s)
{
    u16 count = PocketCount(sBagPocket);

    if (sBagScroll + BAG_ROWS > count)
        sBagScroll = count > BAG_ROWS ? count - BAG_ROWS : 0;
    if (sBagTapped >= count)
        sBagTapped = -1;
    s->pocket = sBagPocket;
    s->keyPocket = sBagPocket == KEYITEMS_POCKET;
    s->bagCount = count;
    s->bagScroll = sBagScroll;
    s->bagCursor = sBagTapped;
    for (int r = 0; r < BAG_ROWS; ++r)
    {
        u16 i = sBagScroll + r;
        if (i >= count)
            break;
        s->items[r] = BagGetItemIdByPocketPosition(sBagPocket + 1, i);
        s->qty[r] = BagGetQuantityByPocketPosition(sBagPocket + 1, i);
    }
    if (sBagTapped >= 0)
        s->descItem = BagGetItemIdByPocketPosition(sBagPocket + 1, sBagTapped);
}

static void SnapshotCard(ViewState *s)
{
    static u16 dex, frames;
    static u8 stars;

    StringCopy(s->name, gSaveBlock2Ptr->playerName);
    s->id = gSaveBlock2Ptr->playerTrainerId[0] | (gSaveBlock2Ptr->playerTrainerId[1] << 8);
    s->money = GetMoney(&gSaveBlock1Ptr->money);
    s->hours = gSaveBlock2Ptr->playTimeHours > 999 ? 999 : gSaveBlock2Ptr->playTimeHours;
    s->minutes = gSaveBlock2Ptr->playTimeMinutes > 59 ? 59 : gSaveBlock2Ptr->playTimeMinutes;
    s->hasDex = FlagGet(FLAG_SYS_POKEDEX_GET);
    /* Counting the Pokédex walks every species; twice a second is plenty. */
    if ((frames++ % 30) == 0)
    {
        dex = !s->hasDex ? 0 : IsNationalPokedexEnabled() ? GetNationalPokedexCount(FLAG_GET_CAUGHT)
                                                         : GetHoennPokedexCount(FLAG_GET_CAUGHT);
        /* trainer_card.c's GetRubyTrainerStars as Emerald fills it in. */
        stars = (GetGameStat(GAME_STAT_ENTERED_HOF) != 0) + (HasAllHoennMons() != 0)
              + (CountPlayerMuseumPaintings() >= CONTEST_CATEGORIES_COUNT);
    }
    s->dex = dex;
    s->stars = stars;
    for (int i = 0; i < NUM_BADGES; ++i)
        if (FlagGet(FLAG_BADGE01_GET + i))
            s->badges |= 1 << i;
}

/* The dex in the game's order: Hoenn numbers until the National Dex. */
static u16 DexNational(u16 index, bool8 national)
{
    return national ? index + 1 : HoennToNationalOrder(index + 1);
}

static void SnapshotDex(ViewState *s)
{
    static u16 seen, own, count, frames;
    static bool8 national;

    /* Walking the whole dex: only every half second. */
    if ((frames++ % 30) == 0)
    {
        u16 total;
        national = IsNationalPokedexEnabled();
        total = national ? NATIONAL_DEX_COUNT : HOENN_DEX_COUNT;
        seen = national ? GetNationalPokedexCount(FLAG_GET_SEEN) : GetHoennPokedexCount(FLAG_GET_SEEN);
        own = national ? GetNationalPokedexCount(FLAG_GET_CAUGHT) : GetHoennPokedexCount(FLAG_GET_CAUGHT);
        /* The list ends at the last mon seen, as the game's does. */
        count = 0;
        for (u16 i = 0; i < total; ++i)
            if (GetSetPokedexFlag(DexNational(i, national), FLAG_GET_SEEN))
                count = i + 1;
    }
    s->national = national;
    s->dexSeen = seen;
    s->dexOwn = own;
    s->dexCount = count;
    if (sDexScroll + DEX_ROWS > count)
        sDexScroll = count > DEX_ROWS ? count - DEX_ROWS : 0;
    s->dexScroll = sDexScroll;
    for (int r = 0; r < DEX_ROWS && sDexScroll + r < count; ++r)
    {
        u16 num = DexNational(sDexScroll + r, national);
        s->dexNum[r] = num;
        s->dexFlags[r] = (GetSetPokedexFlag(num, FLAG_GET_SEEN) ? 1 : 0)
                       | (GetSetPokedexFlag(num, FLAG_GET_CAUGHT) ? 2 : 0);
    }
    s->dexDetail = sDexDetail;
}

static void SnapshotBattler(BattlerView *v, u8 battler)
{
    struct Pokemon *mon;

    if (battler >= gBattlersCount || (gAbsentBattlerFlags & gBitTable[battler]))
        return;
    /* Shown once the game shows its healthbox, so nothing is revealed early. */
    if (gHealthboxSpriteIds[battler] >= MAX_SPRITES || gSprites[gHealthboxSpriteIds[battler]].invisible
     || !gSprites[gHealthboxSpriteIds[battler]].inUse)
        return;
    mon = GetBattlerSide(battler) == B_SIDE_PLAYER ? &gPlayerParty[gBattlerPartyIndexes[battler]]
                                                   : &gEnemyParty[gBattlerPartyIndexes[battler]];
    v->present = TRUE;
    v->side = GetBattlerSide(battler);
    v->level = gBattleMons[battler].level;
    v->hp = gBattleMons[battler].hp;
    v->maxHp = gBattleMons[battler].maxHP;
    v->iconSpecies = GetIconSpecies(gBattleMons[battler].species, gBattleMons[battler].personality);
    v->deoxys = gBattleMons[battler].species == SPECIES_DEOXYS;
    v->gender = GetMonGender(mon);
    GetMonNickname(mon, v->nick);
    if (v->hp == 0)
        v->ailment = AILMENT_FNT;
    else if (gBattleMons[battler].status1 & STATUS1_SLEEP)
        v->ailment = AILMENT_SLP;
    else if (gBattleMons[battler].status1 & STATUS1_PSN_ANY)
        v->ailment = AILMENT_PSN;
    else if (gBattleMons[battler].status1 & STATUS1_BURN)
        v->ailment = AILMENT_BRN;
    else if (gBattleMons[battler].status1 & STATUS1_FREEZE)
        v->ailment = AILMENT_FRZ;
    else if (gBattleMons[battler].status1 & STATUS1_PARALYSIS)
        v->ailment = AILMENT_PRZ;
}

static void SnapshotBattle(ViewState *s)
{
    s->isDouble = (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) != 0;
    s->safari = (gBattleTypeFlags & BATTLE_TYPE_SAFARI) != 0;
    /* Player left, opponent left, player right, opponent right. */
    SnapshotBattler(&s->battlers[0], GetBattlerAtPosition(B_POSITION_PLAYER_LEFT));
    SnapshotBattler(&s->battlers[1], GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT));
    if (s->isDouble)
    {
        SnapshotBattler(&s->battlers[2], GetBattlerAtPosition(B_POSITION_PLAYER_RIGHT));
        SnapshotBattler(&s->battlers[3], GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT));
    }
}

static void CopyText(u8 *dst, int size, const u8 *src)
{
    int n = 0;

    while (src && n < size - 1 && src[n] != EOS)
    {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = EOS;
}

/* What the hidden party menu is asking, for the lower panel. */
static void SnapshotPartyPanel(ViewState *s)
{
    s->menuCount = CtrPartyMenu_GetActions(s->menuNames, MAX_MENU_ITEMS);
    s->menuCols = 1;
    s->message = CtrPartyMenu_GetMessage();
    if (CtrMenu_YesNoOpen())
        s->panel = PANEL_YESNO;
    else if (s->menuCount)
    {
        s->panel = PANEL_ACTIONS;
        s->menuCursor = Menu_GetCursorPos();
    }
    else if (PartyMenuReady())
        s->panel = PANEL_HINT;
    else if (s->message)
        s->panel = PANEL_MESSAGE;
    CopyText(s->text, sizeof(s->text), s->message);
}

/* What the hidden bag is asking. */
static void SnapshotBagPanel(ViewState *s)
{
    s->menuCount = CtrBagMenu_GetActions(s->menuNames, MAX_MENU_ITEMS, &s->menuCols);
    s->message = CtrBagMenu_GetMessage();
    if (CtrMenu_YesNoOpen())
        s->panel = PANEL_YESNO;
    else if (gBagMenu->windowIds[ITEMWIN_QUANTITY] != WINDOW_NONE
          || gBagMenu->windowIds[ITEMWIN_QUANTITY_WIDE] != WINDOW_NONE)
        s->panel = PANEL_QUANTITY;
    else if (s->menuCount)
    {
        s->panel = PANEL_ACTIONS;
        s->menuCursor = Menu_GetCursorPos();
    }
    else if (s->message)
        s->panel = PANEL_MESSAGE;
    else if (BagMenuReady())
        s->panel = PANEL_HINT;
    CopyText(s->text, sizeof(s->text), s->message);
}

static void Snapshot(ViewState *s, u8 mode, u8 pressed)
{
    memset(s, 0, sizeof(*s));
    s->mode = mode;
    s->pressed = pressed;
    s->summary = -1;
    s->partyCursor = -1;
    s->pickMapsec = MAPSEC_NONE;
    if (s->mode == MODE_OFF)
        return;
    s->gender = gSaveBlock2Ptr->playerGender ? FEMALE : MALE;
    s->inBattle = gMain.inBattle;
    s->enabled = EnabledScreens();
    s->screen = sScreen;
    /* The hidden menus take over the view they belong to. */
    if (mode == MODE_PARTY_MENU)
        s->screen = SCR_POKEMON;
    else if (mode == MODE_BAG_MENU)
        s->screen = SCR_BAG;
    else if (mode == MODE_POKENAV)
        s->screen = SCR_POKENAV;
    StringCopy(s->name, gSaveBlock2Ptr->playerName);

    switch (s->mode)
    {
    case MODE_FIELD:
    case MODE_PARTY_MENU:
    case MODE_BAG_MENU:
        switch (s->screen)
        {
        case SCR_MAP:
            s->mapsec = PlayerRegionPosition(&s->cursorX, &s->cursorY);
            s->pickMapsec = sPickMapsec;
            s->pickX = sPickX;
            s->pickY = sPickY;
            break;
        case SCR_POKEMON:
            SnapshotParty(s);
            s->partyCursor = sPartyTapped;
            if (sSummary >= 0 && s->party[sSummary].species && !s->party[sSummary].isEgg)
                SnapshotSummary(s, sSummary);
            else if (mode == MODE_PARTY_MENU)
                SnapshotPartyPanel(s);
            break;
        case SCR_BAG:
            SnapshotBag(s);
            if (mode == MODE_BAG_MENU)
                SnapshotBagPanel(s);
            break;
        case SCR_CARD:
            SnapshotCard(s);
            break;
        case SCR_POKEDEX:
            SnapshotDex(s);
            break;
        case SCR_SAVE:
            s->saveStep = sSaveStep;
            s->canSave = FieldIdle();
            CopyText(s->text, sizeof(s->text), sSaveMessage);
            break;
        case SCR_OPTION:
            s->options[0] = gSaveBlock2Ptr->optionsTextSpeed;
            s->options[1] = gSaveBlock2Ptr->optionsBattleSceneOff;
            s->options[2] = gSaveBlock2Ptr->optionsBattleStyle;
            s->options[3] = gSaveBlock2Ptr->optionsSound;
            s->options[4] = gSaveBlock2Ptr->optionsButtonMode;
            s->options[5] = gSaveBlock2Ptr->optionsWindowFrameType;
            s->options[OPT_VOXEL] = CtrSettings_Voxel();
            s->options[OPT_VOXEL_PITCH] = CtrSettings_VoxelPitch();
            s->options[OPT_VOXEL_ZOOM] = CtrSettings_VoxelZoom();
            break;
        }
        break;
    case MODE_BATTLE_ACTION:
    {
        u8 b = sAsked.battler;
        SnapshotBattle(s);
        SnapshotParty(s);
        s->battler = b;
        s->cursor = gActionSelectionCursor[b];
        /* The FIGHT button previews the four move types. */
        for (int i = 0; i < MAX_MON_MOVES; ++i)
            s->moves4.moves[i] = gBattleMons[b].moves[i];
        break;
    }
    case MODE_BATTLE_MOVE:
    case MODE_BATTLE_TARGET:
    {
        u8 b = sAsked.battler;
        SnapshotBattle(s);
        s->battler = b;
        s->cursor = sMoveCancel ? MAX_MON_MOVES : gMoveSelectionCursor[b];
        memcpy(&s->moves4, &gBattleBufferA[b][4], sizeof(s->moves4));
        break;
    }
    case MODE_BATTLE_INFO:
        SnapshotBattle(s);
        SnapshotParty(s);
        break;
    }
}

/* Loads one icon or picture the snapshot needs; TRUE if it did. */
static bool8 Prefetch(const ViewState *s)
{
    sIconBudget = TRUE;
    for (int i = 0; i < PARTY_SIZE && sIconBudget; ++i)
        if (s->party[i].species)
            MonIcon(s->party[i].iconSpecies, s->party[i].deoxys);
    for (int i = 0; i < MAX_BATTLERS_COUNT && sIconBudget; ++i)
        if (s->battlers[i].present)
            MonIcon(s->battlers[i].iconSpecies, s->battlers[i].deoxys);
    for (int r = 0; r < BAG_ROWS && sIconBudget; ++r)
        if (s->items[r] != ITEM_NONE)
            ItemIcon(s->items[r]);
    if (sIconBudget && s->summary >= 0 && s->heldItem)
        ItemIcon(s->heldItem);
    if (sIconBudget && s->screen == SCR_POKEDEX && s->dexDetail)
        FrontPic(NationalPokedexNumToSpecies(s->dexDetail));
    if (sIconBudget && s->mode == MODE_BATTLE_ACTION)
        ItemIcon(ITEM_ESCAPE_ROPE);
    if (sIconBudget)
    {
        sIconBudget = FALSE;
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Drawing: the button column                                               */
/* ------------------------------------------------------------------------ */

static void DrawColumnButton(const ViewState *s, int i, bool8 on, bool8 enabled)
{
    const u8 *labels[SCR_COUNT] = {
        Ascii(CTR_TEXT("MAP", "MAPA")), gText_MenuPokemon, gText_MenuBag, s->name, gText_MenuPokedex, gText_MenuPokenav,
        gText_MenuSave, gText_MenuOption,
    };
    const Icon *icon = &sRes.column[i];
    int y = 3 + i * 30;

    DrawBoxEx(BOX_MENU, COL_X + 4, y, 9, 3, on);
    if (icon->tiles && enabled)
        DrawSprite(icon->tiles, icon->size, icon->size, COL_X + 2, y - (icon->size == 4 ? 4 : 0), icon->pal.c);
    DrawStr(&sSmall, labels[i], COL_X + 30, y + 6, enabled ? LABEL_FG(on) : TXT_LIGHT,
            enabled ? LABEL_SH(on) : TXT_WHITE);
}

/*
 * The column changes far less often than what is beside it: it is kept,
 * unpressed, in every background cache, and a redraw only paints the chosen
 * and pressed buttons over it. The caches are repainted when which entries
 * exist changes, or the player's name does.
 */
static void DrawColumn(const ViewState *s)
{
    static int cachedMask = -1;
    static u8 cachedName[PLAYER_NAME_LENGTH + 1];

    if (cachedMask != s->enabled || memcmp(cachedName, s->name, sizeof(cachedName)) != 0)
    {
        u16 *canvas = sDst;
        cachedMask = s->enabled;
        memcpy(cachedName, s->name, sizeof(cachedName));
        for (int c = 0; c < CACHE_COUNT; ++c)
        {
            if (!sCache[c] || c == CACHE_WIDE)
                continue;
            sDst = sCache[c];
            for (int i = 0; i < SCR_COUNT; ++i)
                DrawColumnButton(s, i, FALSE, (s->enabled >> i) & 1);
        }
        sDst = canvas;
        /* The canvas started from the stale column: paint all of it. */
        for (int i = 0; i < SCR_COUNT; ++i)
            DrawColumnButton(s, i, FALSE, (s->enabled >> i) & 1);
    }
    for (int i = 0; i < SCR_COUNT; ++i)
    {
        bool8 enabled = (s->enabled >> i) & 1;
        bool8 on = s->screen == i || s->pressed == HIT_COLUMN + i;

        if (on)
            DrawColumnButton(s, i, TRUE, enabled);
        if (enabled)
            AddHit(COL_X, 3 + i * 30 - 3, W - COL_X, 30, HIT_COLUMN + i);
    }
}

/* ------------------------------------------------------------------------ */
/* Drawing: party and summary                                               */
/* ------------------------------------------------------------------------ */

static void SetPartyColor(Pal *pal, u8 offset, u8 id)
{
    pal->c[offset] = Rgb565(sRes.partyRaw[id]);
}

/* party_menu.c's LoadPartyBoxPalette, for the states shown here. */
static void PartyBoxPalette(Pal *pal, const MonView *m, bool8 selected)
{
    static const u8 offsets1[] = {4, 5, 6}, offsets2[] = {1, 7, 8};
    static const u8 normal1[] = {52, 53, 54}, normal2[] = {49, 55, 56};
    static const u8 sel1[] = {116, 117, 118}, sel2[] = {97, 103, 104};
    static const u8 faint1[] = {84, 85, 86}, faint2[] = {81, 87, 88};
    static const u8 selFaint1[] = {148, 149, 150};
    static const u8 noMon[] = {17, 27, 28}, noMonOffsets[] = {1, 11, 12};
    const u8 *ids1, *ids2;

    if (!m->species)
    {
        for (int i = 0; i < 3; ++i)
            SetPartyColor(pal, noMonOffsets[i], noMon[i]);
        return;
    }
    if (m->fainted)
        ids1 = selected ? selFaint1 : faint1, ids2 = selected ? sel2 : faint2;
    else
        ids1 = selected ? sel1 : normal1, ids2 = selected ? sel2 : normal2;
    for (int i = 0; i < 3; ++i)
    {
        SetPartyColor(pal, offsets1[i], ids1[i]);
        SetPartyColor(pal, offsets2[i], ids2[i]);
    }
}

/* Positions from party_menu.c's sPartyBoxInfoRects and sprite coordinates. */
typedef struct
{
    u8 nameX, nameY, levelX, levelY, genderX, genderY, hpX, hpY, maxHpX, maxHpY, barX, barY;
    s8 iconX, iconY, statusX, statusY;
} SlotLayout;

static const SlotLayout sMainLayout = {24, 11, 32, 20, 64, 20, 38, 37, 53, 37, 24, 35, -8, 0, 26, 24};
static const SlotLayout sWideLayout = {22, 3, 30, 12, 62, 12, 102, 12, 117, 12, 88, 10, -8, -6, 24, 15};

static void DrawHpBar(int x, int y, int width, u16 hp, u16 maxHp, const Pal *pal)
{
    static const u8 green[] = {57, 58}, yellow[] = {73, 74}, red[] = {89, 90};
    const u8 *ids;
    int fill;

    if (maxHp == 0)
        return;
    fill = hp * width / maxHp;
    if (hp > 0 && fill == 0)
        fill = 1;
    ids = hp * 2 > maxHp ? green : hp * 5 > maxHp ? yellow : red;
    FillRect(x, y, fill, 1, Rgb565(sRes.partyRaw[ids[1]]));
    FillRect(x, y + 1, fill, 2, Rgb565(sRes.partyRaw[ids[0]]));
    FillRect(x + fill, y, width - fill, 1, pal->c[0x0D]);
    FillRect(x + fill, y + 1, width - fill, 2, pal->c[0x02]);
}

static void DrawStatusIcon(u8 ailment, int x, int y)
{
    if (ailment == AILMENT_NONE || !sRes.statusTiles)
        return;
    DrawSprite(sRes.statusTiles + (ailment - 1) * 4 * 32, 4, 1, x, y, sRes.statusPal.c);
}

static void DrawPartySlot(const MonView *m, int slot, int x, int y, bool8 selected)
{
    bool8 main = slot == 0;
    const SlotLayout *l = main ? &sMainLayout : &sWideLayout;
    const u8 *map;
    int w = main ? 10 : 18, h = main ? 7 : 3;
    Pal pal = sRes.partyPal[main ? 3 : 4];
    u16 fg, sh;

    if (main)
        map = m->isEgg && sRes.slotMainNoHp ? sRes.slotMainNoHp : sRes.slotMain;
    else if (!m->species)
        map = sRes.slotWideEmpty;
    else
        map = m->isEgg && sRes.slotWideNoHp ? sRes.slotWideNoHp : sRes.slotWide;
    PartyBoxPalette(&pal, m, selected);
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx)
            DrawTile(sRes.partyTiles + map[ty * w + tx] * 32, x + tx * 8, y + ty * 8, pal.c, FALSE, FALSE);
    if (!m->species)
        return;

    fg = pal.c[TEXT_COLOR_LIGHT_GRAY];
    sh = pal.c[TEXT_COLOR_DARK_GRAY];
    DrawStr(&sSmall, m->nick, x + l->nameX, y + l->nameY, fg, sh);
    if (!m->isEgg)
    {
        u8 text[16], *end;

        StringCopy(text, gText_LevelSymbol);
        StringAppend(text, Number(m->level, 3, STR_CONV_MODE_LEFT_ALIGN));
        DrawStr(&sSmall, text, x + l->levelX, y + l->levelY, fg, sh);
        if (m->gender == MON_MALE || m->gender == MON_FEMALE)
        {
            u8 color = m->gender == MON_MALE ? 59 : 75;
            DrawStr(&sSmall, m->gender == MON_MALE ? gText_MaleSymbol : gText_FemaleSymbol, x + l->genderX,
                    y + l->genderY, Rgb565(sRes.partyRaw[color]), Rgb565(sRes.partyRaw[color + 1]));
        }
        /* DisplayPartyPokemonHP and ...MaxHP both print a slash, overlapping. */
        StringCopy(text, Number(m->hp, 3, STR_CONV_MODE_RIGHT_ALIGN));
        end = text + StringLength(text);
        end[0] = CHAR_SLASH;
        end[1] = EOS;
        DrawStr(&sSmall, text, x + l->hpX, y + l->hpY, fg, sh);
        StringCopy(text, gText_Slash);
        StringAppend(text, Number(m->maxHp, 3, STR_CONV_MODE_RIGHT_ALIGN));
        DrawStr(&sSmall, text, x + l->maxHpX, y + l->maxHpY, fg, sh);
        DrawHpBar(x + l->barX, y + l->barY, 48, m->hp, m->maxHp, &pal);
        DrawStatusIcon(m->ailment, x + l->statusX, y + l->statusY);
    }
    /* All icons animate, as the one under the cursor does in the party
     * menu; a fainted mon's stays still, as the game keeps it. */
    AddMonIcon(m->iconSpecies, m->deoxys, x + l->iconX, y + l->iconY, m->fainted);
}

/* A message box across the lower panel: the text the hidden menu shows. */
static void DrawPanelMessage(const u8 *text, int y, int ht, bool8 tappable)
{
    DrawBox(BOX_MESSAGE, 0, y, CW / 8, ht);
    DrawStr(&sNormal, text, 18, y + 8, TXT_WHITE, TXT_DARK);
    if (tappable)
        AddHit(0, y, CW, ht * 8, HIT_PANEL);
}

/* The lower panel of the party and bag views, from y to the bottom. */
static void DrawPanel(const ViewState *s, int y, const u8 *hint)
{
    int ht = (H - y) / 8;

    switch (s->panel)
    {
    case PANEL_HINT:
        DrawBox(BOX_MESSAGE, 0, y, 20, ht);
        DrawStr(&sNormal, hint, 18, y + 8, TXT_WHITE, TXT_DARK);
        DrawLabelButton(164, y + 4, 9, ht - 1, gText_Cancel2, s->pressed == HIT_CANCEL, TRUE, HIT_CANCEL);
        break;
    case PANEL_MESSAGE:
        DrawPanelMessage(s->text, y, ht, TRUE);
        break;
    case PANEL_YESNO:
        DrawPanelMessage(s->text, y, ht - 4, FALSE);
        DrawLabelButton(0, H - 32, 15, 4, gText_Yes, s->pressed == HIT_YES, TRUE, HIT_YES);
        DrawLabelButton(120, H - 32, 15, 4, gText_No, s->pressed == HIT_NO, TRUE, HIT_NO);
        break;
    case PANEL_QUANTITY:
    {
        static const u8 up[] = {CHAR_UP_ARROW, EOS}, down[] = {CHAR_DOWN_ARROW, EOS};
        DrawPanelMessage(s->text, y, ht - 4, FALSE);
        DrawLabelButton(0, H - 32, 7, 4, up, s->pressed == HIT_UP, TRUE, HIT_UP);
        DrawLabelButton(56, H - 32, 7, 4, down, s->pressed == HIT_DOWN, TRUE, HIT_DOWN);
        DrawLabelButton(112, H - 32, 8, 4, Ascii(CTR_TEXT("OK", "VALE")), s->pressed == HIT_OK, TRUE, HIT_OK);
        DrawLabelButton(176, H - 32, 8, 4, gText_Cancel2, s->pressed == HIT_CANCEL, TRUE, HIT_CANCEL);
        break;
    }
    case PANEL_ACTIONS:
    {
        /* The game's submenu: two rows of buttons. */
        int cols = (s->menuCount + 1) / 2, cellW;
        if (cols < 1) cols = 1;
        cellW = (CW / cols) / 8;
        for (int i = 0; i < s->menuCount; ++i)
        {
            int cx = (i % cols) * cellW * 8, cy = y + (i / cols) * ((ht / 2) * 8);
            DrawLabelButtonFont(cellW >= 10 ? &sNormal : &sSmall, cx, cy, cellW, ht / 2, s->menuNames[i],
                                s->pressed == HIT_MENU + i, TRUE, HIT_MENU + i);
        }
        break;
    }
    }
}

static void DrawParty(const ViewState *s)
{
    /* One main slot and five wide ones, as on the GBA, in the 240px view. */
    const int mainX = 8, wideX = 94, wideTop = 8, gap = 8;

    for (int i = 0; i < PARTY_SIZE; ++i)
    {
        int x = i == 0 ? mainX : wideX;
        int y = i == 0 ? wideTop + 16 : wideTop + (i - 1) * (24 + gap);
        int hitW = i == 0 ? 80 : 144, hitH = i == 0 ? 56 : 24;

        DrawPartySlot(&s->party[i], i, x, y, s->partyCursor == i || s->pressed == HIT_SLOT + i);
        if (s->party[i].species)
            AddHit(x - 8, y - 4, hitW + 8, hitH + 8, HIT_SLOT + i);
    }
    DrawPanel(s, 168, gText_ChoosePokemon);
}

static void DrawSummary(const ViewState *s)
{
    static const char *const statNames[6] = {CTR_TEXT("HP", "PS"), CTR_TEXT("ATTACK", "ATAQUE"), CTR_TEXT("DEFENSE", "DEFENSA"), CTR_TEXT("SP. ATK", "AT. ESP."), CTR_TEXT("SP. DEF", "DEF. ESP."), CTR_TEXT("SPEED", "VELOC.")};
    const MonView *m = &s->party[s->summary];
    u8 text[24];

    /* Who. */
    DrawBox(BOX_MENU, 0, 0, 30, 7);
    AddMonIcon(m->iconSpecies, m->deoxys, 6, 8, FALSE);
    DrawStr(&sNormal, m->nick, 44, 8, TXT_DARK, TXT_LIGHT);
    if (m->gender == MON_MALE || m->gender == MON_FEMALE)
        DrawStr(&sNormal, m->gender == MON_MALE ? gText_MaleSymbol : gText_FemaleSymbol,
                44 + StrWidth(&sNormal, m->nick) + 4, 8, m->gender == MON_MALE ? TXT_BLUE : TXT_RED,
                m->gender == MON_MALE ? TXT_LBLUE : TXT_LRED);
    StringCopy(text, gText_LevelSymbol);
    StringAppend(text, Number(m->level, 3, STR_CONV_MODE_LEFT_ALIGN));
    DrawStrRight(&sNormal, text, 228, 8, TXT_DARK, TXT_LIGHT);
    DrawStr(&sSmall, gSpeciesNames[m->species], 44, 26, TXT_DARK, TXT_LIGHT);
    DrawTypeIcon(s->types[0], 150, 24);
    if (s->types[1] != s->types[0])
        DrawTypeIcon(s->types[1], 186, 24);
    if (s->heldItem)
    {
        DrawItemIcon(s->heldItem, 12, 30);
        DrawStr(&sSmall, GetItemName(s->heldItem), 44, 40, TXT_DARK, TXT_LIGHT);
    }

    /* Stats, nature and ability. */
    DrawBox(BOX_MENU, 0, 56, 30, 10);
    for (int i = 0; i < 6; ++i)
    {
        int x = 12 + (i % 2) * 112, y = 64 + (i / 2) * 14;
        DrawStr(&sSmall, Ascii(statNames[i]), x, y, TXT_DARK, TXT_LIGHT);
        if (i == 0)
        {
            StringCopy(text, Number(m->hp, 3, STR_CONV_MODE_LEFT_ALIGN));
            StringAppend(text, gText_Slash);
            StringAppend(text, Number(s->stats[0], 3, STR_CONV_MODE_LEFT_ALIGN));
            DrawStrRight(&sSmall, text, x + 100, y, TXT_DARK, TXT_LIGHT);
        }
        else
            DrawStrRight(&sSmall, Number(s->stats[i], 3, STR_CONV_MODE_LEFT_ALIGN), x + 100, y, TXT_DARK, TXT_LIGHT);
    }
    DrawStr(&sSmall, gNatureNamePointers[s->nature], 12, 108, TXT_BLUE, TXT_LBLUE);
    DrawStr(&sSmall, gAbilityNames[s->ability], 124, 108, TXT_BLUE, TXT_LBLUE);

    /* Moves. */
    DrawBox(BOX_MENU, 0, 136, 30, 10);
    for (int i = 0; i < MAX_MON_MOVES; ++i)
    {
        int y = 142 + i * 16;
        if (!s->moves[i])
            continue;
        DrawTypeIcon(gBattleMoves[s->moves[i]].type, 10, y);
        DrawStr(&sSmall, gMoveNames[s->moves[i]], 48, y + 1, TXT_DARK, TXT_LIGHT);
        StringCopy(text, gText_MoveInterfacePP);
        StringAppend(text, Ascii(" "));
        StringAppend(text, Number(s->pp[i], 2, STR_CONV_MODE_RIGHT_ALIGN));
        StringAppend(text, gText_Slash);
        StringAppend(text, Number(s->maxPp[i], 2, STR_CONV_MODE_RIGHT_ALIGN));
        DrawStrRight(&sSmall, text, 228, y + 1, TXT_DARK, TXT_LIGHT);
    }

    /* Previous, back, next. */
    {
        static const u8 left[] = {CHAR_LEFT_ARROW, EOS}, right[] = {CHAR_RIGHT_ARROW, EOS};
        DrawLabelButton(0, 216, 7, 3, left, s->pressed == HIT_PREV, TRUE, HIT_PREV);
        DrawLabelButton(56, 216, 16, 3, gText_Cancel2, s->pressed == HIT_BACK, TRUE, HIT_BACK);
        DrawLabelButton(184, 216, 7, 3, right, s->pressed == HIT_NEXT, TRUE, HIT_NEXT);
    }
}

/* ------------------------------------------------------------------------ */
/* Drawing: region map                                                      */
/* ------------------------------------------------------------------------ */

#define MAP_ORIGIN_X 0
#define MAP_ORIGIN_Y 8

static void DrawMapTile8(u8 tile, int x, int y)
{
    const u8 *src;

    if (!sRes.mapTiles || tile >= sRes.mapTileCount)
        return;
    src = sRes.mapTiles + tile * 64;
    for (int py = 0; py < 8; ++py)
        for (int px = 0; px < 8; ++px)
        {
            /* The map's colours are loaded at palette 7: indices 112 up. */
            u8 v = src[py * 8 + px];
            if (v >= 112 && v < 144 && x + px < CW)
                Put(x + px, y + py, sRes.mapPal[v - 112]);
        }
}

/* The map picture itself, once, into its cache. */
static void BuildMapCache(void)
{
    if (!sCache[CACHE_MAP])
        return;
    memcpy(sCache[CACHE_MAP], sCache[CACHE_MENU], sizeof(sCanvas));
    sDst = sCache[CACHE_MAP];
    FillRect(0, 0, CW, H, sRes.mapPal[0]);
    /* The map is a 64x64 affine map; the ocean around Hoenn is tile 0. */
    for (int ty = -1; ty < H / 8; ++ty)
        for (int tx = 0; tx < CW / 8; ++tx)
        {
            u8 tile = (sRes.mapMap && ty >= 0 && ty < 64) ? sRes.mapMap[ty * 64 + tx] : 0;
            DrawMapTile8(tile, MAP_ORIGIN_X + tx * 8, MAP_ORIGIN_Y + ty * 8);
        }
    sDst = sCanvas;
}

static void DrawRegionMap(const ViewState *s)
{
    u8 name[32];
    bool8 picked = s->pickMapsec != MAPSEC_NONE;
    u8 mapsec = picked ? s->pickMapsec : s->mapsec;

    if (s->mapsec != MAPSEC_NONE && sRes.playerIcon[s->gender])
        DrawSprite(sRes.playerIcon[s->gender], 2, 2, MAP_ORIGIN_X + s->cursorX * 8 - 4,
                   MAP_ORIGIN_Y + s->cursorY * 8 - 4, sRes.playerIconPal[s->gender].c);
    if (picked && sRes.cursorTiles)
        DrawSprite(sRes.cursorTiles, 2, 2, MAP_ORIGIN_X + s->pickX * 8 - 4, MAP_ORIGIN_Y + s->pickY * 8 - 4,
                   sRes.cursorPal.c);
    AddHit(0, 0, CW, 176, HIT_MAP);

    if (mapsec == MAPSEC_NONE)
        return;
    GetMapName(name, mapsec, 0);
    DrawBoxEx(BOX_MENU, 8, 188, 28, 5, picked);
    DrawStrCentered(&sNormal, name, CW / 2, 200, LABEL_FG(picked), LABEL_SH(picked));
}

/* The section under a tapped map cell, as the region map's cursor finds it. */
static void PickMapCell(int x, int y)
{
    int cx = (x - MAP_ORIGIN_X + 64) / 8 - 8, cy = (y - MAP_ORIGIN_Y + 64) / 8 - 8;

    sPickMapsec = MAPSEC_NONE;
    for (int i = 0; i < MAPSEC_NONE; ++i)
    {
        const struct RegionMapLocation *e = &gRegionMapEntries[i];
        int ex = e->x + 1, ey = e->y + 2;
        if (e->width && cx >= ex && cy >= ey && cx < ex + e->width && cy < ey + e->height)
        {
            sPickMapsec = i;
            sPickX = cx;
            sPickY = cy;
            return;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Drawing: trainer card                                                    */
/* ------------------------------------------------------------------------ */

#define CARD_X 0
#define CARD_Y 40

static void CardPals(Pal *pals, u8 stars, u8 gender)
{
    memcpy(pals, sRes.cardPal[stars], sizeof(Pal) * 3);
    if (gender)
        pals[1] = sRes.cardFemaleBg;
    pals[3] = sRes.badgePal;
    pals[4] = sRes.starPal;
}

/* The card's background stripes and front, by star count and gender. */
static void BuildCardCache(u8 stars, u8 gender)
{
    Pal pals[5];

    if (!sCache[CACHE_CARD] || sCardCacheKey == stars * 2 + gender)
        return;
    sCardCacheKey = stars * 2 + gender;
    CardPals(pals, stars, gender);
    memcpy(sCache[CACHE_CARD], sCache[CACHE_MENU], sizeof(sCanvas));
    sDst = sCache[CACHE_CARD];
    FillRect(0, 0, CW, H, pals[0].c[0]);
    if (sRes.cardBg)
        for (int ty = 0; ty < H / 8; ++ty)
            for (int tx = 0; tx < CW / 8; ++tx)
                DrawMapEntry(sRes.cardTiles, sRes.cardTileCount, sRes.cardBg[(ty % 20) * 30 + (tx % 30)],
                             tx * 8, ty * 8, pals);
    if (sRes.cardFront)
        for (int ty = 0; ty < 20; ++ty)
            for (int tx = 0; tx < 30; ++tx)
                DrawMapEntry(sRes.cardTiles, sRes.cardTileCount, sRes.cardFront[ty * 30 + tx],
                             CARD_X + tx * 8, CARD_Y + ty * 8, pals);
    if (sRes.trainerPic[gender])
        DrawSprite(sRes.trainerPic[gender], 8, 8, CARD_X + 20 * 8, CARD_Y + 5 * 8, sRes.trainerPicPal[gender].c);
    for (int i = 0; i < stars; ++i)
        DrawMapEntry(sRes.cardTiles, sRes.cardTileCount, 0x4000 | 143, CARD_X + (15 + i) * 8, CARD_Y + 7 * 8, pals);
    sDst = sCanvas;
}

static void DrawTrainerCard(const ViewState *s)
{
    u8 text[32];
    int bx = CARD_X + 8, by = CARD_Y + 8; /* WIN_CARD_TEXT is at tile (1,1) */

    /* trainer_card.c: PrintNameOnCardFront, PrintIdOnCard, PrintMoneyOnCard,
     * PrintPokedexOnCard, PrintTimeOnCard, DrawStarsAndBadgesOnCard. */
    StringCopy(StringCopy(text, gText_TrainerCardName), s->name);
    DrawStr(&sNormal, text, bx + 16, by + 33, TXT_DARK, TXT_LIGHT);

    StringCopy(StringCopy(text, gText_TrainerCardIDNo), Number(s->id, 5, STR_CONV_MODE_LEADING_ZEROS));
    DrawStr(&sNormal, text, bx + 120 + (96 - StrWidth(&sNormal, text)) / 2, by + 9, TXT_DARK, TXT_LIGHT);

    DrawStr(&sNormal, gText_TrainerCardMoney, bx + 16, by + 57, TXT_DARK, TXT_LIGHT);
    text[0] = CHAR_CURRENCY;
    StringCopy(text + 1, Number(s->money, 6, STR_CONV_MODE_LEFT_ALIGN));
    DrawStrRight(&sNormal, text, bx + 128, by + 57, TXT_DARK, TXT_LIGHT);

    if (s->hasDex)
    {
        DrawStr(&sNormal, gText_TrainerCardPokedex, bx + 16, by + 73, TXT_DARK, TXT_LIGHT);
        DrawStrRight(&sNormal, Number(s->dex, 3, STR_CONV_MODE_LEFT_ALIGN), bx + 128, by + 73, TXT_DARK, TXT_LIGHT);
    }

    DrawStr(&sNormal, gText_TrainerCardTime, bx + 16, by + 89, TXT_DARK, TXT_LIGHT);
    {
        int colon = StrWidth(&sNormal, gText_Colon2), x = bx + 128 - (colon + 30);
        DrawStr(&sNormal, Number(s->hours, 3, STR_CONV_MODE_RIGHT_ALIGN), x, by + 89, TXT_DARK, TXT_LIGHT);
        DrawStr(&sNormal, gText_Colon2, x + 18, by + 89, TXT_DARK, TXT_LIGHT);
        DrawStr(&sNormal, Number(s->minutes, 2, STR_CONV_MODE_LEADING_ZEROS), x + 18 + colon, by + 89, TXT_DARK,
                TXT_LIGHT);
    }

    if (sRes.badgeTiles)
        for (int i = 0; i < NUM_BADGES; ++i)
        {
            int x = CARD_X + (4 + 3 * i) * 8, y = CARD_Y + 15 * 8;
            if (!(s->badges & (1 << i)))
                continue;
            DrawTile(sRes.badgeTiles + (2 * i) * 32, x, y, sRes.badgePal.c, FALSE, FALSE);
            DrawTile(sRes.badgeTiles + (2 * i + 1) * 32, x + 8, y, sRes.badgePal.c, FALSE, FALSE);
            DrawTile(sRes.badgeTiles + (2 * i + 16) * 32, x, y + 8, sRes.badgePal.c, FALSE, FALSE);
            DrawTile(sRes.badgeTiles + (2 * i + 17) * 32, x + 8, y + 8, sRes.badgePal.c, FALSE, FALSE);
        }
}

/* ------------------------------------------------------------------------ */
/* Drawing: bag                                                             */
/* ------------------------------------------------------------------------ */

#define BAG_LIST_Y 24
#define BAG_LIST_ROWS_Y (BAG_LIST_Y + 8)
#define BAG_PANEL_Y 160

static void DrawScrollArrows(int x, int top, int bottom, bool8 canUp, bool8 canDown, u8 pressed)
{
    static const u8 up[] = {CHAR_UP_ARROW, EOS}, down[] = {CHAR_DOWN_ARROW, EOS};
    int half = ((bottom - top) / 8 - 1) / 2;

    DrawBoxEx(BOX_MENU, x, top, 4, half, pressed == HIT_UP);
    DrawBoxEx(BOX_MENU, x, bottom - half * 8, 4, half, pressed == HIT_DOWN);
    DrawStrCentered(&sNormal, up, x + 16, top + half * 4 - 8, canUp ? LABEL_FG(pressed == HIT_UP) : TXT_LIGHT,
                    TXT_LIGHT);
    DrawStrCentered(&sNormal, down, x + 16, bottom - half * 4 - 8,
                    canDown ? LABEL_FG(pressed == HIT_DOWN) : TXT_LIGHT, TXT_LIGHT);
    AddHit(x, top, 32, half * 8, HIT_UP);
    AddHit(x, bottom - half * 8, 32, half * 8, HIT_DOWN);
}

static void DrawBag(const ViewState *s)
{
    /* Pocket tabs, the bag's own pocket names. */
    for (int p = 0; p < POCKETS_COUNT; ++p)
    {
        bool8 on = p == s->pocket || s->pressed == HIT_POCKET + p;
        DrawBoxEx(BOX_MENU, p * 48, 0, 6, 3, on);
        DrawStrCentered(&sSmall, gPocketNamesStringsTable[p], p * 48 + 24, 6, LABEL_FG(on), LABEL_SH(on));
        AddHit(p * 48, 0, 48, 24, HIT_POCKET + p);
    }

    /* The list. */
    DrawBox(BOX_MENU, 0, BAG_LIST_Y, 26, 17);
    for (int r = 0; r < BAG_ROWS; ++r)
    {
        u16 index = s->bagScroll + r;
        int y = BAG_LIST_ROWS_Y + r * BAG_ROW_H;
        u16 item = s->items[r];

        if (index >= s->bagCount)
            break;
        if ((s->bagCursor >= 0 && index == (u16)s->bagCursor) || s->pressed == HIT_ROW + r)
            FillRect(8, y, 192, BAG_ROW_H, TXT_LIGHT);
        if (item == ITEM_NONE)
            continue;
        DrawItemIcon(item, 10, y);
        DrawStr(&sNormal, GetItemName(item), 38, y + 4, TXT_DARK, TXT_WHITE);
        if (!s->keyPocket)
        {
            u8 text[8];
            text[0] = CHAR_x;
            StringCopy(text + 1, Number(s->qty[r], 3, STR_CONV_MODE_LEFT_ALIGN));
            DrawStrRight(&sNormal, text, 196, y + 4, TXT_DARK, TXT_WHITE);
        }
        AddHit(8, y, 192, BAG_ROW_H, HIT_ROW + r);
    }
    if (s->bagCount == 0)
        DrawStrCentered(&sNormal, Ascii("-"), 104, BAG_LIST_ROWS_Y + 52, TXT_LIGHT, TXT_WHITE);
    DrawScrollArrows(208, BAG_LIST_Y, BAG_PANEL_Y, s->bagScroll > 0, s->bagScroll + BAG_ROWS < s->bagCount,
                     s->pressed);

    /* The lower panel: the hidden bag's questions, or the description. */
    if (s->panel == PANEL_NONE || s->panel == PANEL_HINT)
    {
        DrawBox(BOX_MESSAGE, 0, BAG_PANEL_Y, CW / 8, (H - BAG_PANEL_Y) / 8);
        if (s->descItem != ITEM_NONE)
            DrawStr(&sSmall, GetItemDescription(s->descItem), 18, BAG_PANEL_Y + 10, TXT_WHITE, TXT_DARK);
        if (s->panel == PANEL_HINT)
            DrawLabelButton(152, BAG_PANEL_Y + 44, 11, 4, gText_Cancel2, s->pressed == HIT_CANCEL, TRUE, HIT_CANCEL);
    }
    else
        DrawPanel(s, BAG_PANEL_Y, NULL);
}

/* ------------------------------------------------------------------------ */
/* Drawing: Pokédex                                                         */
/* ------------------------------------------------------------------------ */

static void DrawDex(const ViewState *s)
{
    u8 text[24];

    if (s->dexDetail)
    {
        u16 species = NationalPokedexNumToSpecies(s->dexDetail);
        const struct PokedexEntry *entry = &gPokedexEntries[s->dexDetail];
        bool8 caught = GetSetPokedexFlag(s->dexDetail, FLAG_GET_CAUGHT);
        int pic = FrontPic(species);

        DrawBox(BOX_MENU, 0, 0, 30, 12);
        if (pic >= 0)
            DrawSprite(sPics[pic].tiles, 8, 8, 12, 16, sPics[pic].pal.c);
        StringCopy(text, Ascii("No."));
        StringAppend(text, Number(s->national ? s->dexDetail : NationalToHoennOrder(s->dexDetail), 3,
                                  STR_CONV_MODE_LEADING_ZEROS));
        DrawStr(&sNormal, text, 88, 12, TXT_DARK, TXT_LIGHT);
        DrawStr(&sNormal, gSpeciesNames[species], 88, 30, TXT_DARK, TXT_LIGHT);
        DrawTypeIcon(gSpeciesInfo[species].types[0], 88, 50);
        if (gSpeciesInfo[species].types[1] != gSpeciesInfo[species].types[0])
            DrawTypeIcon(gSpeciesInfo[species].types[1], 124, 50);
        if (caught)
        {
#if GAME_LANGUAGE == LANGUAGE_SPANISH
            DrawStr(&sSmall, entry->categoryName, 88, 70, TXT_DARK, TXT_LIGHT);
            StringCopy(text, Ascii("ALT. "));
            StringAppend(text, Number(entry->height / 10, 2, STR_CONV_MODE_LEFT_ALIGN));
            StringAppend(text, Ascii(","));
            StringAppend(text, Number(entry->height % 10, 1, STR_CONV_MODE_LEFT_ALIGN));
            StringAppend(text, Ascii(" m"));
            DrawStr(&sSmall, text, 88, 82, TXT_DARK, TXT_LIGHT);
            StringCopy(text, Ascii("PESO "));
            StringAppend(text, Number(entry->weight / 10, 4, STR_CONV_MODE_LEFT_ALIGN));
            StringAppend(text, Ascii(","));
            StringAppend(text, Number(entry->weight % 10, 1, STR_CONV_MODE_LEFT_ALIGN));
            StringAppend(text, Ascii(" kg"));
            DrawStr(&sSmall, text, 150, 82, TXT_DARK, TXT_LIGHT);
#else
            u32 inches = (entry->height * 10000 / 254 + 50) / 100; /* decimetres to inches */
            u32 pounds = (entry->weight * 100000 / 4536 + 50) / 100; /* hectograms to 0.1 lbs */
            u8 *p;

            DrawStr(&sSmall, entry->categoryName, 88, 70, TXT_DARK, TXT_LIGHT);
            StringCopy(text, Ascii("HT "));
            p = StringAppend(text, Number(inches / 12, 2, STR_CONV_MODE_LEFT_ALIGN));
            p[0] = CHAR_SGL_QUOTE_RIGHT;
            p[1] = EOS;
            StringAppend(text, Number(inches % 12, 2, STR_CONV_MODE_LEADING_ZEROS));
            DrawStr(&sSmall, text, 88, 82, TXT_DARK, TXT_LIGHT);
            StringCopy(text, Ascii("WT "));
            StringAppend(text, Number(pounds / 10, 4, STR_CONV_MODE_LEFT_ALIGN));
            StringAppend(text, Ascii(" lbs."));
            DrawStr(&sSmall, text, 150, 82, TXT_DARK, TXT_LIGHT);
#endif
            DrawBox(BOX_MESSAGE, 0, 96, 30, 15);
            DrawStr(&sSmall, entry->description, 18, 106, TXT_WHITE, TXT_DARK);
        }
        DrawLabelButton(64, 216, 14, 3, gText_Cancel2, s->pressed == HIT_BACK, TRUE, HIT_BACK);
        return;
    }

    /* The list. */
    DrawBox(BOX_MENU, 0, 0, 30, 3);
    StringCopy(text, Ascii(CTR_TEXT("SEEN ", "VISTOS ")));
    StringAppend(text, Number(s->dexSeen, 3, STR_CONV_MODE_LEFT_ALIGN));
    DrawStr(&sSmall, text, 12, 6, TXT_DARK, TXT_LIGHT);
    StringCopy(text, Ascii(CTR_TEXT("OWN ", "TIENES ")));
    StringAppend(text, Number(s->dexOwn, 3, STR_CONV_MODE_LEFT_ALIGN));
    DrawStr(&sSmall, text, 100, 6, TXT_DARK, TXT_LIGHT);
    DrawStrRight(&sSmall, s->national ? Ascii(CTR_TEXT("NATIONAL", "NACIONAL")) : Ascii("HOENN"), 228, 6, TXT_BLUE, TXT_LBLUE);

    DrawBox(BOX_MENU, 0, 24, 26, 27);
    for (int r = 0; r < DEX_ROWS && s->dexScroll + r < s->dexCount; ++r)
    {
        int y = 32 + r * 24;
        u16 num = s->dexNum[r];
        bool8 seen = s->dexFlags[r] & 1, caught = s->dexFlags[r] & 2;

        if (s->pressed == HIT_ROW + r)
            FillRect(8, y, 192, 24, TXT_LIGHT);
        if (caught && sRes.ballTiles)
            DrawSprite(sRes.ballTiles, 2, 2, 10, y + 4, sRes.ballPal.c);
        StringCopy(text, Ascii("No."));
        StringAppend(text, Number(s->national ? num : s->dexScroll + r + 1, 3, STR_CONV_MODE_LEADING_ZEROS));
        DrawStr(&sSmall, text, 30, y + 6, TXT_DARK, TXT_WHITE);
        DrawStr(&sNormal, seen ? gSpeciesNames[NationalPokedexNumToSpecies(num)] : Ascii("----------"), 84, y + 4,
                TXT_DARK, TXT_WHITE);
        if (seen)
            AddHit(8, y, 192, 24, HIT_ROW + r);
    }
    DrawScrollArrows(208, 24, H, s->dexScroll > 0, s->dexScroll + DEX_ROWS < s->dexCount, s->pressed);
}

/* ------------------------------------------------------------------------ */
/* Drawing: save, options, PokéNav                                          */
/* ------------------------------------------------------------------------ */

static void DrawSave(const ViewState *s)
{
    DrawBox(BOX_MESSAGE, 0, 40, 30, 8);
    DrawStr(&sNormal, s->text, 18, 52, TXT_WHITE, TXT_DARK);
    if (s->saveStep == SAVE_DONE)
    {
        DrawLabelButton(64, 136, 14, 5, Ascii(CTR_TEXT("OK", "VALE")), s->pressed == HIT_OK, TRUE, HIT_OK);
        return;
    }
    DrawLabelButton(8, 136, 13, 6, gText_Yes, s->pressed == HIT_YES, s->canSave, HIT_YES);
    DrawLabelButton(128, 136, 13, 6, gText_No, s->pressed == HIT_NO, TRUE, HIT_NO);
}

static const u8 *OptionValue(int row, u8 value)
{
    static u8 frame[16];

    switch (row)
    {
    case 0: return value == 0 ? gText_TextSpeedSlow : value == 1 ? gText_TextSpeedMid : gText_TextSpeedFast;
    case 1: return value ? gText_BattleSceneOff : gText_BattleSceneOn;
    case 2: return value ? gText_BattleStyleSet : gText_BattleStyleShift;
    case 3: return value ? gText_SoundStereo : gText_SoundMono;
    case 4: return value == 0 ? gText_ButtonTypeNormal : value == 1 ? gText_ButtonTypeLR : gText_ButtonTypeLEqualsA;
    case OPT_VOXEL: return value ? gText_BattleSceneOn : gText_BattleSceneOff;
    case OPT_VOXEL_PITCH: return Number(value, 2, STR_CONV_MODE_LEFT_ALIGN);
    case OPT_VOXEL_ZOOM:
        StringCopy(frame, Number(value, 3, STR_CONV_MODE_LEFT_ALIGN));
        frame[StringLength(frame) + 1] = EOS;
        frame[StringLength(frame)] = CHAR_PERCENT;
        return frame;
    default:
        StringCopy(frame, gText_FrameType);
        StringAppend(frame, Number(value + 1, 2, STR_CONV_MODE_LEFT_ALIGN));
        return frame;
    }
}

/* A window frame as the game draws its message boxes, from its 3x3 tiles. */
static void DrawWindowFrame(u8 type, int x, int y, int wt, int ht)
{
    const struct TilesPal *frame = GetWindowFrameTilesPal(type);
    const u8 *tiles = frame ? Port_ResolveAssetPointer(frame->tiles) : NULL;
    const u16 *raw = frame ? Port_ResolveAssetPointer(frame->pal) : NULL;
    Pal pal;

    if (!tiles || !raw)
        return;
    ToPals(&pal, raw, 1);
    FillRect(x, y, wt * 8, ht * 8, TXT_WHITE);
    for (int ty = -1; ty <= ht; ++ty)
        for (int tx = -1; tx <= wt; ++tx)
        {
            int row = ty < 0 ? 0 : ty == ht ? 2 : 1, col = tx < 0 ? 0 : tx == wt ? 2 : 1;
            if (row == 1 && col == 1)
                continue;
            DrawTile(tiles + (row * 3 + col) * 32, x + tx * 8, y + ty * 8, pal.c, FALSE, FALSE);
        }
}

static void DrawOptions(const ViewState *s)
{
    static const u8 left[] = {CHAR_LEFT_ARROW, EOS}, right[] = {CHAR_RIGHT_ARROW, EOS};
    const u8 *names[OPTION_ROWS] = {gText_TextSpeed, gText_BattleScene, gText_BattleStyle, gText_Sound,
                                    gText_ButtonMode, gText_Frame, Ascii("VOXEL 3D"), Ascii(CTR_TEXT("3D ANGLE", "ANGULO 3D")),
                                    Ascii(CTR_TEXT("3D ZOOM", "ZOOM 3D"))};
    /* The frame stays last, above its preview. */
    static const u8 order[OPTION_ROWS] = {OPT_TEXT_SPEED, OPT_BATTLE_SCENE, OPT_BATTLE_STYLE, OPT_SOUND,
                                          OPT_BUTTON_MODE, OPT_VOXEL, OPT_VOXEL_PITCH, OPT_VOXEL_ZOOM,
                                          OPT_FRAME};
    /* The camera rows only mean something with the voxel overworld on; with
     * them there is no room left for the frame's preview. */
    bool8 camera = OPTION_SHOWN > OPT_VOXEL && s->options[OPT_VOXEL];
    const int pitch = OPTION_SHOWN > 6 ? 26 : 30;

    for (int slot = 0, row = 0; row < OPTION_ROWS; ++row)
    {
        int i = order[row], y;
        if (i >= OPTION_SHOWN || ((i == OPT_VOXEL_PITCH || i == OPT_VOXEL_ZOOM) && !camera))
            continue;
        y = 4 + slot++ * pitch;
        bool8 on = s->pressed == HIT_OPTION + i || s->pressed == HIT_OPTION + HIT_OPTION_BACK + i;

        DrawBoxEx(BOX_MENU, 0, y, 30, 3, on);
        DrawStr(&sSmall, names[i], 10, y + 6, LABEL_FG(on), LABEL_SH(on));
        DrawStr(&sSmall, left, 112, y + 6, LABEL_FG(on), LABEL_SH(on));
        DrawStrCentered(&sSmall, OptionValue(i, s->options[i]), 170, y + 6, on ? TXT_WHITE : TXT_RED,
                        on ? TXT_DARK : TXT_LRED);
        DrawStr(&sSmall, right, 222, y + 6, LABEL_FG(on), LABEL_SH(on));
        AddHit(0, y, 136, 24, HIT_OPTION + HIT_OPTION_BACK + i);
        AddHit(136, y, 104, 24, HIT_OPTION + i);
    }
    /* What the chosen frame looks like. */
    if (camera)
        return;
    DrawWindowFrame(s->options[5], 24, 196, 24, 3);
    DrawStrCentered(&sNormal, OptionValue(5, s->options[5]), CW / 2, 200, TXT_DARK, TXT_LIGHT);
}

/* ------------------------------------------------------------------------ */
/* Drawing: battle                                                          */
/* ------------------------------------------------------------------------ */

#define HEADER_H 56

static void DrawTypeIcon(u8 type, int x, int y)
{
    static const u8 palettes[NUMBER_OF_MON_TYPES] = {
        [TYPE_NORMAL] = 0, [TYPE_FIGHTING] = 0, [TYPE_FLYING] = 1, [TYPE_POISON] = 1,
        [TYPE_GROUND] = 0, [TYPE_ROCK] = 0, [TYPE_BUG] = 2, [TYPE_GHOST] = 1,
        [TYPE_STEEL] = 0, [TYPE_MYSTERY] = 2, [TYPE_FIRE] = 0, [TYPE_WATER] = 1,
        [TYPE_GRASS] = 2, [TYPE_ELECTRIC] = 0, [TYPE_PSYCHIC] = 1, [TYPE_ICE] = 1,
        [TYPE_DRAGON] = 2, [TYPE_DARK] = 0,
    }; /* pokemon_summary_screen.c's sMoveTypeToOamPaletteNum, minus 13 */

    if (sRes.typeTiles && type < NUMBER_OF_MON_TYPES)
        DrawSprite(sRes.typeTiles + type * 8 * 32, 4, 2, x, y, sRes.typePal[palettes[type]].c);
}

static void DrawBattlerPanel(const BattlerView *v, int x, int y, bool8 compact)
{
    const Pal *pal = &sRes.partyPal[4];

    if (!v->present)
        return;
    if (compact)
    {
        DrawStr(&sSmall, v->nick, x, y, TXT_DARK, TXT_LIGHT);
        DrawHpBar(x + 72, y + 5, 48, v->hp, v->maxHp, pal);
        DrawStatusIcon(v->ailment, x + 124, y + 3);
        return;
    }
    AddMonIcon(v->iconSpecies, v->deoxys, x, y - 4, v->hp == 0);
    DrawStr(&sSmall, v->nick, x + 34, y, TXT_DARK, TXT_LIGHT);
    {
        u8 text[12];
        StringCopy(text, gText_LevelSymbol);
        StringAppend(text, Number(v->level, 3, STR_CONV_MODE_LEFT_ALIGN));
        DrawStrRight(&sSmall, text, x + 142, y, TXT_DARK, TXT_LIGHT);
    }
    DrawHpBar(x + 34, y + 16, 96, v->hp, v->maxHp, pal);
    DrawStatusIcon(v->ailment, x + 34, y + 23);
    /* The game shows exact HP for the player's side only. */
    if (v->side == B_SIDE_PLAYER)
    {
        u8 text[12];
        StringCopy(text, Number(v->hp, 3, STR_CONV_MODE_RIGHT_ALIGN));
        StringAppend(text, gText_Slash);
        StringAppend(text, Number(v->maxHp, 3, STR_CONV_MODE_RIGHT_ALIGN));
        DrawStrRight(&sSmall, text, x + 130, y + 21, TXT_DARK, TXT_LIGHT);
    }
}

static void DrawBattleHeader(const ViewState *s)
{
    DrawBox(BOX_MENU, 0, 0, 20, HEADER_H / 8);
    DrawBox(BOX_MENU, 160, 0, 20, HEADER_H / 8);
    if (s->isDouble)
    {
        DrawBattlerPanel(&s->battlers[0], 10, 8, TRUE);
        DrawBattlerPanel(&s->battlers[2], 10, 28, TRUE);
        DrawBattlerPanel(&s->battlers[1], 170, 8, TRUE);
        DrawBattlerPanel(&s->battlers[3], 170, 28, TRUE);
    }
    else
    {
        DrawBattlerPanel(&s->battlers[0], 10, 12, FALSE);
        DrawBattlerPanel(&s->battlers[1], 170, 12, FALSE);
    }
}

static void DrawBattleActions(const ViewState *s)
{
    static const char *const safari[4] = {CTR_TEXT("BALL", "BALL"), CTR_TEXT("POK*BLOCK", "POK*CUBO"), CTR_TEXT("GO NEAR", "ACERCARSE"), CTR_TEXT("RUN", "HUIR")};
    bool8 on[4];

    for (int i = 0; i < 4; ++i)
        on[i] = s->cursor == i || s->pressed == HIT_ACTION + i;

    /* FIGHT: the move types it leads to. */
    DrawButton(16, 60, 36, 10, on[0], HIT_ACTION + 0);
    if (s->safari)
    {
        DrawStrCentered(&sNormal, Ascii(safari[0]), 160, 90, LABEL_FG(on[0]), LABEL_SH(on[0]));
    }
    else
    {
        int count = 0;
        DrawStrCentered(&sNormal, Ascii(CTR_TEXT("FIGHT", "LUCHA")), 160, 76, LABEL_FG(on[0]), LABEL_SH(on[0]));
        for (int i = 0; i < MAX_MON_MOVES; ++i)
            if (s->moves4.moves[i] != MOVE_NONE)
                ++count;
        for (int i = 0, x = 160 - (count * 40 - 8) / 2; i < MAX_MON_MOVES; ++i)
            if (s->moves4.moves[i] != MOVE_NONE)
            {
                DrawTypeIcon(gBattleMoves[s->moves4.moves[i]].type, x, 104);
                x += 40;
            }
    }

    /* BAG, POKéMON, RUN. */
    DrawButton(16, 148, 11, 11, on[1], HIT_ACTION + 1);
    DrawButton(116, 148, 11, 11, on[2], HIT_ACTION + 2);
    DrawButton(216, 148, 11, 11, on[3], HIT_ACTION + 3);
    if (s->safari)
    {
        DrawStrCentered(&sNormal, Ascii(safari[1]), 60, 184, LABEL_FG(on[1]), LABEL_SH(on[1]));
        DrawStrCentered(&sNormal, Ascii(safari[2]), 160, 184, LABEL_FG(on[2]), LABEL_SH(on[2]));
        DrawStrCentered(&sNormal, Ascii(safari[3]), 260, 184, LABEL_FG(on[3]), LABEL_SH(on[3]));
        return;
    }
    if (sRes.bagTiles[s->gender])
        DrawSprite(sRes.bagTiles[s->gender], 8, 8, 28, 150, sRes.bagPal.c);
    DrawStrCentered(&sNormal, Ascii(CTR_TEXT("BAG", "MOCHILA")), 60, 212, LABEL_FG(on[1]), LABEL_SH(on[1]));
    if (s->battlers[0].present)
        AddMonIcon(s->battlers[0].iconSpecies, s->battlers[0].deoxys, 144, 168, FALSE);
    else if (s->party[0].species)
        AddMonIcon(s->party[0].iconSpecies, s->party[0].deoxys, 144, 168, FALSE);
    DrawStrCentered(&sNormal, Ascii("POK*MON"), 160, 212, LABEL_FG(on[2]), LABEL_SH(on[2]));
    DrawItemIcon(ITEM_ESCAPE_ROPE, 248, 176);
    DrawStrCentered(&sNormal, Ascii(CTR_TEXT("RUN", "HUIR")), 260, 212, LABEL_FG(on[3]), LABEL_SH(on[3]));
}

static void DrawBattleMoves(const ViewState *s)
{
    for (int i = 0; i < MAX_MON_MOVES; ++i)
    {
        int x = i & 1 ? 164 : 12, y = i & 2 ? 140 : 64;
        u16 move = s->moves4.moves[i];
        bool8 on = move != MOVE_NONE && (s->cursor == i || s->pressed == HIT_MOVE + i);
        const struct BattleMove *data = &gBattleMoves[move];
        u8 text[20];

        DrawBoxEx(BOX_MENU, x, y, 18, 9, on);
        if (move == MOVE_NONE)
        {
            DrawStrCentered(&sNormal, Ascii("-"), x + 72, y + 28, TXT_LIGHT, TXT_WHITE);
            continue;
        }
        AddHit(x, y, 144, 72, HIT_MOVE + i);
        DrawStr(&sNormal, gMoveNames[move], x + 14, y + 9, s->moves4.currentPp[i] ? LABEL_FG(on) : TXT_RED,
                s->moves4.currentPp[i] ? LABEL_SH(on) : TXT_LRED);
        DrawTypeIcon(data->type, x + 14, y + 30);
        StringCopy(text, gText_MoveInterfacePP);
        StringAppend(text, Ascii(" "));
        StringAppend(text, Number(s->moves4.currentPp[i], 2, STR_CONV_MODE_RIGHT_ALIGN));
        StringAppend(text, gText_Slash);
        StringAppend(text, Number(s->moves4.maxPp[i], 2, STR_CONV_MODE_RIGHT_ALIGN));
        DrawStrRight(&sSmall, text, x + 130, y + 32, LABEL_FG(on), LABEL_SH(on));
        {
            int tx = DrawStr(&sSmall, Ascii(CTR_TEXT("POW ", "POT. ")), x + 14, y + 50, LABEL_FG(on), LABEL_SH(on));
            tx = DrawStr(&sSmall, data->power > 1 ? Number(data->power, 3, STR_CONV_MODE_LEFT_ALIGN) : Ascii("---"),
                         tx, y + 50, LABEL_FG(on), LABEL_SH(on));
            tx = DrawStr(&sSmall, Ascii(CTR_TEXT("   ACC ", "   PREC. ")), tx, y + 50, LABEL_FG(on), LABEL_SH(on));
            DrawStr(&sSmall, data->accuracy ? Number(data->accuracy, 3, STR_CONV_MODE_LEFT_ALIGN) : Ascii("---"),
                    tx, y + 50, LABEL_FG(on), LABEL_SH(on));
        }
    }
    DrawLabelButton(12, 216, 37, 3, gText_Cancel2, s->pressed == HIT_CANCEL || s->cursor == MAX_MON_MOVES, TRUE,
                    HIT_CANCEL);
}

static void DrawBattleTarget(const ViewState *s)
{
    static const u8 left[] = {CHAR_LEFT_ARROW, EOS}, right[] = {CHAR_RIGHT_ARROW, EOS};

    DrawLabelButton(12, 64, 18, 9, left, s->pressed == HIT_TARGET_LEFT, TRUE, HIT_TARGET_LEFT);
    DrawLabelButton(164, 64, 18, 9, right, s->pressed == HIT_TARGET_RIGHT, TRUE, HIT_TARGET_RIGHT);
    DrawLabelButton(12, 144, 18, 9, Ascii(CTR_TEXT("OK", "VALE")), s->pressed == HIT_TARGET_OK, TRUE, HIT_TARGET_OK);
    DrawLabelButton(164, 144, 18, 9, gText_Cancel2, s->pressed == HIT_CANCEL, TRUE, HIT_CANCEL);
}

static void DrawBattleInfo(const ViewState *s)
{
    /* The party, so a switch can be planned while the turn plays out. The
     * message is not repeated here: the top screen shows it. */
    DrawBox(BOX_MENU, 0, 64, 40, 22);
    for (int i = 0; i < PARTY_SIZE; ++i)
    {
        const MonView *m = &s->party[i];
        int x = 12 + (i % 3) * 100, y = 100 + (i / 3) * 72;

        if (!m->species)
            continue;
        AddMonIcon(m->iconSpecies, m->deoxys, x, y, m->fainted);
        DrawStr(&sSmall, m->nick, x + 34, y + 2, TXT_DARK, TXT_LIGHT);
        if (!m->isEgg)
        {
            DrawHpBar(x + 34, y + 18, 48, m->hp, m->maxHp, &sRes.partyPal[4]);
            DrawStatusIcon(m->ailment, x + 34, y + 24);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Redraw and present                                                       */
/* ------------------------------------------------------------------------ */

static void BuildBackgroundCaches(void)
{
    sOX = 0;
    /* The 240px view with the column beside it, and the whole screen. */
    sDst = sCache[CACHE_MENU];
    DrawPartyBackground(CW / 8, H / 8);
    DrawColumnBackground();
    sDst = sCache[CACHE_WIDE];
    DrawPartyBackground(W / 8, H / 8);
    BuildMapCache();
    sDst = sCanvas;
}

static void Render(const ViewState *s)
{
    ResolveFonts();
    sHitCount = 0;
    sAnimCount = 0;
    sDst = sCanvas;
    sOX = 0;

    if (s->mode == MODE_OFF)
    {
        memset(sCanvas, 0, sizeof(sCanvas));
    }
    else if (s->mode >= MODE_BATTLE_INFO)
    {
        CopyCache(CACHE_WIDE);
        DrawBattleHeader(s);
        if (s->mode == MODE_BATTLE_ACTION)
            DrawBattleActions(s);
        else if (s->mode == MODE_BATTLE_MOVE)
            DrawBattleMoves(s);
        else if (s->mode == MODE_BATTLE_TARGET)
            DrawBattleTarget(s);
        else
            DrawBattleInfo(s);
    }
    else
    {
        /* In battle the bag and the party menu have the whole screen. */
        bool8 column = !s->inBattle;

        if (s->screen == SCR_MAP && column)
            CopyCache(CACHE_MAP);
        else if (s->screen == SCR_CARD && column)
        {
            BuildCardCache(s->stars > 4 ? 4 : s->stars, s->gender);
            CopyCache(sCache[CACHE_CARD] ? CACHE_CARD : CACHE_MENU);
        }
        else
            CopyCache(column ? CACHE_MENU : CACHE_WIDE);
        sOX = column ? 0 : (W - CW) / 2;
        switch (s->screen)
        {
        case SCR_MAP: DrawRegionMap(s); break;
        case SCR_POKEMON:
            if (s->summary >= 0)
                DrawSummary(s);
            else
                DrawParty(s);
            break;
        case SCR_BAG: DrawBag(s); break;
        case SCR_CARD: DrawTrainerCard(s); break;
        case SCR_POKEDEX: DrawDex(s); break;
        case SCR_SAVE: DrawSave(s); break;
        case SCR_OPTION: DrawOptions(s); break;
        }
        sOX = 0;
        if (column)
            DrawColumn(s);
    }
    DrawAnimIcons();
    /* Left of the column is the PokéNav's while the compositor draws it. */
    CtrBottom_Blit(sCanvas, CtrVideo_BottomInUse() ? CW : 0, W);
}

/* ------------------------------------------------------------------------ */
/* Pressing buttons for the player, in the hidden menus and in battle       */
/* ------------------------------------------------------------------------ */

enum
{
    PLAN_NONE,
    PLAN_PRESS,      /* one press of `keys` */
    PLAN_KEYS,       /* `keys`, then A */
    PLAN_START,      /* open start menu entry `target` */
    PLAN_PARTY,      /* walk gPartyMenu.slotId, then A */
    PLAN_POCKET,     /* switch the bag's pocket */
    PLAN_BAG_ITEM,   /* walk the bag list, then A */
    PLAN_MENU,       /* walk a game menu's cursor, then A */
};

typedef struct
{
    u8 kind, steps, wait, pocket, cols, tries, maxWait;
    bool8 release;
    s16 target;
    u16 keys;
} Plan;

static Plan sPlan;
/* What follows the current plan: opening the party menu on a mon, the bag on
 * an item... Each step waits for the game to reach the screen it needs. */
static Plan sQueue[2];
static u8 sQueued;
static u16 sInjected;

static Plan MakePlan(u8 kind, s16 target)
{
    Plan p;

    memset(&p, 0, sizeof(p));
    p.kind = kind;
    p.target = target;
    p.pocket = gBagPosition.pocket;
    p.maxWait = 45;
    return p;
}

static void StartPlan(u8 kind, s16 target)
{
    sPlan = MakePlan(kind, target);
    sQueued = 0;
}

static void QueuePlan(Plan p)
{
    /* The screen it waits for is behind a fade and a menu setup. */
    p.maxWait = 150;
    if (sQueued < ARRAY_COUNT(sQueue))
        sQueue[sQueued++] = p;
}

static void Press(u16 keys)
{
    StartPlan(PLAN_PRESS, 0);
    sPlan.keys = keys;
}

static void FinishPlan(void)
{
    if (sQueued)
    {
        sPlan = sQueue[0];
        sQueue[0] = sQueue[1];
        --sQueued;
    }
    else
        sPlan.kind = PLAN_NONE;
}

static void CancelPlan(void)
{
    if (sPlan.kind == PLAN_START)
        CtrStartMenu_Request(START_NONE);
    sPlan.kind = PLAN_NONE;
    sQueued = 0;
}

/* Where the game's cursor is, or FALSE while that menu is not taking input. */
static bool8 PlanCursor(s16 *cursor)
{
    const u8 *names[MAX_MENU_ITEMS];
    u8 cols;

    switch (sPlan.kind)
    {
    case PLAN_PARTY:
        if (!PartyMenuReady()) return FALSE;
        *cursor = gPartyMenu.slotId;
        return TRUE;
    case PLAN_POCKET:
        if (!BagMenuReady()) return FALSE;
        *cursor = gBagPosition.pocket;
        return TRUE;
    case PLAN_BAG_ITEM:
        if (!BagMenuReady() || gBagPosition.pocket != sPlan.pocket) return FALSE;
        *cursor = BagMenuIndex();
        return TRUE;
    case PLAN_MENU:
        if (gPaletteFade.active) return FALSE;
        if (gMain.callback2 == CB2_BagMenuRun ? !CtrBagMenu_GetActions(names, MAX_MENU_ITEMS, &cols)
                                              : !CtrPartyMenu_GetActions(names, MAX_MENU_ITEMS))
            return FALSE;
        *cursor = Menu_GetCursorPos();
        return TRUE;
    }
    return FALSE;
}

static u16 PlanStep(s16 cur, s16 target)
{
    switch (sPlan.kind)
    {
    case PLAN_PARTY:
        if (cur >= PARTY_SIZE)
            return DPAD_DOWN;                     /* Cancel/Confirm wrap to 0 */
        if (gPartyMenu.layout == PARTY_LAYOUT_SINGLE)
        {
            if (target == 0) return DPAD_LEFT;
            if (cur == 0) return DPAD_RIGHT;
        }
        else if ((cur < 2) != (target < 2))
        {
            return target < 2 ? DPAD_LEFT : DPAD_RIGHT;
        }
        return target > cur ? DPAD_DOWN : DPAD_UP;
    case PLAN_POCKET:
        return target > cur ? DPAD_RIGHT : DPAD_LEFT;
    case PLAN_BAG_ITEM:
        return target > cur ? DPAD_DOWN : DPAD_UP;
    case PLAN_MENU:
        if (sPlan.cols == 2 && (cur & 1) != (target & 1))
            return target & 1 ? DPAD_RIGHT : DPAD_LEFT;
        return target > cur ? DPAD_DOWN : DPAD_UP;
    }
    return 0;
}

/* START with a request pending opens that entry (see start_menu.c). */
static void RunStartPlan(void)
{
    if (sPlan.tries && !CtrStartMenu_Pending())
    {
        FinishPlan();                             /* served */
        return;
    }
    if (++sPlan.wait < 8 && sPlan.tries)
        return;
    /* START only registers with the player free to move; try a few times. */
    if (sPlan.tries >= 6 || gMain.callback2 != CB2_Overworld || CtrStartMenu_Busy())
    {
        if (sPlan.tries >= 6 || ++sPlan.steps > 90)
            CancelPlan();
        return;
    }
    CtrStartMenu_Request(sPlan.target);
    sInjected = START_BUTTON;
    sPlan.release = TRUE;
    sPlan.wait = 0;
    ++sPlan.tries;
}

static void RunPlan(void)
{
    s16 cur;

    sInjected = 0;
    if (sPlan.kind == PLAN_NONE)
        return;
    /* The player's own buttons always win. */
    if (CtrInput_Get()->held & CTR_KEY_GAME)
    {
        CancelPlan();
        return;
    }
    /* A press only registers as new after a frame with the key up. */
    if (sPlan.release)
    {
        sPlan.release = FALSE;
        return;
    }
    switch (sPlan.kind)
    {
    case PLAN_PRESS:
        sInjected = sPlan.keys;
        FinishPlan();
        sPlan.release = TRUE;
        return;
    case PLAN_KEYS:
        sInjected = sPlan.keys;
        sPlan = MakePlan(PLAN_PRESS, 0);
        sPlan.keys = A_BUTTON;
        sPlan.release = TRUE;
        return;
    case PLAN_START:
        RunStartPlan();
        return;
    }
    if (!PlanCursor(&cur))
    {
        if (++sPlan.wait > sPlan.maxWait)
            CancelPlan();
        return;
    }
    if (++sPlan.steps > 24)
    {
        CancelPlan();
        return;
    }
    if (cur == sPlan.target)
    {
        if (sPlan.kind != PLAN_POCKET)
            sInjected = A_BUTTON;
        FinishPlan();
        /* The next step must not see this press as its own. */
        sPlan.release = TRUE;
        return;
    }
    sInjected = PlanStep(cur, sPlan.target);
    sPlan.release = TRUE;
}

uint16_t CtrBottom_InjectedKeys(void)
{
    return sInjected;
}

/* ------------------------------------------------------------------------ */
/* The PokéNav by touch                                                     */
/* ------------------------------------------------------------------------ */

/*
 * The PokéNav runs as it is, drawn left of the column by the compositor; a
 * tap on one of its screens becomes the buttons that screen reads, pressed
 * only while the PokéNav waits for input (CtrPokenav_Screen): an option or a
 * list entry is reached with one press of the D-pad, the cursor put next to
 * it first (NavNext), and chosen with A; a place on the map
 * is walked to, a ribbon is picked. The POKéNAV button of the column is B,
 * and any other button of it leaves the PokéNav for its own screen.
 */
enum
{
    NAV_NONE,
    NAV_PRESS,    /* keys, once */
    NAV_MENU,     /* walk the menu cursor to target */
    NAV_LIST,     /* walk the list selection to target */
    NAV_OPTION,   /* walk Match Call's options cursor to target */
    NAV_PARTY,    /* walk the condition screen's mon to target */
    NAV_MAP,      /* dx, dy presses on the map */
    NAV_RIBBON,   /* walk the ribbon cursor to target */
    NAV_MARK,     /* walk the markings menu's cursor to target */
    NAV_LEAVE,    /* B until the PokéNav closes */
};

static struct
{
    u8 kind, steps, wait;
    bool8 release;
    s16 target, dx, dy;
    u16 keys, finish;
} sNav;

/* Condition graph: the party's balls down the right edge, CANCEL below. */
#define NAV_BALL_X 212
#define NAV_BALL_TOP 8
#define NAV_BALL_STEP 20
/* Ribbon summary: 16x16 cells from here, RIBBONS_PER_ROW to a row. */
#define NAV_RIBBON_X 88
#define NAV_RIBBON_Y 32
#define NAV_RIBBONS_PER_ROW 9
/* Match Call's options: rows of its info box, left of the list. */
#define NAV_OPTION_Y 72
#define NAV_OPTION_W 88

static void NavStart(u8 kind, s16 target, u16 finish)
{
    memset(&sNav, 0, sizeof(sNav));
    sNav.kind = kind;
    sNav.target = target;
    sNav.finish = finish;
}

static void NavPress(u16 keys)
{
    NavStart(NAV_PRESS, 0, 0);
    sNav.keys = keys;
}

static struct PokenavMonList *NavMonList(void)
{
    return GetSubstructPtr(POKENAV_SUBSTRUCT_MON_LIST);
}

/*
 * The press that takes a cursor from cursor to target in one step: the
 * cursor is put right next to the target first (set), so the game's own
 * move - its sound, the option sliding out - happens once, for the target.
 */
static u16 NavNext(int cursor, int target, void (*set)(int))
{
    if (target > cursor + 1) set(target - 1);
    else if (target < cursor - 1) set(target + 1);
    return target > cursor ? DPAD_DOWN : DPAD_UP;
}

static void NavSetMenu(int cursor) { CtrPokenavMenu_SetCursor(cursor); }
static void NavSetList(int cursor) { CtrPokenavList_SetSelected((u16)cursor); }
static void NavSetOption(int cursor) { CtrPokenavMatchCall_SetOption((u16)cursor); }
static void NavSetMark(int cursor) { CtrMonMarkings_SetCursor((s8)cursor); }

/* One step of the plan, or 0 while the cursor is not known. */
static u16 NavStep(bool8 *done)
{
    int cursor = 0, count;
    u16 top, selected, shown, total, option, options, normal, gift, giftStart;
    u8 x, y, width;
    bool8 zoomed, moving, expanded;
    s16 cx, cy;
    struct PokenavMonList *mons;

    *done = FALSE;
    switch (sNav.kind)
    {
    case NAV_PRESS:
        *done = TRUE;
        return sNav.keys;
    case NAV_MENU:
        count = CtrPokenavMenu_Options(&cursor);
        if (sNav.target >= count) break;
        if (cursor == sNav.target) { *done = TRUE; return sNav.finish; }
        return NavNext(cursor, sNav.target, NavSetMenu);
    case NAV_LIST:
        if (!CtrPokenavList_View(&x, &y, &width, &top, &selected, &shown, &total) || sNav.target >= total) break;
        if (selected == sNav.target) { *done = TRUE; return sNav.finish; }
        return NavNext(selected, sNav.target, NavSetList);
    case NAV_OPTION:
        if (CtrPokenavMatchCall_Input(&option, &options) != 1 || sNav.target >= options) break;
        if (option == sNav.target) { *done = TRUE; return sNav.finish; }
        return NavNext(option, sNav.target, NavSetOption);
    case NAV_PARTY:
        if (!(mons = NavMonList()) || sNav.target >= mons->listCount) break;
        if (mons->currIndex == sNav.target) { *done = TRUE; return sNav.finish; }
        return sNav.target > mons->currIndex ? DPAD_DOWN : DPAD_UP;
    case NAV_MAP:
        if (!CtrRegionMap_Cursor(&cx, &cy, &zoomed, &moving) || moving) return 0;
        if (sNav.dx > 0) { --sNav.dx; return DPAD_RIGHT; }
        if (sNav.dx < 0) { ++sNav.dx; return DPAD_LEFT; }
        if (sNav.dy > 0) { --sNav.dy; return DPAD_DOWN; }
        if (sNav.dy < 0) { ++sNav.dy; return DPAD_UP; }
        *done = TRUE;
        return sNav.finish;
    case NAV_RIBBON:
        if (!CtrPokenavRibbons_Summary(&selected, &normal, &gift, &giftStart, &expanded)) break;
        if (!expanded) return A_BUTTON;
        if (selected == sNav.target) { *done = TRUE; return 0; }
        if (selected / NAV_RIBBONS_PER_ROW != sNav.target / NAV_RIBBONS_PER_ROW)
            return sNav.target > selected ? DPAD_DOWN : DPAD_UP;
        return sNav.target > selected ? DPAD_RIGHT : DPAD_LEFT;
    case NAV_MARK:
    {
        s8 mark;
        s16 mx, my;

        if (!CtrPokenavCondition_Marking() || !CtrMonMarkings_Menu(&mark, &mx, &my)) break;
        if (mark == sNav.target) { *done = TRUE; return sNav.finish; }
        return NavNext(mark, sNav.target, NavSetMark);
    }
    case NAV_LEAVE:
        return B_BUTTON;
    }
    /* What the plan was for is gone. */
    sNav.kind = NAV_NONE;
    return 0;
}

static void RunNav(u8 mode)
{
    bool8 ready, done;
    u16 keys;

    if (sNav.kind == NAV_NONE)
        return;
    if (mode != MODE_POKENAV)
    {
        sNav.kind = NAV_NONE;
        return;
    }
    if (CtrInput_Get()->held & CTR_KEY_GAME)
    {
        sNav.kind = NAV_NONE;
        return;
    }
    /* A press only registers as new after a frame with the key up. */
    if (sNav.release)
    {
        sNav.release = FALSE;
        return;
    }
    /* A single press also answers what waits inside a task: a call's text. */
    CtrPokenav_Screen(&ready);
    keys = ready || sNav.kind == NAV_PRESS ? NavStep(&done) : 0;
    if (!keys)
    {
        /* Waiting for the PokéNav to take input, or a move to end. */
        if (++sNav.wait > 240)
            sNav.kind = NAV_NONE;
        return;
    }
    if (done && sNav.kind != NAV_LEAVE)
        sNav.kind = NAV_NONE;
    if (++sNav.steps > 64)
        sNav.kind = NAV_NONE;
    sInjected = keys;
    sNav.release = TRUE;
    sNav.wait = 0;
}

/* A tap at (x, y) of the PokéNav's picture. */
static void NavTap(int x, int y)
{
    bool8 ready, zoomed, moving, expanded;
    u32 screen = CtrPokenav_Screen(&ready);
    int yStart, deltaY, cursor, count, row;
    u16 top, selected, shown, total, option, options, normal, gift, giftStart;
    u8 lx, ly, width;
    s16 cx, cy;
    struct PokenavMonList *mons;

    if (y < 0 || y >= 160)
        return;
    switch (screen)
    {
    case POKENAV_MAIN_MENU:
    case POKENAV_MAIN_MENU_CURSOR_ON_MAP:
    case POKENAV_CONDITION_MENU:
    case POKENAV_CONDITION_SEARCH_MENU:
    case POKENAV_MAIN_MENU_CURSOR_ON_MATCH_CALL:
    case POKENAV_MAIN_MENU_CURSOR_ON_RIBBONS:
        count = CtrPokenavMenu_Options(&cursor);
        CtrPokenavMenu_Rows(&yStart, &deltaY);
        row = (y - yStart + deltaY / 2 + deltaY) / deltaY - 1;
        if (x >= 112 && row >= 0 && row < count)
            NavStart(NAV_MENU, row, A_BUTTON);
        else if (x < 88 && y >= 16 && y < 40)
            NavPress(B_BUTTON);         /* the header: back */
        else
            NavPress(A_BUTTON);         /* a message waiting (no ribbons yet) */
        break;
    case POKENAV_REGION_MAP:
        if (y >= 144)
        {
            /* The help bar: "A ZOOM", then "B CANCEL". */
            if (x < 40) NavPress(A_BUTTON);
            else if (x < 112) NavPress(B_BUTTON);
            break;
        }
        if (!CtrRegionMap_Cursor(&cx, &cy, &zoomed, &moving))
            break;
        {
            int step = zoomed ? 16 : 8;
            int dx = x - cx, dy = y - cy;

            NavStart(NAV_MAP, 0, 0);
            sNav.dx = (dx + (dx >= 0 ? step / 2 : -step / 2)) / step;
            sNav.dy = (dy + (dy >= 0 ? step / 2 : -step / 2)) / step;
            /* On the cursor: the place is chosen, zoom in or out on it. */
            if (!sNav.dx && !sNav.dy)
                sNav.finish = A_BUTTON;
        }
        break;
    case POKENAV_CONDITION_GRAPH_PARTY:
    case POKENAV_CONDITION_GRAPH_SEARCH:
        mons = NavMonList();
        {
            s8 mark;
            s16 mx, my;

            /* The markings menu, open over the graph: a row, or out of it. */
            if (CtrPokenavCondition_Marking() && CtrMonMarkings_Menu(&mark, &mx, &my))
            {
                row = (y - my - 8) / 16;
                if (x >= mx && x < mx + 64 && y >= my + 8 && row < 6)
                    NavStart(NAV_MARK, row, A_BUTTON);
                else
                    NavPress(B_BUTTON);
                break;
            }
        }
        if (x < 88 && y < 40)
            NavPress(B_BUTTON);
        else if (screen == POKENAV_CONDITION_GRAPH_PARTY && x >= NAV_BALL_X && mons)
        {
            row = (y - NAV_BALL_TOP + NAV_BALL_STEP / 2 + NAV_BALL_STEP) / NAV_BALL_STEP - 1;
            if (row == PARTY_SIZE)
                NavStart(NAV_PARTY, mons->listCount - 1, A_BUTTON);   /* CANCEL */
            else if (row >= 0 && row < mons->listCount - 1)
                NavStart(NAV_PARTY, row, 0);
        }
        else if (x < 80 && y >= 56)
            NavPress(y < 100 ? DPAD_UP : DPAD_DOWN);   /* the picture: previous, next */
        else if (screen == POKENAV_CONDITION_GRAPH_SEARCH)
            NavPress(A_BUTTON);                       /* markings */
        break;
    case POKENAV_CONDITION_SEARCH_RESULTS:
    case POKENAV_MATCH_CALL:
    case POKENAV_RIBBONS_MON_LIST:
        if (screen == POKENAV_MATCH_CALL)
        {
            switch (CtrPokenavMatchCall_Input(&option, &options))
            {
            case 1:
                row = (y - NAV_OPTION_Y) / 16;
                if (x < NAV_OPTION_W && y >= NAV_OPTION_Y && row < options)
                    NavStart(NAV_OPTION, row, A_BUTTON);
                else
                    NavPress(B_BUTTON);
                return;
            case 2:
                NavPress(B_BUTTON);
                return;
            case 3:
                NavPress(A_BUTTON);
                return;
            }
        }
        if (x < 88 && y < 32)
        {
            NavPress(B_BUTTON);
            break;
        }
        if (!CtrPokenavList_View(&lx, &ly, &width, &top, &selected, &shown, &total) || x < lx || x >= lx + width)
            break;
        if (y < ly)
            NavPress(DPAD_LEFT);        /* a page up */
        else if (y >= ly + 16 * shown)
            NavPress(DPAD_RIGHT);       /* a page down */
        else if (top + (y - ly) / 16 < total)
            NavStart(NAV_LIST, top + (y - ly) / 16, A_BUTTON);
        break;
    case POKENAV_RIBBONS_SUMMARY_SCREEN:
        if (!CtrPokenavRibbons_Summary(&selected, &normal, &gift, &giftStart, &expanded))
            break;
        if (x >= NAV_RIBBON_X && x < NAV_RIBBON_X + 16 * NAV_RIBBONS_PER_ROW && y >= NAV_RIBBON_Y)
        {
            int pos = (y - NAV_RIBBON_Y) / 16 * NAV_RIBBONS_PER_ROW + (x - NAV_RIBBON_X) / 16;

            if (pos < normal || (pos >= giftStart && pos < giftStart + gift))
            {
                NavStart(NAV_RIBBON, pos, 0);
                break;
            }
        }
        if (expanded)
            NavPress(B_BUTTON);
        else if (x < 88 && y < 32)
            NavPress(B_BUTTON);
        else if (x < 80 && y >= 64)
            NavPress(y < 104 ? DPAD_UP : DPAD_DOWN);   /* the picture: previous, next */
        break;
    }
}

/* A drag across the PokéNav's picture: the next or previous page or mon. */
static void NavSwipe(int dy)
{
    bool8 ready;
    u32 screen = CtrPokenav_Screen(&ready);
    bool8 next = dy < 0;

    switch (screen)
    {
    case POKENAV_CONDITION_SEARCH_RESULTS:
    case POKENAV_MATCH_CALL:
    case POKENAV_RIBBONS_MON_LIST:
        NavPress(next ? DPAD_RIGHT : DPAD_LEFT);
        break;
    case POKENAV_CONDITION_GRAPH_PARTY:
    case POKENAV_CONDITION_GRAPH_SEARCH:
    case POKENAV_RIBBONS_SUMMARY_SCREEN:
        NavPress(next ? DPAD_DOWN : DPAD_UP);
        break;
    }
}


/* ------------------------------------------------------------------------ */
/* What a tap does                                                          */
/* ------------------------------------------------------------------------ */

static struct
{
    bool8 active, dragged;
    s16 startX, startY, lastX, lastY;
    u8 pressed;
    u16 dragScroll;
} sTouch;

/* The save, done here as start_menu.c's SaveDoSaveCallback does it. */
static void DoSave(void)
{
    u8 status;

    SaveMapView();
    IncrementGameStat(GAME_STAT_SAVED_GAME);
    if (gDifferentSaveFile == TRUE)
    {
        status = TrySavingData(SAVE_OVERWRITE_DIFFERENT_FILE);
        gDifferentSaveFile = FALSE;
    }
    else
    {
        status = TrySavingData(SAVE_NORMAL);
    }
    StringExpandPlaceholders(sSaveMessage, status == SAVE_STATUS_OK ? gText_PlayerSavedGame : gText_SaveError);
    if (status == SAVE_STATUS_OK)
        PlaySE(SE_SAVE);
    sSaveStep = SAVE_DONE;
}

static void OpenSave(void)
{
    sSaveStep = SAVE_ASK;
    StringExpandPlaceholders(sSaveMessage, gText_ConfirmSave);
}

/* A game menu entry: SUMMARY is shown here, the rest runs in the game. */
static void ChooseMenuEntry(u8 index)
{
    if (sShown.mode == MODE_PARTY_MENU && index < sShown.menuCount && sShown.menuNames[index] == gText_Summary5)
    {
        sSummary = gPartyMenu.slotId;
        Press(B_BUTTON); /* close the submenu; the summary is drawn here */
        return;
    }
    StartPlan(PLAN_MENU, index);
    sPlan.cols = sShown.menuCols;
}

static void Answer(u8 id)
{
    if (id == HIT_YES)
    {
        /* Some questions default to NO: go up to YES first. */
        StartPlan(PLAN_KEYS, 0);
        sPlan.keys = DPAD_UP;
    }
    else if (id == HIT_NO || id == HIT_CANCEL)
        Press(B_BUTTON);
    else if (id == HIT_OK || id == HIT_PANEL)
        Press(A_BUTTON);
    else if (id == HIT_UP)
        Press(DPAD_UP);
    else if (id == HIT_DOWN)
        Press(DPAD_DOWN);
}

static void ActivateSummary(u8 id)
{
    if (id == HIT_BACK)
        sSummary = -1;
    else if (id == HIT_PREV || id == HIT_NEXT)
    {
        for (int n = 0; n < PARTY_SIZE; ++n)
        {
            sSummary = (sSummary + (id == HIT_NEXT ? 1 : PARTY_SIZE - 1)) % PARTY_SIZE;
            if (GetMonData(&gPlayerParty[sSummary], MON_DATA_SPECIES) != SPECIES_NONE
             && !GetMonData(&gPlayerParty[sSummary], MON_DATA_IS_EGG))
                break;
        }
    }
}

static void ActivatePokemon(u8 id, u8 mode)
{
    if (sSummary >= 0)
    {
        ActivateSummary(id);
        return;
    }
    if (id >= HIT_SLOT && id < HIT_SLOT + PARTY_SIZE)
    {
        u8 slot = id - HIT_SLOT;

        sPartyTapped = slot;
        if (mode == MODE_PARTY_MENU)
        {
            if (PartyMenuReady())
                StartPlan(PLAN_PARTY, slot);
        }
        else if (FieldIdle() && CtrStartMenu_Available())
        {
            /* The party menu, hidden, on this mon, with its menu open. */
            BeginSession(FALSE);
            StartPlan(PLAN_START, START_POKEMON);
            QueuePlan(MakePlan(PLAN_PARTY, slot));
        }
        return;
    }
    if (mode != MODE_PARTY_MENU)
        return;
    if (id >= HIT_MENU && id < HIT_MENU + MAX_MENU_ITEMS)
        ChooseMenuEntry(id - HIT_MENU);
    else
        Answer(id);
}

static void ActivateBag(u8 id, u8 mode)
{
    if (id >= HIT_POCKET && id < HIT_POCKET + POCKETS_COUNT)
    {
        sBagPocket = id - HIT_POCKET;
        sBagScroll = 0;
        sBagTapped = -1;
        if (mode == MODE_BAG_MENU && BagMenuReady())
            StartPlan(PLAN_POCKET, sBagPocket);
    }
    else if (id >= HIT_ROW && id < HIT_ROW + BAG_ROWS)
    {
        u16 index = sBagScroll + (id - HIT_ROW);

        sBagTapped = index;
        if (mode == MODE_BAG_MENU)
        {
            if (BagMenuReady())
            {
                Plan item = MakePlan(PLAN_BAG_ITEM, index);
                item.pocket = sBagPocket;
                if (gBagPosition.pocket != sBagPocket)
                {
                    StartPlan(PLAN_POCKET, sBagPocket);
                    QueuePlan(item);
                }
                else
                {
                    sPlan = item;
                    sQueued = 0;
                }
            }
        }
        else if (FieldIdle() && CtrStartMenu_Available())
        {
            /* The bag, hidden, on this item, with its menu open. */
            Plan pocket = MakePlan(PLAN_POCKET, sBagPocket), item = MakePlan(PLAN_BAG_ITEM, index);
            item.pocket = sBagPocket;
            BeginSession(FALSE);
            StartPlan(PLAN_START, START_BAG);
            QueuePlan(pocket);
            QueuePlan(item);
        }
    }
    else if (id == HIT_UP && sShown.panel != PANEL_QUANTITY)
    {
        if (sBagScroll > 0)
            --sBagScroll;
    }
    else if (id == HIT_DOWN && sShown.panel != PANEL_QUANTITY)
        ++sBagScroll; /* clamped by the snapshot */
    else if (mode == MODE_BAG_MENU)
    {
        if (id >= HIT_MENU && id < HIT_MENU + MAX_MENU_ITEMS)
            ChooseMenuEntry(id - HIT_MENU);
        else
            Answer(id);
    }
}

static void ActivateOption(u8 id)
{
    bool8 back = id >= HIT_OPTION + HIT_OPTION_BACK;
    u8 row = (id - HIT_OPTION) % HIT_OPTION_BACK;
    static const u8 counts[OPTION_ROWS] = {3, 2, 2, 2, 3, WINDOW_FRAMES_COUNT, 2, 0, 0};
    u8 value, step = back ? counts[row] - 1 : 1;

    if (row >= OPTION_SHOWN)
        return;
    if (row == OPT_VOXEL)
    {
        CtrSettings_SetVoxel(!CtrSettings_Voxel());
        PlaySE(SE_SELECT);
        return;
    }
    if (row == OPT_VOXEL_PITCH || row == OPT_VOXEL_ZOOM)
    {
        if (!CtrSettings_Voxel())
            return;
        if (row == OPT_VOXEL_PITCH)
            CtrSettings_StepVoxelPitch(back ? -1 : 1);
        else
            CtrSettings_StepVoxelZoom(back ? -1 : 1);
        PlaySE(SE_SELECT);
        return;
    }
    switch (row)
    {
    case 0: value = gSaveBlock2Ptr->optionsTextSpeed; break;
    case 1: value = gSaveBlock2Ptr->optionsBattleSceneOff; break;
    case 2: value = gSaveBlock2Ptr->optionsBattleStyle; break;
    case 3: value = gSaveBlock2Ptr->optionsSound; break;
    case 4: value = gSaveBlock2Ptr->optionsButtonMode; break;
    default: value = gSaveBlock2Ptr->optionsWindowFrameType; break;
    }
    value = (value + step) % counts[row];
    switch (row)
    {
    case 0: gSaveBlock2Ptr->optionsTextSpeed = value; break;
    case 1: gSaveBlock2Ptr->optionsBattleSceneOff = value; break;
    case 2: gSaveBlock2Ptr->optionsBattleStyle = value; break;
    case 3: gSaveBlock2Ptr->optionsSound = value; SetPokemonCryStereo(value); break;
    case 4: gSaveBlock2Ptr->optionsButtonMode = value; break;
    default: gSaveBlock2Ptr->optionsWindowFrameType = value; break;
    }
    PlaySE(SE_SELECT);
}

static void Activate(u8 id, u8 mode)
{
    if (id == HIT_NONE)
        return;
    CtrLog_Write(CTR_LOG_INPUT, "bottom screen: tap %02x (mode %u screen %u panel %u)", id, mode, sShown.screen,
                 sShown.panel);
    if (mode >= MODE_BATTLE_INFO)
    {
        /* The controller waiting for it takes it on its next frame. */
        if (mode != MODE_BATTLE_INFO)
            sBattleTap = id;
        return;
    }

    if (id >= HIT_COLUMN && id < HIT_COLUMN + SCR_COUNT)
    {
        u8 screen = id - HIT_COLUMN;

        if (mode == MODE_POKENAV)
        {
            /* Its own button is its B; any other leaves it for that screen. */
            if (screen == SCR_POKENAV)
                NavPress(B_BUTTON);
            else
            {
                sScreen = screen;
                NavStart(NAV_LEAVE, 0, 0);
            }
            return;
        }
        /* A hidden menu is closed first, at a point where B leaves it. */
        if (mode != MODE_FIELD)
        {
            if ((mode == MODE_PARTY_MENU && PartyMenuReady() && sSummary < 0) || (mode == MODE_BAG_MENU && BagMenuReady()))
            {
                Press(B_BUTTON);
                sScreen = screen;
            }
            return;
        }
        /* The PokéNav takes the area when it opens; the top keeps the world
         * from the moment it is asked for, fade included. */
        if (screen == SCR_POKENAV)
        {
            if (FieldIdle())
            {
                StartPlan(PLAN_START, START_POKENAV);
                BeginSession(FALSE);
            }
            return;
        }
        if (screen == SCR_SAVE && sScreen != SCR_SAVE)
            OpenSave();
        if (screen == SCR_POKEMON)
            sSummary = -1;
        if (screen == SCR_POKEDEX && sScreen == SCR_POKEDEX)
            sDexDetail = 0;
        sScreen = screen;
        sPickMapsec = MAPSEC_NONE;
        return;
    }

    switch (sShown.screen)
    {
    case SCR_MAP:
        if (id == HIT_MAP)
            PickMapCell(sTouch.lastX, sTouch.lastY);
        break;
    case SCR_POKEMON:
        ActivatePokemon(id, mode);
        break;
    case SCR_BAG:
        ActivateBag(id, mode);
        break;
    case SCR_POKEDEX:
        if (id == HIT_BACK)
            sDexDetail = 0;
        else if (id >= HIT_ROW && id < HIT_ROW + DEX_ROWS)
            sDexDetail = sShown.dexNum[id - HIT_ROW];
        else if (id == HIT_UP)
            sDexScroll = sDexScroll > DEX_ROWS ? sDexScroll - DEX_ROWS : 0;
        else if (id == HIT_DOWN)
            sDexScroll += DEX_ROWS; /* clamped by the snapshot */
        break;
    case SCR_SAVE:
        if (id == HIT_YES && FieldIdle())
        {
            if (sSaveStep == SAVE_ASK && gSaveFileStatus != SAVE_STATUS_EMPTY && gSaveFileStatus != SAVE_STATUS_CORRUPT)
            {
                sSaveStep = SAVE_OVERWRITE;
                StringExpandPlaceholders(sSaveMessage, gDifferentSaveFile ? gText_DifferentSaveFile
                                                                         : gText_AlreadySavedFile);
            }
            else
                DoSave();
        }
        else if (id == HIT_NO || id == HIT_OK)
        {
            OpenSave();
            sScreen = SCR_MAP;
        }
        break;
    case SCR_OPTION:
        if (id >= HIT_OPTION && id < HIT_OPTION + 2 * HIT_OPTION_BACK)
            ActivateOption(id);
        break;
    }
}

/* Returns the id to show as pressed. */
static u8 ProcessTouch(u8 mode)
{
    const CtrInput *in = CtrInput_Get();

    if (mode != sShown.mode)
    {
        /* A touch that began on another screen does not act on this one. */
        sTouch.active = FALSE;
        return HIT_NONE;
    }
    if (in->touchDown)
    {
        sTouch.active = TRUE;
        sTouch.dragged = FALSE;
        sTouch.startX = sTouch.lastX = in->touchX;
        sTouch.startY = sTouch.lastY = in->touchY;
        sTouch.pressed = HitTest(in->touchX, in->touchY);
        sTouch.dragScroll = sShown.screen == SCR_POKEDEX ? sDexScroll : sBagScroll;
    }
    else if (in->touchActive && sTouch.active)
    {
        int dy = in->touchY - sTouch.startY, dx = in->touchX - sTouch.startX;

        sTouch.lastX = in->touchX;
        sTouch.lastY = in->touchY;
        if (!sTouch.dragged && (dy > 8 || dy < -8 || dx > 8 || dx < -8))
            sTouch.dragged = TRUE;
        /* Dragging a list scrolls it, a row per row height. */
        if (sTouch.dragged && sTouch.pressed >= HIT_ROW && sTouch.pressed < HIT_ROW + 16)
        {
            int scroll = sTouch.dragScroll - dy / 24;
            if (scroll < 0) scroll = 0;
            if (sShown.screen == SCR_POKEDEX)
                sDexScroll = scroll;
            else if (mode == MODE_FIELD)
                sBagScroll = scroll;
        }
    }
    else if (in->touchUp && sTouch.active)
    {
        sTouch.active = FALSE;
        if (mode == MODE_POKENAV && sTouch.startX < CW)
        {
            if (!sTouch.dragged)
                NavTap(sTouch.lastX, CtrVideo_BottomPictureY(sTouch.lastY));
            else if (sTouch.lastY - sTouch.startY > 24 || sTouch.lastY - sTouch.startY < -24)
                NavSwipe(sTouch.lastY - sTouch.startY);
            return HIT_NONE;
        }
        if ((!sTouch.dragged || sTouch.pressed == HIT_MAP) && HitTest(sTouch.lastX, sTouch.lastY) == sTouch.pressed)
            Activate(sTouch.pressed, mode);
        return HIT_NONE;
    }
    if (!sTouch.active || sTouch.dragged)
        return HIT_NONE;
    return sTouch.pressed;
}

/* ------------------------------------------------------------------------ */
/* Entry points                                                             */
/* ------------------------------------------------------------------------ */

void CtrBottom_Init(void)
{
    uint64_t start = CtrPlatform_Ticks();

    LoadResources();
    if (sRes.ready)
        BuildBackgroundCaches();
    sShown.mode = 0xFF;
    CtrLog_Write(CTR_LOG_VIDEO, "bottom screen: resources %s in %.1f ms", sRes.ready ? "ready" : "MISSING",
                 CtrPlatform_TickMs(CtrPlatform_Ticks() - start));
}

void CtrBottom_Frame(void)
{
    static u32 frames;
    static bool8 iconsPending, held;
    u8 mode, pressed;
    bool8 hold;

    if (!sRes.ready)
        return;
    ++frames;
    sAsked = sAsk;
    sAsk.kind = ASK_NONE;
    if (sAsked.kind == ASK_NONE)
        sBattleTap = HIT_NONE;

    mode = CurrentMode();
    pressed = ProcessTouch(mode);
    RunPlan();
    RunNav(mode);

    /* The hidden menus: the top screen keeps the world meanwhile. */
    hold = UpdateSession(mode, sPlan.kind != PLAN_NONE);
    if (hold != held)
    {
        CtrVideo_HoldTop(hold);
        held = hold;
        if (!hold)
        {
            sPartyTapped = -1;
            sSummary = -1;
        }
    }
    if (hold && mode != MODE_POKENAV)   /* the PokéNav is on show */
        FastForward();

    /* The PokéNav's last frame stays left of the column until repainted. */
    {
        static bool navDrawn;
        bool drawn = CtrVideo_BottomInUse();

        if (drawn != navDrawn)
            sForceRedraw = TRUE;
        navDrawn = drawn;
    }
    Snapshot(&sState, mode, pressed);

    /* One RomFS read at most, and a single redraw once the icons are in. */
    if (Prefetch(&sState))
        iconsPending = TRUE;
    else if (iconsPending)
    {
        iconsPending = FALSE;
        sForceRedraw = TRUE;
    }

    if (sForceRedraw || memcmp(&sState, &sShown, sizeof(sState)) != 0)
    {
        uint64_t start = CtrPlatform_Ticks();
        static float peak;
        float ms;

        if (sState.mode != sShown.mode)
            CtrLog_Write(CTR_LOG_VIDEO, "bottom screen: mode %u", sState.mode);
        sShown = sState;
        sForceRedraw = FALSE;
        sAnimFrame = (frames >> 4) & 1;
        Render(&sShown);
        ms = CtrPlatform_TickMs(CtrPlatform_Ticks() - start);
        if (ms > peak + 0.25f)
        {
            peak = ms;
            CtrLog_Write(CTR_LOG_VIDEO, "bottom screen: redraw peak %.2f ms (mode %u screen %u)", ms, sShown.mode,
                         sShown.screen);
        }
        return;
    }
    if (sAnimCount && (u8)((frames >> 4) & 1) != sAnimFrame)
    {
        sAnimFrame = (frames >> 4) & 1;
        AnimateIcons();
    }
}
