#!/usr/bin/env python3
"""Generate a small redistributable Winamp Classic skin using only stdlib."""
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
OUT = HERE / "skin" / "sapphire"
OUT.mkdir(parents=True, exist_ok=True)


def clamp(v):
    return max(0, min(255, int(v)))


def bmp24(path: Path, w: int, h: int, pixel):
    stride = (w * 3 + 3) & ~3
    rows = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b = pixel(x, y)
            row.extend((clamp(b), clamp(g), clamp(r)))
        row.extend(b"\0" * (stride - w * 3))
        rows.extend(row)
    header = struct.pack("<2sIHHI", b"BM", 54 + len(rows), 0, 0, 54)
    dib = struct.pack("<IIIHHIIIIII", 40, w, h, 1, 24, 0, len(rows), 2835, 2835, 0, 0)
    path.write_bytes(header + dib + rows)


def line(px, x0, y0, x1, y1, color):
    if x0 == x1:
        for y in range(max(0, y0), min(len(px), y1 + 1)):
            if 0 <= x0 < len(px[0]): px[y][x0] = color
    elif y0 == y1:
        for x in range(max(0, x0), min(len(px[0]), x1 + 1)):
            if 0 <= y0 < len(px): px[y0][x] = color


def rect(px, x0, y0, x1, y1, color, fill=True):
    if fill:
        for y in range(max(0, y0), min(len(px), y1 + 1)):
            for x in range(max(0, x0), min(len(px[0]), x1 + 1)):
                px[y][x] = color
    else:
        line(px, x0, y0, x1, y0, color); line(px, x0, y1, x1, y1, color)
        line(px, x0, y0, x0, y1, color); line(px, x1, y0, x1, y1, color)


def canvas(w, h, top=(10, 17, 34), bottom=(18, 7, 34)):
    px = [[(0,0,0)] * w for _ in range(h)]
    for y in range(h):
        t = y / max(1, h - 1)
        base = tuple(top[i] * (1-t) + bottom[i] * t for i in range(3))
        for x in range(w):
            glow = max(0.0, 1.0 - abs(x - w * 0.58) / (w * 0.68))
            px[y][x] = (base[0] + 5*glow, base[1] + 8*glow, base[2] + 16*glow)
    return px

CYAN=(54,231,255); CYAN2=(17,112,151); VIOLET=(177,92,255); DARK=(5,9,20); WHITE=(224,247,255)

px = canvas(275,116)
rect(px,0,0,274,115,CYAN,False); rect(px,2,2,272,113,VIOLET,False)
rect(px,5,5,269,18,(7,12,28),True); line(px,6,19,268,19,CYAN2)
for x in range(8,267,12): line(px,x,8,x+6,8,CYAN)
rect(px,8,25,146,67,DARK,True); rect(px,8,25,146,67,CYAN2,False)
for i in range(22):
    bar = (i*11 + 8) % 33
    rect(px,13+i*5,61-bar,15+i*5,62,CYAN if i%3 else VIOLET,True)
rect(px,152,25,267,48,DARK,True); rect(px,152,25,267,48,CYAN2,False)
rect(px,152,53,267,67,DARK,True); rect(px,152,53,267,67,VIOLET,False)
rect(px,8,73,267,107,(8,13,29),True); rect(px,8,73,267,107,CYAN2,False)
for x in range(12,264,16): line(px,x,103,x+8,103,VIOLET)
bmp24(OUT/"MAIN.BMP",275,116,lambda x,y:px[y][x])

px = canvas(275,116, (8,16,31), (21,7,37))
rect(px,0,0,274,115,CYAN,False); rect(px,3,3,271,112,VIOLET,False)
rect(px,6,6,268,19,DARK,True); line(px,7,20,267,20,CYAN2)
for i in range(10):
    x=18+i*24
    rect(px,x,29,x+6,99,(7,13,27),True); rect(px,x,29,x+6,99,CYAN2,False)
    h=(i*13+24)%60
    rect(px,x+1,98-h,x+5,98,CYAN if i%2 else VIOLET,True)
rect(px,12,104,262,109,DARK,True); rect(px,12,104,180,109,CYAN,True)
bmp24(OUT/"EQMAIN.BMP",275,116,lambda x,y:px[y][x])

px = canvas(275,110,(7,15,30),(17,7,31))
rect(px,0,0,274,109,CYAN,False); rect(px,2,2,272,107,VIOLET,False)
rect(px,5,5,269,18,DARK,True); line(px,6,19,268,19,CYAN2)
rect(px,7,23,267,88,(4,9,19),True); rect(px,7,23,267,88,CYAN2,False)
for y in range(28,84,11): line(px,10,y,264,y,(17,36,59))
rect(px,7,92,267,104,(7,13,26),True); rect(px,7,92,267,104,VIOLET,False)
bmp24(OUT/"PLEDIT.BMP",275,110,lambda x,y:px[y][x])

px = [[DARK] * 136 for _ in range(36)]
segments=[(0,22),(23,45),(46,68),(69,91),(92,113),(114,135)]
for row in range(2):
    y0=row*18
    for n,(x0,x1) in enumerate(segments):
        rect(px,x0,y0,x1,y0+17,(9+row*6,18+row*4,37+row*8),True)
        rect(px,x0,y0,x1,y0+17,VIOLET if row else CYAN2,False)
        cx=(x0+x1)//2; cy=y0+8; c=CYAN if row==0 else WHITE
        if n==0:
            for d in range(5): line(px,cx+2-d,cy-d,cx+2-d,cy+d,c)
            line(px,cx-5,cy-5,cx-5,cy+5,c)
        elif n==1:
            for d in range(6): line(px,cx-4+d,cy-d,cx-4+d,cy+d,c)
        elif n==2:
            rect(px,cx-5,cy-5,cx-2,cy+5,c,True); rect(px,cx+2,cy-5,cx+5,cy+5,c,True)
        elif n==3: rect(px,cx-5,cy-5,cx+5,cy+5,c,True)
        elif n==4:
            for d in range(5): line(px,cx-2+d,cy-d,cx-2+d,cy+d,c)
            line(px,cx+5,cy-5,cx+5,cy+5,c)
        else:
            for d in range(6): line(px,cx-5+d,cy-4,cx-5+d,cy+4,c)
bmp24(OUT/"CBUTTONS.BMP",136,36,lambda x,y:px[y][x])

(OUT/"PLEDIT.TXT").write_text(
    "Normal=#CFEFFF\nCurrent=#38E7FF\nNormalBG=#040913\nSelectedBG=#4B2173\nFont=Arial\n",
    encoding="ascii",
)
print(OUT)
