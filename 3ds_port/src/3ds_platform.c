#include <3ds.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "3ds_platform.h"
#include "3ds_data.h"
#include "3ds_audio.h"
#include "3ds_video.h"
#include "3ds_assets.h"
#include "voxel/ctr_voxel.h"
/* The bottom-screen diagnostic overlay: off, and no longer toggled by SELECT.
 * The figures it showed are still logged periodically to the SD card. */
static bool sOverlay = false;

static CtrPlatformHooks sHooks;
static aptHookCookie sAptHook;
static bool sInitialized, sExit, sReset, sWaiting;
static uint64_t sFrames, sStart, sLastFrame, sFrameMs;
static uint64_t sWorkStart, sLastTick, sExitStart;
static CtrTiming sTiming;

static void Lifecycle(APT_HookType type, void *parameter)
{
    (void)parameter;
    if (type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP)
        CtrLog_Write(CTR_LOG_BOOT, "suspend/sleep");
    else if (type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP)
    {
        CtrInput_Clear();
        sLastFrame = CtrPlatform_Milliseconds();
        sLastTick = sExitStart = 0;
        CtrLog_Write(CTR_LOG_BOOT, "resume");
    }
    else if (type == APTHOOK_ONEXIT)
        sExit = true;
}

uint64_t CtrPlatform_Milliseconds(void)
{
    return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000);
}

uint64_t CtrPlatform_Ticks(void)
{
    return svcGetSystemTick();
}

float CtrPlatform_TickMs(uint64_t ticks)
{
    return (float)(ticks * 1000.0 / SYSCLOCK_ARM11);
}

uint64_t CtrPlatform_FrameCount(void)
{
    return sFrames;
}

const CtrTiming *CtrPlatform_GetTiming(void) { return &sTiming; }
void CtrPlatform_NoteBottom(float ms) { sTiming.bottomMs = ms; }

bool CtrPlatform_Init(void)
{
    gfxInitDefault();
    /* A New 3DS runs the application core at 804 MHz with its L2 cache when
     * asked; an Old 3DS ignores this. */
    osSetSpeedupEnable(true);
    CtrLog_Init();
    sInitialized = true;
    atexit(CtrPlatform_Shutdown);
    aptHook(&sAptHook, Lifecycle, NULL);
    CtrInput_Clear();
    sFrames = 0;
    sStart = sLastFrame = CtrPlatform_Milliseconds();
    CtrLog_Write(CTR_LOG_VIDEO, "diagnostic console; game renderer will use GPU");
    CtrLog_Write(CTR_LOG_INPUT, "HID: held/down/up, touch, raw circle pad");
    if (!CtrFs_Init())
    {
        CtrLog_Write(CTR_LOG_ERROR, "platform init: filesystem failed");
        return false;
    }
    CtrSettings_Load();
    if (!CtrData_Init())
        CtrPlatform_ShowDataError(CtrData_ErrorTitle(), CtrData_ErrorDetail());
    if (!CtrVideo_Init())
    {
        CtrLog_Write(CTR_LOG_ERROR, "platform init: GPU video failed");
        return false;
    }
    CtrLog_SetOverlay(sOverlay);
    /* A console without dumped DSP firmware still plays; it just plays silent.
     * CtrAudio_Init says so in the log and the game is left running. */
    CtrAudio_Init();
    return true;
}

void CtrPlatform_SetHooks(const CtrPlatformHooks *hooks)
{
    if (hooks)
        sHooks = *hooks;
    else
        memset(&sHooks, 0, sizeof(sHooks));
}

/* Free memory in the two pools a 3DSX actually competes for: the application
 * heap newlib allocates from, and the linear heap the GPU can address. */
void CtrPlatform_ReportMemory(const char *stage)
{
    struct mallinfo info = mallinfo();

    CtrLog_Write(CTR_LOG_BOOT, "memory %s: heap used=%u free=%u, linear free=%lu, VRAM free=%lu",
                 stage, (unsigned)info.uordblks, (unsigned)info.fordblks,
                 (unsigned long)linearSpaceFree(), (unsigned long)vramSpaceFree());
}

void CtrPlatform_RequestExit(void) { sExit = true; }
void CtrPlatform_RequestReset(void) { sReset = true; }

bool CtrPlatform_BeginFrame(void)
{
    if (sExit || !aptMainLoop())
        return false;
    sWorkStart = svcGetSystemTick();
    CtrInput_Scan();
    const CtrInput *input = CtrInput_Get();
    /* Leave START and SELECT individually available to ReadKeys. Only the
     * deliberate one-second chord exits the game. */
    if ((input->physicalHeld & (CTR_KEY_START | CTR_KEY_SELECT)) == (CTR_KEY_START | CTR_KEY_SELECT))
    {
        if (!sExitStart) sExitStart = CtrPlatform_Milliseconds();
        if (CtrPlatform_Milliseconds() - sExitStart >= 1000)
        {
            CtrLog_Write(CTR_LOG_INPUT, "START+SELECT: exit");
            return false;
        }
    }
    else sExitStart = 0;
    if (input->resetDown)
        CtrPlatform_RequestReset();
    if (sReset)
    {
        sReset = false;
        CtrLog_Write(CTR_LOG_BOOT, "logical reset");
        if (sHooks.reset)
            sHooks.reset();
        else
            CTR_UNIMPLEMENTED("reset hook");
    }
    return true;
}

void CtrPlatform_EndFrame(void)
{
    if (sWaiting)
        return;
    sWaiting = true;
    uint64_t endStart = svcGetSystemTick();
    if (sHooks.audioFrame)
        sHooks.audioFrame();
    /* Latch the logical frame BEFORE GPU reads its registers/OAM/palette.
     * C3D owns display pacing and swapping; no second VBlank wait. */
    if (sHooks.vblank)
        sHooks.vblank();
    uint64_t presentStart = svcGetSystemTick();
    /* Before the present, which reads them: its DROP line is about the frame
     * that just ran, and the voxel builds after FrameEnd estimate the next. */
    sTiming.gameMs = (endStart - sWorkStart) * 1000.0f / SYSCLOCK_ARM11;
    sTiming.vblankMs = (presentStart - endStart) * 1000.0f / SYSCLOCK_ARM11;
    /* FrameEnd(0) also flushes the console's linear LCD buffer. Calling
     * gfxFlushBuffers here would flush BOTH screens again, not just bottom. */
    if (sHooks.videoPresent)
        sHooks.videoPresent();
    ++sFrames;
    uint64_t now = CtrPlatform_Milliseconds();
    sFrameMs = now - sLastFrame;
    sLastFrame = now;
    uint64_t tick = svcGetSystemTick();
    sTiming.frameMs = sLastTick ? (tick - sLastTick) * 1000.0 / SYSCLOCK_ARM11 : 0;
    sLastTick = tick;
    sTiming.workMs = (tick - sWorkStart) * 1000.0 / SYSCLOCK_ARM11;
    sTiming.workMs -= CtrVideo_GetStats()->waitMs;
    if (sTiming.workMs < 0) sTiming.workMs = 0;
    if (sTiming.workMs > sTiming.peakWorkMs) sTiming.peakWorkMs = sTiming.workMs;
    if (sTiming.workMs > 1000.0f / 60) ++sTiming.slowFrames;
    sWaiting = false;
}

void CtrPlatform_Diagnostic(uint32_t frames, uint32_t aPresses, uint32_t checks)
{
    const CtrInput *input = CtrInput_Get();
    if (sFrames % 30 != 0 || !sOverlay) return;
    const CtrVideoStats *video = CtrVideo_GetStats();
    const struct CtrAssetStats *assets = CtrAssets_GetStats();
    const CtrAudioStats *audio = CtrAudio_Stats();
    const CtrVoxelStats *voxel = CtrVoxel_GetStats();
    char text[1020];
    snprintf(text, sizeof(text),
        "Pokemon Emerald 3Ds Dual Screen\n"
        "FPS: %5.1f FRAME: %-10lu\n"
        "Frame:%6.2f Work:%6.2f ms\n"
        "Peak:%6.2f Slow:%-8lu\n"
        "CPU:%6.2f ms GPU:%6.2f ms\n"
        "Assets:%4lu/%lu  %lu/%lu KiB\n"
        "Hit:%-7lu Miss:%-7lu\n"
        "Evict:%-5lu Err:%-5lu Peak:%lu KiB\n"
        "Tiles:%4lu Upload:%4lu Verr:%lu\n"
        "Audio:%s q%lu/%d u:%-4lu d:%lu\n"
        "Mix:%5.2f Sub:%4.2f ms V:%-2lu\n"
        "Song:%-4lu Peak:%-5lu %.0f Hz\n"
        "Held:%03x Down:%03x Touch:%d\n"
        "Voxel:%-7s atl%lu msh%lu ch%lu/%lu gap%lu\n"
        "Vtx:%-6lu spr%-5lu drop%lu\n"
        "Lin:%4lu Vram:%4lu KiB\n"
        "Voxel err:%lu\n"
        "Mesh:%5.1f Peak:%6.1f ms\n"
        "Elapsed:%llu s\n"
        "SELECT: overlay  X: reset\n"
        "START+SELECT 1s: exit",
        video->fps, (unsigned long)frames,
        sTiming.frameMs, sTiming.workMs, sTiming.peakWorkMs, (unsigned long)sTiming.slowFrames,
        video->cpuMs, video->gpuMs,
        (unsigned long)assets->loads, (unsigned long)assets->entries,
        (unsigned long)(assets->bytes >> 10), (unsigned long)(assets->budget >> 10),
        (unsigned long)assets->hits, (unsigned long)assets->misses,
        (unsigned long)assets->evictions, (unsigned long)assets->errors,
        (unsigned long)(assets->peakBytes >> 10),
        (unsigned long)video->tiles, (unsigned long)video->uploads, (unsigned long)video->errors,
        CtrAudio_Available() ? "on " : "off",
        (unsigned long)audio->queued, CTR_AUDIO_TARGET_DEPTH,
        (unsigned long)audio->underruns, (unsigned long)audio->drops,
        audio->mixMs, audio->submitMs, (unsigned long)audio->voices,
        (unsigned long)audio->song, (unsigned long)audio->peak, audio->rateHz,
        input->held, input->down, input->touchActive,
        CtrVoxel_Status(),
        (unsigned long)voxel->atlasRebuilds, (unsigned long)voxel->meshRebuilds,
        (unsigned long)voxel->visibleChunks, (unsigned long)voxel->chunks,
        (unsigned long)voxel->chunksMissing,
        (unsigned long)voxel->vertices, (unsigned long)voxel->spriteUpdates,
        (unsigned long)voxel->dropped,
        voxel->linearFree >> 10, voxel->vramFree >> 10,
        (unsigned long)voxel->errors,
        voxel->meshMs, voxel->meshPeakMs,
        (unsigned long long)((sLastFrame - sStart) / 1000));
    (void)aPresses; (void)checks;
    CtrLog_DrawOverlay(text);
}

void CtrPlatform_Shutdown(void)
{
    if (!sInitialized)
        return;
    sInitialized = false;
    CtrSettings_Shutdown();
    if (sHooks.shutdown)
        sHooks.shutdown();
    CtrVideo_Shutdown();
    CtrAudio_Shutdown();
    CtrLog_Write(CTR_LOG_BOOT, "clean shutdown, platform frames=%llu", (unsigned long long)sFrames);
    aptUnhook(&sAptHook);
    CtrFs_Shutdown();
    CtrLog_Close();
    gfxExit();
}

/* Before the GPU compositor exists: a plain text screen on the top LCD that
 * tells the player what to do, until START. */
void CtrPlatform_ShowDataError(const char *title, const char *detail)
{
    PrintConsole console;

    consoleInit(GFX_TOP, &console);
    consoleSelect(&console);
    consoleClear();
    printf("\x1b[2;2HPokemon Emerald 3Ds Dual Screen\n\n\x1b[31m %s\x1b[0m\n\n", title);
    for (const char *line = detail; line && *line;)
    {
        const char *end = strchr(line, '\n');
        int length = end ? (int)(end - line) : (int)strlen(line);
        printf(" %.*s\n", length, line);
        line = end ? end + 1 : NULL;
    }
    printf("\n\n Press START to exit.");
    while (aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    exit(EXIT_FAILURE);
}

void CtrPlatform_Fatal(const char *reason)
{
    CtrLog_Write(CTR_LOG_ERROR, "%s", reason);
    CtrLog_ShowFatal(reason);
    /* Keep the error visible and lifecycle responsive, without claiming PASS. */
    while (aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    exit(EXIT_FAILURE);
}

/* Marks along a stretch of game code (see port_platform.h): what each step of
 * a map load costs on the console, which an emulator cannot say. */
void Port_ProfileMark(const char *label)
{
    static uint64_t sLast;
    static unsigned sLogged;
    uint64_t now = svcGetSystemTick();
    float ms = sLast ? (float)((now - sLast) * 1000.0 / SYSCLOCK_ARM11) : 0.0f;

    sLast = now;
    if (ms >= 0.8f && ms < 200.0f && sLogged < 400)
    {
        ++sLogged;
        CtrLog_Write(CTR_LOG_VIDEO, "PROF %s %.1f ms", label, ms);
    }
}

/* Buckets along a path that runs every frame (the game's VBlank handler):
 * Port_ProfBegin starts the clock, Port_ProfAcc(name) charges the time since
 * the previous call to `name`; averaged and logged every 600 frames. */
#define PROF_BUCKETS 12
static struct { const char *name; uint64_t ticks; } sProf[PROF_BUCKETS];
static uint64_t sProfLast;
static unsigned sProfFrames;

void Port_ProfBegin(void)
{
    if (sProfFrames >= 600)
    {
        char text[256];
        size_t used = 0;

        for (unsigned i = 0; i < PROF_BUCKETS && sProf[i].name != NULL && used < sizeof(text) - 32; ++i)
        {
            used += (size_t)snprintf(text + used, sizeof(text) - used, " %s=%.2f", sProf[i].name,
                                     sProf[i].ticks * 1000.0 / SYSCLOCK_ARM11 / sProfFrames);
            sProf[i].ticks = 0;
        }
        CtrLog_Write(CTR_LOG_VIDEO, "PROF vblank handler (ms/frame):%s", text);
        sProfFrames = 0;
    }
    ++sProfFrames;
    sProfLast = svcGetSystemTick();
}

void Port_ProfAcc(const char *name)
{
    uint64_t now = svcGetSystemTick();

    for (unsigned i = 0; i < PROF_BUCKETS; ++i)
    {
        if (sProf[i].name == name || sProf[i].name == NULL)
        {
            sProf[i].name = name;
            sProf[i].ticks += now - sProfLast;
            break;
        }
    }
    sProfLast = now;
}
