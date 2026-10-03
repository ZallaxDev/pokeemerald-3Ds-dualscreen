#include <stdbool.h>

#include "3ds_video.h"
#include "voxel_grade.h"

/*
 * Each channel pushed away from the colour's luma by this much. The GBA art
 * is pastel, made for a dim, unlit screen; lit and shaded in 3D it read flat
 * and washed out. 1.30 was chosen against captures of 1.0, 1.30 and 1.50:
 * richer greens and reds without the lit ground turning garish. Contrast is
 * left alone, so the shadows do not deepen.
 */
#define VOXEL_GRADE_SATURATION 1.30f

/* Graded RGBA5551, alpha set, for every BGR555 colour: 64 KiB of heap. */
static uint16_t sTable[1u << 15];
static bool sReady;

static unsigned Channel(float value)
{
    int c = (int)(value + 0.5f);

    return c < 0 ? 0u : c > 31 ? 31u : (unsigned)c;
}

void VoxelGrade_Init(void)
{
    if (sReady)
        return;
    for (unsigned bgr = 0; bgr < (1u << 15); ++bgr)
    {
        float r = (float)(bgr & 31), g = (float)((bgr >> 5) & 31), b = (float)(bgr >> 10);
        float y = 0.299f * r + 0.587f * g + 0.114f * b;
        unsigned gr = Channel(y + (r - y) * VOXEL_GRADE_SATURATION);
        unsigned gg = Channel(y + (g - y) * VOXEL_GRADE_SATURATION);
        unsigned gb = Channel(y + (b - y) * VOXEL_GRADE_SATURATION);

        sTable[bgr] = CtrVideo_RGBA5551((uint16_t)(gr | gg << 5 | gb << 10));
    }
    sReady = true;
}

uint16_t VoxelGrade_RGBA5551(uint16_t bgr15)
{
    return sReady ? sTable[bgr15 & 0x7FFF] : CtrVideo_RGBA5551(bgr15);
}

void VoxelGrade_Texels(uint16_t *texels, unsigned count)
{
    if (!sReady)
        return;
    for (unsigned i = 0; i < count; ++i)
    {
        uint16_t t = texels[i];
        unsigned bgr = ((t >> 11) & 31) | ((t >> 6) & 31) << 5 | ((t >> 1) & 31) << 10;

        texels[i] = (uint16_t)((sTable[bgr] & ~1u) | (t & 1u));
    }
}

void VoxelGrade_Brighten(uint16_t *texels, unsigned count, float factor)
{
    for (unsigned i = 0; i < count; ++i)
    {
        uint16_t t = texels[i];
        unsigned r = Channel((float)((t >> 11) & 31) * factor);
        unsigned g = Channel((float)((t >> 6) & 31) * factor);
        unsigned b = Channel((float)((t >> 1) & 31) * factor);

        texels[i] = (uint16_t)(r << 11 | g << 6 | b << 1 | (t & 1u));
    }
}
