#include <time.h>
#include <math.h>
#include "3ds_daynight.h"

void CtrDayNight_At(unsigned seconds, float rgb[3])
{
    static const unsigned hours[] = {0, 5, 7, 9, 17, 19, 21, 24};
    static const float colors[][3] = {
        {0.52f, 0.60f, 0.82f}, {0.52f, 0.60f, 0.82f}, {1.00f, 0.86f, 0.76f}, {1.00f, 1.00f, 1.00f},
        {1.00f, 1.00f, 1.00f}, {1.00f, 0.74f, 0.62f}, {0.52f, 0.60f, 0.82f}, {0.52f, 0.60f, 0.82f}};
    seconds %= 86400;
    unsigned i = 0;
    while (seconds >= hours[i + 1] * 3600)
        ++i;
    float t = (float)(seconds - hours[i] * 3600) / ((hours[i + 1] - hours[i]) * 3600);
    t = t * t * (3.0f - 2.0f * t);
    for (unsigned c = 0; c < 3; ++c)
        rgb[c] = colors[i][c] + (colors[i + 1][c] - colors[i][c]) * t;
}

void CtrDayNight_Tint(float rgb[3])
{
    time_t now = time(NULL);
    struct tm value;
    if (now == (time_t)-1 || !localtime_r(&now, &value))
    {
        rgb[0] = rgb[1] = rgb[2] = 1.0f;
        return;
    }
    CtrDayNight_At(value.tm_hour * 3600 + value.tm_min * 60 + value.tm_sec, rgb);
}

uint16_t CtrDayNight_Color(uint16_t color, const float rgb[3])
{
    unsigned r = (unsigned)((color & 31) * rgb[0] + 0.5f);
    unsigned g = (unsigned)(((color >> 5) & 31) * rgb[1] + 0.5f);
    unsigned b = (unsigned)(((color >> 10) & 31) * rgb[2] + 0.5f);
    return (uint16_t)(r | g << 5 | b << 10 | (color & 0x8000));
}

void CtrDayNight_SunAt(unsigned seconds, float *dx, float *dz)
{
    /* Sun and moon follow an east-to-west arc, twelve hours apart. */
    unsigned phase = ((seconds % 86400 + 86400 - 6 * 3600) % (12 * 3600)) / 600;
    float angle = (float)phase * 3.14159265358979323846f / 72.0f;
    *dx = -1.60f * cosf(angle);
    *dz = 0.40f + 0.30f * sinf(angle);
    if (fabsf(*dx) < 0.0001f)
        *dx = 0.0f;
}

void CtrDayNight_Sun(float *dx, float *dz)
{
    time_t now = time(NULL);
    struct tm value;
    if (now == (time_t)-1 || !localtime_r(&now, &value))
    {
        *dx = 0.85f;
        *dz = 0.55f;
        return;
    }
    CtrDayNight_SunAt(value.tm_hour * 3600 + value.tm_min * 60 + value.tm_sec, dx, dz);
}

float CtrDayNight_NightAt(unsigned seconds)
{
    seconds %= 86400;
    float t;
    if (seconds < 5 * 3600 || seconds >= 20 * 3600)
        return 1.0f;
    if (seconds < 7 * 3600)
        t = (7 * 3600 - seconds) / 7200.0f;
    else if (seconds < 18 * 3600)
        return 0.0f;
    else
        t = (seconds - 18 * 3600) / 7200.0f;
    return t * t * (3.0f - 2.0f * t);
}

float CtrDayNight_Night(void)
{
    time_t now = time(NULL);
    struct tm value;
    if (now == (time_t)-1 || !localtime_r(&now, &value))
        return 0.0f;
    return CtrDayNight_NightAt(value.tm_hour * 3600 + value.tm_min * 60 + value.tm_sec);
}
