/* Exercise the actual C emitter: the ground pass lays every cell flat,
 * including water, and is the same built whole, a row at a time or chunk by
 * chunk, does not move with negative map origins, and the modelled buildings
 * come out the same sliced as in one go. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voxel_mesh_builder.h"
#include "voxel_regions.h"
#include "voxel_atlas.h"
#include "voxel_building.h"

static VoxelMapInstance sMap={.layoutId=1,.width=8,.height=8};
static VoxelAtlasMap sAtlas;
static VoxelVertex sWhole[12000],sChunks[12000];

unsigned VoxelRegions_RoleAt(unsigned id,int x,int y)
{
    (void)id; (void)x; (void)y;
    return VOXEL_ROLE_FLOOR;
}
bool VoxelWorld_UsesTreeSprites(const VoxelMapInstance *m) { (void)m; return false; }
const VoxelMapInstance *VoxelWorld_Instance(unsigned i) { return i==0 ? &sMap : NULL; }
const VoxelMapInstance *VoxelWorld_GetInstanceAt(int x,int y)
{
    return x>=sMap.originX && y>=sMap.originY && x<sMap.originX+8 && y<sMap.originY+8 ? &sMap : NULL;
}
int VoxelWorld_GetMetatileId(int x,int y)
{
    assert(VoxelWorld_GetInstanceAt(x,y));
    return (y-sMap.originY)*8+x-sMap.originX;
}
/* One pond cell; the rest is ground, blocked or not - a house's cells, a
 * cliff's, a hedge's all come here as ground now. */
VoxelVisualShape VoxelWorld_ClassifyTile(int x,int y)
{
    if (!VoxelWorld_GetInstanceAt(x,y)) return VOXEL_SHAPE_VOID;
    return x-sMap.originX==6 && y-sMap.originY==2 ? VOXEL_SHAPE_WATER : VOXEL_SHAPE_FLAT;
}
int VoxelWorld_BorderMetatile(int x,int y) { (void)x; (void)y; return -1; }
void VoxelWorld_GetPlayerWorldCoords(float *x,float *z) { *x=7+sMap.originX; *z=7+sMap.originY; }
void VoxelAtlas_SlotUV(unsigned slot,float *u,float *v,float *ue,float *ve)
{
    *u=(slot%32)/32.0f; *ue=*u+1.0f/32;
    *v=1-(slot/32)/16.0f; *ve=*v-1.0f/16;
}
void VoxelAtlas_SolidUV(VoxelSolidColor c,float *u,float *v,float *ue,float *ve)
{
    VoxelAtlas_SlotUV((unsigned)c,u,v,ue,ve);
}
static void Init(VoxelBuilder *b,VoxelVertex *v)
{
    VoxelBuilder_Init(b,v,12000);
    VoxelBuilder_SetAtlas(b,&sAtlas);
    VoxelBuilder_SetOrigin(b,sMap.originX,sMap.originY);
    VoxelMesh_BeginWindow(sMap.originX,sMap.originY,sMap.originX+8,sMap.originY+8);
}
static void Emit(VoxelBuilder *b,int x0,int y0,int x1,int y1)
{
    VoxelMesh_EmitInstance(b,&sMap,x0+sMap.originX,y0+sMap.originY,
        x1+sMap.originX,y1+sMap.originY);
}
static int Compare(const void *aa,const void *bb)
{
    const VoxelVertex *a=aa,*b=bb;
    const float av[]={a->x,a->y,a->z,a->u,a->v,a->shade};
    const float bv[]={b->x,b->y,b->z,b->u,b->v,b->shade};
    for (int i=0;i<6;++i) { if(av[i]<bv[i])return -1; if(av[i]>bv[i])return 1; }
    return 0;
}
int main(void)
{
    VoxelBuilder whole,part;
    for (unsigned i=0;i<64;++i) sAtlas.slotOf[i]=(uint16_t)(i+1);
    Init(&whole,sWhole); Emit(&whole,0,0,8,8);
    /* Water has been flush since 00485c4fe: one quad per cell, without
     * artificial bank walls over the shore already drawn in the texture. */
    assert(whole.count==64*6 && !whole.dropped && !whole.uncovered);
    for(unsigned i=0;i<whole.count;++i)
    {
        VoxelVertex *v=&sWhole[i];
        assert(isfinite(v->x)&&isfinite(v->y)&&isfinite(v->z));
        assert(v->x>=0 && v->x<=8 && v->z>=0 && v->z<=8);
        assert(v->y==0);
    }
    /* Chunk by chunk: the same triangles. */
    Init(&part,sChunks);
    for(int y=0;y<8;y+=4) for(int x=0;x<8;x+=4) Emit(&part,x,y,x+4,y+4);
    assert(part.count==whole.count && !part.dropped);
    qsort(sWhole,whole.count,sizeof(*sWhole),Compare);
    qsort(sChunks,part.count,sizeof(*sChunks),Compare);
    assert(!memcmp(sWhole,sChunks,whole.count*sizeof(*sWhole)));
    /* Negative map origins: map-local geometry does not move. */
    sMap.originX=-16; sMap.originY=-24;
    Init(&part,sChunks); Emit(&part,0,0,8,8);
    qsort(sChunks,part.count,sizeof(*sChunks),Compare);
    assert(part.count==whole.count);
    for(unsigned i=0;i<whole.count;++i)
    {
        assert(fabsf(sWhole[i].x-sChunks[i].x)<.00001f);
        assert(fabsf(sWhole[i].y-sChunks[i].y)<.00001f);
        assert(fabsf(sWhole[i].z-sChunks[i].z)<.00001f);
        assert(fabsf(sWhole[i].v-sChunks[i].v)<.00001f);
    }
    sMap.originX=sMap.originY=0;
    /* Built a row at a time, as the renderer spreads a chunk over frames:
     * the very same mesh, in the same order, as the whole build. */
    Init(&whole,sWhole);
    VoxelMesh_EmitInstance(&whole,&sMap,0,0,8,8);
    Init(&part,sChunks);
    for(int y=0;y<8;++y) VoxelMesh_EmitGroundRow(&part,&sMap,0,8,y);
    assert(whole.count>0 && part.count==whole.count);
    assert(!memcmp(sWhole,sChunks,whole.count*sizeof(*sWhole)));
    /* The modelled buildings, a few dozen triangles a slice as the renderer
     * spreads them over frames, come out exactly as in one go. */
    if(VoxelBuildings_Init())
    {
        static VoxelVertex modelsWhole[200000],modelsSliced[200000];
        static VoxelMapInstance town;
        unsigned layouts=0,seed=7;
        for(unsigned id=1;id<1024 && layouts<24;++id)
        {
            VoxelBuildingCursor cursor={0,0,0};
            memset(&town,0,sizeof(town));
            town.layoutId=(uint16_t)id; town.width=town.height=256;
            town.originX=-40; town.originY=12;
            if(VoxelBuildings_PageOf(&town)<0) continue;
            ++layouts;
            VoxelBuilder_Init(&whole,modelsWhole,200000); VoxelBuilder_SetOrigin(&whole,-40,12);
            VoxelBuilder_Init(&part,modelsSliced,200000); VoxelBuilder_SetOrigin(&part,-40,12);
            VoxelBuildings_EmitInstance(&whole,&town,-40,12,216,268);
            while(!VoxelBuildings_EmitSome(&part,&town,-40,12,216,268,&cursor,1+(seed=seed*1103515245u+12345u)%97))
                ;
            assert(!whole.dropped && whole.count>0 && part.count==whole.count);
            assert(!memcmp(modelsWhole,modelsSliced,whole.count*sizeof(*modelsWhole)));
        }
        assert(layouts>0);
        VoxelBuildings_Shutdown();
    }
    else
        puts("NOTE ground: " VOXEL_BUILDINGS_PATH " absent, sliced models not checked");
    puts("PASS ground: flat cells, flush water, chunks, negative origins, row-by-row builds, cell drafts, sliced models");
    return 0;
}
