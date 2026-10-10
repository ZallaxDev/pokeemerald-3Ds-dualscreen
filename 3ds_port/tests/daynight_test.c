#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "3ds_daynight.h"

int main(void)
{
    float tint[3], previous[3], midnight[3];
    CtrDayNight_At(0, midnight);
    assert(midnight[2] > midnight[0]);
    CtrDayNight_At(12 * 3600, tint);
    assert(tint[0] == 1 && tint[1] == 1 && tint[2] == 1);
    for (unsigned color = 0; color < 65536; ++color)
        assert(CtrDayNight_Color(color, tint) == color);
    CtrDayNight_At(19 * 3600, tint);
    assert(tint[0] > tint[1] && tint[1] > tint[2]);
    CtrDayNight_At(0, previous);
    for (unsigned second = 1; second <= 86400; ++second)
    {
        CtrDayNight_At(second, tint);
        for (unsigned c = 0; c < 3; ++c)
        {
            assert(tint[c] >= 0.5f && tint[c] <= 1.0f);
            assert(fabsf(tint[c] - previous[c]) < 0.001f);
            previous[c] = tint[c];
        }
    }
    for (unsigned c = 0; c < 3; ++c)
        assert(tint[c] == midnight[c]);
    assert(CtrDayNight_Color(0, midnight) == 0);
    assert((CtrDayNight_Color(0xffff, midnight) & 0x8000) != 0);
    float dx, dz;
    CtrDayNight_SunAt(6 * 3600, &dx, &dz);
    assert(dx < 0 && dz > 0); /* dawn: shadows west */
    CtrDayNight_SunAt(12 * 3600, &dx, &dz);
    assert(dx == 0 && dz > 0);
    CtrDayNight_SunAt(17 * 3600, &dx, &dz);
    assert(dx > 0 && dz > 0); /* afternoon: shadows east */
    float lastDx = dx, lastDz = dz;
    CtrDayNight_SunAt(17 * 3600 + 599, &dx, &dz);
    assert(dx == lastDx && dz == lastDz);
    CtrDayNight_SunAt(17 * 3600 + 600, &dx, &dz);
    assert(dx != lastDx);
    CtrDayNight_SunAt(0, &dx, &dz);
    CtrDayNight_SunAt(86400, &lastDx, &lastDz);
    assert(dx == lastDx && dz == lastDz);
    puts("day/night: schedule, midnight continuity and color conversion passed");
    return 0;
}
