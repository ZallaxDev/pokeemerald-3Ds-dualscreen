/*
 * The stereoscopic Pokémon viewer, game side (3ds_stereo_mon.h): which
 * Pokémon the top screen shows, its picture from the game's own tables, and
 * the gallery's keys.
 *
 * The Pokédex's info page (shown on the bottom screen, pokedex.c under
 * PLATFORM_3DS) puts its Pokémon on the top screen whenever the planes file
 * has it. In the field, L+R+START (or START held alone for a second and a
 * half, for an emulator's keyboard) opens a gallery of every Pokémon in the
 * file; while it is open the game gets no keys.
 */
#include <string.h>

#include "global.h"
#include "main.h"
#include "data.h"
#include "pokemon.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_setup.h"
#include "graphics.h"
#include "overworld.h"
#include "random.h"
#include "wild_encounter.h"
#include "constants/map_groups.h"
#include "constants/wild_encounter.h"
#include "constants/map_types.h"
#include "constants/battle.h"
#include "constants/trainers.h"
#include "constants/opponents.h"
#include "constants/characters.h"
#include "constants/species.h"

#include "port_platform.h"
#include "3ds_extras.h"
#include "3ds_input.h"
#include "3ds_log.h"
#include "3ds_video.h"
#include "3ds_stereo_mon.h"

/* pokedex.c: the Pokémon on the info page of the Pokédex opened from the
 * field, as a national dex number, or 0. */
u16 CtrPokedex_InfoDexNum(void);
/* pokedex.c: the list is scrolling; the viewer keeps what it shows. */
bool8 CtrPokedex_ListScrolling(void);
bool8 CtrPokedex_IsOpen(void);
/* pokedex.c: closes the Pokédex (leave: to the field). */
bool8 CtrPokedex_Close(bool8 leave);
/* Frames the hologram stays up after the Pokédex names no Pokémon while it
 * is still open: a page turning, a cursor on an unseen one. */
#define DEX_HOLD_FRAMES 60

static void BattleFrame(void);

#define OPEN_KEYS (CTR_KEY_L | CTR_KEY_R)
#define START_HOLD_FRAMES 90
#define FRAME_BYTES (CTR_STEREO_MON_DIM * CTR_STEREO_MON_DIM / 2)

static bool8 sGallery;
static unsigned sIndex;
/* What the viewer shows, and in which of its two modes. */
static u16 sShown;
static bool8 sShownGallery;
/* Frames the game still gets no keys: the press that closed the gallery. */
static u8 sMask;
/* Frames START has been held on its own. */
static u16 sStartHeld;
static u8 sDexHold;

/* LZ77 data of a game symbol (an asset stub, resolved from the game data)
 * into a buffer of `capacity` bytes; the size comes from the stream's own
 * header. As 3ds_bottom_ui.c's Unlz, without the heap: the game's is nearly
 * full by the time the field is up. */
static u32 Unlz(const void *src, void *dst, u32 capacity)
{
    const u8 *res = src ? Port_ResolveAssetPointer(src) : NULL;
    u32 header, size;

    if (!res)
        return 0;
    header = res[0] | (res[1] << 8) | (res[2] << 16) | ((u32)res[3] << 24);
    size = header >> 8;
    if ((header & 0xFF) != 0x10 || size == 0 || size > capacity)
        return 0;
    LZ77UnCompWram((const u32 *)res, dst);
    return size;
}

/* 4bpp tiles (64x64 a frame) with a palette as 64x64 RGBA5551 frames (rows
 * top to bottom, 0 = transparent). */
static void DecodeTiles(const u8 *tiles, unsigned frames, const u16 *pal, u16 *out)
{
    for (unsigned f = 0; f < frames; ++f)
        for (unsigned y = 0; y < CTR_STEREO_MON_DIM; ++y)
            for (unsigned x = 0; x < CTR_STEREO_MON_DIM; ++x)
            {
                unsigned tile = (y / 8) * 8 + x / 8;
                u8 v = (tiles[f * FRAME_BYTES + tile * 32 + (y & 7) * 4 + (x & 7) / 2] >> ((x & 1) * 4)) & 15;

                out[f * CTR_STEREO_MON_DIM * CTR_STEREO_MON_DIM + y * CTR_STEREO_MON_DIM + x] =
                    v ? CtrVideo_RGBA5551(pal[v]) : 0;
            }
}

/* The picture an id names (CTR_STEREO_ID_*): a species' front (Castform's
 * has four frames, every other one or two; the viewer takes two) or back,
 * a trainer's front, or a player's back (an uncompressed sheet of up to
 * five frames). Returns its frames, 0 without a picture. */
static unsigned DecodePicture(u16 id, u16 *out)
{
    static u8 tiles[CTR_STEREO_MON_MAX_FRAMES * FRAME_BYTES];
    static u16 pal[16];
    unsigned kind = id & 0xC000, number = id & 0x3FFF, frames = 0;
    const void *tilesLz = NULL, *palLz = NULL;
    u32 size;

    if (kind == 0 || kind == CTR_STEREO_ID_BACK)
    {
        u16 species = NationalPokedexNumToSpecies(number);

        if (species == SPECIES_NONE) return 0;
        tilesLz = kind == 0 ? gMonFrontPicTable[species].data : gMonBackPicTable[species].data;
        palLz = gMonPaletteTable[species].data;
    }
    else if (kind == CTR_STEREO_ID_TRAINER)
    {
        tilesLz = gTrainerFrontPicTable[number].data;
        palLz = gTrainerFrontPicPaletteTable[number].data;
    }
    else
    {
        /* The players' backs are raw sheets. */
        const u8 *sheet = Port_ResolveAssetPointer(gTrainerBackPicTable[number].data);

        size = gTrainerBackPicTable[number].size;
        if (sheet == NULL || size < FRAME_BYTES || Unlz(gTrainerBackPicPaletteTable[number].data, pal, sizeof(pal)) != sizeof(pal))
            return 0;
        frames = size / FRAME_BYTES;
        if (frames > CTR_STEREO_MON_MAX_FRAMES) frames = CTR_STEREO_MON_MAX_FRAMES;
        DecodeTiles(sheet, frames, pal, out);
        return frames;
    }
    size = Unlz(tilesLz, tiles, sizeof(tiles));
    if (size >= FRAME_BYTES && Unlz(palLz, pal, sizeof(pal)) == sizeof(pal))
    {
        frames = size >= 2 * FRAME_BYTES ? 2 : 1;
        DecodeTiles(tiles, frames, pal, out);
    }
    else
        CtrLog_Write(CTR_LOG_ERROR, "stereo: id %04x has no picture (%lu bytes)", id, (unsigned long)size);
    return frames;
}

/* A game string in ASCII capitals, from the game's own character set. */
static void AsciiStr(const u8 *s, char *out, unsigned size)
{
    unsigned n = 0;

    while (*s != EOS && n + 1 < size)
    {
        u8 c = *s++;
        char a;

        if (c >= CHAR_A && c < CHAR_A + 26) a = 'A' + (c - CHAR_A);
        else if (c >= CHAR_a && c < CHAR_a + 26) a = 'A' + (c - CHAR_a);
        else if (c >= CHAR_0 && c < CHAR_0 + 10) a = '0' + (c - CHAR_0);
        else if (c == CHAR_SPACE) a = ' ';
        else if (c == CHAR_PERIOD) a = '.';
        else if (c == CHAR_HYPHEN) a = '-';
        else if (c == CHAR_MALE) a = 'M';
        else if (c == CHAR_FEMALE) a = 'F';
        else a = ' ';
        out[n++] = a;
    }
    out[n] = 0;
}

static void AsciiName(u16 species, char *out, unsigned size) { AsciiStr(gSpeciesNames[species], out, size); }

/* The name of what an id shows:
 * the species (a back says so), a trainer's name when every trainer with
 * that picture shares it (the rival, a leader) else their class, a player. */
void CtrStereoMonGame_Name(uint16_t id, char *out, unsigned size)
{
    static const char *const players[] = {"BRENDAN", "MAY", "RED", "LEAF", "RS BRENDAN", "RS MAY", "WALLY", "STEVEN"};
    unsigned kind = id & 0xC000, number = id & 0x3FFF;
    char name[CTR_STEREO_MON_NAME];

    if (size == 0) return;
    out[0] = 0;
    if (kind == 0 || kind == CTR_STEREO_ID_BACK)
    {
        u16 species = NationalPokedexNumToSpecies(number);

        if (species == SPECIES_NONE) return;
        AsciiName(species, name, sizeof(name));
        snprintf(out, size, kind == 0 ? "%s" : "%s BACK", name);
    }
    else if (kind == CTR_STEREO_ID_TRAINER)
    {
        const struct Trainer *first = NULL;
        bool8 sameName = TRUE;

        for (unsigned i = 1; i < TRAINERS_COUNT; ++i)
        {
            if (gTrainers[i].trainerPic != number) continue;
            if (first == NULL) first = &gTrainers[i];
            else if (memcmp(first->trainerName, gTrainers[i].trainerName, sizeof(first->trainerName)) != 0) sameName = FALSE;
        }
        if (first == NULL) snprintf(out, size, "TRAINER %u", number);
        else AsciiStr(sameName ? first->trainerName : gTrainerClassNames[first->trainerClass], out, size);
    }
    else
        snprintf(out, size, "%s BACK", number < sizeof(players) / sizeof(players[0]) ? players[number] : "PLAYER");
}

/*
 * The Pokédex's backdrop: the battle terrain the species is met in. Its
 * first place in the wild encounter tables decides - tall grass (a cave's
 * if the map is underground), water (the sea's on an ocean route, else a
 * pond's), rock smash a mountain's - and a species never met wild takes
 * one by its type. The terrain's tiles, tilemap and palettes (the game's
 * own, as the battle loads them into palettes 2..4) are decoded into
 * 256x128 pixels, row by row, for the viewer (CtrStereoMon_BackdropRow).
 */
struct Terrain
{
    const u32 *tiles, *tilemap, *palette;
};

static const struct Terrain sTerrains[] =
{
    [BATTLE_ENVIRONMENT_GRASS] = {gBattleEnvironmentTiles_TallGrass, gBattleEnvironmentTilemap_TallGrass, gBattleEnvironmentPalette_TallGrass},
    [BATTLE_ENVIRONMENT_LONG_GRASS] = {gBattleEnvironmentTiles_LongGrass, gBattleEnvironmentTilemap_LongGrass, gBattleEnvironmentPalette_LongGrass},
    [BATTLE_ENVIRONMENT_SAND] = {gBattleEnvironmentTiles_Sand, gBattleEnvironmentTilemap_Sand, gBattleEnvironmentPalette_Sand},
    [BATTLE_ENVIRONMENT_UNDERWATER] = {gBattleEnvironmentTiles_Underwater, gBattleEnvironmentTilemap_Underwater, gBattleEnvironmentPalette_Underwater},
    [BATTLE_ENVIRONMENT_WATER] = {gBattleEnvironmentTiles_Water, gBattleEnvironmentTilemap_Water, gBattleEnvironmentPalette_Water},
    [BATTLE_ENVIRONMENT_POND] = {gBattleEnvironmentTiles_PondWater, gBattleEnvironmentTilemap_PondWater, gBattleEnvironmentPalette_PondWater},
    [BATTLE_ENVIRONMENT_MOUNTAIN] = {gBattleEnvironmentTiles_Rock, gBattleEnvironmentTilemap_Rock, gBattleEnvironmentPalette_Rock},
    [BATTLE_ENVIRONMENT_CAVE] = {gBattleEnvironmentTiles_Cave, gBattleEnvironmentTilemap_Cave, gBattleEnvironmentPalette_Cave},
    [BATTLE_ENVIRONMENT_BUILDING] = {gBattleEnvironmentTiles_Building, gBattleEnvironmentTilemap_Building, gBattleEnvironmentPalette_Building},
};

static bool8 InTable(const struct WildPokemonInfo *info, unsigned count, u16 species)
{
    if (info == NULL) return FALSE;
    for (unsigned i = 0; i < count; ++i)
        if (info->wildPokemon[i].species == species) return TRUE;
    return FALSE;
}

static u8 TerrainOf(u16 species)
{
    u8 found = 0xFF;

    for (unsigned i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED) && found == 0xFF; ++i)
    {
        const struct WildPokemonHeader *h = &gWildMonHeaders[i];
        const struct MapHeader *map = Overworld_GetMapHeaderByGroupAndId(h->mapGroup, h->mapNum);
        u8 type = map ? map->mapType : MAP_TYPE_ROUTE;

        if (InTable(h->landMonsInfo, LAND_WILD_COUNT, species))
            found = type == MAP_TYPE_UNDERGROUND ? BATTLE_ENVIRONMENT_CAVE : BATTLE_ENVIRONMENT_GRASS;
        else if (InTable(h->waterMonsInfo, WATER_WILD_COUNT, species) || InTable(h->fishingMonsInfo, FISH_WILD_COUNT, species))
            found = type == MAP_TYPE_OCEAN_ROUTE ? BATTLE_ENVIRONMENT_WATER
                  : type == MAP_TYPE_UNDERWATER ? BATTLE_ENVIRONMENT_UNDERWATER : BATTLE_ENVIRONMENT_POND;
        else if (InTable(h->rockSmashMonsInfo, ROCK_WILD_COUNT, species))
            found = BATTLE_ENVIRONMENT_MOUNTAIN;
    }
    if (found != 0xFF) return found;
    switch (gSpeciesInfo[species].types[0])
    {
    case TYPE_WATER: return BATTLE_ENVIRONMENT_WATER;
    case TYPE_ROCK: case TYPE_GROUND: case TYPE_STEEL: case TYPE_DARK: return BATTLE_ENVIRONMENT_CAVE;
    case TYPE_FIRE: case TYPE_DRAGON: return BATTLE_ENVIRONMENT_MOUNTAIN;
    case TYPE_PSYCHIC: case TYPE_GHOST: return BATTLE_ENVIRONMENT_BUILDING;
    default: return BATTLE_ENVIRONMENT_GRASS;
    }
}

static void Backdrop(u16 species)
{
    static u8 tiles[8192];
    /* The battle background is 512 pixels wide: two screen blocks side by
     * side, the left one first; its left 256 columns are the picture. */
    static u16 map[2 * 32 * 32];
    static u16 pal[3 * 16];
    static u16 row[CTR_STEREO_MON_BACKDROP_W];
    u8 terrain = TerrainOf(species);
    const struct Terrain *t = &sTerrains[terrain];
    u32 tilesSize, mapSize, palSize;

    CtrStereoMon_BackdropBegin();
    tilesSize = Unlz(t->tiles, tiles, sizeof(tiles));
    mapSize = Unlz(t->tilemap, map, sizeof(map));
    palSize = Unlz(t->palette, pal, sizeof(pal));
    CtrLog_Write(CTR_LOG_VIDEO, "stereo: backdrop terrain %u: tiles %lu map %lu palette %lu bytes", terrain,
                 (unsigned long)tilesSize, (unsigned long)mapSize, (unsigned long)palSize);
    if (tilesSize == 0 || mapSize == 0 || palSize < sizeof(pal))
        return;
    for (unsigned y = 0; y < CTR_STEREO_MON_BACKDROP_H; ++y)
    {
        for (unsigned x = 0; x < CTR_STEREO_MON_BACKDROP_W; ++x)
        {
            u16 entry = map[(y / 8) * 32 + x / 8];
            unsigned tile = entry & 0x3ff, palette = entry >> 12;
            unsigned tx = (entry & 0x400) ? 7 - (x & 7) : (x & 7), ty = (entry & 0x800) ? 7 - (y & 7) : (y & 7);
            u8 v = tile < 256 ? (tiles[tile * 32 + ty * 4 + tx / 2] >> ((tx & 1) * 4)) & 15 : 0;
            u16 colour = palette >= 2 && palette < 5 ? pal[(palette - 2) * 16 + v] : pal[0];

            row[x] = CtrVideo_RGBA5551(colour);
        }
        CtrStereoMon_BackdropRow(y, row);
    }
}

static void Show(u16 id, bool8 gallery)
{
    static u16 picture[CTR_STEREO_MON_MAX_FRAMES][CTR_STEREO_MON_DIM * CTR_STEREO_MON_DIM];
    char name[CTR_STEREO_MON_NAME];
    unsigned frames = DecodePicture(id, picture[0]);

    CtrStereoMonGame_Name(id, name, sizeof(name));
    if (frames && CtrStereoMon_ShowId(id, picture[0], frames, name, gallery))
    {
        if (!gallery) Backdrop(NationalPokedexNumToSpecies(id));
        CtrLog_Write(CTR_LOG_VIDEO, "stereo: id %04x %s, %u frames (%s)", id, name, frames, gallery ? "gallery" : "pokedex");
    }
}

void CtrStereoMonGame_Frame(void)
{
    const CtrInput *input = CtrInput_Get();
    unsigned down = input->physicalDown, held = input->physicalHeld;
    u16 national = 0;
    bool8 gallery = FALSE;

    if (sMask) --sMask;
    if (!CtrStereoMon_Load())
        return;
    BattleFrame();
    if ((held & CTR_KEY_GAME) == CTR_KEY_START && !sGallery)
        ++sStartHeld;
    else
        sStartHeld = 0;
    if (((held & OPEN_KEYS) == OPEN_KEYS && (down & CTR_KEY_START)) || sStartHeld == START_HOLD_FRAMES)
    {
        sGallery = !sGallery;
        sMask = 2;
        if (sGallery && sShown && !sShownGallery)
        {
            /* Opened over the Pokédex's Pokémon: start the gallery there. */
            int index = CtrStereoMon_GalleryIndexOf(sShown);

            if (index >= 0) sIndex = (unsigned)index;
        }
    }
    else if (sGallery && (down & CTR_KEY_B))
    {
        sGallery = FALSE;
        sMask = 2;
    }
    if (sGallery)
    {
        unsigned count = CtrStereoMon_GalleryCount();

        if (count == 0)
            sGallery = FALSE;
        else
        {
            if (down & CTR_KEY_RIGHT) sIndex = (sIndex + 1) % count;
            if (down & CTR_KEY_LEFT) sIndex = (sIndex + count - 1) % count;
            CtrStereoMon_Press(down & (CTR_KEY_UP | CTR_KEY_DOWN | CTR_KEY_L | CTR_KEY_R | CTR_KEY_A | CTR_KEY_X));
            national = CtrStereoMon_GalleryIdAt(sIndex);
            gallery = TRUE;
        }
    }
    if (!gallery)
    {
        u16 dex = CtrPokedex_InfoDexNum();

        if (dex != 0 && CtrStereoMon_Has(dex))
            national = dex;
        /* The list scrolling past, or a page turning: the hologram holds
         * rather than blinking off and composing itself again. */
        if (sShown && !sShownGallery && CtrPokedex_IsOpen())
        {
            if (national != 0 && national != sShown) sDexHold = 0;
            else if (CtrPokedex_ListScrolling() || (national == 0 && sDexHold < DEX_HOLD_FRAMES))
            {
                national = sShown;
                if (!CtrPokedex_ListScrolling()) ++sDexHold;
            }
            else if (national == sShown) sDexHold = 0;
        }
        else
            sDexHold = 0;
    }
    if (national != sShown || gallery != sShownGallery)
    {
        sShown = national;
        sShownGallery = gallery;
        if (national == 0)
            CtrStereoMon_Hide();
        else
            Show(national, gallery);
    }
}

bool CtrStereoMonGame_TakesKeys(void)
{
    return sGallery || sMask != 0;
}

/*
 * The battle's four positions for the compositor (CtrStereoBattle_SetSlot):
 * each one's sprite tile, its picture buffer (gMonSpritesGfxPtr: the
 * Pokémon's front or back as the game decoded it, or the trainer's front
 * during the intro), the ids of the planes the buffer could be showing -
 * the Pokémon's (Transformed, what it turned into; the party's otherwise)
 * and, on the opponent's side in a trainer battle, the trainer's - and on
 * the player's side the raw sheet of the player's back with its id. A
 * position without a sprite is cleared.
 */
static u16 MonIdOf(u8 battler, bool8 player)
{
    u16 species = gBattleSpritesDataPtr->battlerData[battler].transformSpecies;
    u16 national;

    if (species == SPECIES_NONE && gBattlerPartyIndexes[battler] < PARTY_SIZE)
        species = GetMonData(player ? &gPlayerParty[gBattlerPartyIndexes[battler]]
                                    : &gEnemyParty[gBattlerPartyIndexes[battler]], MON_DATA_SPECIES);
    if (species == SPECIES_NONE || species >= NUM_SPECIES) return 0;
    national = SpeciesToNationalPokedexNum(species);
    if (player) return CtrStereoMon_HasBack(CTR_STEREO_ID_BACK | national) ? (CTR_STEREO_ID_BACK | national) : 0;
    return CtrStereoMon_Has(national) ? national : 0;
}

static u16 TrainerIdOf(unsigned position)
{
    u16 trainer, pic;

    if (!(gBattleTypeFlags & BATTLE_TYPE_TRAINER) || (gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER)))
        return 0;
    trainer = position == B_POSITION_OPPONENT_RIGHT && (gBattleTypeFlags & BATTLE_TYPE_TWO_OPPONENTS)
            ? gTrainerBattleOpponent_B : gTrainerBattleOpponent_A;
    if (trainer == 0 || trainer >= TRAINERS_COUNT) return 0;
    pic = gTrainers[trainer].trainerPic;
    return CtrStereoMon_HasBack(CTR_STEREO_ID_TRAINER | pic) ? (CTR_STEREO_ID_TRAINER | pic) : 0;
}

static void BattleFrame(void)
{
    if (!gMain.inBattle || gMonSpritesGfxPtr == NULL || gBattleSpritesDataPtr == NULL
        || gBattleSpritesDataPtr->battlerData == NULL)
    {
        CtrStereoBattle_ClearBattlers();
        return;
    }
    for (unsigned position = 0; position < CTR_STEREO_SLOTS; ++position)
    {
        u8 battler = GetBattlerAtPosition(position);
        bool8 player = position == B_POSITION_PLAYER_LEFT || position == B_POSITION_PLAYER_RIGHT;
        bool8 right = position == B_POSITION_PLAYER_RIGHT || position == B_POSITION_OPPONENT_RIGHT;
        u8 spriteId;
        const u8 *backSheet = NULL;
        unsigned backFrames = 0;
        u16 playerId = 0;

        if (battler >= gBattlersCount || (right && !(gBattleTypeFlags & BATTLE_TYPE_DOUBLE)))
        {
            CtrStereoBattle_SetSlot(position, 0, NULL, 0, 0, 0, 0, NULL, 0);
            continue;
        }
        spriteId = gBattlerSpriteIds[battler];
        if (spriteId >= MAX_SPRITES || !gSprites[spriteId].inUse)
        {
            CtrStereoBattle_SetSlot(position, 0, NULL, 0, 0, 0, 0, NULL, 0);
            continue;
        }
        if (player)
        {
            u16 backPic = position == B_POSITION_PLAYER_RIGHT ? TRAINER_BACK_PIC_STEVEN
                        : gSaveBlock2Ptr->playerGender + TRAINER_BACK_PIC_BRENDAN;

            if (CtrStereoMon_HasBack(CTR_STEREO_ID_PLAYER | backPic))
            {
                backSheet = Port_ResolveAssetPointer(gTrainerBackPicTable[backPic].data);
                backFrames = gTrainerBackPicTable[backPic].size / CTR_STEREO_BATTLE_FRAME_BYTES;
                playerId = CTR_STEREO_ID_PLAYER | backPic;
            }
        }
        CtrStereoBattle_SetSlot(position, gSprites[spriteId].oam.tileNum, gMonSpritesGfxPtr->sprites.ptr[position],
                                CTR_STEREO_BUFFER_FRAMES, MonIdOf(battler, player), player ? 0 : TrainerIdOf(position),
                                playerId, backSheet, backFrames);
    }
}

uint16_t CtrStereoMonGame_WildSpecies(uint16_t species)
{
    unsigned count;
    u16 pick;

    if (!CtrExtras_Get("wild_3d") || !CtrStereoMon_Load()) return species;
    count = CtrStereoMon_Count();
    if (count == 0 || CtrStereoMon_Has(SpeciesToNationalPokedexNum(species))) return species;
    pick = NationalPokedexNumToSpecies(CtrStereoMon_NationalAt(Random() % count));
    return pick != SPECIES_NONE ? pick : species;
}
