#include <assert.h>
#define VOXEL_HOST_FILES
#define PORT_LOG(...) ((void)0)
#include "../src/voxel/voxel_sign.c"
float VoxelRelief_Base(const VoxelMapInstance *m)
{
    (void)m;
    return 0;
}
float VoxelRelief_CellLift(const VoxelMapInstance *m, int x, int y)
{
    (void)m;
    (void)x;
    (void)y;
    return 0;
}
float VoxelRelief_CellShift(const VoxelMapInstance *m, int x, int y)
{
    (void)m;
    (void)x;
    (void)y;
    return 0;
}
void VoxelBuilder_Quad(VoxelBuilder *b, const VoxelVertex *a, const VoxelVertex *c,
                       const VoxelVertex *d, const VoxelVertex *e)
{
    (void)c;
    (void)d;
    (void)e;
    assert(a->y > 0.5f && a->y < 2.0f);
    assert(b->count + 6 <= b->capacity);
    b->count += 6;
}
int main(void)
{
    VoxelSign_Init();
    assert(sCount > 0);
    unsigned lamps = 0;
    for (unsigned i = 0; i < sCount; ++i)
        if (VoxelNight_Lantern(sIndex[i].head))
            ++lamps;
    assert(lamps > 0);
    VoxelBuilder b = {.capacity = 8190};
    VoxelMapInstance m = {.layoutId = 2, .width = 128, .height = 128};
    VoxelSign_EmitNight(&b, &m, 0, 0, 128, 128);
    assert(b.count == 0);
    m.layoutId = 4;
    VoxelSign_EmitNight(&b, &m, 0, 0, 128, 128);
    assert(b.count > 0);
    m.indoor = true;
    b.count = 0;
    VoxelSign_EmitNight(&b, &m, 0, 0, 128, 128);
    assert(b.count == 0);
    printf("PASS night lamps: %u lanterns, glass geometry, building corners excluded, indoor "
           "exclusion\n",
           lamps);
    VoxelSign_Shutdown();
}
