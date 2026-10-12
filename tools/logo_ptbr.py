#!/usr/bin/env python3
"""Preview of the Portuguese title logo, VERSÃO over ESMERALDA, drawn for
this project in the palette of the player's own logo:

    python tools/logo_ptbr.py --tree build/upstream --out logo.png

The port draws the same at load time (3ds_port/src/3ds_icons_ptbr.c,
DrawLogo); this reads only the palette from the tree and writes nothing
into the repository. '@' stands for Ã.
"""
import argparse
from pathlib import Path

from PIL import Image

# Big letters: 9 wide, 12 high, strokes 3 px.
BIG={
'E':["#########","#########","###......","###......","########.","########.","###......","###......","###......","#########","#########","#########"],
'S':[".#######.","#########","###....##","###......","########.",".########","......###","......###","##....###","#########",".#######.","........."],
'M':["###...###","####.####","#########","#########","###.#.###","###...###","###...###","###...###","###...###","###...###","###...###","###...###"],
'R':["########.","#########","###...###","###...###","###...###","########.","########.","###.####.","###..####","###...###","###...###","###...###"],
'A':["..#####..",".#######.","###...###","###...###","###...###","#########","#########","###...###","###...###","###...###","###...###","###...###"],
'L':["###......","###......","###......","###......","###......","###......","###......","###......","###......","#########","#########","#########"],
'D':["#######..","########.","###..####","###...###","###...###","###...###","###...###","###...###","###..####","########.","#######..","........."],
}
# Small letters: 7 wide, 9 high, strokes 2 px; the tilde above Ã.
SMALL={
'V':["###..###","###..###","###..###","###..###","###..###",".######.",".######.","..####..","..####..","...##..."],
'E':["########","########","###.....","###.....","#######.","#######.","###.....","###.....","########","########"],
'R':["#######.","########","###..###","###..###","########","#######.","###.###.","###..###","###..###","###..###"],
'S':[".#######","########","###.....","#######.",".#######","......##","......##","########","########",".######."],
'@':["..##..#.",".#..##..","........",".######.","###..###","###..###","########","########","###..###","###..###"],
'O':[".######.","########","###..###","###..###","###..###","###..###","###..###","###..###","########",".######."],
}
OLDSMALL={
'V':["##...##","##...##","##...##","##...##",".##.##.",".##.##.",".##.##.","..###..","...#..."],
'E':["#######","#######","##.....","######.","######.","##.....","##.....","#######","#######"],
'R':["######.","#######","##...##","##...##","######.","#####..","##.###.","##..###","##...##"],
'S':[".######","#######","##.....","######.",".######",".....##",".....##","#######","######."],
'A':["..###..",".#####.","##...##","##...##","#######","#######","##...##","##...##","##...##"],
'O':[".#####.","#######","##...##","##...##","##...##","##...##","##...##","#######",".#####."],
}
TILDE=[".##..#.","#..##.."]
W,H=128,32
def round_corners(g):
    g=[list(r) for r in g]; h=len(g); w=len(g[0])
    for (y,x,ny,nx) in ((0,0,0,1),(0,w-1,0,w-2),(h-1,0,h-1,1),(h-1,w-1,h-1,w-2)):
        if g[y][x]=='#' and (y+1<h and g[y+1][x]=='#' if y==0 else g[y-1][x]=='#'): g[y][x]='.'
    return [''.join(r) for r in g]
for k in list(BIG): BIG[k]=round_corners([r[:5]+r[4]+r[5:] for r in BIG[k]])
def place(mask,text,font,x0,y0,gap,arc,tilde_on=None):
    x=x0; cols=[]
    for ch in text:
        g=font[ch]; w=len(g[0])
        for c in range(w):
            col=x+c
            dy=arc(col)
            for r,row in enumerate(g):
                if row[c]=='#':
                    y=y0+r+dy
                    if 0<=col<W and 0<=y<H: mask[y][col]=1
        if ch==tilde_on:
            for r,row in enumerate(TILDE):
                for c in range(len(row)):
                    if row[c]=='#':
                        col=x+c; y=y0-3+r+arc(col)
                        if 0<=col<W and 0<=y<H: mask[y][col]=1
        x+=w+gap
    return x-gap
def width(text,font,gap): return sum(len(font[c][0]) for c in text)+gap*(len(text)-1)
def render(big,small,smallTilde):
    img=[[0]*W for _ in range(H)]
    layers=[]
    bw=width(big,BIG,1); bx=(W-bw)//2
    mb=[[0]*W for _ in range(H)]
    place(mb,big,BIG,bx,14,1,lambda c: 2)
    sw=width(small,SMALL,1); sx=(W-sw)//2
    ms=[[0]*W for _ in range(H)]
    place(ms,small,SMALL,sx,2,1,lambda c: 1)
    for m in (mb,ms):
        # outline: two rings, the inner near black (4), the outer dark (e)
        def dil(m,n):
            o=[[0]*W for _ in range(H)]
            for y in range(H):
                for x in range(W):
                    if any(0<=y+dy<H and 0<=x+dx<W and m[y+dy][x+dx] for dy in range(-n,n+1) for dx in range(-n,n+1) if abs(dx)+abs(dy)<=n+ (1 if n>1 else 0)):
                        o[y][x]=1
            return o
        d1=dil(m,1); d2=dil(m,2)
        for y in range(H):
            for x in range(W):
                if d2[y][x] and not d1[y][x]: img[y][x]=0xE
                if d1[y][x] and not m[y][x]: img[y][x]=0x4
                if m[y][x]:
                    # shading: the lower part of each stroke greyer
                    # white, the stroke's bottom row grey, its right edge light grey
                    if y+1<H and not m[y+1][x]: img[y][x]=0x8
                    elif x+1<W and not m[y][x+1]: img[y][x]=0xC
                    else: img[y][x]=0xF
    return img


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tree', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    pal = Image.open(args.tree / 'graphics/title_screen/emerald_version.png').getpalette()
    img = render('ESMERALDA', 'VERS@O', False)
    im = Image.new('P', (W, H))
    im.putpalette(pal)
    for y in range(H):
        for x in range(W):
            im.putpixel((x, y), img[y][x])
    im.convert('RGB').resize((W * 8, H * 8), Image.NEAREST).save(args.out)
    print('preview:', args.out)


if __name__ == '__main__':
    main()
