# usage: wstatedump.py <pid>   one JSON line: solved panels and open doors, by the same rules as the proxy's detector
import os, sys, json
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); panels, doors = {}, {}
for i, e in enumerate(p.entity_table()):
    if not e: continue
    t = p.type_of(e)
    if t == TI_PANEL: panels[i] = p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
    elif t == TI_DOOR: doors[i] = p.u32(e + 0x1d0) == 0x3f800000
print(json.dumps({'panels': {hex(k_): True for k_, v in panels.items() if v}, 'doors': {hex(k_): True for k_, v in doors.items() if v}, 'n_panels': len(panels), 'n_doors': len(doors)}))
