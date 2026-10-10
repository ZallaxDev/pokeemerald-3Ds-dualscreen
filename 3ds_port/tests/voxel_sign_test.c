/* Pixel geometry and real baked sidecar. Never writes into ROMFS. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "voxel_sign.h"
#include "voxel_regions.h"
#include "voxel_relief.h"

static bool sFloor;
static unsigned sLayout;
static int sX, sY;

void VoxelBuilder_Quad(VoxelBuilder *b, const VoxelVertex *a, const VoxelVertex *c,
                       const VoxelVertex *d, const VoxelVertex *e)
{
    assert(b->count + 6 <= b->capacity);
    b->vertices[b->count++] = *a; b->vertices[b->count++] = *c;
    b->vertices[b->count++] = *d; b->vertices[b->count++] = *a;
    b->vertices[b->count++] = *d; b->vertices[b->count++] = *e;
}
void VoxelMesh_Top(VoxelBuilder *b,float x,float z,float h,float inset,
                    float u,float v,float ue,float ve,float shade)
{
    (void)b; (void)x; (void)z; (void)h; (void)inset;
    (void)v; (void)ue; (void)ve; (void)shade;
    assert(u == .5f); /* donor, not the board */
    sFloor = true;
}
bool VoxelMesh_TileUV(VoxelBuilder *b,int x,int y,float *u,float *v,float *ue,float *ve)
{
    (void)b; (void)x;
    *u = y == sY+1 ? .5f : 0; *ue = *u + .25f;
    *v = 1; *ve = .75f;
    return true;
}
/* Signs here stand on level ground: no relief under them. */
float VoxelRelief_Base(const VoxelMapInstance *inst) { (void)inst; return 0; }
float VoxelRelief_CellLift(const VoxelMapInstance *inst, int x, int y)
{ (void)inst; (void)x; (void)y; return 0; }
float VoxelRelief_CellShift(const VoxelMapInstance *inst, int x, int y)
{ (void)inst; (void)x; (void)y; return 0; }

const int16_t *VoxelRelief_Cell(const VoxelMapInstance *inst, int x, int y)
{
    (void)inst; (void)x; (void)y;
    return NULL;
}
unsigned VoxelRegions_RoleAt(unsigned id,int x,int y)
{
    return id == sLayout && x == sX && y == sY ? VOXEL_ROLE_SIGNPOST : VOXEL_ROLE_FLOOR;
}

int main(int argc,char **argv)
{
    static VoxelVertex vertices[4096];
    VoxelBuilder b = {.vertices=vertices,.capacity=4096};
    VoxelSignMask m = {.width=16,.height=16,.u0=.25f,.v0=1,
                       .du=1.0f/512,.dv=-1.0f/256};
    /* Board whose last painted row is NOT the metatile's last row. Previously
     * the front floated above the side faces, and SOUTH reversed its text. */
    for (int y=2;y<=12;++y) m.opaque[y]=0x3FFC;
    VoxelSign_Emit(&b,2,3,0,&m,VOXEL_SIGN_PINNED_DEPTH,1);
    assert(b.count > 0 && b.count < 132*12);
    bool base=false, front=false, back=false;
    for (unsigned i=0;i<b.count;++i)
    {
        VoxelVertex *v=&vertices[i];
        assert(v->y>=0 && v->y<=11.0f/16);
        assert(v->z==3.6875f || v->z==3.8125f);
        assert(v->u>=m.u0+2*m.du && v->u<=m.u0+14*m.du);
        if (v->y==0) base=true;
    }
    for (unsigned i=0;i<b.count;i+=6)
    {
        VoxelVertex *a=&vertices[i], *c=&vertices[i+1];
        if (a->z==c->z && a->x!=c->x)
        {
            if (a->z==3.8125f) { assert(a->x<c->x && a->u<c->u); front=true; }
            if (a->z==3.6875f) back=true;
        }
    }
    assert(base && front && back);
    /* A lamp: a post in the sign's cell, its lantern in the cell north. The
     * lantern stands on the post, textured from its own cell. */
    {
        VoxelSignMask l = {.width=16,.height=32,.headRows=16,.u0=.25f,.v0=1,
                           .du=1.0f/512,.dv=-1.0f/256,.headU0=.5f,.headV0=.5f};
        bool top=false;
        for (int y=4;y<32;++y) l.opaque[y]=0x0380;
        b.count=0;
        VoxelSign_Emit(&b,2,3,0,&l,VOXEL_SIGN_PINNED_DEPTH,1);
        for (unsigned i=0;i<b.count;++i)
        {
            VoxelVertex *v=&vertices[i];
            assert(v->y>=0 && v->y<=28.0f/16);
            assert(v->z==4.6875f || v->z==4.8125f);
            if (v->y>1.0f) { assert(v->u>=.5f && v->v<=.5f); top=true; }
            else if (v->y<.99f) assert(v->u<.5f && v->v<=1 && v->v>=.75f);
        }
        assert(top);
        b.count=0;
    }
    /* Runtime consumes the very file shipped by the bake, not a fake resource
     * written over it by the test. The first record identifies our fixture. */
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");
    unsigned char header[8], key[6];
    assert(f && fread(header,1,8,f)==8 && memcmp(header,"VXS2",4)==0);
    assert(fread(key,1,6,f)==6); fclose(f);
    sLayout=key[0]|(key[1]<<8); sX=key[2]|(key[3]<<8); sY=key[4]|(key[5]<<8);
    VoxelMapInstance inst={.layoutId=(int)sLayout};
    b.count=0;
    assert(VoxelSign_EmitCell(&b,&inst,sX,sY));
    assert(sFloor && b.count>0 && b.dropped==0);
    assert(!VoxelSign_EmitCell(&b,&inst,sX+1,sY));
    VoxelSign_Shutdown();
    puts("PASS signs: board paint, two-pixel depth, UV orientation, grounded shell, ROMFS runtime");
    return 0;
}
