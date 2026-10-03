#ifndef CTR_LOG_H
#define CTR_LOG_H
#include <stdbool.h>

typedef enum
{
    CTR_LOG_BOOT, CTR_LOG_VIDEO, CTR_LOG_INPUT, CTR_LOG_FS,
    CTR_LOG_AUDIO, CTR_LOG_GAME, CTR_LOG_ERROR
} CtrLogCategory;

void CtrLog_Init(void);
void CtrLog_Close(void);
/* The last drain of the log to the card: how long it took and how long ago it
 * ended, in ms (0 and a large age before the first). */
void CtrLog_LastDrain(float *ms, float *agoMs);
void CtrLog_SetOverlay(bool enabled);
void CtrLog_DrawOverlay(const char *text);
void CtrLog_ShowFatal(const char *reason);
void CtrLog_Write(CtrLogCategory category, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
