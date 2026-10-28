"""Scan a game's image for floats that could be the field of view in any common form. Usage: Windows Python wfovscan.py <pid> <fov_deg> <aspect>
Prints module-relative offsets of matches (degrees, radians, tan(half), cot(half), tan*aspect, and their float32 neighbours within 1e-4)."""
import os, sys, struct, math, ctypes
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); fov = float(sys.argv[2]); asp = float(sys.argv[3])
half = math.radians(fov) / 2
cand = {'deg': fov, 'rad': math.radians(fov), 'halfrad': half, 'tan': math.tan(half), 'cot': 1 / math.tan(half), 'tan*asp': math.tan(half) * asp, 'cot/asp': 1 / (math.tan(half) * asp), 'hfov_deg': math.degrees(2 * math.atan(math.tan(half) * asp)), 'hfov_tan': math.tan(half) * asp}
lo, hi = 0x1000, 0x700000
for off in range(lo, hi, 0x10000):
    d = p.rd(p.base + off, 0x10000)
    if not d: continue
    fl = struct.unpack('<%df' % (len(d) // 4), d)
    for i, v in enumerate(fl):
        for n, c in cand.items():
            if abs(v - c) < 1e-4 * max(1, abs(c)): print(hex(off + i * 4), n, v)
