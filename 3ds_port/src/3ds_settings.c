/*
 * The port's own settings, kept on the SD card next to the save
 * (sdmc:/3ds/emerald3ds/settings.txt) and written as soon as one changes.
 *
 * They are not part of the game's save: the GBA save block has no room for
 * them, and a setting that only sticks once the game is saved is not what a
 * player expects from a console option. One "key=value" per line; unknown
 * keys are ignored, so older and newer builds can share the file.
 */
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include "3ds_platform.h"

#define SETTINGS_FILE "settings.txt"

/* The voxel overworld is opt-in: the classic 2D field is the default. */
static bool sVoxel = false;

/*
 * The voxel camera: a few degrees either way of its 40 degree pitch, and a
 * few zoom steps (percent of the adapted distance, higher is closer). The
 * renderer builds what the camera actually sees at any of them (its frustum,
 * ctr_voxel.c); further out or flatter would only cost more chunks per frame.
 */
static const int sPitches[] = {34, 37, 40, 43, 46};
static const int sZooms[] = {90, 100, 110, 120};
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static int sPitch = 2, sZoom = 1;
/* The HD-2D tilt-shift blur over the voxel picture (3ds_video.c): on unless
 * turned off. */
static bool sVoxelBlur = true;
/* Battles in front of the voxel world rather than the GBA's scenery
 * (3ds_video.c, RenderBattleWorld): off unless turned on. */
static bool sVoxelBattle = false;
/* The FPS counter in the top screen's corner (3ds_video.c): off unless
 * turned on. */
static bool sShowFps = false;

static int Find(const int *values, int count, int value, int fallback)
{
    for (int i = 0; i < count; ++i)
        if (values[i] == value)
            return i;
    return fallback;
}

void CtrSettings_Load(void)
{
    FILE *file = CtrFs_OpenData(SETTINGS_FILE, "r");
    char line[64];

    if (file == NULL)
    {
        CtrLog_Write(CTR_LOG_FS, "settings: none yet, defaults (voxel=0 fps=0)");
        return;
    }
    while (fgets(line, sizeof(line), file))
    {
        int value;

        if (strncmp(line, "voxel=", 6) == 0)
            sVoxel = line[6] == '1';
        else if (sscanf(line, "voxel_pitch=%d", &value) == 1)
            sPitch = Find(sPitches, COUNT(sPitches), value, sPitch);
        else if (sscanf(line, "voxel_zoom=%d", &value) == 1)
            sZoom = Find(sZooms, COUNT(sZooms), value, sZoom);
        else if (strncmp(line, "voxel_blur=", 11) == 0)
            sVoxelBlur = line[11] != '0';
        else if (strncmp(line, "voxel_battle=", 13) == 0)
            sVoxelBattle = line[13] == '1';
        else if (strncmp(line, "fps=", 4) == 0)
            sShowFps = line[4] == '1';
    }
    fclose(file);
    CtrLog_Write(CTR_LOG_FS, "settings: voxel=%d pitch=%d zoom=%d blur=%d battle=%d fps=%d",
                 sVoxel ? 1 : 0, sPitches[sPitch], sZooms[sZoom], sVoxelBlur ? 1 : 0,
                 sVoxelBattle ? 1 : 0, sShowFps ? 1 : 0);
}

/*
 * Written by a thread of its own. Opening, truncating and closing a file on
 * the SD card took a second on hardware, and the settings are changed from
 * the bottom screen in the middle of play: every tap on an option froze the
 * game for that long. The text is composed here and handed over; the thread
 * writes the latest one, waiting on the card while the game runs.
 */
static Thread sSaver;
static LightEvent sSaveWake;
static LightLock sSaveLock = 1;
static char sSaveText[160];
static bool sSavePending, sSaveQuit;

static void WriteText(const char *text)
{
    FILE *file = CtrFs_OpenData(SETTINGS_FILE, "w");

    if (file == NULL)
    {
        CtrLog_Write(CTR_LOG_ERROR, "settings: could not open %s", SETTINGS_FILE);
        return;
    }
    bool ok = fputs(text, file) >= 0;
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
        CtrLog_Write(CTR_LOG_ERROR, "settings: write failed for %s", SETTINGS_FILE);
}

static void Saver(void *arg)
{
    char text[sizeof(sSaveText)];

    (void)arg;
    for (;;)
    {
        bool pending, quit;

        LightEvent_Wait(&sSaveWake);
        LightLock_Lock(&sSaveLock);
        pending = sSavePending;
        quit = sSaveQuit;
        sSavePending = false;
        memcpy(text, sSaveText, sizeof(text));
        LightLock_Unlock(&sSaveLock);
        if (pending)
            WriteText(text);
        if (quit)
            break;
    }
}

static void Save(void)
{
    char text[sizeof(sSaveText)] = {0};

    /* Settings and shutdown are submitted by the game thread. */
    if (sSaveQuit)
        return;

    snprintf(text, sizeof(text),
             "voxel=%d\nvoxel_pitch=%d\nvoxel_zoom=%d\nvoxel_blur=%d\nvoxel_battle=%d\nfps=%d\n",
             sVoxel ? 1 : 0, sPitches[sPitch], sZooms[sZoom], sVoxelBlur ? 1 : 0, sVoxelBattle ? 1 : 0,
             sShowFps ? 1 : 0);
    if (sSaver == NULL)
    {
        s32 priority = 0x30;

        LightEvent_Init(&sSaveWake, RESET_ONESHOT);
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        sSaver = threadCreate(Saver, NULL, 8 * 1024, priority + 1 <= 0x3F ? priority + 1 : 0x3F,
                              -2, false);
        if (sSaver == NULL)
        {
            WriteText(text); /* no thread: written here, as before */
            return;
        }
    }
    LightLock_Lock(&sSaveLock);
    memcpy(sSaveText, text, sizeof(text));
    sSavePending = true;
    LightLock_Unlock(&sSaveLock);
    LightEvent_Signal(&sSaveWake);
}

void CtrSettings_Shutdown(void)
{
    LightLock_Lock(&sSaveLock);
    sSaveQuit = true;
    LightLock_Unlock(&sSaveLock);
    if (sSaver == NULL)
        return;
    LightEvent_Signal(&sSaveWake);
    threadJoin(sSaver, U64_MAX);
    threadFree(sSaver);
    sSaver = NULL;
}

bool CtrSettings_Voxel(void)
{
    return sVoxel;
}

void CtrSettings_SetVoxel(bool on)
{
    if (sVoxel == on)
        return;
    sVoxel = on;
    Save();
    CtrLog_Write(CTR_LOG_FS, "settings: voxel=%d", on ? 1 : 0);
}

int CtrSettings_VoxelPitch(void)
{
    return sPitches[sPitch];
}

int CtrSettings_VoxelZoom(void)
{
    return sZooms[sZoom];
}

/* One step up or down, wrapping round like the game's own options. */
static void Step(int *index, int count, int direction)
{
    *index = (*index + (direction < 0 ? count - 1 : 1)) % count;
    Save();
}

void CtrSettings_StepVoxelPitch(int direction)
{
    Step(&sPitch, COUNT(sPitches), direction);
}

void CtrSettings_StepVoxelZoom(int direction)
{
    Step(&sZoom, COUNT(sZooms), direction);
}

bool CtrSettings_VoxelBlur(void)
{
    return sVoxelBlur;
}

void CtrSettings_SetVoxelBlur(bool on)
{
    if (sVoxelBlur == on)
        return;
    sVoxelBlur = on;
    Save();
}

bool CtrSettings_VoxelBattle(void)
{
    return sVoxelBattle;
}

void CtrSettings_SetVoxelBattle(bool on)
{
    if (sVoxelBattle == on)
        return;
    sVoxelBattle = on;
    Save();
}

bool CtrSettings_ShowFps(void)
{
    return sShowFps;
}

void CtrSettings_SetShowFps(bool on)
{
    if (sShowFps == on)
        return;
    sShowFps = on;
    Save();
}
