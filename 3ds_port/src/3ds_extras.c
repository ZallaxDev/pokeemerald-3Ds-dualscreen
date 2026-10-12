/*
 * The optional settings of the ENHANCEMENTS and CHEATS pages of OPTIONS
 * (3ds_extras.h). Each feature adds its line to gCtrExtras and, for the game
 * code that reads it, a function below.
 */
#include <string.h>
#include "global.h"
#include "3ds_locale.h"
#include "3ds_extras.h"
#include "3ds_locale.h"
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

const char *const gCtrExtrasOffOn[2] = {CTR_TEXT("OFF", "OFF", "NON", "NENHUM"), CTR_TEXT("ON", "ON", "OUI", "SIM")};

static const char *const sVisibleWild[] = {CTR_TEXT("OFF", "OFF", "NON", "NENHUM"), CTR_TEXT("FEW", "FEW", "PEU", "POUCOS"), CTR_TEXT("SOME", "SOME", "MOYEN", "ALGUNS"), CTR_TEXT("MANY", "MANY", "BEAUCOUP", "MUITOS")};
static const char *const sEncounterRate[] = {CTR_TEXT("NORMAL", "NORMAL", "NORMAL", "NORMAL"), CTR_TEXT("OFF", "OFF", "NON", "NENHUM"), "1/4", "1/2", "2X"};
static const char *const sShinyOdds[] = {"1/8192", "1/4096", "1/1024", "1/256", "1/64", CTR_TEXT("ALWAYS", "ALWAYS", "TOUJOURS", "SEMPRE")};
/* The voxel world per eye with the 3D slider (3ds_video.c): AUTO is on a
 * New 3DS only (the second pass costs an Old 3DS the frame), and how deep
 * the slider's full travel goes, in percent of the renderer's depth. */
static const char *const sVoxelStereo[] = {CTR_TEXT("AUTO", "AUTO", "AUTO", "AUTO"), CTR_TEXT("ON", "ON", "OUI", "SIM"), CTR_TEXT("OFF", "OFF", "NON", "NENHUM")};
static const char *const sVoxelDepths[] = {"25%", "50%", "75%", "100%", "125%", "150%", "200%"};
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

static const char *const sDexDone[] = {CTR_TEXT("TAP TO SET", "TAP TO SET", "ACTIVER", "TOQUE P/ ATIVAR")};
/*
 * Give items (CHEATS): a pocket, an item of it, a count, and GIVE puts them in
 * the bag (OPTIONS is only open in the field). The item steps through the
 * pocket's items in the game's order, skipping unused ids.
 */
static const char *const sGivePockets[] = {CTR_TEXT("ITEMS", "ITEMS", "OBJETS", "ITENS"), CTR_TEXT("POKE BALLS", "POKE BALLS", "POKÉ BALLS", "POKé BOLAS"), CTR_TEXT("TMS & HMS", "TMS & HMS", "CT ET CS", "MTs E MOs"), CTR_TEXT("BERRIES", "BERRIES", "BAIES", "FRUTAS"), CTR_TEXT("KEY ITEMS", "KEY ITEMS", "OBJ. RARES", "ITENS CHAVE")};
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

static const char *const sGiveText[] = {CTR_TEXT("TAP TO GIVE", "TAP TO GIVE", "DONNER", "TOQUE P/ DAR")};
static const char *const sGiveOpenText[] = {CTR_TEXT("TAP TO OPEN", "TAP TO OPEN", "OUVRIR", "TOQUE P/ ABRIR")};
static const char *const sGiveBackText[] = {CTR_TEXT("BACK TO CHEATS", "BACK TO CHEATS", "RETOUR", "VOLTAR")};

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
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D WORLD", "MUNDO 3D", "MONDE 3D", "MUNDO 3D"), "voxel_stereo", 3, 0, sVoxelStereo, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D DEPTH", "PROFUNDIDAD 3D", "PROFONDEUR 3D", "PROFUNDIDADE 3D"), "voxel_depth", 7, 3, sVoxelDepths, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("SPEED", "SPEED", "VITESSE", "VELOCIDADE"), "speed", 0, 0, NULL, NULL, SpeedStep, SpeedText},
    /* The game and the music keep real time on a slow frame; the sound
     * engine mixes on the DSP's clock (3ds_platform.c, 3ds_audio.c). */
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("REAL-TIME PACING", "RITMO REAL", "RYTHME RÉEL", "RITMO REAL"), "audio_clock", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("EXP FOR CATCHING", "EXP FOR CATCHING", "EXP. CAPTURE", "EXP. POR CAPTURA"), "exp_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("PARTY EXP SHARE", "PARTY EXP SHARE", "MULTI EXP.", "EXP. PARA EQUIPE"), "exp_share", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("TRADE EVO LV. 40", "TRADE EVO LV. 40", "ÉVOL. NIV. 40", "EVO. TROCA NV. 40"), "trade_evo", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("HMS WITHOUT MOVE", "HMS WITHOUT MOVE", "CS SANS CAP.", "MOS SEM GOLPE"), "field_hms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("VISIBLE WILD", "VISIBLE WILD", "SAUVAGES", "SELVAGEM VISÍVEL"), "visible_wild", 4, 0, sVisibleWild, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("IVS AND EVS", "IVS AND EVS", "IV ET EV", "IVS E EVS"), "ivs_evs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("REPEL PROMPT", "REPEL PROMPT", "RAPPEL REPOUSSE", "AVISO REPELENTE"), "repel_prompt", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("INFINITE TMS", "INFINITE TMS", "CT INFINIES", "MTS INFINITAS"), "infinite_tms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("RUN INDOORS", "RUN INDOORS", "COURSE INTÉR.", "CORRER DENTRO"), "run_indoors", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    /* Moves physical or special by their own category, as from Gen IV on
     * (pokemon.c, MoveSplitEnabled). */
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("PHYS/SPEC SPLIT", "PHYS/SPEC SPLIT", "PHYS./SPÉC.", "FÍS./ESP."), "move_split", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("WILD ENCOUNTERS", "WILD ENCOUNTERS", "RENCONTRES", "ENCONTROS SELV."), "encounter_rate", 5, 0, sEncounterRate, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("SHINY ODDS", "SHINY ODDS", "CHROMATIQUES", "CHANCE SHINY"), "shiny_odds", 6, 0, sShinyOdds, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("ALWAYS CATCH", "ALWAYS CATCH", "CAPTURE SÛRE", "CAPTURA GARANTIDA"), "always_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INSTANT VICTORY", "INSTANT VICTORY", "VICTOIRE IMM.", "VITÓRIA IMEDIATA"), "instant_victory", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("FAST EGGS", "FAST EGGS", "OEUFS RAPIDES", "OVOS RÁPIDOS"), "fast_eggs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INFINITE MONEY", "INFINITE MONEY", "ARGENT INFINI", "DINHEIRO INFINITO"), "infinite_money", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("HOENN DEX FULL", "HOENN DEX FULL", "DEX HOENN PLEIN", "DEX HOENN COMPL."), "dex_hoenn", 0, 0, sDexDone, CompleteHoennDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX ON", "NATIONAL DEX ON", "DEX NAT. ACTIF", "ATIVAR DEX NAC."), "dex_national_on", 0, 0, sDexDone, UnlockNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX FULL", "NATIONAL DEX FULL", "DEX NAT. PLEIN", "DEX NAC. COMPLETA"), "dex_national", 0, 0, sDexDone, CompleteNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("GIVE ITEMS", "GIVE ITEMS", "DONNER OBJETS", "DAR ITENS"), "give_open", 0, 0, sGiveOpenText, GiveOpen, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE ITEMS", "GIVE ITEMS", "DONNER OBJETS", "DAR ITENS"), "give_back", 0, 0, sGiveBackText, GiveBack, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("POCKET", "POCKET", "POCHE", "BOLSO"), "give_pocket", 5, 0, sGivePockets, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("ITEM", "ITEM", "OBJET", "ITEM"), "give_item", 0, 0, NULL, NULL, GiveItemStep, GiveItemText},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("HOW MANY", "HOW MANY", "QUANTITÉ", "QUANTIDADE"), "give_count", 5, 0, sGiveCounts, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE", "GIVE", "DONNER", "DAR"), "give_now", 0, 0, sGiveText, GiveItemsNow, NULL, NULL},
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
