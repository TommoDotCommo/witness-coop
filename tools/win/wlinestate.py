"""The line on one panel of a running game, read from the game's memory (build 11370412): traced edges (+0x228), the trace state (+0x280: 0 drawing or
held, 2 the game's verdict "wrong", 3 taken back), solved flags (+0x294 1.0 and +0x298 1). Prints one JSON line. Read-only.
usage: wlinestate.py <pid> <panel id hex>"""
import json, os, sys
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); e = p.entity(int(sys.argv[2], 16))
if not e or p.type_of(e) != TI_PANEL: print(json.dumps({'ok': False})); sys.exit(1)
print(json.dumps({'ok': True, 'traced': p.u32(e + 0x228), 'state': p.u32(e + 0x280), 'solved': p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1}))
