#!/usr/bin/env python3
"""Side-by-side picture of two walking maps over one window: navcmp.py A.bin B.bin x0 y0 x1 y1 out.png [scale]   (metres; left A, right B)
green: a standable surface (brighter = higher), red: cells with something but nothing standable, black: not in the map, blue: no ground."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win')); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wnav
from navmap import png

def paint(m, x0, y0, x1, y1, S):
    cx0, cx1, cy0, cy1 = int(x0 / .5), int(x1 / .5), int(y0 / .5), int(y1 / .5); rows = []
    for cy in range(cy1, cy0 - 1, -1):
        line = [bytearray() for _ in range(S)]
        for cx in range(cx0, cx1 + 1):
            c = (cx, cy); r = m.cells.get(c)
            if r is None: col = (0, 0, 0)
            elif r[0] < -50: col = (30, 60, 200)
            else:
                s = [z for z in (m.surfaces(c) or []) if z is not None and z > wnav.SEA]
                if s: v = 90 + int(min(1.0, max(0.0, (s[0] % 20) / 20)) * 150); col = (0, v, 0)
                else: col = (200, 30, 30)
                if r[2] & wnav.WALKED: col = (255, 255, 255)
                elif r[2] & wnav.BUMPED: col = (230, 0, 230)
            for b in line: b.extend(bytes(col) * S)
        rows.extend(line)
    return rows, (cx1 - cx0 + 1) * S

if __name__ == '__main__':
    a, b = wnav.Map(), wnav.Map(); a.load(sys.argv[1]); b.load(sys.argv[2]); x0, y0, x1, y1 = map(float, sys.argv[3:7]); S = int(sys.argv[8]) if len(sys.argv) > 8 else 4
    ra, wa = paint(a, x0, y0, x1, y1, S); rb, wb = paint(b, x0, y0, x1, y1, S)
    rows = [bytearray(x + bytes([255, 255, 0] * 2) + y) for x, y in zip(ra, rb)]
    png(sys.argv[7], wa + wb + 2, len(rows), rows); print('wrote', sys.argv[7])
