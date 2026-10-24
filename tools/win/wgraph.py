"""What powers what: for every cable the entity it targets (+0xcc, an engine id), for every panel the entity it targets (+0x2b4), and every entity's kind.
Usage: Windows Python wgraph.py <pid> <out json file>. Engine ids are the table index plus the table's base id (1 in this build); the output uses table indices."""
import os, sys, json
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); base_id = p.u32(p.u64(p.base + GLOBALS) + 8)
names = {TI_CABLE: 'cable', TI_PANEL: 'panel', TI_DOOR: 'door', TI_LASER: 'laser'}
out = {'cable_target': {}, 'panel_target': {}, 'types': {}}
for i, e in enumerate(p.entity_table()):
    if not e: continue
    ty = p.type_of(e)
    if ty == TI_CABLE: out['cable_target'][i] = p.u32(e + 0xcc) - base_id
    elif ty == TI_PANEL: out['panel_target'][i] = p.u32(e + 0x2b4) - base_id
    out['types'][i] = names.get(ty, 'other')
json.dump(out, open(sys.argv[2], 'w'))
print(len(out['cable_target']), 'cables,', len(out['panel_target']), 'panels')
