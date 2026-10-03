"""Exercise production map dirty rectangles against a complete redraw (mock raster primitives)."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from voxel_runtime_test import function, ROOT

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    bottom = (ROOT / 'src/3ds_bottom_ui.c').read_text()
    source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8; typedef uint16_t u16; typedef bool bool8;
#define TRUE true
#define FALSE false
#define W 320
#define H 240
#define CW 240
#define MAP_ORIGIN_X 8
#define MAP_ORIGIN_Y 8
#define MAPSEC_NONE 255
#define SCR_MAP 0
#define MODE_OFF 0
#define MODE_BATTLE_INFO 10
#define BAG_VIEW_WHOLE 2
#define CACHE_MAP 0
#define BOX_MENU 0
#define HIT_MAP 0
#define LABEL_FG(x) (x)
#define LABEL_SH(x) (x)
typedef struct { u8 screen,mode,inBattle,bagView,mapsec,cursorX,cursorY,gender,pickMapsec,pickX,pickY,pressed,enabled; } ViewState;
static u16 sCanvas[W*H], cache[W*H], fb[W*H], expected[W*H], *sCache[1]={cache}, *sDst;
static int sOX, sAnimCount, sNormal, sAnim[1], blits;
static struct { const u8 *playerIcon[2], *cursorTiles; struct {u16 c[16];} playerIconPal[2],cursorPal; } sRes;
static bool CtrVideo_BottomInUse(void) {return false;}
static bool CtrVideo_BottomWhole(void) {return false;}
static void ResolveFonts(void) {}
static void IconRect(const int *a,int*x,int*y,int*z,int*w) {(void)a;*x=*y=*z=*w=0;}
static void Fill(int x,int y,int w,int h,u16 color) {
    for(int i=x;i<x+w;++i) for(int j=y;j<y+h;++j)
        if(i>=0&&i<W&&j>=0&&j<H) sDst[i*H+H-1-j]=color;
}
static void DrawSprite(const u8 *p,int w,int h,int x,int y,const u16 *c) {
    (void)c; Fill(x,y,w*8,h*8,*p);
}
static void GetMapName(u8 *name,u8 map,unsigned pad) {(void)pad;name[0]=map;}
static void DrawBoxEx(int k,int x,int y,int w,int h,bool dark) {(void)k;Fill(x,y,w*8,h*8,dark?2:3);}
static void DrawStrCentered(const int*f,const u8*t,int x,int y,int a,int b) {
    (void)f;(void)a;(void)b;Fill(x-20,y,40,16,t[0]+10);
}
static void AddHit(int a,int b,int c,int d,int e) {(void)a;(void)b;(void)c;(void)d;(void)e;}
static void CtrBottom_BlitRect(const u16 *p,int x0,int y0,int x1,int y1) {
    ++blits;
    for(int x=x0;x<x1;++x) for(int y=y0;y<y1;++y) fb[x*H+H-1-y]=p[x*H+H-1-y];
}
'''
    for name in ('static void DrawRegionName(', 'static void DrawRegionMap(',
                 'static bool8 MapCursorOnlyMoved(', 'static void MarkRect(',
                 'static bool8 RectsMeet(', 'static bool8 MoveMapCursor('):
        source += function(bottom, name)
    source += r'''
static void Check(ViewState from,ViewState to,int count) {
    sDst=sCanvas;memcpy(sCanvas,cache,sizeof(cache));DrawRegionMap(&from);memcpy(fb,sCanvas,sizeof(fb));
    sDst=expected;memcpy(expected,cache,sizeof(cache));DrawRegionMap(&to);
    assert(MapCursorOnlyMoved(&to,&from));blits=0;
    assert(MoveMapCursor(&from,&to));assert(blits==count);
    assert(!memcmp(expected,sCanvas,sizeof(expected)));assert(!memcmp(expected,fb,sizeof(expected)));
}
int main(void) {
    static const u8 icon=90,cursor=91;
    sRes.playerIcon[0]=&icon;sRes.cursorTiles=&cursor;
    for(unsigned i=0;i<W*H;++i)cache[i]=(u16)(i*13);
    ViewState a={.mode=1,.mapsec=2,.cursorX=9,.cursorY=8,.pickMapsec=MAPSEC_NONE},b=a;
    b.cursorX++;Check(a,b,2);
    b.mapsec=32;Check(a,b,3);
    b.cursorX=a.cursorX;Check(a,b,3);
    a.pickMapsec=b.pickMapsec=6;a.pickX=b.pickX=9;a.pickY=b.pickY=8;Check(a,b,2);
    b.pressed=1;assert(!MapCursorOnlyMoved(&b,&a));b=a;b.enabled=1;assert(!MapCursorOnlyMoved(&b,&a));
    b=a;b.mode=2;assert(!MapCursorOnlyMoved(&b,&a));b=a;b.mapsec=MAPSEC_NONE;assert(!MapCursorOnlyMoved(&b,&a));
    puts("PASS bottom map: partial/full canvas and framebuffer equivalence, region changes, same cell, picked marker, unrelated state fallback");
}
'''
    with tempfile.TemporaryDirectory(prefix='bottom-map-', dir=ROOT / 'build') as tmp:
        path=Path(tmp)/'test.c';path.write_text(source);exe=Path(tmp)/'test.exe'
        subprocess.run([args.cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__ == '__main__':
    main()
