#ifndef CTR_DAYNIGHT_H
#define CTR_DAYNIGHT_H

#include <stdint.h>

/* Console wall clock, independent of the game's adjustable logical RTC. */
void CtrDayNight_Tint(float rgb[3]);
float CtrDayNight_Night(void);
void CtrDayNight_Sun(float *dx, float *dz);

/* Pure schedules for host verification. Sun direction uses ten-minute
 * steps so changing it cannot trigger a mesh rebuild every shown frame. */
void CtrDayNight_At(unsigned seconds, float rgb[3]);
float CtrDayNight_NightAt(unsigned seconds);
void CtrDayNight_SunAt(unsigned seconds, float *dx, float *dz);
uint16_t CtrDayNight_Color(uint16_t color, const float rgb[3]);

#endif
