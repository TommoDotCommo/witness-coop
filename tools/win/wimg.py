# Image helpers for the Windows-side tools (standard library only): the proxy's `shot` BMPs (32 bit, top down) to PNG, a contact sheet, a line drawn across an image.
# Loaded with exec(compile(open(...).read(), ...)) like wlib.py (python -I does not look next to the script).
import os, struct, zlib
def read_bmp(path):
    d = open(path, 'rb').read(); off = struct.unpack_from('<I', d, 10)[0]; w, h = struct.unpack_from('<ii', d, 18); top = h < 0; h = abs(h)
    rows = []
    for y in range(h):
        yy = y if top else h - 1 - y; rows.append(d[off + yy * w * 4: off + (yy + 1) * w * 4])
    return w, h, rows        # BGRA rows, top first
def write_png(path, w, h, rows_rgb):
    raw = b''.join(b'\x00' + bytes(r) for r in rows_rgb)
    def chunk(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
def rgb_rows(w, h, rows, step=1, hline=None):
    o = []
    for y in range(0, h, step):
        r = rows[y]; line = bytearray()
        for x in range(0, w, step): line += bytes((r[x * 4 + 2], r[x * 4 + 1], r[x * 4]))
        if hline is not None and abs(y - hline) < step: line = bytearray(bytes((0, 255, 255)) * (len(line) // 3))
        o.append(line)
    return o
def bmp_to_png(src, dst, step=1, hline=None):
    w, h, rows = read_bmp(src); rr = rgb_rows(w, h, rows, step, hline); write_png(dst, (w + step - 1) // step, len(rr), rr)
