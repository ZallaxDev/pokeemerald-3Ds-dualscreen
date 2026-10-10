#ifndef VOXEL_NIGHT_H
#define VOXEL_NIGHT_H
#include <math.h>
#include "voxel_mesh_builder.h"

/* Three representable steps in the scene's DEPTH16 buffer. A world-space
 * separation of 1/512 tile often quantizes to the same depth as the wall. */
#define VOXEL_NIGHT_DEPTH_UNITS 3u
#define VOXEL_NIGHT_DEPTH_BIAS (VOXEL_NIGHT_DEPTH_UNITS / 65535.0f)

/* Bounds describe the exact individual pane. Include all opaque glass,
 * including darker gradients and reflected warm colours near its bottom. */
static inline bool VoxelNight_Glass(uint16_t t)
{
    return (t & 1) != 0;
}

/* A lantern has a narrow finial above a wide head. Some sign masks also
 * touch a building corner; those broad, flat heads must never glow. */
static inline bool VoxelNight_Lantern(const uint16_t head[16])
{
    unsigned top = head[0], middle = head[4], count = 0;
    for (unsigned i = 0; i < 16; ++i)
        count += (middle >> i) & 1;
    return top && !(top & (top - 1)) && count >= 7;
}

/* Clip a textured face to one glass run, retaining its actual 3D surface. */
static inline unsigned VoxelNight_Clip(const VoxelVertex tri[3], VoxelVertex out[8], float u0,
                                       float v0, float u1, float v1)
{
    VoxelVertex a[8], b[8];
    unsigned n = 3;
    for (unsigned i = 0; i < 3; ++i)
        a[i] = tri[i];
    for (unsigned side = 0; side < 4 && n; ++side)
    {
        float edge = side == 0 ? u0 : side == 1 ? u1 : side == 2 ? v0 : v1;
        unsigned m = 0;
        VoxelVertex prev = a[n - 1];
        float pd = (side < 2 ? prev.u : prev.v) - edge;
        if (side & 1)
            pd = -pd;
        for (unsigned i = 0; i < n; ++i)
        {
            VoxelVertex cur = a[i];
            float cd = (side < 2 ? cur.u : cur.v) - edge;
            if (side & 1)
                cd = -cd;
            if ((pd < 0) != (cd < 0))
            {
                float t = pd / (pd - cd);
                b[m++] = (VoxelVertex){prev.x + (cur.x - prev.x) * t, prev.y + (cur.y - prev.y) * t,
                                       prev.z + (cur.z - prev.z) * t, prev.u + (cur.u - prev.u) * t,
                                       prev.v + (cur.v - prev.v) * t, 1};
            }
            if (cd >= 0)
                b[m++] = cur;
            prev = cur;
            pd = cd;
        }
        n = m;
        for (unsigned i = 0; i < n; ++i)
            a[i] = b[i];
    }
    for (unsigned i = 0; i < n; ++i)
    {
        out[i] = a[i];
        out[i].shade = 1;
        out[i].z += 1.0f / 512.0f;
    }
    return n;
}
#endif
