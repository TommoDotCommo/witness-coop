"""Mean absolute colour difference (0..255) between a full-size screenshot and a smaller picture of the same scene.
Usage: Windows Python wcompare.py <full.bmp> <small.bmp>   (both 32-bit BMPs as written by the proxy's shot / partnershot commands)"""
import struct, sys
def load(p):
    d = open(p, 'rb').read(); off = struct.unpack_from('<I', d, 10)[0]; w, h = struct.unpack_from('<ii', d, 18); top = h < 0; h = abs(h)
    rows = [d[off + y * w * 4: off + (y + 1) * w * 4] for y in range(h)]
    return w, h, (rows if top else rows[::-1])
fw, fh, f = load(sys.argv[1]); sw, sh, s = load(sys.argv[2]); kx, ky = fw // sw, fh // sh; total = n = 0
for y in range(sh):
    for x in range(0, sw, 2):
        acc = [0, 0, 0]
        for j in range(ky):
            row = f[y * ky + j]
            for i in range(kx):
                o = (x * kx + i) * 4; acc[0] += row[o]; acc[1] += row[o + 1]; acc[2] += row[o + 2]
        o = x * 4
        for c in range(3): total += abs(acc[c] / (kx * ky) - s[y][o + c]); n += 1
print('%.2f' % (total / n))
