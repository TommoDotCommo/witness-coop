"""Read a panel's puzzle graph, build 11370412. Usage: wpanel.py <pid> <hex id>.

Field names/offsets: witness-randomizer Source/Memory.cpp (GLOBALS == 0x62D0A0).
This probe never writes to the game.
"""
import json, os, struct, sys
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); eid = int(sys.argv[2], 0); e = p.entity(eid)
if not e or p.type_of(e) != TI_PANEL: raise SystemExit('not a Machine_Panel')
def array(off, fmt, count):
    if not 0 <= count <= 4096: raise ValueError('invalid puzzle array count')
    ptr = p.u64(e + off)
    return list(struct.unpack('<' + fmt * count, p.rd(ptr, struct.calcsize('<' + fmt) * count))) if count else []
n, m = p.u32(e + 0x3b4), p.u32(e + 0x3b8)
table_base_id = p.u32(p.u64(p.base + GLOBALS) + 8)
target = p.u32(e + 0x2b4)
print(json.dumps(dict(id=hex(eid), position=struct.unpack('<3f', p.rd(e+0x24,12)),
    orientation=struct.unpack('<4f', p.rd(e+0x34,16)),
    power=struct.unpack('<4f',p.rd(e+0x2a0,16)), target=hex(target), table_base_id=table_base_id,
    target_index=hex(target-table_base_id) if target >= table_base_id and target != 0 else None,
    grid=[p.u32(e+0x440),p.u32(e+0x444)], dots=array(0x3c0,'f',n*2),
    flags=array(0x3c8,'I',n), connections=list(zip(array(0x3d0,'I',m),array(0x3d8,'I',m)))),indent=2))
