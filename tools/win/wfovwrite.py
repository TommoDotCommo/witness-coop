"""Test-only: set the game's field-of-view option (a float in the image) and read the projection scale terms back.
Usage: Windows Python wfovwrite.py <pid> <degrees|0> [keep]   (0: only read; without `keep` the old value is restored afterwards)
Prints one JSON line: {"vfov": options value, "sx": m00, "sy": m11, "vfov_matrix": degrees from m11}."""
import os, sys, struct, time, json, math
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
OPT, PX, PY = 0x6208BC, 0x649910, 0x649924
p = attach(int(sys.argv[1]), write=True); deg = float(sys.argv[2])
def state():
    sx, sy = struct.unpack('<2f', p.rd(p.base + PX, 4) + p.rd(p.base + PY, 4)); o = struct.unpack('<f', p.rd(p.base + OPT, 4))[0]
    return {'vfov': round(o, 4), 'sx': sx, 'sy': sy, 'vfov_matrix': round(2 * math.degrees(math.atan(1 / abs(sy))), 3), 'aspect_matrix': round(abs(sy / sx), 4), 'm': [round(v, 4) for v in struct.unpack('<56f', p.rd(p.base + 0x649900, 224))]}
if deg:
    old = p.rd(p.base + OPT, 4); p.wr(p.base + OPT, struct.pack('<f', deg)); time.sleep(2.0)
    if len(sys.argv) < 4 or sys.argv[3] != 'keep': print(json.dumps(state())); p.wr(p.base + OPT, old); time.sleep(1.0)
print(json.dumps(state()))
