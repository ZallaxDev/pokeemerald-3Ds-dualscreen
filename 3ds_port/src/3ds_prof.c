/*
 * Frame profiler (compat/port_prof.h). Sections add their ticks to the frame
 * being made; at present the frame is reported and cleared. A frame that took
 * longer than the display allows is logged whole, section by section, with
 * the screen it was on, and every few seconds the averages are logged: which
 * part of the engine a hitch came from is then in the SD log.
 */
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "3ds_platform.h"
#include "../compat/port_prof.h"

/* A frame over this much work is logged on its own (the display gives 16.7). */
#define PROF_SLOW_MS 15.0f
#define PROF_SLOW_LINES 4000
#define PROF_AVERAGE_FRAMES 300

static uint32_t sFrame[PORT_PROF_COUNT];
static uint64_t sSum[PORT_PROF_COUNT];
static float sWorkSum, sWorkPeak;
static unsigned sFrames, sSlowLines;
static uintptr_t sScene;

static const char *const sNames[PORT_PROF_COUNT] = {
    "task", "spr", "fade", "vbl", "vcb", "dma3", "mix",
    "io", "lz", "copy", "lines", "pal", "lay", "draw", "end",
};

/* Only when there is a log to report to: every section is two tick reads,
 * and the engine's copies alone are hundreds of sections a frame. */
static int sOn = -1;

static bool ProfOn(void)
{
    if (sOn < 0)
        sOn = CtrLog_Enabled();
    return sOn;
}

uint32_t Port_ProfTick(void)
{
    return ProfOn() ? (uint32_t)svcGetSystemTick() : 0;
}

void Port_ProfAdd(unsigned section, uint32_t start)
{
    if (ProfOn() && section < PORT_PROF_COUNT)
        sFrame[section] += (uint32_t)svcGetSystemTick() - start;
}

void Port_ProfScene(const void *callback2)
{
    sScene = (uintptr_t)callback2;
}

static int Sections(char *text, size_t size, const uint32_t *ticks, const uint64_t *sums, unsigned frames)
{
    int used = 0;

    for (unsigned i = 0; i < PORT_PROF_COUNT && used < (int)size; ++i)
    {
        float ms = sums ? (float)(sums[i] * 1000.0 / SYSCLOCK_ARM11 / frames)
                        : (float)(ticks[i] * 1000.0 / SYSCLOCK_ARM11);

        if (ms >= 0.05f)
            used += snprintf(text + used, size - used, " %s=%.2f", sNames[i], ms);
    }
    return used;
}

void CtrProf_EndFrame(float workMs, float waitMs, uint64_t frame)
{
    char text[400];

    if (!ProfOn())
        return;
    if (workMs > PROF_SLOW_MS && sSlowLines < PROF_SLOW_LINES)
    {
        ++sSlowLines;
        Sections(text, sizeof(text), sFrame, NULL, 0);
        CtrLog_Write(CTR_LOG_VIDEO, "PROF slow f=%llu cb2=%08lx work=%.2f wait=%.2f game=%.2f:%s",
                     (unsigned long long)frame, (unsigned long)sScene, workMs, waitMs,
                     CtrPlatform_GetTiming()->gameMs, text);
    }
    for (unsigned i = 0; i < PORT_PROF_COUNT; ++i)
        sSum[i] += sFrame[i];
    memset(sFrame, 0, sizeof(sFrame));
    sWorkSum += workMs;
    if (workMs > sWorkPeak) sWorkPeak = workMs;
    if (++sFrames == PROF_AVERAGE_FRAMES)
    {
        Sections(text, sizeof(text), NULL, sSum, sFrames);
        CtrLog_Write(CTR_LOG_VIDEO, "PROF avg f=%llu cb2=%08lx work=%.2f peak=%.2f:%s",
                     (unsigned long long)frame, (unsigned long)sScene, sWorkSum / sFrames,
                     sWorkPeak, text);
        memset(sSum, 0, sizeof(sSum));
        sWorkSum = sWorkPeak = 0.0f;
        sFrames = 0;
    }
}
