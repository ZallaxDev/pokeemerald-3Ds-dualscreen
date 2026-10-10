#include <assert.h>
#include <stdio.h>
#include "voxel_night.h"
#include "3ds_daynight.h"
static unsigned Depth16(float z)
{
    return (unsigned)floorf(z * 65535.0f + 0.5f);
}
int main(void)
{
    assert(VoxelNight_Glass((8 << 11) | (20 << 6) | (25 << 1) | 1));
    assert(VoxelNight_Glass(0xffff));
    assert(VoxelNight_Glass((10 << 11) | (10 << 6) | (12 << 1) | 1));
    assert(!VoxelNight_Glass((8 << 11) | (20 << 6) | (25 << 1)));
    const uint16_t lantern[16] = {[0] = 0x100, [4] = 0x7f8};
    const uint16_t wall[16] = {[0] = 0xff00, [4] = 0xff00};
    assert(VoxelNight_Lantern(lantern));
    assert(!VoxelNight_Lantern(wall));
    VoxelVertex tri[3] = {{0, 0, 2, 0, 0, 1}, {4, 0, 2, 4, 0, 1}, {0, 4, 2, 0, 4, 1}}, out[8];
    unsigned n = VoxelNight_Clip(tri, out, 1, 1, 2, 2);
    assert(n >= 3 && n <= 7);
    for (unsigned i = 0; i < n; ++i)
    {
        assert(out[i].u >= 1 && out[i].u <= 2 && out[i].v >= 1 && out[i].v <= 2);
        assert(out[i].x + out[i].y <= 4.0001f);
        assert(fabsf(out[i].z - (2.0f + 1.0f / 512.0f)) < 0.0001f);
    }
    assert(VoxelNight_Clip(tri, out, 5, 5, 6, 6) == 0);
    assert(CtrDayNight_NightAt(0) == 1 && CtrDayNight_NightAt(12 * 3600) == 0);
    assert(CtrDayNight_NightAt(6 * 3600) == 0.5f && CtrDayNight_NightAt(19 * 3600) == 0.5f);
    float last = CtrDayNight_NightAt(0);
    for (unsigned t = 1; t <= 86400; ++t)
    {
        float now = CtrDayNight_NightAt(t);
        assert(now >= 0 && now <= 1 && fabsf(now - last) < 0.001f);
        last = now;
    }
    /* Sweep camera-dependent sub-step positions and small mesh rounding
     * errors: lights always win over their facade, while closer geometry
     * remains in front. A world-space micro-offset alone can tie the wall. */
    unsigned oldTies = 0;
    for (unsigned step = 100; step < 65000; step += 137)
    {
        for (unsigned phase = 0; phase < 100; ++phase)
        {
            float wall = (step + phase / 100.0f) / 65535.0f;
            for (int error = -10; error <= 10; ++error)
            {
                float light = wall + error / 20.0f / 65535.0f;
                oldTies += Depth16(light) == Depth16(wall);
                assert(Depth16(light + VOXEL_NIGHT_DEPTH_BIAS) > Depth16(wall));
                assert(Depth16(light + VOXEL_NIGHT_DEPTH_BIAS) < Depth16(wall + 8.0f / 65535.0f));
            }
        }
    }
    assert(oldTies > 0);
    puts("PASS night lights: glass/frame mask, face clipping, surface depth, DEPTH16 camera "
         "sweep/occlusion, day/night ramp");
}
