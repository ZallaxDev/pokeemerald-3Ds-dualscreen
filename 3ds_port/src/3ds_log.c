#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include "3ds_log.h"

static PrintConsole sConsole;
static PrintConsole sOverlay;
static bool sOverlayVisible;
static const char *const sTags[] =
{
    "BOOT", "VIDEO", "INPUT", "FS", "AUDIO", "GAME", "ERROR"
};

/*
 * The SD card is written by a thread of its own. Written from the caller, as
 * it used to be, every flush stalled whatever frame happened to log: an SD
 * write takes anything from a millisecond to half a second on real hardware
 * (the card's own housekeeping), and an error flushed at once, so a failure
 * logged every frame brought the game to a crawl by itself. Callers now only
 * copy the line into a ring; the writer drains it below the game's priority,
 * so it runs while the game waits for the display.
 *
 * Errors and lifecycle lines still reach the card promptly - the writer is
 * woken for them - and the ring is drained on close. A full ring drops lines
 * and says how many, rather than make a caller wait for the card.
 */
#define LOG_RING (32 * 1024)
#define LOG_FLUSH_MS 2000

static FILE *sFile;
static char sRing[LOG_RING];
static unsigned sHead, sTail;   /* written at sHead, drained from sTail */
static unsigned sDropped;
static LightLock sLock;
/* Held for a whole drain, so that two threads draining inline (no writer
 * thread) never share the chunk buffer or close the file twice; and around
 * the console, which the overlay and a fatal error both draw on. */
static LightLock sDrainLock, sConsoleLock;
static LightEvent sWake;
static Thread sWriter;
static volatile bool sQuit;

static unsigned RingUsed(void)
{
    return (sHead - sTail + LOG_RING) % LOG_RING;
}

static void RingPut(const char *text, unsigned length)
{
    if (length >= LOG_RING - RingUsed())
    {
        ++sDropped;
        return;
    }
    for (unsigned i = 0; i < length; ++i)
    {
        sRing[sHead] = text[i];
        sHead = (sHead + 1) % LOG_RING;
    }
}

/* Everything in the ring, out to the card: by the writer, by the closer, or
 * inline by every caller when there is no writer thread. */
static volatile uint64_t sDrainStart, sDrainEnd;

void CtrLog_LastDrain(float *ms, float *agoMs)
{
    uint64_t start = sDrainStart, end = sDrainEnd, now = svcGetSystemTick();

    /* One still under way: its time so far, ended no time ago. */
    if (start > end)
    {
        *ms = (float)(now - start) * 1000.0f / SYSCLOCK_ARM11;
        *agoMs = 0.0f;
        return;
    }
    *ms = (float)(end - start) * 1000.0f / SYSCLOCK_ARM11;
    *agoMs = end != 0 ? (float)(now - end) * 1000.0f / SYSCLOCK_ARM11 : 1.0e6f;
}

static void Drain(void)
{
    static char chunk[4096];

    LightLock_Lock(&sDrainLock);
    sDrainStart = svcGetSystemTick();
    for (;;)
    {
        unsigned n = 0, dropped;

        LightLock_Lock(&sLock);
        while (sTail != sHead && n < sizeof(chunk))
        {
            chunk[n++] = sRing[sTail];
            sTail = (sTail + 1) % LOG_RING;
        }
        dropped = sDropped;
        sDropped = 0;
        LightLock_Unlock(&sLock);
        if (dropped != 0 && sFile != NULL)
            fprintf(sFile, "[ERROR] log ring full: %u lines dropped\n", dropped);
        if (n == 0)
            break;
        if (sFile != NULL && fwrite(chunk, 1, n, sFile) != n)
        {
            fclose(sFile);
            sFile = NULL;
        }
    }
    if (sFile != NULL && fflush(sFile) != 0)
    {
        fclose(sFile);
        sFile = NULL;
    }
    sDrainEnd = svcGetSystemTick();
    LightLock_Unlock(&sDrainLock);
}

static void Writer(void *arg)
{
    (void)arg;
    while (!sQuit)
    {
        LightEvent_WaitTimeout(&sWake, (s64)LOG_FLUSH_MS * 1000000);
        Drain();
    }
}

/*
 * The bottom screen belongs to the game's own features, so no console is made
 * there. What the game and the platform layer print to stdout/stderr (bios.c
 * reports bad pointers with printf, for one) goes into the log a line at a time
 * instead of onto a screen. A console is only made for a fatal error, or for
 * the diagnostic overlay if it is ever turned back on.
 */
static bool sConsoleReady;
static char sStdLine[256];
static unsigned sStdLength;
static LightLock sStdLock;

static ssize_t StdWrite(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r; (void)fd;
    LightLock_Lock(&sStdLock);
    for (size_t i = 0; i < len; ++i)
    {
        char c = ptr[i];
        if (c != '\n' && c != '\r' && sStdLength < sizeof(sStdLine) - 1)
            sStdLine[sStdLength++] = c;
        if ((c == '\n' || sStdLength == sizeof(sStdLine) - 1) && sStdLength != 0)
        {
            sStdLine[sStdLength] = 0;
            sStdLength = 0;
            CtrLog_Write(CTR_LOG_GAME, "%s", sStdLine);
        }
    }
    LightLock_Unlock(&sStdLock);
    return (ssize_t)len;
}

static const devoptab_t sStdLog = { .name = "log", .write_r = StdWrite };

static void EnsureConsole(void)
{
    if (sConsoleReady) return;
    consoleInit(GFX_BOTTOM, &sConsole);
    sConsoleReady = true;
}

/* Nothing draws the bottom screen until a feature does: leave it black. */
static void ClearBottomScreen(void)
{
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    memset(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), 0, 320 * 240 * 2);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

void CtrLog_Init(void)
{
    s32 priority = 0x30;

    LightLock_Init(&sStdLock);
    ClearBottomScreen();
    devoptab_list[STD_OUT] = &sStdLog;
    devoptab_list[STD_ERR] = &sStdLog;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/emerald3ds", 0777);
    LightLock_Init(&sLock);
    LightLock_Init(&sDrainLock);
    LightLock_Init(&sConsoleLock);
    LightEvent_Init(&sWake, RESET_ONESHOT);
    sHead = sTail = sDropped = 0;
    sQuit = false;
    /* One session per file: the last one is kept as port-prev.log, so the log
     * never grows without bound and a crash is still readable after relaunch. */
    /* No log file unless sdmc:/3ds/emerald3ds/debug.txt exists: players never
     * pay for SD writes; to diagnose, create that file (any content). */
    sFile = NULL;
    {
        FILE *marker = fopen("sdmc:/3ds/emerald3ds/debug.txt", "r");

        if (marker != NULL)
        {
            fclose(marker);
            remove("sdmc:/3ds/emerald3ds/port-prev.log");
            rename("sdmc:/3ds/emerald3ds/port.log", "sdmc:/3ds/emerald3ds/port-prev.log");
            sFile = fopen("sdmc:/3ds/emerald3ds/port.log", "w");
        }
    }
    if (sFile)
    {
        /* The ring already batches; a second buffer would only copy twice. */
        setvbuf(sFile, NULL, _IONBF, 0);
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        sWriter = threadCreate(Writer, NULL, 8 * 1024,
                               priority + 2 <= 0x3F ? priority + 2 : 0x3F, -2, false);
    }
    CtrLog_Write(CTR_LOG_BOOT, "--- new session ---");
    CtrLog_Write(CTR_LOG_FS, "SD log %s", sFile == NULL ? "disabled (no debug.txt) or unavailable"
                                         : sWriter != NULL ? "ready" : "ready, written inline");
}

bool CtrLog_Enabled(void)
{
    return sFile != NULL;
}

void CtrLog_Write(CtrLogCategory category, const char *format, ...)
{
    char message[384];
    char line[416];
    va_list args;
    unsigned tag = (unsigned)category;
    int length;
    /* Without debug.txt there is nowhere for the line to go: a player's
     * console skips the formatting (floats through vfprintf are slow on the
     * ARM11) and the debug-output SVC altogether. */
    if (sFile == NULL)
        return;
    if (tag >= sizeof(sTags) / sizeof(sTags[0]))
        tag = CTR_LOG_ERROR;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    length = snprintf(line, sizeof(line), "[%s] %s\n", sTags[tag], message);
    if (length < 0)
        return;
    if (length >= (int)sizeof(line))
        length = sizeof(line) - 1;
    /* The bottom screen no longer mirrors the log: only the debug output and
     * the SD file receive it, and the screen is left to the overlay. */
    svcOutputDebugString(line, length);
    if (sFile == NULL)
        return;
    LightLock_Lock(&sLock);
    RingPut(line, (unsigned)length);
    LightLock_Unlock(&sLock);
    if (sWriter == NULL)
    {
        /* No thread: write inline, but in batches, not one SD write per line. */
        if (tag == CTR_LOG_BOOT || tag == CTR_LOG_ERROR || RingUsed() > LOG_RING / 2)
            Drain();
    }
    else if (tag == CTR_LOG_BOOT || tag == CTR_LOG_ERROR)
    {
        /* Prompt, but an error logged every frame must not keep the card busy. */
        static uint64_t sLastWake;
        uint64_t now = svcGetSystemTick();

        if (tag == CTR_LOG_BOOT || now - sLastWake > SYSCLOCK_ARM11 / 4)
        {
            sLastWake = now;
            LightEvent_Signal(&sWake);
        }
    }
}

void CtrLog_Close(void)
{
    if (sWriter != NULL)
    {
        sQuit = true;
        LightEvent_Signal(&sWake);
        threadJoin(sWriter, U64_MAX);
        threadFree(sWriter);
        sWriter = NULL;
    }
    Drain();
    if (sFile)
        fclose(sFile);
    sFile = NULL;
}

void CtrLog_SetOverlay(bool enabled)
{
    LightLock_Lock(&sConsoleLock);
    sOverlayVisible = enabled;
    /* Hidden and never shown: no console, nothing on the bottom screen. */
    if (!enabled && !sConsoleReady)
    {
        LightLock_Unlock(&sConsoleLock);
        return;
    }
    EnsureConsole();
    consoleSelect(&sConsole);
    consoleSetWindow(&sConsole, 0, 0, 40, 30);
    consoleClear();
    /* Without the log rows the overlay has the whole screen. */
    sOverlay = sConsole;
    LightLock_Unlock(&sConsoleLock);
}

/* With the log off the screen, a fatal error would otherwise leave it blank. */
void CtrLog_ShowFatal(const char *reason)
{
    LightLock_Lock(&sConsoleLock);
    sOverlayVisible = false;
    EnsureConsole();
    consoleSelect(&sConsole);
    consoleSetWindow(&sConsole, 0, 0, 40, 30);
    consoleClear();
    printf("\x1b[1;1HFATAL: %.32s", reason);
    printf("\x1b[3;1HLog (if debug.txt exists): port.log");
    printf("\x1b[5;1HSTART: exit");
    LightLock_Unlock(&sConsoleLock);
}

void CtrLog_DrawOverlay(const char *text)
{
    if (!sOverlayVisible) return;
    LightLock_Lock(&sConsoleLock);
    consoleSelect(&sOverlay);
    /* libctru flushes BOTH LCD buffers at every newline. Position rows with
     * ANSI instead: the GPU frame-end flush covers the completed overlay. */
    unsigned row = 1;
    while (*text && row <= 30)
    {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        char line[41];
        size_t visible = length < 40 ? length : 40;
        memset(line, ' ', 40);
        memcpy(line, text, visible);
        line[40] = 0;
        printf("\x1b[%u;1H%s", row++, line);
        text += length;
        if (*text == '\n') ++text;
    }
    consoleSelect(&sConsole);
    LightLock_Unlock(&sConsoleLock);
}
