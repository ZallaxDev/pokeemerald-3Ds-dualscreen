"""Exercise production map dirty rectangles against a complete redraw (mock raster primitives).

Also the map extras (3ds_field_qol_extras.inc): the wild list's tables, timed
grass, methods, seen-only names and pages; the Repel's steps; the clock
banner; their hits and their partial redraws, all with production functions."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from voxel_runtime_test import function, ROOT


def between(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first) + len(end)] + '\n'


def statement(source, start):
    return between(source, start, ';')


def define(source, name):
    match = re.search(r'^#define ' + name + r'\b.*$', source, re.M)
    assert match, name
    return match.group(0) + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    bottom = (ROOT / 'src/3ds_bottom_ui.c').read_text(encoding='utf-8')
    clock = (ROOT / 'src/3ds_clock_store.c').read_text(encoding='utf-8')
    extras = (ROOT / 'src/3ds_extras.c').read_text(encoding='utf-8')
    table = (ROOT / 'src/3ds_field_qol_extras.inc').read_text(encoding='utf-8')
    # Entries after the sentinel corrupt count-based settings lookup.
    assert extras.index('#include "3ds_field_qol_extras.inc"') < extras.index('    {0},')
    hook_patch = ROOT.parent / 'patches/pokeemerald/0078-field-clock-notice.patch'
    if hook_patch.is_file():
        hook = hook_patch.read_text(encoding='utf-8')
        assert '+#include "3ds_clock_store.h"' in hook
        assert ' void NewGameInitData(void)\n {\n+#ifdef PLATFORM_3DS\n+    CtrClockStore_Load(NULL, 0);' in hook
    else:
        # Bootstrap overlays the port into upstream without the patch directory.
        # Verify the applied production hook there, rather than skipping it.
        new_game = (ROOT.parent / 'src/new_game.c').read_text(encoding='utf-8')
        assert '#include "3ds_clock_store.h"' in new_game
        init = function(new_game, 'void NewGameInitData(void)')
        reset = '#ifdef PLATFORM_3DS\n    CtrClockStore_Load(NULL, 0);\n#endif'
        assert reset in init
        assert init.index(reset) < init.index('gSaveFileStatus')
    for key in ('map_encounters', 'repel_steps', 'clock_notice'):
        assert f'"{key}", 2, 0, gCtrExtrasOffOn' in table
    source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "3ds_clock_store.h"
typedef uint8_t u8; typedef uint16_t u16; typedef bool bool8;
#define TRUE true
#define FALSE false
#define W 320
#define H 240
#define CW 240
#define CLOCK_X 5
#define CLOCK_Y 5
#define CLOCK_W 230
#define CLOCK_H 15
#define MAP_ORIGIN_X 8
#define MAP_ORIGIN_Y 28
#define MAP_WIN_X0 4
#define MAP_WIN_Y0 28
#define MAP_WIN_X1 236
#define MAP_WIN_Y1 196
#define NAME_X 5
#define NAME_Y 203
#define NAME_W 230
#define NAME_H 32
#define MAPSEC_NONE 255
#define SCR_MAP 0
#define SCR_OPTION 1
#define OPTION_ROWS 7
#define OPT_VOXEL 6
#define HIT_NONE 0
#define HIT_OPTION 20
#define HIT_COLUMN 40
#define COL_ITEMS 4
#define FOCUS_NONE 0
#define MODE_OFF 0
#define MODE_FIELD 1
#define MODE_BATTLE_INFO 10
#define MODE_BATTLE_ACTION 11
#define HIT_QUICK_BALL 60
#define ITEM_NONE 0
#define BAG_VIEW_WHOLE 2
#define CACHE_MAP 0
#define BOX_MENU 0
#define HIT_MAP 0
#define HIT_MAP_INFO 0x91
#define HIT_MAP_PAGE 0x92
#define HIT_CLOCK_NOTICE 0x93
#define LABEL_FG(x) (x)
#define LABEL_SH(x) (x)
#define CTR_TEXT(english, spanish, french, italian) spanish
#define CTR_TIME_MORNING 0
#define CTR_TIME_NIGHT 2
#define TXT_WHITE 5
#define TXT_DARK 6
#define TXT_LIGHT 7
#define SPECIES_NONE 0
#define FLAG_GET_SEEN 0
#define VAR_REPEL_STEP_COUNT 0x4021
#define VAR_ALTERING_CAVE_WILD_SET 0x403E
#define MAP_GROUP(map) ((map) >> 8)
#define MAP_NUM(map) ((map) & 0xFF)
#define MAP_UNDEFINED (0xFF | (0xFF << 8))
#define MAP_ALTERING_CAVE ((24 << 8) | 5)
'''
    source += between(bottom, '#define ENC_ROWS', '} EncRow;')
    source += r'''
typedef struct { u8 screen,mode,inBattle,bagView,mapsec,cursorX,cursorY,gender,pickMapsec,pickX,pickY,pressed,enabled;
    u8 clockShown,clockValid,clockRate,clockPhase,blink,cursor,options[OPTION_ROWS]; u16 clockMinute;
    u8 encOn,encView,encPage,encPages,encMethod,encCount,repelShown,clockNotice; u16 repelSteps; EncRow enc[ENC_ROWS];
    u8 optPage,optSub,optFocus,focus,extras[OPTION_ROWS],safari; u16 quickBall,quickBallCount; } ViewState;
typedef struct {int x0,y0,x1,y1;} Rect;
static bool RectAddPlate(Rect *rect,int hit) {(void)rect;(void)hit;return false;}
static bool RectAddHit(Rect *rect,int hit) {(void)rect;(void)hit;return false;}
static int BattleCursorHit(const ViewState *state,int cursor) {(void)state;return cursor;}
static u16 sCanvas[W*H], cache[W*H], fb[W*H], expected[W*H], *sCache[1]={cache}, *sDst;
static int sOX, sNormal, sSmall, blits;
static struct {int chosen,plate,chosenShadow;} sLook = {2,3,4};
static int clipX0=0,clipY0=0,clipX1=W,clipY1=H;
/* As production: the window inside the redraw's own clip. */
static void ClipToMapWindow(int saved[4]) {
    saved[0]=clipX0;saved[1]=clipY0;saved[2]=clipX1;saved[3]=clipY1;
    if(clipX0<MAP_WIN_X0)clipX0=MAP_WIN_X0;
    if(clipY0<MAP_WIN_Y0)clipY0=MAP_WIN_Y0;
    if(clipX1>MAP_WIN_X1)clipX1=MAP_WIN_X1;
    if(clipY1>MAP_WIN_Y1)clipY1=MAP_WIN_Y1;
}
static void RestoreClip(const int saved[4]) {clipX0=saved[0];clipY0=saved[1];clipX1=saved[2];clipY1=saved[3];}
static struct { const u8 *playerIcon[2], *cursorTiles; struct {u16 c[16];} playerIconPal[2],cursorPal; } sRes;
static bool CtrVideo_BottomInUse(void) {return false;}
static bool CtrVideo_BottomWhole(void) {return false;}
static void ResolveFonts(void) {}
static void Fill(int x,int y,int w,int h,u16 color) {
    for(int i=x;i<x+w;++i) for(int j=y;j<y+h;++j)
        if(i>=clipX0&&i<clipX1&&j>=clipY0&&j<clipY1) sDst[i*H+H-1-j]=color;
}
static void DrawSprite(const u8 *p,int w,int h,int x,int y,const u16 *c) {
    (void)c; Fill(x,y,w*8,h*8,*p);
}
static void GetMapName(u8 *name,u8 map,unsigned pad) {(void)pad;name[0]=map;name[1]=0;}
static void DrawPlate(int x,int y,int width,int height,const int*look,int unused,bool pressed) {
    (void)unused;Fill(x,y,width,height,*look+(pressed?100:0));
}
static const u8 *Ascii(const char *text) {return (const u8 *)text;}
static void DrawStrIn(const int*font,const u8*text,int x0,int x1,int y0,int y1,int fg,int shadow) {
    (void)font;(void)fg;(void)shadow;
    unsigned color=10;for(unsigned index=0;index<strlen((const char*)text);++index)color+=text[index];
    Fill(x0+2,y0+2,x1-x0-4,y1-y0-4,color);
}
static int hitIds[64], hitCount;
static void AddHit(int a,int b,int c,int d,int e) {(void)a;(void)b;(void)c;(void)d;if(hitCount<64)hitIds[hitCount++]=e;}
static bool HasHit(int id) {for(int i=0;i<hitCount;++i)if(hitIds[i]==id)return true;return false;}
static void CtrBottom_BlitRect(const u16 *p,int x0,int y0,int x1,int y1) {
    ++blits;
    for(int x=x0;x<x1;++x) for(int y=y0;y<y1;++y) fb[x*H+H-1-y]=p[x*H+H-1-y];
}
/* The game's side of the map extras. */
static const u8 gSpeciesNames[8][11] = {"", "AAA", "BBB", "CCC", "DDD", "EEE", "FFF", "GGG"};
struct WildPokemon { u8 minLevel, maxLevel; u16 species; };
struct WildPokemonInfo { u8 encounterRate; const struct WildPokemon *wildPokemon; };
struct WildPokemonHeader { u8 mapGroup, mapNum; const struct WildPokemonInfo *landMonsInfo, *waterMonsInfo, *rockSmashMonsInfo, *fishingMonsInfo; };
static const struct WildPokemon routeLand[12] = {{2,3,1},{2,2,2},{3,4,1},{4,4,3},{2,2,2},{5,5,4},{3,3,1},{4,4,1},{6,6,5},{6,6,5},{7,7,6},{7,7,6}};
static const struct WildPokemon timedLand[12] = {{2,3,7},{2,2,2},{3,4,7},{4,4,3},{2,2,2},{5,5,4},{3,3,7},{4,4,7},{6,6,5},{6,6,5},{7,7,6},{7,7,6}};
static const struct WildPokemon routeFish[10] = {{5,10,1},{5,10,2},{10,30,2},{10,30,3},{10,30,4},{20,40,5},{20,40,5},{20,40,6},{20,40,7},{25,45,7}};
static const struct WildPokemon seaWater[5] = {{20,30,3},{20,30,3},{20,30,4},{30,35,4},{30,35,5}};
static const struct WildPokemon caveA[12] = {{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1},{9,9,1}};
static const struct WildPokemon caveC[12] = {{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6},{8,8,6}};
static const struct WildPokemonInfo routeLandInfo={20,routeLand}, timedInfo={20,timedLand}, routeFishInfo={30,routeFish},
    seaWaterInfo={4,seaWater}, caveAInfo={7,caveA}, caveBInfo={7,timedLand}, caveCInfo={7,caveC};
static const struct WildPokemonHeader gWildMonHeaders[] = {
    {0,1,&routeLandInfo,NULL,NULL,&routeFishInfo}, {0,2,NULL,&seaWaterInfo,NULL,NULL},
    {24,5,&caveAInfo,NULL,NULL,NULL}, {24,5,&caveBInfo,NULL,NULL,NULL}, {24,5,&caveCInfo,NULL,NULL,NULL},
    {0xFF,0xFF,NULL,NULL,NULL,NULL}};
static struct { struct { int8_t mapGroup, mapNum; } location; } sSave1, *gSaveBlock1Ptr=&sSave1;
static u16 sVars[0x100];
static u16 VarGet(u16 id) {return sVars[id-0x4000];}
static bool sTimed; static unsigned landGroup=99, landNum=99;
static const struct WildPokemonInfo *CtrEncounters_Land(const struct WildPokemonInfo *o,unsigned g,unsigned n) {
    landGroup=g;landNum=n;return sTimed&&o?&timedInfo:o;
}
static unsigned sMapped;
static u16 CtrEncounters_DisplaySpecies(u16 species) {return sMapped&&species==1?(sMapped==2?2:7):species;}
static int8_t sSeen[256];
static u16 SpeciesToNationalPokedexNum(u16 species) {return species+100;}
static int8_t GetSetPokedexFlag(u16 national,u8 caseId) {assert(caseId==FLAG_GET_SEEN);return sSeen[national];}
static int sExtra[3];
static int CtrExtras_Get(const char *key) {
    if(!strcmp(key,"map_encounters"))return sExtra[0];
    if(!strcmp(key,"repel_steps"))return sExtra[1];
    assert(!strcmp(key,"clock_notice"));return sExtra[2];
}
static CtrClockStoreStatus sStatus; static unsigned sSerial;
CtrClockStoreStatus CtrClockStore_Status(void) {return sStatus;}
unsigned CtrClockStore_LoadSerial(void) {return sSerial;}
static bool8 sEncView; static u8 sEncPage; static unsigned sClockNoticeSerial;
static ViewState sShown;
static u8 sPickMapsec;
#define SE_SELECT 1
static unsigned selectSounds;
static void PlaySE(int sound) {assert(sound==SE_SELECT);++selectSounds;}
'''
    source += function(clock, 'bool CtrClockStore_NeedsNotice(')
    source += between(bottom, 'enum { ENC_LAND', '};')
    for name in ('sEncLandRates', 'sEncWaterRates', 'sEncFishRates', 'sEncFirst', 'sEncSlots'):
        source += statement(bottom, 'static const u8 ' + name)
    for name in ('NAME_SIDE_X', 'ENC_X0', 'ENC_X1', 'ENC_TITLE_Y', 'ENC_TITLE_H', 'ENC_ROW_Y', 'ENC_ROW_H',
                 'ENC_COL_LEVEL', 'ENC_COL_PERCENT', 'NOTICE_X', 'NOTICE_Y', 'NOTICE_W', 'NOTICE_H'):
        source += define(bottom, name)
    for name in ('static u8 EncCollect(', 'static const struct WildPokemonHeader *EncHeader(',
                 'static void SnapshotEncounters(', 'static void SnapshotMapExtras(',
                 'static void RectInit(', 'static void RectAdd(', 'static bool8 OptionsAreaOnly(',
                 'static void DrawNameSide(', 'static void DrawRegionName(', 'static void DrawMapClock(',
                 'static void DrawMapMarks(', 'static const u8 *EncName(', 'static void DrawEncounters(',
                 'static void DrawClockNotice(', 'static void DrawRegionMap(',
                 'static bool8 MapCursorOnlyMoved(', 'static void MarkRect(',
                 'static bool8 MoveMapCursor(', 'static bool8 DirtyRect(', 'static void MapExtrasTap('):
        source += function(bottom, name)
    source += r'''
static void Check(ViewState from,ViewState to,int count) {
    sDst=sCanvas;memcpy(sCanvas,cache,sizeof(cache));DrawRegionMap(&from);memcpy(fb,sCanvas,sizeof(fb));
    sDst=expected;memcpy(expected,cache,sizeof(cache));DrawRegionMap(&to);
    assert(MapCursorOnlyMoved(&to,&from));blits=0;
    assert(MoveMapCursor(&from,&to));assert(blits==count);
    assert(!memcmp(expected,sCanvas,sizeof(expected)));assert(!memcmp(expected,fb,sizeof(expected)));
}
static void CheckFooter(ViewState from,ViewState to) {
    Rect rect;
    sDst=sCanvas;memcpy(sCanvas,cache,sizeof(cache));DrawRegionMap(&from);
    sDst=expected;memcpy(expected,cache,sizeof(cache));DrawRegionMap(&to);
    assert(DirtyRect(&to,&from,&rect));
    assert(rect.x0==CLOCK_X&&rect.y0==CLOCK_Y&&rect.x1==CLOCK_X+CLOCK_W&&rect.y1==CLOCK_Y+CLOCK_H);
    for(int column=rect.x0;column<rect.x1;++column)for(int row=rect.y0;row<rect.y1;++row)
        sCanvas[column*H+H-1-row]=cache[column*H+H-1-row];
    sDst=sCanvas;DrawMapClock(&to);
    assert(!memcmp(expected,sCanvas,sizeof(expected)));
    to.enabled++;assert(!DirtyRect(&to,&from,&rect));
}
/* A whole drawing of a state, as Render's (the map cache, then the view). */
static void Full(const ViewState *s,u16 *dst) {
    sDst=dst;memcpy(dst,cache,sizeof(cache));clipX0=clipY0=0;clipX1=W;clipY1=H;hitCount=0;DrawRegionMap(s);
}
/* DirtyRect's part, redrawn as RenderPart does (the cache, then the view
 * inside the clip), must leave what a whole drawing of the new state is. */
static void CheckPart(ViewState from,ViewState to,Rect want) {
    Rect rect;
    Full(&from,sCanvas);Full(&to,expected);
    assert(DirtyRect(&to,&from,&rect));
    assert(rect.x0==want.x0&&rect.y0==want.y0&&rect.x1==want.x1&&rect.y1==want.y1);
    for(int column=rect.x0;column<rect.x1;++column)for(int row=rect.y0;row<rect.y1;++row)
        sCanvas[column*H+H-1-row]=cache[column*H+H-1-row];
    clipX0=rect.x0;clipY0=rect.y0;clipX1=rect.x1;clipY1=rect.y1;sDst=sCanvas;DrawRegionMap(&to);
    clipX0=clipY0=0;clipX1=W;clipY1=H;
    assert(!memcmp(expected,sCanvas,sizeof(expected)));
}
static void Extras(ViewState *s) {
    u8 mapsec=s->mapsec,cx=s->cursorX,cy=s->cursorY,pick=s->pickMapsec;
    memset(s,0,sizeof(*s));s->mode=MODE_FIELD;s->mapsec=mapsec;s->cursorX=cx;s->cursorY=cy;s->pickMapsec=pick;
    SnapshotMapExtras(s);
}
static void TestTables(void) {
    EncRow rows[ENC_ROWS];
    ViewState s;
    int total=0;
    /* Duplicate slots are one row: chances added up, levels' range, slot order. */
    assert(EncCollect(rows,routeLand,0,12,sEncLandRates)==6);
    assert(rows[0].species==1&&rows[0].percent==40&&rows[0].minLevel==2&&rows[0].maxLevel==4);
    assert(rows[1].species==2&&rows[1].percent==30&&rows[1].minLevel==2&&rows[1].maxLevel==2);
    assert(rows[2].species==3&&rows[2].percent==10&&rows[4].species==5&&rows[4].percent==8);
    assert(rows[5].species==6&&rows[5].percent==2&&rows[5].minLevel==7);
    for(int i=0;i<6;++i)total+=rows[i].percent;
    assert(total==100);
    /* A route with grass and fishing: four pages, no surfing or Rock Smash invented. */
    sSave1.location.mapGroup=0;sSave1.location.mapNum=1;sExtra[0]=1;sEncView=1;sEncPage=0;sTimed=false;
    memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encPages==4&&s.encPage==0&&s.encMethod==ENC_LAND&&s.encCount==6&&s.enc[0].species==1);
    assert(landGroup==0&&landNum==1);
    /* The timed tables: the grass page is what CtrEncounters_Land gives. */
    sTimed=true;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encMethod==ENC_LAND&&s.enc[0].species==7&&s.enc[0].percent==40);
    sEncPage=1;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encMethod==ENC_OLD_ROD&&s.encCount==2&&s.enc[0].species==1&&s.enc[0].percent==70&&s.enc[1].percent==30);
    sEncPage=6;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encPage==2&&s.encMethod==ENC_GOOD_ROD&&s.encCount==3&&s.enc[0].species==2&&s.enc[0].percent==60);
    sEncPage=3;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encMethod==ENC_SUPER_ROD&&s.encCount==3&&s.enc[0].species==5&&s.enc[0].percent==80);
    assert(s.enc[2].species==7&&s.enc[2].percent==5&&s.enc[2].minLevel==20&&s.enc[2].maxLevel==45);
    /* Water only; no table at all. */
    sEncPage=0;sSave1.location.mapNum=2;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encPages==1&&s.encMethod==ENC_WATER&&s.encCount==3&&s.enc[0].percent==90);
    sSave1.location.mapNum=9;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encPages==0&&s.encCount==0);
    /* Altering Cave: its set; one past the sets is the first. */
    sTimed=false;sSave1.location.mapGroup=24;sSave1.location.mapNum=5;sVars[VAR_ALTERING_CAVE_WILD_SET-0x4000]=2;
    memset(&s,0,sizeof(s));SnapshotEncounters(&s);assert(s.encCount==1&&s.enc[0].species==6&&s.enc[0].percent==100);
    sVars[VAR_ALTERING_CAVE_WILD_SET-0x4000]=3;
    memset(&s,0,sizeof(s));SnapshotEncounters(&s);assert(s.encCount==1&&s.enc[0].species==1);
    /* Seen by national number. */
    sSave1.location.mapGroup=0;sSave1.location.mapNum=1;sSeen[102]=1;
    memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(!s.enc[0].seen&&s.enc[1].species==2&&s.enc[1].seen&&!s.enc[2].seen);
    /* Mapping precedes visibility: seeing the original does not reveal the new species. */
    sSeen[101]=1;sMapped=true;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.enc[0].species==7&&!s.enc[0].seen&&s.enc[0].percent==40);
    sSeen[107]=1;memset(&s,0,sizeof(s));SnapshotEncounters(&s);assert(s.enc[0].seen);
    sMapped=2;memset(&s,0,sizeof(s));SnapshotEncounters(&s);
    assert(s.encCount==5&&s.enc[0].species==2&&s.enc[0].percent==70);
    assert(s.enc[0].minLevel==2&&s.enc[0].maxLevel==4);
    sSeen[101]=sSeen[107]=0;sMapped=false;
}
static void TestExtras(void) {
    ViewState s={.mapsec=2,.cursorX=9,.cursorY=8,.pickMapsec=6};
    u16 repel=VarGet(VAR_REPEL_STEP_COUNT);
    /* Off: nothing of them, the map as it was. */
    sExtra[0]=sExtra[1]=sExtra[2]=0;sEncView=1;sStatus=CTR_CLOCK_STORE_INVALID;sSerial=1;sVars[0x21]=40;
    Extras(&s);
    assert(!s.encOn&&!s.encView&&!s.encPages&&!s.repelShown&&!s.repelSteps&&!s.clockNotice);
    assert(s.cursorX==9&&s.pickMapsec==6);
    /* The list: no marks, no picked cell, the area's tables. */
    sExtra[0]=1;sEncPage=0;sSave1.location.mapGroup=0;sSave1.location.mapNum=1;
    Extras(&s);assert(s.encOn&&s.encView&&s.encPages==4&&s.cursorX==0&&s.cursorY==0&&s.pickMapsec==MAPSEC_NONE);
    sEncView=0;s.cursorX=9;s.pickMapsec=6;Extras(&s);assert(s.encOn&&!s.encView&&!s.encPages&&s.cursorX==9&&s.pickMapsec==6);
    /* The Repel's steps, read and never written. */
    sExtra[1]=1;sVars[0x21]=0;Extras(&s);assert(!s.repelShown&&!s.repelSteps);
    sVars[0x21]=57;Extras(&s);assert(s.repelShown&&s.repelSteps==57&&VarGet(VAR_REPEL_STEP_COUNT)==57);
    (void)repel;
    /* The banner: a saved clock lost, until dismissed for that load. */
    sExtra[2]=1;sClockNoticeSerial=0;sSerial=4;
    sStatus=CTR_CLOCK_STORE_INVALID;Extras(&s);assert(s.clockNotice);
    sStatus=CTR_CLOCK_STORE_MISMATCH;Extras(&s);assert(s.clockNotice);
    sClockNoticeSerial=4;Extras(&s);assert(!s.clockNotice);
    sSerial=5;Extras(&s);assert(s.clockNotice);
    sStatus=CTR_CLOCK_STORE_MISSING;Extras(&s);assert(s.clockNotice);
    sStatus=CTR_CLOCK_STORE_NONE;Extras(&s);assert(!s.clockNotice);
    sStatus=CTR_CLOCK_STORE_RECOVERED;Extras(&s);assert(!s.clockNotice);
    sStatus=CTR_CLOCK_STORE_RESTORED;Extras(&s);assert(!s.clockNotice);
    sExtra[2]=0;sStatus=CTR_CLOCK_STORE_INVALID;Extras(&s);assert(!s.clockNotice);
    sExtra[0]=sExtra[1]=0;sEncView=0;
}
static void TestInput(void) {
    memset(&sShown,0,sizeof(sShown));sEncView=0;sEncPage=5;sPickMapsec=8;
    MapExtrasTap(HIT_MAP_INFO);assert(!sEncView&&sEncPage==5&&sPickMapsec==8);
    sShown.encOn=1;MapExtrasTap(HIT_MAP_INFO);
    assert(sEncView&&sEncPage==0&&sPickMapsec==MAPSEC_NONE);
    sShown.encView=1;sShown.encPages=4;sShown.encPage=3;
    MapExtrasTap(HIT_MAP_PAGE);assert(sEncPage==0);
    sShown.encPage=1;MapExtrasTap(HIT_MAP_PAGE);assert(sEncPage==2);
    sShown.encPages=1;sEncPage=0;MapExtrasTap(HIT_MAP_PAGE);assert(sEncPage==0);
    sShown.encPages=0;MapExtrasTap(HIT_MAP_PAGE);assert(sEncPage==0);
    sShown.encPages=4;sShown.encView=0;MapExtrasTap(HIT_MAP_PAGE);assert(sEncPage==0);
    sShown.encView=1;MapExtrasTap(HIT_MAP_INFO);assert(!sEncView&&sEncPage==0);
    sShown.clockNotice=0;sSerial=20;sClockNoticeSerial=5;
    MapExtrasTap(HIT_CLOCK_NOTICE);assert(sClockNoticeSerial==5);
    sShown.clockNotice=1;MapExtrasTap(HIT_CLOCK_NOTICE);assert(sClockNoticeSerial==20);
    MapExtrasTap(HIT_MAP);assert(sClockNoticeSerial==20);
    assert(selectSounds==4);
}
static void TestDrawing(void) {
    Rect name={NAME_X,NAME_Y,NAME_X+NAME_W,NAME_Y+NAME_H}, both={CLOCK_X,CLOCK_Y,NAME_X+NAME_W,NAME_Y+NAME_H};
    ViewState map={.mode=1,.mapsec=2,.cursorX=9,.cursorY=8,.pickMapsec=MAPSEC_NONE},list,a,b;
    Rect rect;
    /* Off, the hits are the map's alone. */
    Full(&map,sCanvas);assert(HasHit(HIT_MAP)&&!HasHit(HIT_MAP_INFO)&&!HasHit(HIT_MAP_PAGE)&&!HasHit(HIT_CLOCK_NOTICE));
    /* Seen-only names: an unseen species draws as "???", whichever it is. */
    list=map;list.encOn=list.encView=1;list.encPages=2;list.encMethod=ENC_LAND;list.encCount=2;
    list.enc[0]=(EncRow){1,2,4,60,1};list.enc[1]=(EncRow){2,3,3,40,0};
    assert(!strcmp((const char *)EncName(&list.enc[1]),"???"));assert(EncName(&list.enc[0])==gSpeciesNames[1]);
    Full(&list,sCanvas);assert(!HasHit(HIT_MAP)&&HasHit(HIT_MAP_PAGE)&&HasHit(HIT_MAP_INFO));
    a=list;a.enc[1].species=5;Full(&a,expected);assert(!memcmp(sCanvas,expected,sizeof(expected)));
    a=list;a.enc[1].seen=1;Full(&a,expected);assert(memcmp(sCanvas,expected,sizeof(expected)));
    /* The list replaces the map: the marks are not drawn. */
    a=list;a.cursorX=20;Full(&a,expected);assert(!memcmp(sCanvas,expected,sizeof(expected)));
    a=list;a.encPages=1;Full(&a,expected);assert(!HasHit(HIT_MAP_PAGE)&&HasHit(HIT_MAP_INFO));
    a=list;a.encPages=0;a.encCount=0;Full(&a,expected);assert(!HasHit(HIT_MAP_PAGE));
    /* Toggled, paged, a row seen, the banner: whole redraws, not a stale part. */
    assert(!DirtyRect(&list,&map,&rect));
    a=list;a.encPage=1;assert(!DirtyRect(&a,&list,&rect));
    a=list;a.enc[1].seen=1;assert(!DirtyRect(&a,&list,&rect));
    a=map;a.clockNotice=1;assert(!DirtyRect(&a,&map,&rect));
    Full(&a,sCanvas);assert(HasHit(HIT_CLOCK_NOTICE)&&HasHit(HIT_MAP));
    /* The Repel's steps: the name plate alone, on the map and on the list. */
    a=map;b=a;b.repelShown=1;b.repelSteps=100;CheckPart(a,b,name);
    a=b;b.repelSteps=99;CheckPart(a,b,name);
    a=b;b.repelSteps=0;b.repelShown=0;CheckPart(a,b,name);
    a=list;a.repelShown=1;a.repelSteps=12;b=a;b.repelSteps=11;CheckPart(a,b,name);
    /* With the clock's minute too: both parts. */
    a=map;a.clockShown=a.clockValid=1;a.clockMinute=600;a.repelShown=1;a.repelSteps=5;
    b=a;b.clockMinute=601;b.repelSteps=4;CheckPart(a,b,both);
    b=a;b.clockMinute=601;CheckFooter(a,b);
    /* The plate pressed and let go. */
    a=map;a.encOn=1;b=a;b.pressed=HIT_MAP_INFO;CheckPart(a,b,name);CheckPart(b,a,name);
    b=a;b.pressed=HIT_MAP_PAGE;assert(!DirtyRect(&b,&a,&rect));
    /* A step across cells that also spends a Repel step: the marks and the plate. */
    a=map;a.repelShown=1;a.repelSteps=50;b=a;b.cursorX++;b.repelSteps=49;Check(a,b,3);
    b=a;b.cursorX++;Check(a,b,2);
    /* No mark moving on the list or under the banner. */
    a=list;b=a;b.mapsec=32;assert(!MapCursorOnlyMoved(&b,&a));
    a=map;a.clockNotice=1;b=a;b.cursorX++;assert(!MapCursorOnlyMoved(&b,&a));
    clipX0=clipY0=0;clipX1=W;clipY1=H;
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
    a.pickMapsec=MAPSEC_NONE;a.clockShown=1;a.clockValid=1;a.clockMinute=1439;a.clockRate=120;a.clockPhase=2;
    b=a;b.cursorX++;Check(a,b,2);b.mapsec=32;Check(a,b,3);
    a.clockValid=0;b=a;b.mapsec=32;Check(a,b,3);
    b=a;b.clockMinute++;assert(!MapCursorOnlyMoved(&b,&a));
    a.clockShown=0;b=a;b.clockShown=1;b.clockValid=1;b.clockRate=120;b.clockMinute=1439;b.clockPhase=2;CheckFooter(a,b);
    a=b;b.clockMinute=0;CheckFooter(a,b);a=b;b.clockRate=1;CheckFooter(a,b);
    a=b;b.clockValid=0;CheckFooter(a,b);a=b;b.clockShown=0;CheckFooter(a,b);
    a.clockShown=1;a.clockValid=1;a.clockPhase=1;a.clockMinute=959;
    b=a;b.clockMinute=960;CheckFooter(a,b);
    b.pressed=1;assert(!MapCursorOnlyMoved(&b,&a));b=a;b.enabled=1;assert(!MapCursorOnlyMoved(&b,&a));
    b=a;b.mode=2;assert(!MapCursorOnlyMoved(&b,&a));b=a;b.mapsec=MAPSEC_NONE;assert(!MapCursorOnlyMoved(&b,&a));
    ViewState optionsFrom={.mode=MODE_FIELD,.screen=SCR_OPTION},optionsTo=optionsFrom;
    Rect optionsRect;
    optionsTo.optPage=1;optionsTo.options[0]=1;
    assert(OptionsAreaOnly(&optionsTo,&optionsFrom));
    assert(DirtyRect(&optionsTo,&optionsFrom,&optionsRect));
    assert(optionsRect.x0==0&&optionsRect.y0==0&&optionsRect.x1==CW&&optionsRect.y1==H);
    optionsTo.pressed=HIT_COLUMN;
    assert(!OptionsAreaOnly(&optionsTo,&optionsFrom));
    TestTables();
    TestExtras();
    TestDrawing();
    TestInput();
    puts("PASS bottom map: partial/full canvas and framebuffer equivalence, region changes with clock, invalid clock, picked marker, unrelated state fallback; "
         "map extras: tables/timed grass/methods/Altering Cave, seen-only names, mapped aggregation/seen masking, page inputs and hits, Repel and clock parts, clock banner gating");
}
'''
    with tempfile.TemporaryDirectory(prefix='bottom-map-', dir=ROOT / 'build') as tmp:
        path=Path(tmp)/'test.c';path.write_text(source,encoding='utf-8');exe=Path(tmp)/'test.exe'
        subprocess.run([args.cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-iquote',str(ROOT / 'include'),
                        str(path),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__ == '__main__':
    main()
