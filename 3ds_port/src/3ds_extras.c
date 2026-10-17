/*
 * The optional settings of the ENHANCEMENTS and CHEATS pages of OPTIONS
 * (3ds_extras.h). Each feature adds its line to gCtrExtras and, for the game
 * code that reads it, a function below.
 */
#include <string.h>
#include "global.h"
#include "3ds_locale.h"
#include "3ds_extras.h"
#include "3ds_platform.h"
#include "3ds_daynight.h"
#include "event_data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "constants/pokedex.h"
#include "sound.h"
#include "characters.h"
#include "item.h"
#include "constants/item.h"
#include "constants/items.h"
#include "constants/songs.h"
#include "3ds_locale.h"

const char *const gCtrExtrasOffOn[2] = {CTR_TEXT("OFF", "NO", "NON", "NO", "オフ"), CTR_TEXT("ON", "SÍ", "OUI", "SÌ", "オン")};

static const char *const sDayNight[] = {CTR_TEXT("OFF", "NO", "NON", "NO", "オフ"), "AUTO"};
static const char *const sClockSpeeds[] = {"1X", "6X", "12X", "24X", "60X", "120X"};
static const char *const sTimedEncounters[] = {CTR_TEXT("ORIGINAL", "ORIGINAL", "ORIGINAL", "ORIGINALE", "オリジナル"),
                                               CTR_TEXT("TIMED", "POR HORARIO", "PAR HORAIRE", "PER ORARIO", "じかんたい")};

static const char *const sVisibleWild[] = {CTR_TEXT("OFF", "NO", "NON", "NO", "オフ"), CTR_TEXT("FEW", "POCOS", "PEU", "POCHE", "すこし"), CTR_TEXT("SOME", "ALGUNOS", "MOYEN", "ALCUNE", "ふつう"), CTR_TEXT("MANY", "MUCHOS", "BEAUCOUP", "MOLTE", "たくさん")};
static const char *const sEncounterRate[] = {CTR_TEXT("NORMAL", "NORMAL", "NORMAL", "NORMALE", "ふつう"), CTR_TEXT("OFF", "NO", "NON", "NO", "オフ"), "1/4", "1/2", "2X"};
static const char *const sShinyOdds[] = {"1/8192", "1/4096", "1/1024", "1/256", "1/64", CTR_TEXT("ALWAYS", "SIEMPRE", "TOUJOURS", "SEMPRE", "かならず")};
/* The voxel world per eye with the 3D slider (3ds_video.c): AUTO is on a
 * New 3DS only (the second pass costs an Old 3DS the frame), and how deep
 * the slider's full travel goes, in percent of the renderer's depth. Read
 * only with the voxel overworld on: a build without it has no cells for
 * them, and with it off they are greyed (CtrExtras_NeedsVoxel). */
#if CTR_VOXEL_ENABLED
static const char *const sVoxelStereo[] = {CTR_TEXT("AUTO", "AUTO", "AUTO", "AUTO", "オート"), CTR_TEXT("ON", "SÍ", "OUI", "SÌ", "オン"), CTR_TEXT("OFF", "NO", "NON", "NO", "オフ")};
static const char *const sVoxelDepths[] = {"25%", "50%", "75%", "100%", "125%", "150%", "200%"};
#endif
/*
 * Pokedex (CHEATS): the Hoenn Pokedex complete, the National Pokedex
 * unlocked, or complete (and unlocked): every Pokemon seen and caught. OPTIONS
 * is only open in the field; the player saves to keep it.
 */
static void SetDexSeenCaught(u16 national)
{
    GetSetPokedexFlag(national, FLAG_SET_SEEN);
    GetSetPokedexFlag(national, FLAG_SET_CAUGHT);
}

static void CompleteHoennDex(void)
{
    for (u16 hoenn = 1; hoenn <= HOENN_DEX_COUNT; hoenn++)
        SetDexSeenCaught(HoennToNationalOrder(hoenn));
    PlaySE(SE_SUCCESS);
}

static void UnlockNationalDex(void)
{
    EnableNationalPokedex();
    PlaySE(SE_SUCCESS);
}

static void CompleteNationalDex(void)
{
    EnableNationalPokedex();
    for (u16 national = 1; national <= NATIONAL_DEX_COUNT; national++)
        SetDexSeenCaught(national);
    PlaySE(SE_SUCCESS);
}

static const char *const sDexDone[] = {CTR_TEXT("TAP TO SET", "TOCA PARA ACTIVAR", "ACTIVER", "ATTIVA", "タッチで せってい")};
/*
 * Give items (CHEATS): a pocket, an item of it, a count, and GIVE puts them in
 * the bag (OPTIONS is only open in the field). The item steps through the
 * pocket's items in the game's order, skipping unused ids.
 */
static const char *const sGivePockets[] = {CTR_TEXT("ITEMS", "OBJETOS", "OBJETS", "OGGETTI", "どうぐ"), CTR_TEXT("POKE BALLS", "POKé BALLS", "POKÉ BALLS", "POKé BALL", "ボール"), CTR_TEXT("TMS & HMS", "MT Y MO", "CT ET CS", "MT E MN", "わざマシン"), CTR_TEXT("BERRIES", "BAYAS", "BAIES", "BACCHE", "きのみ"), CTR_TEXT("KEY ITEMS", "OBJ. CLAVE", "OBJ. RARES", "STRUM. BASE", "たいせつなもの")};
static const char *const sGiveCounts[] = {"1", "5", "10", "50", "99"};
static const u8 sGiveCountValues[] = {1, 5, 10, 50, 99};

static bool8 GiveItemUsable(u16 item, u8 pocket)
{
    u8 name[ITEM_NAME_LENGTH + 1];

    if (item == ITEM_NONE || item >= ITEMS_COUNT || GetPocketByItemId(item) != pocket)
        return FALSE;
    CopyItemName(item, name);
    return name[0] != CHAR_QUESTION_MARK && name[0] != EOS;
}

static u8 GivePocket(void)
{
    int pocket = CtrSettings_GetInt("give_pocket", 0);

    return POCKET_ITEMS + (pocket >= 0 && pocket < (int)ARRAY_COUNT(sGivePockets) ? pocket : 0);
}

/* The chosen item, or the pocket's first when that is not one of it. */
static u16 GiveItem(void)
{
    u16 item = CtrSettings_GetInt("give_item", ITEM_NONE);
    u8 pocket = GivePocket();

    if (GiveItemUsable(item, pocket))
        return item;
    for (item = 1; item < ITEMS_COUNT; item++)
        if (GiveItemUsable(item, pocket))
            return item;
    return ITEM_NONE;
}

static void GiveItemStep(int direction)
{
    u16 item = GiveItem();
    u8 pocket = GivePocket();

    if (item == ITEM_NONE)
        return;
    for (u16 tries = 0; tries < ITEMS_COUNT; tries++)
    {
        item = direction < 0 ? (item <= 1 ? ITEMS_COUNT - 1 : item - 1) : (item + 1 >= ITEMS_COUNT ? 1 : item + 1);
        if (GiveItemUsable(item, pocket))
            break;
    }
    CtrSettings_SetInt("give_item", item);
}

static const u8 *GiveItemText(void)
{
    static u8 name[ITEM_NAME_LENGTH + 1];
    u16 item = GiveItem();

    if (item == ITEM_NONE)
        name[0] = EOS;
    else
        CopyItemName(item, name);
    return name;
}

static void GiveItemsNow(void)
{
    u16 item = GiveItem();
    int count = CtrSettings_GetInt("give_count", 0);
    u16 quantity = sGiveCountValues[count >= 0 && count < (int)ARRAY_COUNT(sGiveCountValues) ? count : 0];

    /* Key items come one at a time. */
    if (GetPocketByItemId(item) == POCKET_KEY_ITEMS)
        quantity = 1;
    if (item != ITEM_NONE && CheckBagHasSpace(item, quantity) && AddBagItem(item, quantity))
        PlaySE(SE_SUCCESS);
    else
        PlaySE(SE_FAILURE);
}

static const char *const sGiveText[] = {CTR_TEXT("TAP TO GIVE", "TOCA PARA DAR", "DONNER", "DAI", "タッチで もらう")};
static const char *const sGiveOpenText[] = {CTR_TEXT("TAP TO OPEN", "TOCA PARA ABRIR", "OUVRIR", "APRI", "タッチで ひらく")};
static const char *const sGiveBackText[] = {CTR_TEXT("BACK TO CHEATS", "VOLVER A TRUCOS", "RETOUR", "INDIETRO", "チートに もどる")};

/* The cells have a screen of their own, opened from the first. */
static void GiveOpen(void)
{
    CtrExtras_ShowScreen(CTR_EXTRAS_CHEATS, 1);
}

static void GiveBack(void)
{
    CtrExtras_ShowScreen(CTR_EXTRAS_CHEATS, 0);
}

/*
 * Fast-forward's SPEED (1x, 1.5x, 2x, 3x, 4x; settings.txt speed=, ZR/ZL on a New 3DS):
 * with the tabs, a cell of ENHANCEMENTS rather than a seventh row of
 * SETTINGS.
 */
static void SpeedStep(int direction)
{
    CtrSettings_StepSpeed(direction, true);
}

static const u8 *SpeedText(void)
{
    static u8 text[6];
    int halves = CtrSettings_SpeedHalves();
    u8 *end = text;

    *end++ = CHAR_0 + halves / 2;
    if (halves & 1)
    {
        *end++ = CHAR_PERIOD;
        *end++ = CHAR_5;
    }
    *end++ = CHAR_x;
    *end = EOS;
    return text;
}

const CtrExtra gCtrExtras[] =
{
    /* Features add their lines here. */
#if CTR_VOXEL_ENABLED
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D WORLD", "MUNDO 3D", "MONDE 3D", "MONDO 3D", "3D ワールド"), "voxel_stereo", 3, 0, sVoxelStereo, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D DEPTH", "PROFUNDIDAD 3D", "PROFONDEUR 3D", "PROFOND. 3D", "3D おくゆき"), "voxel_depth", 7, 3, sVoxelDepths, NULL, NULL, NULL},
#endif
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("SPEED", "VELOC. JUEGO", "VITESSE", "VELOCITÀ", "スピード"), "speed", 0, 0, NULL, NULL, SpeedStep, SpeedText},
    /* The game and the music keep real time on a slow frame; the sound
     * engine mixes on the DSP's clock (3ds_platform.c, 3ds_audio.c). */
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("REAL-TIME PACING", "RITMO REAL", "RYTHME RÉEL", "RITMO REALE", "リアルタイム"), "audio_clock", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("EXP FOR CATCHING", "EXP AL CAPTURAR", "EXP. CAPTURE", "ESP. CATTURA", "つかまえて けいけんち"), "exp_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("PARTY EXP SHARE", "EXP TODO EQUIPO", "MULTI EXP.", "CONDIV. ESP.", "みんなに けいけんち"), "exp_share", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("TRADE EVO LV. 40", "EVOL. INTERC. N40", "ÉVOL. NIV. 40", "SCAMBIO LIV. 40", "つうしん しんか Lv40"), "trade_evo", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("HMS WITHOUT MOVE", "MO SIN APRENDER", "CS SANS CAP.", "MN SENZA MOSSA", "ひでんわざ なしで"), "field_hms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("VISIBLE WILD", "SALVAJES VISIBLES", "SAUV. VISIBLES", "SELV. VISIBILI", "やせいが みえる"), "visible_wild", 4, 0, sVisibleWild, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("IVS AND EVS", "IV Y EV", "IV ET EV", "IV ED EV", "こたいち どりょくち"), "ivs_evs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("REPEL PROMPT", "AVISO REPELENTE", "RAPPEL REPOUSSE", "AVVISO REPELL.", "むしよけ かくにん"), "repel_prompt", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("INFINITE TMS", "MT INFINITAS", "CT INFINIES", "MT INFINITE", "わざマシン むげん"), "infinite_tms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("RUN INDOORS", "CORRER DENTRO", "COURSE INTÉR.", "CORRI DENTRO", "へやでも はしる"), "run_indoors", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    /* Moves physical or special by their own category, as from Gen IV on
     * (pokemon.c, MoveSplitEnabled). */
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("PHYS/SPEC SPLIT", "FÍSICO / ESPECIAL", "PHYS./SPÉC.", "FIS./SPEC.", "ぶつり/とくしゅ"), "move_split", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("WILD ENCOUNTERS", "ENC. SALVAJES", "RENCONTRES", "INCONTRI", "やせいの でかた"), "encounter_rate", 5, 0, sEncounterRate, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("SHINY ODDS", "PROB. VARIOCOLOR", "CHROMATIQUES", "PROB. SHINY", "いろちがい"), "shiny_odds", 6, 0, sShinyOdds, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("ALWAYS CATCH", "CAPTURA SEGURA", "CAPTURE SÛRE", "CATTURA SICURA", "かならず つかまる"), "always_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INSTANT VICTORY", "VICTORIA INSTANT.", "VICTOIRE IMM.", "VITT. IMM.", "すぐに かつ"), "instant_victory", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("FAST EGGS", "HUEVOS RÁPIDOS", "OEUFS RAPIDES", "UOVA VELOCI", "タマゴが はやい"), "fast_eggs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INFINITE MONEY", "DINERO INFINITO", "ARGENT INFINI", "SOLDI INFINITI", "おかね むげん"), "infinite_money", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("HOENN DEX FULL", "DEX HOENN LLENA", "DEX HOENN PLEIN", "DEX HOENN PIENO", "ホウエンずかん かんせい"), "dex_hoenn", 0, 0, sDexDone, CompleteHoennDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX ON", "DEX NACIONAL ACT.", "DEX NAT. ACTIF", "DEX NAZ. ATTIVO", "ぜんこくずかん オン"), "dex_national_on", 0, 0, sDexDone, UnlockNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX FULL", "DEX NACIONAL LLENA", "DEX NAT. PLEIN", "DEX NAZ. PIENO", "ぜんこくずかん かんせい"), "dex_national", 0, 0, sDexDone, CompleteNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("GIVE ITEMS", "DAR OBJETOS", "DONNER OBJETS", "DAI OGGETTI", "どうぐを もらう"), "give_open", 0, 0, sGiveOpenText, GiveOpen, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE ITEMS", "DAR OBJETOS", "DONNER OBJETS", "DAI OGGETTI", "どうぐを もらう"), "give_back", 0, 0, sGiveBackText, GiveBack, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("POCKET", "BOLSILLO", "POCHE", "TASCA", "ポケット"), "give_pocket", 5, 0, sGivePockets, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("ITEM", "OBJETO", "OBJET", "OGGETTO", "どうぐ"), "give_item", 0, 0, NULL, NULL, GiveItemStep, GiveItemText},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("HOW MANY", "CANTIDAD", "QUANTITÉ", "QUANTI", "いくつ"), "give_count", 5, 0, sGiveCounts, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE", "DAR", "DONNER", "DAI", "もらう"), "give_now", 0, 0, sGiveText, GiveItemsNow, NULL, NULL},
    /* The clock (3ds_daynight.c): outdoor light by the hour, its speed, the
     * wild Pokémon of the hour (3ds_encounters.c) and the bottom screen's
     * clock. ENHANCEMENTS is full, twelve cells a screen: its second. */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("DAY / NIGHT", "DÍA / NOCHE", "JOUR / NUIT", "GIORNO / NOTTE", "ひると よる"), "day_night", 2, 0, sDayNight, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("CLOCK SPEED", "VELOC. RELOJ", "VITESSE HORLOGE", "VELOC. OROLOGIO", "とけいの はやさ"), "clock_speed", CTR_DAYNIGHT_SPEED_COUNT, 0, sClockSpeeds, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("WILD POKEMON", "POKéMON SALV.", "POKÉMON SAUV.", "POKéMON SELV.", "やせいの ポケモン"), "timed_encounters", 2, 0, sTimedEncounters, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("SHOW CLOCK", "VER RELOJ", "VOIR HORLOGE", "MOSTRA OROLOGIO", "とけい ひょうじ"), "show_clock", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    /* The lead Pokémon walking behind the player, on HGSS's sprites from the
     * data pack (src/follower_mon.c). */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("FOLLOWER", "ACOMPAÑANTE", "COMPAGNON", "COMPAGNO", "つれあるき"), "follower", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {0},
};

/* The last line only keeps the array from being empty. */
const unsigned gCtrExtraCount = ARRAY_COUNT(gCtrExtras) - 1;

int CtrExtras_Value(const CtrExtra *extra)
{
    int value = CtrSettings_GetInt(extra->key, extra->fallback);

    return value >= 0 && value < extra->count ? value : extra->fallback;
}

/* One step either way, wrapping round like the game's options; an action
 * runs instead. */
void CtrExtras_Step(const CtrExtra *extra, int direction)
{
    if (extra->count == 0)
    {
        if (extra->act)
            extra->act();
        return;
    }
    CtrSettings_SetInt(extra->key, (CtrExtras_Value(extra) + (direction < 0 ? extra->count - 1 : 1)) % extra->count);
    if (extra->act)
        extra->act();
}

int CtrExtras_Get(const char *key)
{
    for (unsigned i = 0; i < gCtrExtraCount; ++i)
        if (strcmp(gCtrExtras[i].key, key) == 0)
            return CtrExtras_Value(&gCtrExtras[i]);
    return 0;
}

bool CtrExtras_NeedsVoxel(const CtrExtra *extra)
{
#if CTR_VOXEL_ENABLED
    return extra->values == sVoxelStereo || extra->values == sVoxelDepths;
#else
    (void)extra;
    return false;
#endif
}

bool CtrExtras_PageUsed(unsigned page)
{
    for (unsigned i = 0; i < gCtrExtraCount; ++i)
        if (CTR_EXTRAS_TAB(gCtrExtras[i].page) == page)
            return true;
    return false;
}
