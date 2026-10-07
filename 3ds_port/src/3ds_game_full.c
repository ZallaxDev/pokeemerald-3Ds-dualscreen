/*
 * Entry point: the real game, driven by its own AgbMain loop.
 *
 * Phases 1-4 ran a bootstrap that owned the frame loop and called selected
 * game routines. From here the ownership is inverted, exactly as on hardware:
 * AgbMain runs forever and every iteration blocks in WaitForVBlank ->
 * VBlankIntrWait, which is where the 3DS backend presents the frame, pumps APT
 * and scans HID. Nothing about the game's control flow is re-implemented.
 */

/* Before the game's headers, whose global.h defines abs and friends. */
#include <setjmp.h>
#include <stdlib.h>

#include "global.h"
#include "main.h"
#include "overworld.h"
#include "gba/flash_internal.h"
#include "gpu_regs.h"
#include "cgb_audio.h"
#include "sound.h"
#include "sound_mixer.h"
#include "m4a.h"
#include "scanline_effect.h"
#include "item_menu.h"
#include "party_menu.h"
#include "fldeff_misc.h"
#include "battle_pyramid.h"
#include "sprite.h"
#include "constants/party_menu.h"
#ifdef CTR_TEST_BATTLE
#include "battle_setup.h"
#include "script_pokemon_util.h"
#include "constants/species.h"
#endif
#include "port_platform.h"
#include "port_log.h"
#include "port_prof.h"

#include "3ds_platform.h"
#include "3ds_video.h"
#include "3ds_assets.h"
#include "3ds_audio.h"
#include "3ds_bottom.h"

#include <stdio.h>
#include <string.h>

void AgbMain(void);
extern IntrFunc gIntrTable[];
void CtrEmu_Reset(void);
u16 Platform_GetKeyInput(void);
void CtrEmu_BeginVBlank(void);
void CtrEmu_EndVBlank(void);

/* gIntrTableTemplate order: VCount, Serial, Timer3, HBlank, VBlank, ... */
#define INTR_INDEX_VBLANK 4

static u32 sFrames;
static bool sStarted;

/*
 * The CGB channels are generated in software beside the sample mixer, and they
 * need the output rate before the first note. m4a fixes that rate itself the
 * moment it starts (pcmSamplesPerVBlank is 701 under PORTABLE, pcmFreq is 60
 * times that), but it starts inside AgbMain, after the first mix could already
 * have happened. So the rate is set from the same constant the DSP was opened
 * with, and checked against what m4a actually chose on the first frame.
 */
static void StartCgbChannels(void)
{
    cgb_audio_init(CTR_AUDIO_SAMPLE_RATE);
}

/*
 * Soft reset: A+B+START+SELECT, and the game's own DoSoftReset (after the save
 * is cleared, a failed save, the clock reset and the like). The GBA clears its
 * RAM and starts the game again. Here the game's variables (its .data and .bss,
 * gathered by emerald3ds.ld.in) are put back as they were when AgbMain first
 * started, the emulated hardware is cleared, and AgbMain is entered again. Not
 * put back: the game's platform/ layer (emulated hardware, the flash with the
 * save), the port's state and the data the port loaded before AgbMain.
 */
extern char __ctr_game_data[], __ctr_game_data_end[];
extern char __ctr_game_bss[], __ctr_game_bss_end[];
static char *sGameAtStart;
static jmp_buf sAgbMainEntry;
static u32 sResets;

static size_t GameDataBytes(void) { return (size_t)(__ctr_game_data_end - __ctr_game_data); }
static size_t GameBssBytes(void) { return (size_t)(__ctr_game_bss_end - __ctr_game_bss); }

static void KeepGameAtStart(void)
{
    sGameAtStart = malloc(GameDataBytes() + GameBssBytes());
    if (sGameAtStart == NULL)
    {
        CtrLog_Write(CTR_LOG_ERROR, "soft reset unavailable: no memory for %lu KiB",
                     (unsigned long)((GameDataBytes() + GameBssBytes()) >> 10));
        return;
    }
    memcpy(sGameAtStart, __ctr_game_data, GameDataBytes());
    memcpy(sGameAtStart + GameDataBytes(), __ctr_game_bss, GameBssBytes());
    CtrLog_Write(CTR_LOG_GAME, "soft reset ready: game data %lu KiB, bss %lu KiB",
                 (unsigned long)(GameDataBytes() >> 10), (unsigned long)(GameBssBytes() >> 10));
}

/*
 * The game reads the keys the frame boundary scanned, and a reset comes before
 * the next boundary: with A+B+START+SELECT still held, AgbMain would reset
 * again at once, for ever, and nothing would scan the keys (nor answer HOME).
 * The GBA reads them live and resets until they are let go; here the reset
 * waits, frame by frame, until they are.
 */
static void WaitForResetKeysUp(void)
{
    const u16 keys = A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON;

    while ((Platform_GetKeyInput() & keys) == keys)
    {
        CtrPlatform_SleepUs(1000000 / 60);
        if (!CtrPlatform_BeginFrame())
        {
            CtrPlatform_Shutdown();
            exit(0);
        }
    }
}

void CtrGame_SoftReset(void)
{
    if (sGameAtStart == NULL)
    {
        CTR_STUB("RESET", "soft reset unavailable");
        return;
    }
    /* The sound worker mixes from the game's variables: not while they change. */
    CtrAudio_LockSound();
    memcpy(__ctr_game_data, sGameAtStart, GameDataBytes());
    memcpy(__ctr_game_bss, sGameAtStart + GameDataBytes(), GameBssBytes());
    CtrEmu_Reset();
    StartCgbChannels();
    CtrAudio_UnlockSound();
    ++sResets;
    WaitForResetKeysUp();
    longjmp(sAgbMainEntry, 1);
}

static void CheckAudioRate(void)
{
    static bool checked;
    const struct SoundInfo *info = SOUND_INFO_PTR;
    unsigned expected;

    if (checked || info == NULL || info->ident != ID_NUMBER)
        return;
    checked = true;
    expected = (unsigned)info->pcmFreq;
    if (expected != CTR_AUDIO_SAMPLE_RATE)
    {
        /* Pitch and the CGB channels would both be wrong; say so rather than
         * let it pass as a mysteriously detuned soundtrack. */
        CtrLog_Write(CTR_LOG_ERROR,
                     "mixer runs at %u Hz but the DSP was opened at %d Hz",
                     expected, CTR_AUDIO_SAMPLE_RATE);
        cgb_audio_init(expected);
    }
    else
    {
        CtrLog_Write(CTR_LOG_AUDIO, "mixer and DSP agree at %u Hz, %u samples per frame",
                     expected, (unsigned)info->pcmSamplesPerVBlank);
    }
}

/* What the overlay reports about the frame the mixer has just produced. */
static void SampleAudioStats(void)
{
    const struct SoundInfo *info = SOUND_INFO_PTR;
    /* One block of memory, two parallel declarations of it: m4a calls it a
     * SoundInfo, the mixer a SoundMixerState. The channel array only has a
     * name in the second one. */
    const struct SoundMixerState *mixer = (const struct SoundMixerState *)info;
    CtrAudioStats *stats = CtrAudio_Stats();
    unsigned voices = 0;

    if (info == NULL || info->ident != ID_NUMBER)
        return;
    for (unsigned i = 0; i < mixer->numChans && i < MAX_SAMPLE_CHANNELS; ++i)
    {
        if ((mixer->chans[i].status & 0xC7) != 0)
            ++voices;
    }
    stats->voices = voices;
    stats->song = GetCurrentMapMusic();
}

/*
 * The screens staged as one whole GBA picture (compat/ctr_gba_stage.h) or shown
 * centred (compat/ctr_gba_centred.h) install their VBlank callbacks through
 * here, so every callback in a set belongs to one of them. The mode lasts
 * while one of those is the active callback.
 * Between two scenes the game clears the callback for a frame or two; those
 * frames keep whatever the last callback said, so a scene change does not
 * flash the next frame at the other scale.
 */
#define CTR_STAGE_CALLBACKS 24
typedef struct
{
    IntrCallback callbacks[CTR_STAGE_CALLBACKS];
    /* For the centred set, which screen installed each callback. */
    uint8_t screens[CTR_STAGE_CALLBACKS];
    unsigned count;
    bool on;
    /* The screen of the active callback, kept across the gaps between scenes. */
    unsigned screen;
    const char *name;
} StageSet;
/* Pictures staged whole (ctr_gba_stage.h), screens shown centred
 * (ctr_gba_centred.h) and the battle scene (ctr_gba_battle.h). */
static StageSet sStage = {.name = "GBA stage"}, sCentred = {.name = "GBA centred"};
static StageSet sBattle = {.name = "GBA battle"};
/* The battle transitions (ctr_gba_transition.h), over the field. */
static StageSet sTransition = {.name = "GBA transition"};

static void RememberCallback(StageSet *set, IntrCallback callback, unsigned screen)
{
    unsigned i;

    for (i = 0; callback && i < set->count; ++i)
        if (set->callbacks[i] == callback) break;
    if (callback && i == set->count && i < CTR_STAGE_CALLBACKS)
        set->callbacks[set->count++] = callback;
    if (callback && i < set->count)
        set->screens[i] = (uint8_t)screen;
}

void CtrStage_SetVBlankCallback(IntrCallback callback)
{
    RememberCallback(&sStage, callback, 0);
    SetVBlankCallback(callback);
}

static void SetCentredCallback(IntrCallback callback, unsigned screen)
{
    RememberCallback(&sCentred, callback, screen);
    SetVBlankCallback(callback);
}

void CtrCentred_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_PLAIN);
}

void CtrCentredMainMenu_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_MAIN_MENU);
}

void CtrCentredNaming_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_NAMING);
}

void CtrCentredClock_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_CLOCK);
}

void CtrCentredStarter_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_STARTER);
}

void CtrCentredPokenav_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_POKENAV);
}

void CtrCentredStorage_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_STORAGE);
}

void CtrCentredSummary_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, CTR_CENTRED_SUMMARY);
}

/* The bag from the field goes left of the column, from anywhere else over the
 * whole bottom screen. */
void CtrCentredBag_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, gBagPosition.location == ITEMMENULOCATION_FIELD ? CTR_CENTRED_BAG
                                                                                  : CTR_CENTRED_BAG_WHOLE);
}

/* The party menu from the field goes left of the column, from a battle, a
 * contest or a facility over the whole bottom screen. */
void CtrCentredParty_SetVBlankCallback(IntrCallback callback)
{
    SetCentredCallback(callback, !gMain.inBattle && gPartyMenu.menuType == PARTY_MENU_TYPE_FIELD
                                     ? CTR_CENTRED_PARTY : CTR_CENTRED_PARTY_WHOLE);
}

/* The Pokédex's own callback. The page it shows for a mon just caught puts
 * back the battle's, which stays the battle's. */
IntrCallback CtrPokedex_VBlankCallback(void);

void CtrCentredPokedex_SetVBlankCallback(IntrCallback callback)
{
    if (callback == CtrPokedex_VBlankCallback())
    {
        SetCentredCallback(callback, CTR_CENTRED_POKEDEX);
        return;
    }
    SetVBlankCallback(callback);
}

void CtrBattle_SetVBlankCallback(IntrCallback callback)
{
    RememberCallback(&sBattle, callback, 0);
    SetVBlankCallback(callback);
}

void CtrTransition_SetVBlankCallback(IntrCallback callback)
{
    RememberCallback(&sTransition, callback, 0);
    SetVBlankCallback(callback);
}

static void UpdateSet(StageSet *set, IntrCallback callback)
{
    bool on = false;

    for (unsigned i = 0; i < set->count; ++i)
        if (set->callbacks[i] == callback)
        {
            on = true;
            set->screen = set->screens[i];
        }
    if (on != set->on)
        CtrLog_Write(CTR_LOG_VIDEO, "%s %s at frame %lu", set->name, on ? "on" : "off",
                     (unsigned long)sFrames);
    set->on = on;
}

static void UpdateStage(void)
{
    IntrCallback callback = gMain.vblankCallback;

    if (callback)
    {
        UpdateSet(&sStage, callback);
        UpdateSet(&sCentred, callback);
        UpdateSet(&sBattle, callback);
        UpdateSet(&sTransition, callback);
    }
    CtrVideo_SetStage(sStage.on);
    CtrVideo_SetCentred(sCentred.on ? sCentred.screen : CTR_CENTRED_NONE);
    CtrVideo_SetBattle(sBattle.on);
    CtrVideo_SetTransition(sTransition.on);
}

/*
 * The registers of every line of a battle transition's frame. On a GBA the
 * transitions change them between lines, from an HBlank interrupt (the swirl,
 * the slice, the ripple...) or an HBlank DMA (the wipes' window edges, the
 * white bars' brightness); here the frame is composed at once, after the
 * VBlank handler. So the lines are played through here: for each one the
 * registers are recorded, then the HBlank DMAs and the HBlank interrupt run
 * as they would at the end of that line. Afterwards the registers are put
 * back as the VBlank handler left them, which is what line 0 shows, and the
 * DMA channels are untouched: nothing else runs them, and the transitions
 * arm them again at every VBlank.
 */
#define INTR_INDEX_HBLANK 3
static uint16_t sLineRegs[CTR_GBA_LINES][CTR_LINE_REGS];

static void HBlankDmas(unsigned line)
{
    for (unsigned n = 0; n < 4; ++n)
    {
        const uint8_t *src = (const uint8_t *)(uintptr_t)(&REG_DMA0SAD)[n * 3];
        uintptr_t dest = (uintptr_t)(&REG_DMA0DAD)[n * 3];
        u32 control = (&REG_DMA0CNT)[n * 3];
        unsigned flags = control >> 16, count = control & 0xFFFF;
        unsigned unit = (flags & DMA_32BIT) ? 4 : 2;
        uintptr_t reg = dest - (uintptr_t)REG_BASE;

        if (!(flags & DMA_ENABLE) || (flags & DMA_START_MASK) != DMA_START_HBLANK)
            continue;
        if (src == NULL || reg < CTR_LINE_REG_FIRST
         || reg + count * unit > CTR_LINE_REG_FIRST + CTR_LINE_REGS * 2)
            continue;
        /* A repeating HBlank DMA reloads its count and keeps its source
         * moving: the end of line y copies entry y of the table. */
        if ((flags & DMA_SRC_MASK) == DMA_SRC_INC)
            src += (size_t)line * count * unit;
        memcpy((uint8_t *)REG_BASE + reg, src, count * unit);
    }
}

static void CaptureLineRegisters(void)
{
    static uint16_t saved[CTR_LINE_REGS];
    uint16_t *regs = (uint16_t *)((uint8_t *)REG_BASE + CTR_LINE_REG_FIRST);
    bool hblank = REG_IME && (REG_IE & INTR_FLAG_HBLANK) && (REG_DISPSTAT & DISPSTAT_HBLANK_INTR)
               && gIntrTable[INTR_INDEX_HBLANK];

    if (!sTransition.on)
    {
        CtrVideo_SetLineRegisters(NULL, 0);
        return;
    }
    memcpy(saved, regs, sizeof(saved));
    for (unsigned y = 0; y < CTR_GBA_LINES; ++y)
    {
        memcpy(sLineRegs[y], regs, sizeof(saved));
        if (y + 1 == CTR_GBA_LINES)
            break;
        REG_VCOUNT = y;
        HBlankDmas(y);
        if (hblank)
            gIntrTable[INTR_INDEX_HBLANK]();
    }
    memcpy(regs, saved, sizeof(saved));
    REG_VCOUNT = 0;
    CtrVideo_SetLineRegisters(&sLineRegs[0][0], CTR_GBA_LINES);
}

/*
 * The per-scanline register values of the frame about to be presented. On a
 * GBA an HBlank DMA feeds them to one register line by line; here the VBlank
 * handler has just armed that transfer (ScanlineEffect_InitHBlankDmaTransfer)
 * and swapped buffers, so the buffer the DMA would read is the other one.
 * Background scroll registers are handed over, which is what the waves of the
 * intro and the title screen drive, and window 0's edges, which the PokéNav
 * moves to light its chosen option.
 */
/*
 * The dark of a cave that needs Flash, and of the Battle Pyramid: the field
 * writes a circle of window 0 edges round the middle of the screen, line by
 * line (src/field_screen_effect.c SetFlashScanlineEffectWindowBoundaries),
 * with everything outside it hidden but the text layer. An 8-bit edge cannot
 * reach across 400 pixels, so the compositor is handed the circle itself:
 * its radius, read back from the line through its centre, where the game
 * writes exactly centre -/+ radius, and the player it is centred on (on the
 * GBA the middle of the screen is the middle of the player's metatile).
 */
static bool CaptureFieldLight(const struct ScanlineEffect *effect, bool active)
{
    const struct Sprite *player;
    unsigned across, left, radius;

    if (!active || effect->dmaDest != &REG_WIN0H || ((effect->dmaControl >> 16) & DMA_32BIT)
     || sTransition.on || gSaveBlock1Ptr == NULL || gMapHeader.mapLayout == NULL
     || (gSaveBlock1Ptr->flashLevel == 0 && !InBattlePyramid_())
     || gPlayerAvatar.spriteId >= MAX_SPRITES)
        return false;
    /* Both buffers hold the same circle once the radius settles; the one the
     * DMA would read this frame is the one just swapped out. */
    across = gScanlineEffectRegBuffers[effect->srcBuffer ^ 1][DISPLAY_HEIGHT / 2];
    left = across >> 8;
    radius = left > 0 ? DISPLAY_WIDTH / 2 - left : DISPLAY_WIDTH / 2;
    if ((across & 0xFF) == 0 && left == 0) return false;
    player = &gSprites[gPlayerAvatar.spriteId];
    CtrVideo_SetFieldLight(true, player->x + gSpriteCoordOffsetX,
                           player->y + gSpriteCoordOffsetY + 8, (int)radius);
    return true;
}

static void CaptureLineScroll(void)
{
    const struct ScanlineEffect *effect = &gScanlineEffect;
    uintptr_t reg = (uintptr_t)effect->dmaDest - (uintptr_t)REG_ADDR_BG0HOFS;
    bool wide = ((effect->dmaControl >> 16) & DMA_32BIT) != 0;
    bool active = effect->state != 0 && effect->state != 3 && effect->dmaDest;

    if (CaptureFieldLight(effect, active))
    {
        CtrVideo_SetLineWindow(NULL, 0, false);
        CtrVideo_SetLineScroll(0, false, NULL, 0);
        return;
    }
    CtrVideo_SetFieldLight(false, 0, 0, 0);
    /* 32-bit: WIN0H and WIN1H together, as the condition graph writes them. */
    if (active && effect->dmaDest == &REG_WIN0H)
        CtrVideo_SetLineWindow(effect->dmaSrcBuffers[effect->srcBuffer ^ 1], 160, wide);
    else
        CtrVideo_SetLineWindow(NULL, 0, false);
    if (!active || reg >= 0x10 || (reg & 1) || (wide && reg + 4 > 0x10))
    {
        CtrVideo_SetLineScroll(0, false, NULL, 0);
        return;
    }
    CtrVideo_SetLineScroll((unsigned)reg, wide,
                           effect->dmaSrcBuffers[effect->srcBuffer ^ 1], DISPLAY_HEIGHT);
}

void CtrGame_VBlank(void)
{
    PORT_PROF_BEGIN(vblank);
    CtrEmu_BeginVBlank();
    /*
     * The GBA VBlank handler is the game's own VBlankIntr: it runs the vblank
     * callback, applies buffered GPU registers and services DMA3 requests. It
     * must observe the same enable bits the game sets.
     */
    if (REG_IME && (REG_IE & INTR_FLAG_VBLANK) && gIntrTable[INTR_INDEX_VBLANK])
        gIntrTable[INTR_INDEX_VBLANK]();
    CtrEmu_EndVBlank();
    PORT_PROF_END(vblank, PORT_PROF_VBLANK);
    UpdateStage();
    PORT_PROF_BEGIN(lines);
    CaptureLineScroll();
    CaptureLineRegisters();
    PORT_PROF_END(lines, PORT_PROF_LINES);
    CheckAudioRate();
    SampleAudioStats();
    ++sFrames;
}

uint32_t CtrGame_Frames(void) { return sFrames; }
uint32_t CtrGame_APresses(void) { return 0; }
uint32_t CtrGame_Checks(void) { return 0; }

bool CtrGame_IsOverworld(void)
{
    if (gMapHeader.mapLayout == NULL || gSaveBlock1Ptr == NULL)
        return false;
    return gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic;
}

/* The poison step's effect is running (see DrawPoisonVeil in 3ds_video.c). */
bool CtrGame_FieldPoison(void)
{
    return FldEffPoison_IsActive() != 0;
}

/* AgbMain never returns, so a reset request restarts the process state here. */
void CtrGame_Frame(void)
{
}

void CtrGame_Init(void)
{
    const struct CtrAssetStats *stats;

    if (sStarted)
    {
        /* Once: a soft reset re-enters AgbMain itself (CtrGame_SoftReset). */
        CTR_STUB("RESET", "CtrGame_Init called again");
        return;
    }
    sStarted = true;

    /* Before anything reads a game table: the game's linked read-only data
     * lives in the data pack, and its region is empty until this returns. */
    if (!CtrGameData_Init())
        CtrAssets_Fatal("the game data bundle is required before anything else");
    CtrEmu_Reset();
    memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));

    /* Fail loudly here rather than letting the game read stub bytes as tiles,
     * or zeroes as script opcodes. */
    Port_AssetPreload();
    CtrAssets_StartWarmup();
    if (!CtrScripts_Init())
        CtrAssets_Fatal("the script bundle is required before AgbMain");
    if (!CtrMaps_Init())
        CtrAssets_Fatal("map payloads are required to reach the overworld");
    /* gSongTable already points into this region; without it every song header
     * is zeroes and MP2K would run a silent, malformed track. */
    if (!CtrSongs_Init())
        CtrAssets_Fatal("the song bundle is required before AgbMain");
    Port_PreloadLatinFonts();
    /* The bottom screen decodes its graphics once, here, off any frame. */
    CtrBottom_Init();
    /* Before AgbMain, because CheckForFlashMemory asks Port_SaveIsAvailable
     * early and a save that loads later would never be seen. */
    Port_SaveInit();
    StartCgbChannels();

    stats = CtrAssets_GetStats();
    CtrLog_Write(CTR_LOG_FS, "resources ready: %lu assets, index %lu KiB, maps %lu KiB, scripts %lu KiB",
                 (unsigned long)stats->entries, (unsigned long)(stats->indexBytes >> 10),
                 (unsigned long)(CtrMaps_Bytes() >> 10), (unsigned long)(CtrScripts_Bytes() >> 10));
    CtrLog_Write(CTR_LOG_AUDIO, "songs: %lu KiB resident, game data %lu KiB",
                 (unsigned long)(CtrSongs_Bytes() >> 10), (unsigned long)(CtrGameData_Bytes() >> 10));

    CtrVideo_Bind((CtrVideoMemory){VRAM_, (const uint16_t *)PLTT,
                  (const uint16_t *)OAM, (const uint16_t *)REG_BASE});
    /* A 3DSX gets far less memory under a homebrew loader than an emulator
     * hands out, and the first thing AgbMain does is allocate. Record what is
     * left before and during the first frames so a failure there is readable. */
    KeepGameAtStart();
    CtrPlatform_ReportMemory("before AgbMain");
    if (setjmp(sAgbMainEntry) == 0)
        CtrLog_Write(CTR_LOG_GAME, "entering AgbMain");
    else
        CtrLog_Write(CTR_LOG_GAME, "soft reset %lu: entering AgbMain again", (unsigned long)sResets);
    AgbMain();
    CtrPlatform_Fatal("AgbMain returned");
}

/*
 * Called by the portable VBlankIntrWait. One iteration per frame: present,
 * then acquire the next frame's input. A closed lid, a Home menu exit or the
 * exit chord ends the process from here, which is the only place where the
 * game is at a consistent point.
 */
/*
 * A 64-column background only fills the viewport if the whole chain does its
 * part: the field camera has to draw the extra columns, CopyBgTilemapBufferToVram
 * has to move both screenblocks, and the renderer has to read the second one.
 * Counting live entries per screenblock says which link is missing.
 */
static void ReportTilemapFill(void)
{
    char text[128];
    int used = 0;

    for (unsigned bg = 1; bg <= 3 && used < (int)sizeof(text) - 24; ++bg)
    {
        unsigned control = GetGpuReg(REG_OFFSET_BG0CNT + bg * 2);
        const u16 *map = (const u16 *)(VRAM_ + ((control >> 8) & 31) * 0x800);
        unsigned first = 0, second = 0;

        for (unsigned i = 0; i < 1024; ++i)
        {
            if (map[i]) ++first;
            if (map[1024 + i]) ++second;
        }
        used += snprintf(text + used, sizeof(text) - used, "BG%u %u/%u ", bg, first, second);
    }
    CtrLog_Write(CTR_LOG_VIDEO, "tilemap entries per screenblock: %s", text);
}

void CtrGame_WaitFrame(void)
{
    static MainCallback lastCallback;
    static u32 nextReport;

    /* The boot crash reported on hardware happens between AgbMain and the first
     * logged frame, so the first frames are traced step by step. */
    if (sFrames < 4)
        CtrLog_Write(CTR_LOG_GAME, "frame %lu: game reached VBlank wait", (unsigned long)sFrames);
    Port_ProfScene((const void *)gMain.callback2);
    CtrPlatform_EndFrame();
    if (sFrames < 4)
    {
        CtrPlatform_ReportMemory("after present");
        CtrLog_Write(CTR_LOG_VIDEO, "DISPCNT=%04x BG0=%04x BG1=%04x BG2=%04x BG3=%04x",
                     GetGpuReg(REG_OFFSET_DISPCNT), GetGpuReg(REG_OFFSET_BG0CNT),
                     GetGpuReg(REG_OFFSET_BG1CNT), GetGpuReg(REG_OFFSET_BG2CNT),
                     GetGpuReg(REG_OFFSET_BG3CNT));
    }
    if (!CtrPlatform_BeginFrame())
    {
        CtrPlatform_Shutdown();
        exit(0);
    }
    CtrPlatform_Diagnostic(sFrames, 0, 0);
#ifdef CTR_TEST_BATTLE
    /* Azahar measurement aid (-DCTR_TEST_BATTLE=frame): a wild battle from
     * wherever the save stands, once the field is up at that frame. */
    {
        static bool started;

        if (!started && sFrames >= CTR_TEST_BATTLE && gMain.callback2 == CB2_Overworld)
        {
            started = true;
            CreateScriptedWildMon(SPECIES_ZIGZAGOON, 5, 0);
            BattleSetup_StartScriptedWildBattle();
        }
    }
#endif
    /* Touch was just scanned and the game has not read its keys yet. */
    {
        uint64_t start = CtrPlatform_Ticks();

        CtrBottom_Frame();
        CtrPlatform_NoteBottom(CtrPlatform_TickMs(CtrPlatform_Ticks() - start));
    }

    /* Which screen the game is on, and what the resource layer is doing for it.
     * A callback change is the only unambiguous marker of progress in the log. */
    if (gMain.callback2 != lastCallback || sFrames >= nextReport)
    {
        const struct CtrAssetStats *stats = CtrAssets_GetStats();

        lastCallback = gMain.callback2;
        nextReport = sFrames + 600;
        CtrLog_Write(CTR_LOG_VIDEO, "DISPCNT=%04x BG0=%04x BG1=%04x BG2=%04x BG3=%04x",
                     GetGpuReg(REG_OFFSET_DISPCNT), GetGpuReg(REG_OFFSET_BG0CNT),
                     GetGpuReg(REG_OFFSET_BG1CNT), GetGpuReg(REG_OFFSET_BG2CNT),
                     GetGpuReg(REG_OFFSET_BG3CNT));
        ReportTilemapFill();
        {
            /* What the renderer actually reads: the applied register bank, not
             * the buffered value the game wrote. */
            const u16 *applied = (const u16 *)REG_BASE;
            CtrLog_Write(CTR_LOG_VIDEO, "applied BG0=%04x BG1=%04x BG2=%04x BG3=%04x "
                         "HOFS=%u/%u/%u/%u VOFS=%u/%u/%u/%u",
                         applied[0x08 / 2], applied[0x0a / 2], applied[0x0c / 2], applied[0x0e / 2],
                         applied[0x10 / 2], applied[0x14 / 2], applied[0x18 / 2], applied[0x1c / 2],
                         applied[0x12 / 2], applied[0x16 / 2], applied[0x1a / 2], applied[0x1e / 2]);
            CtrLog_Write(CTR_LOG_VIDEO, "WIN0H=%04x WIN1H=%04x WIN0V=%04x WIN1V=%04x "
                         "WININ=%04x WINOUT=%04x",
                         applied[0x40 / 2], applied[0x42 / 2], applied[0x44 / 2],
                         applied[0x46 / 2], applied[0x48 / 2], applied[0x4a / 2]);
        }
        /* The save blocks exist only once the game has allocated them. Until
         * then gSaveBlock1Ptr is null, and reading through it faults on
         * hardware, where page zero is unmapped; an emulator that maps it
         * hides the bug. */
        const struct SaveBlock1 *save = gSaveBlock1Ptr;

        CtrLog_Write(CTR_LOG_GAME,
                     "frame=%lu cb2=%08lx state=%u map=%d/%d assets=%lu/%lu %luKiB hit=%lu miss=%lu evict=%lu err=%lu",
                     (unsigned long)sFrames, (unsigned long)(uintptr_t)gMain.callback2,
                     gMain.state, save ? save->location.mapGroup : -1,
                     save ? save->location.mapNum : -1,
                     (unsigned long)stats->loads, (unsigned long)stats->entries,
                     (unsigned long)(stats->bytes >> 10), (unsigned long)stats->hits,
                     (unsigned long)stats->misses, (unsigned long)stats->evictions,
                     (unsigned long)stats->errors);
    }
}
