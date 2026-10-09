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
#include "event_data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "constants/pokedex.h"
#include "sound.h"
#include "characters.h"
#include "item.h"
#include "sound.h"
#include "constants/item.h"
#include "constants/items.h"
#include "constants/songs.h"

const char *const gCtrExtrasOffOn[2] = {CTR_TEXT("OFF", "OFF", "NON"), CTR_TEXT("ON", "ON", "OUI")};

static const char *const sVisibleWild[] = {CTR_TEXT("OFF", "OFF", "NON"), CTR_TEXT("FEW", "FEW", "PEU"), CTR_TEXT("SOME", "SOME", "MOYEN"), CTR_TEXT("MANY", "MANY", "BEAUCOUP")};
static const char *const sEncounterRate[] = {CTR_TEXT("NORMAL", "NORMAL", "NORMAL"), CTR_TEXT("OFF", "OFF", "NON"), "1/4", "1/2", "2X"};
static const char *const sShinyOdds[] = {"1/8192", "1/4096", "1/1024", "1/256", "1/64", CTR_TEXT("ALWAYS", "ALWAYS", "TOUJOURS")};
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

static const char *const sDexDone[] = {CTR_TEXT("TAP TO SET", "TAP TO SET", "ACTIVER")};
/*
 * Give items (CHEATS): a pocket, an item of it, a count, and GIVE puts them in
 * the bag (OPTIONS is only open in the field). The item steps through the
 * pocket's items in the game's order, skipping unused ids.
 */
static const char *const sGivePockets[] = {CTR_TEXT("ITEMS", "ITEMS", "OBJETS"), CTR_TEXT("POKE BALLS", "POKE BALLS", "POKÉ BALLS"), CTR_TEXT("TMS & HMS", "TMS & HMS", "CT ET CS"), CTR_TEXT("BERRIES", "BERRIES", "BAIES"), CTR_TEXT("KEY ITEMS", "KEY ITEMS", "OBJ. RARES")};
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

static const char *const sGiveText[] = {CTR_TEXT("TAP TO GIVE", "TAP TO GIVE", "DONNER")};
static const char *const sGiveOpenText[] = {CTR_TEXT("TAP TO OPEN", "TAP TO OPEN", "OUVRIR")};
static const char *const sGiveBackText[] = {CTR_TEXT("BACK TO CHEATS", "BACK TO CHEATS", "RETOUR")};

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
 * Fast-forward's SPEED (1x to 4x; settings.txt speed=, ZR/ZL on a New 3DS):
 * with the tabs, a cell of ENHANCEMENTS rather than a seventh row of
 * SETTINGS.
 */
static void SpeedStep(int direction)
{
    CtrSettings_StepSpeed(direction, true);
}

static const u8 *SpeedText(void)
{
    static u8 text[4];

    text[0] = CHAR_0 + CtrSettings_Speed();
    text[1] = CHAR_x;
    text[2] = EOS;
    return text;
}

const CtrExtra gCtrExtras[] =
{
    /* Features add their lines here. */
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("SPEED", "SPEED", "VITESSE"), "speed", 0, 0, NULL, NULL, SpeedStep, SpeedText},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("EXP FOR CATCHING", "EXP FOR CATCHING", "EXP. CAPTURE"), "exp_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("PARTY EXP SHARE", "PARTY EXP SHARE", "MULTI EXP."), "exp_share", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("TRADE EVO LV. 40", "TRADE EVO LV. 40", "ÉVOL. NIV. 40"), "trade_evo", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("HMS WITHOUT MOVE", "HMS WITHOUT MOVE", "CS SANS CAP."), "field_hms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("VISIBLE WILD", "VISIBLE WILD", "SAUVAGES"), "visible_wild", 4, 0, sVisibleWild, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, "IVS AND EVS", "ivs_evs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, "REPEL PROMPT", "repel_prompt", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, "INFINITE TMS", "infinite_tms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, "RUN INDOORS", "run_indoors", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("WILD ENCOUNTERS", "WILD ENCOUNTERS", "RENCONTRES"), "encounter_rate", 5, 0, sEncounterRate, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("SHINY ODDS", "SHINY ODDS", "CHROMATIQUES"), "shiny_odds", 6, 0, sShinyOdds, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("ALWAYS CATCH", "ALWAYS CATCH", "CAPTURE SÛRE"), "always_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INSTANT VICTORY", "INSTANT VICTORY", "VICTOIRE IMM."), "instant_victory", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("FAST EGGS", "FAST EGGS", "OEUFS RAPIDES"), "fast_eggs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INFINITE MONEY", "INFINITE MONEY", "ARGENT INFINI"), "infinite_money", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("HOENN DEX FULL", "HOENN DEX FULL", "DEX HOENN PLEIN"), "dex_hoenn", 0, 0, sDexDone, CompleteHoennDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX ON", "NATIONAL DEX ON", "DEX NAT. ACTIF"), "dex_national_on", 0, 0, sDexDone, UnlockNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX FULL", "NATIONAL DEX FULL", "DEX NAT. PLEIN"), "dex_national", 0, 0, sDexDone, CompleteNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("GIVE ITEMS", "GIVE ITEMS", "DONNER OBJETS"), "give_open", 0, 0, sGiveOpenText, GiveOpen, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE ITEMS", "GIVE ITEMS", "DONNER OBJETS"), "give_back", 0, 0, sGiveBackText, GiveBack, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("POCKET", "POCKET", "POCHE"), "give_pocket", 5, 0, sGivePockets, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("ITEM", "ITEM", "OBJET"), "give_item", 0, 0, NULL, NULL, GiveItemStep, GiveItemText},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("HOW MANY", "HOW MANY", "QUANTITÉ"), "give_count", 5, 0, sGiveCounts, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE", "GIVE", "DONNER"), "give_now", 0, 0, sGiveText, GiveItemsNow, NULL, NULL},
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

bool CtrExtras_PageUsed(unsigned page)
{
    for (unsigned i = 0; i < gCtrExtraCount; ++i)
        if (CTR_EXTRAS_TAB(gCtrExtras[i].page) == page)
            return true;
    return false;
}
