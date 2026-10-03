/*
 * Entry point: the real game, driven by its own AgbMain loop.
 *
 * Phases 1-4 ran a bootstrap that owned the frame loop and called selected
 * game routines. From here the ownership is inverted, exactly as on hardware:
 * AgbMain runs forever and every iteration blocks in WaitForVBlank ->
 * VBlankIntrWait, which is where the 3DS backend presents the frame, pumps APT
 * and scans HID. Nothing about the game's control flow is re-implemented.
 */

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
#include "constants/party_menu.h"
#include "port_platform.h"
#include "port_log.h"

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
static void CaptureLineScroll(void)
{
    const struct ScanlineEffect *effect = &gScanlineEffect;
    uintptr_t reg = (uintptr_t)effect->dmaDest - (uintptr_t)REG_ADDR_BG0HOFS;
    bool wide = ((effect->dmaControl >> 16) & DMA_32BIT) != 0;
    bool active = effect->state != 0 && effect->state != 3 && effect->dmaDest;

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
    /* Where the VBlank handler's ~5 ms a frame go (it is the largest fixed
     * cost of the frame): averaged, logged every 600 frames. */
    static uint64_t sSum[6];
    static unsigned sCount;
    uint64_t t0 = CtrPlatform_Ticks(), t1, t2, t3, t4, t5;

    CtrEmu_BeginVBlank();
    t1 = CtrPlatform_Ticks();
    /*
     * The GBA VBlank handler is the game's own VBlankIntr: it runs the vblank
     * callback, applies buffered GPU registers and services DMA3 requests. It
     * must observe the same enable bits the game sets.
     */
    if (REG_IME && (REG_IE & INTR_FLAG_VBLANK) && gIntrTable[INTR_INDEX_VBLANK])
        gIntrTable[INTR_INDEX_VBLANK]();
    t2 = CtrPlatform_Ticks();
    CtrEmu_EndVBlank();
    t3 = CtrPlatform_Ticks();
    UpdateStage();
    CaptureLineScroll();
    CaptureLineRegisters();
    t4 = CtrPlatform_Ticks();
    CheckAudioRate();
    SampleAudioStats();
    t5 = CtrPlatform_Ticks();
    ++sFrames;
    sSum[0] += t1 - t0; sSum[1] += t2 - t1; sSum[2] += t3 - t2;
    sSum[3] += t4 - t3; sSum[4] += t5 - t4;
    if (++sCount == 600)
    {
        PORT_LOG("PROF vblank avg ms: begin=%.2f intr=%.2f end=%.2f stage+lines=%.2f audio=%.2f\n",
                 sSum[0] * 1000.0 / 268111856.0 / sCount, sSum[1] * 1000.0 / 268111856.0 / sCount,
                 sSum[2] * 1000.0 / 268111856.0 / sCount, sSum[3] * 1000.0 / 268111856.0 / sCount,
                 sSum[4] * 1000.0 / 268111856.0 / sCount);
        memset(sSum, 0, sizeof(sSum));
        sCount = 0;
    }
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

/* AgbMain never returns, so a reset request restarts the process state here. */
void CtrGame_Frame(void)
{
}

void CtrGame_Init(void)
{
    const struct CtrAssetStats *stats;

    if (sStarted)
    {
        /* SoftReset arrives through the platform hook; AgbMain owns the loop
         * and cannot be re-entered, so the request is reported, not faked. */
        CTR_STUB("RESET", "soft reset during AgbMain is not supported yet");
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
    CtrPlatform_ReportMemory("before AgbMain");
    CtrLog_Write(CTR_LOG_GAME, "entering AgbMain");
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
