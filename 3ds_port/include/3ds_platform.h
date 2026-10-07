#ifndef CTR_PLATFORM_H
#define CTR_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "3ds_log.h"
#include "3ds_input.h"

/* SDK-free boundary: game translation units never include libctru headers. */
typedef struct {
    float frameMs, workMs, peakWorkMs;
    /* Of the last frame: the game up to its VBlank wait, then audio and the
     * VBlank handler. What remains of workMs is the present. */
    float gameMs, vblankMs;
    /* ... of which the bottom screen, before the game ran (CtrBottom_Frame). */
    float bottomMs;
    uint32_t slowFrames;
} CtrTiming;

typedef struct
{
    void (*vblank)(void);
    void (*videoPresent)(void);
    void (*audioFrame)(void);
    void (*reset)(void);
    void (*shutdown)(void);
} CtrPlatformHooks;

bool CtrPlatform_Init(void);
void CtrPlatform_Shutdown(void);
bool CtrPlatform_BeginFrame(void);
void CtrPlatform_EndFrame(void);
void CtrPlatform_SetHooks(const CtrPlatformHooks *hooks);
void CtrPlatform_RequestReset(void);
void CtrPlatform_RequestExit(void);
void CtrPlatform_Fatal(const char *reason) __attribute__((noreturn));
void CtrPlatform_ShowDataError(const char *title, const char *detail) __attribute__((noreturn));
uint64_t CtrPlatform_Milliseconds(void);
/* Raw ARM11 ticks, for game translation units that cannot include libctru. */
uint64_t CtrPlatform_Ticks(void);
float CtrPlatform_TickMs(uint64_t ticks);
uint64_t CtrPlatform_FrameCount(void);
const CtrTiming *CtrPlatform_GetTiming(void);
void CtrPlatform_NoteBottom(float ms);
void CtrPlatform_Diagnostic(uint32_t gameFrames, uint32_t aPresses, uint32_t checks);
void CtrPlatform_ReportMemory(const char *stage);
/*
 * Threads and locks for the port's own workers, usable from game translation
 * units. A CtrLock is a libctru LightLock. A thread is started detached, one
 * priority step below the caller, on core (-2: the application's default;
 * 1: the system core, asking for its share first, or the default if refused).
 */
typedef int32_t CtrLock;
void CtrLock_Init(CtrLock *lock);
void CtrLock_Lock(CtrLock *lock);
void CtrLock_Unlock(CtrLock *lock);
bool CtrPlatform_StartThread(void (*entry)(void *), void *arg, unsigned stack, int core);
void CtrPlatform_SleepUs(unsigned microseconds);
/* The frame profiler's report and reset, at present (3ds_prof.c). */
void CtrProf_EndFrame(float workMs, float waitMs, uint64_t frame);

bool CtrFs_Init(void);
void CtrFs_Shutdown(void);
FILE *CtrFs_OpenAsset(const char *relativePath);
FILE *CtrFs_OpenData(const char *relativePath, const char *mode);

/* The port's own settings (settings.txt on the SD card, see 3ds_settings.c). */
void CtrSettings_Load(void);
void CtrSettings_Shutdown(void);
bool CtrSettings_Voxel(void);
void CtrSettings_SetVoxel(bool on);
/* Voxel camera pitch in degrees and zoom in percent, from a short fixed list. */
int CtrSettings_VoxelPitch(void);
int CtrSettings_VoxelZoom(void);
void CtrSettings_StepVoxelPitch(int direction);
void CtrSettings_StepVoxelZoom(int direction);
/* The voxel picture's tilt-shift blur, on by default. */
bool CtrSettings_VoxelBlur(void);
void CtrSettings_SetVoxelBlur(bool on);
/* Battles in front of the voxel world (3ds_video.c), off by default. */
bool CtrSettings_VoxelBattle(void);
void CtrSettings_SetVoxelBattle(bool on);
/* The FPS counter on the top screen, off by default. */
bool CtrSettings_ShowFps(void);
void CtrSettings_SetShowFps(bool on);
/* Running without holding B, B then walking (the bottom screen's RUN), off by default. */
bool CtrSettings_RunAlways(void);
void CtrSettings_SetRunAlways(bool on);
/* Settings kept by key (the OPTIONS pages of 3ds_extras.c). */
int CtrSettings_GetInt(const char *key, int fallback);
void CtrSettings_SetInt(const char *key, int value);
/* Fast-forward: game frames per shown frame, 1 to 4 (OPTIONS, ZR/ZL). */
int CtrSettings_Speed(void);
void CtrSettings_StepSpeed(int direction, bool wrap);
/* False on a fast-forwarded frame that is not shown: the sound engine skips
 * it, so music and effects keep their own speed (3ds_sound.c). */
bool CtrPlatform_SoundTick(void);

void CtrGame_Init(void);
/* The game's variables back to how AgbMain first saw them, AgbMain again:
 * returns only when the reset is unavailable. */
void CtrGame_SoftReset(void);
void CtrGame_Frame(void);
void CtrGame_VBlank(void);
uint32_t CtrGame_Frames(void);
uint32_t CtrGame_APresses(void);
uint32_t CtrGame_Checks(void);
/* The field is on screen (CB2_Overworld), whichever way it is drawn. */
bool CtrGame_IsOverworld(void);
bool CtrGame_FieldPoison(void);

/* C identifiers cannot start with '3'. Logs retain the plan's 3DS_STUB tag. */
#define CTR_STUB(id, message) CtrLog_Write(CTR_LOG_GAME, "[3DS_STUB] %s: %s", id, message)
#define CTR_UNIMPLEMENTED(id) CtrPlatform_Fatal("[3DS_UNIMPLEMENTED] " id)

#endif
