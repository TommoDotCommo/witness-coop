# usage: wstatedump.py <pid>   one JSON line: solved/powered panels, open doors and lit cables (power requires requested and visible level 1;
#                              a cable is lit when its +0xc4 and +0xc8 are 1.0)
import os, sys, json
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); panels, doors, powered, cables, links = {}, {}, {}, {}, {}
for i, e in enumerate(p.entity_table()):
    if not e: continue
    t = p.type_of(e)
    if t == TI_PANEL:
        panels[i] = p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
        links[i] = p.u32(e + 0x2c8)
        powered[i] = p.u32(e + 0x2a0) == 0x3f800000 and p.u32(e + 0x2a4) == 0x3f800000
    elif t == TI_DOOR: doors[i] = p.u32(e + 0x1d0) == 0x3f800000
    elif t == TI_CABLE: cables[i] = p.u32(e + 0xc4) == 0x3f800000 and p.u32(e + 0xc8) == 0x3f800000
print(json.dumps({'panels': {hex(k_): True for k_, v in panels.items() if v}, 'doors': {hex(k_): True for k_, v in doors.items() if v}, 'powered_panels': {hex(k_): True for k_, v in powered.items() if v}, 'lit_cables': {hex(k_): True for k_, v in cables.items() if v}, 'power_src': {hex(k_): hex(v) for k_, v in links.items() if v}, 'n_panels': len(panels), 'n_doors': len(doors), 'n_cables': len(cables)}))
