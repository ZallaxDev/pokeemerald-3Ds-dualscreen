/* Run the real saver while its first SD write is deliberately stalled. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <3ds.h>
static int TestClose(FILE *file);
#define fclose TestClose
#include "../src/3ds_settings.c"
#undef fclose
static pthread_mutex_t diskMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t diskCond = PTHREAD_COND_INITIALIZER;
static bool entered, releaseDisk;
static unsigned writes, errors;
static int fault;
static char saved[256];
FILE *CtrFs_OpenData(const char *name, const char *mode) {
    assert(strcmp(name, "settings.txt") == 0 && strcmp(mode, "w") == 0);
    pthread_mutex_lock(&diskMutex);
    entered = true; pthread_cond_signal(&diskCond);
    while (!releaseDisk) pthread_cond_wait(&diskCond, &diskMutex);
    pthread_mutex_unlock(&diskMutex);
    return fault == 1 ? NULL : tmpfile();
}
static int TestClose(FILE *file) {
    rewind(file); size_t n = fread(saved, 1, sizeof(saved)-1, file); saved[n] = 0;
    ++writes; int rc = fclose(file); return fault == 2 ? -1 : rc;
}
void CtrLog_Write(CtrLogCategory category, const char *fmt, ...) {
    (void)fmt; if (category == CTR_LOG_ERROR) ++errors;
}
static void *SlowCard(void *unused) {
    (void)unused; struct timespec delay = {1, 0}; nanosleep(&delay, NULL);
    pthread_mutex_lock(&diskMutex); releaseDisk = true;
    pthread_cond_signal(&diskCond); pthread_mutex_unlock(&diskMutex); return NULL;
}
int main(int argc, char **argv) {
    fault = argc > 1 ? argv[1][0] - '0' : 0;
    CtrSettings_SetVoxel(true);
    pthread_mutex_lock(&diskMutex);
    while (!entered) pthread_cond_wait(&diskCond, &diskMutex);
    pthread_mutex_unlock(&diskMutex);
    /* Queue newer text while the worker owns an older snapshot. */
    CtrSettings_SetVoxelBlur(false); CtrSettings_SetShowFps(true);
    pthread_t card; assert(pthread_create(&card, NULL, SlowCard, NULL) == 0);
    CtrSettings_Shutdown(); pthread_join(card, NULL);
    assert(sSaver == NULL && sSaveQuit && !sSavePending);
    assert(releaseDisk);
    if (fault == 1) assert(errors == 2 && writes == 0);
    else {
        assert(writes == 2 && strstr(saved, "voxel=1\n") && strstr(saved, "voxel_blur=0\n") && strstr(saved, "fps=1\n"));
        assert(errors == (fault == 2 ? 2u : 0u));
    }
    CtrSettings_Shutdown(); /* idempotent, no freed-thread access */
    puts("PASS settings: stalled write, latest snapshot drained, close and open errors, repeated shutdown");
    return 0;
}
