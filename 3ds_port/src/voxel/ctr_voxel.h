#ifndef CTR_VOXEL_H
#define CTR_VOXEL_H

#include <stdbool.h>
#include <citro3d.h>

bool CtrVoxel_Init(void);
void CtrVoxel_Shutdown(void);

/* Cheap and side-effect free: decides which renderer composes this frame. */
bool CtrVoxel_IsAvailable(void);

/* Called after C3D_FrameBegin(), with the previous frame's GPU work finished:
 * everything it writes goes to linear memory that C3D_FrameEnd() then flushes. */
bool CtrVoxel_Update(void);
/*
 * Called after C3D_FrameEnd() of a frame the voxel world was drawn in, with
 * the tick C3D_FrameBegin() returned on: builds chunks and atlases while the
 * GPU draws that frame, in what is left of it before the next VBlank once the
 * game and the audio have had theirs. Nothing it does touches the GPU; what it
 * finishes is uploaded by the next Update.
 */
void CtrVoxel_AfterSubmit(uint64_t frameBeginTick);
/* Game VRAM animation transfer: tile numbers are relative to BG_VRAM. */
void CtrVoxel_NotifyTilesetAnimWrite(unsigned firstTile, unsigned tileCount);

/* Draws into the given target. Never opens or closes a frame. */
void CtrVoxel_Draw(C3D_RenderTarget *target, float eyeOffset);
/* The GBA brightness effect (BLDY) of the frame about to be drawn, as the
 * compositor reads it: on the backgrounds and on the sprites, towards white
 * or black. The palette fade is read by the voxel module itself. */
void CtrVoxel_SetBrightness(float backgrounds, float sprites, bool white);
/* How much glow the 2D compositor adds around the brightest parts of the
 * voxel picture this frame (0: none): the light's bloom, 0 indoors. */
float CtrVoxel_Bloom(void);

typedef struct
{
    unsigned instances;
    unsigned chunks;
    unsigned visibleChunks;
    unsigned vertices;
    unsigned atlasRebuilds;
    unsigned meshRebuilds;
    unsigned spriteUpdates;
    unsigned animationUploads, animatedMetatiles;
    unsigned reflections;
    unsigned errors;
    /* Vertices the last chunk build could not fit. */
    unsigned dropped;
    /*
     * Chunks the view needed this frame and did not get, because the per-frame
     * build budget ran out before reaching them. A steady non-zero number is
     * the cache thrashing rather than filling, which on screen is a black
     * square-edged hole - so it is worth a number of its own.
     */
    unsigned chunksMissing;
    /* Milliseconds the last frame spent visiting the view and building
     * chunks and atlases, and the worst seen. The steady frame is fine; what
     * the overlay could not show was the spike. */
    float meshMs, meshPeakMs;
    /* The whole update, billboards and page streaming included. */
    float updateMs, updatePeakMs;
    /* Where the rest of the update went: the maps on screen and the lighting
     * reset that follows a change to them (region layouts come off RomFS
     * there), the atlas job, and the billboards. */
    float worldMs, atlasMs, spritesMs;
    /* ... and of what no other figure holds: the building pages' stream, the
     * animated tiles' recomposition, the drafts. */
    float streamMs, animMs, draftMs;
    /* Spent building after the last FrameEnd (CtrVoxel_AfterSubmit), and the
     * budget it had. */
    float afterMs, afterBudgetMs;
    /* Chunks built this frame, and still waiting for a later one. */
    unsigned frameBuilds, pendingBuilds;
    /* Squares drawn this frame as their draft only, and drafts made in all. */
    unsigned draftsVisible, draftsMade;
    /* Bytes still free in the two pools this competes for. VRAM is here
     * because it is almost entirely unused, which is the whole argument for
     * moving the mesh into it. */
    unsigned long linearFree, vramFree;
} CtrVoxelStats;

const CtrVoxelStats *CtrVoxel_GetStats(void);

/*
 * Why the last frame did or did not take the voxel path, as a short word for
 * the debug overlay: off, nomap, noatlas, nomesh, on.
 */
const char *CtrVoxel_Status(void);

/*
 * Gives back the VRAM of atlases the overworld is not using right now - every
 * tileset pair but the current map's. For the 2D compositor, on a frame the
 * overworld does not draw. Returns the bytes released.
 */
unsigned long CtrVoxel_ReleaseIdleVram(void);

/* A map just entered by a cut is still being built (see VOXEL_WARMUP_MS). */
bool CtrVoxel_IsWarmingUp(void);

/*
 * The 3D battle (3ds_video.c, RenderBattleWorld). From Begin to End, Update
 * draws the world as the battle's scenery: seen from a camera of its own on
 * a stage chosen near the player (voxel_battle.c), with nobody in it. Begin
 * once per battle, on its first frame; Update ends it by itself, giving the
 * field its camera back, once the game has left the battle.
 */
bool CtrVoxel_IsAvailableForBattle(void);
void CtrVoxel_BeginBattle(void);
void CtrVoxel_EndBattle(void);
bool CtrVoxel_InBattle(void);
/* Before each battle Update: whether the intro is sliding its scenery in
 * (the camera glides in meanwhile), and how far BG3 is scrolled from rest in
 * GBA pixels (a move shaking the scenery shakes the camera). */
void CtrVoxel_SetBattleFrame(bool introSliding, float shakeX, float shakeY);

#endif
