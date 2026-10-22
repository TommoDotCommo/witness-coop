# usage: wread.py <pid> [entity ids...]   camera and per-entity position and power (read-only)
import os, sys, struct
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1]))
cam, ang = p.camera()
print('base', hex(p.base)); print('camera pos', cam); print('camera ang', ang)
for s in sys.argv[2:]:
    eid = int(s, 0); e = p.entity(eid)
    if not e: print('entity', hex(eid), 'null'); continue
    print('entity', hex(eid), 'pos', struct.unpack('<3f', p.rd(e + 0x24, 12)), 'power', struct.unpack('<2f', p.rd(e + 0x2A0, 8)))
