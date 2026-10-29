"""Picture of a walking map (tools/win/wnav.py): python3 tools/navmap.py <wnav_map.bin> [more maps] -o out.png [--path x,y;x,y;...]
North (+y) up. Green shades: walkable floor by height; red: something in the way between 0.5 and 1.9 m above the floor; white: walked;
magenta: bumped into; black: never seen; the optional path in yellow."""
import os, sys, struct, zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win'))
import wnav


def png(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    def chunk(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    args = sys.argv[1:]; out = 'navmap.png'; path = []
    if '-o' in args: i = args.index('-o'); out = args[i + 1]; del args[i:i + 2]
    if '--path' in args: i = args.index('--path'); path = [tuple(map(float, q.split(','))) for q in args[i + 1].split(';') if q]; del args[i:i + 2]
    m = wnav.Map()
    for a in args: m.load(a)
    if not m.cells: sys.exit('empty map')
    xs = [c[0] for c in m.cells]; ys = [c[1] for c in m.cells]; x0, x1, y0, y1 = min(xs) - 2, max(xs) + 2, min(ys) - 2, max(ys) + 2
    S = 2 if (x1 - x0) * (y1 - y0) < 400000 else 1; W, H = (x1 - x0 + 1) * S, (y1 - y0 + 1) * S
    floors = [r[0] for r in m.cells.values() if r[0] > -1e3]; zlo, zhi = min(floors), max(floors) + 1e-6
    img = [bytearray(W * 3) for _ in range(H)]
    def put(cx, cy, col):
        for a in range(S):
            for b in range(S):
                row = img[(y1 - cy) * S + b]; k = ((cx - x0) * S + a) * 3; row[k:k + 3] = bytes(col)
    for c, (z, mask, fl) in m.cells.items():
        if fl & wnav.WALKED: col = (255, 255, 255)
        elif fl & wnav.BUMPED: col = (255, 0, 255)
        elif mask & wnav.BAND_MASK: col = (200, 40, 30)
        else: g = int(80 + 170 * (z - zlo) / (zhi - zlo)); col = (20, g, 40)
        put(c[0], c[1], col)
    for x, y in path:
        c = wnav.cell_of(x, y)
        if x0 <= c[0] <= x1 and y0 <= c[1] <= y1: put(c[0], c[1], (255, 220, 0))
    png(out, W, H, img)
    print('%s: %d cells, x %.0f..%.0f y %.0f..%.0f m, floor %.1f..%.1f m' % (out, len(m.cells), x0 * wnav.CELL, x1 * wnav.CELL, y0 * wnav.CELL, y1 * wnav.CELL, zlo, zhi))


if __name__ == '__main__': main()
