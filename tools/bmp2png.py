#!/usr/bin/env python3
"""Convert the proxy's in-game screenshots (32-bit top-down BMP from the `shot` command) to PNG, standard library only.
usage: bmp2png.py in.bmp out.png [scale]   (scale 2 halves the size by sampling)"""
import struct, sys, zlib
src, dst = sys.argv[1], sys.argv[2]; step = int(sys.argv[3]) if len(sys.argv) > 3 else 1
d = open(src, 'rb').read(); off = struct.unpack_from('<I', d, 10)[0]; w, h = struct.unpack_from('<ii', d, 18); top_down = h < 0; h = abs(h)
rows = []
for y in range(0, h, step):
    yy = y if top_down else h - 1 - y; base = off + yy * w * 4; row = bytearray(b'\x00')
    for x in range(0, w, step): o = base + x * 4; row += bytes((d[o + 2], d[o + 1], d[o]))
    rows.append(bytes(row))
W, H = (w + step - 1) // step, len(rows)
def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
open(dst, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b''))
print('wrote', dst, W, H)
