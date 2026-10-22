"""Naturally solve the first black/white tutorial panel (index 0x18af), powering index 0x1b.

Usage: Windows Python wbot_power.py <pid> <game folder>. Calibrated for the lab's 1920x1080 back buffer.
The middle horizontal path separates the black and white symbols. The target must remain unsolved.
"""
import os, sys, math
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); c = Control(sys.argv[2]); e = p.entity(0x18af); target = p.entity(0x1b)
if not e or p.type_of(e) != TI_PANEL or not target or p.type_of(target) != TI_PANEL: raise SystemExit('tutorial panel type mismatch')
if p.u32(e + 0x298) or p.u32(target + 0x2a4): raise SystemExit('requires fresh unsolved/unpowered chain')
# Arrive above the ground: a first long teleport at ground height can be displaced by collision resolution.
for attempt in range(3):
    c.send(['tp -158.745 -69.925 10.0 2.749 0.003'], 2.5)
    pos, _ = p.camera()
    if math.dist(pos, (-158.745, -69.925, 6.7143)) < 0.15: break
else: raise SystemExit('did not reach the calibrated standing point')
c.click(settle=2.5)
c.send(['mouse -10 0'] * 8, 1)
c.click(settle=0.5)
c.send(['mouse 18 0'] * 40, 2)
c.click(settle=4)
c.send(['shot solved_power_by_bot'], 1)
solved = p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
powered = p.u32(target + 0x2a0) == 0x3f800000 and p.u32(target + 0x2a4) == 0x3f800000
unsolved = p.u32(target + 0x298) == 0
print('panel 0x18af solved:', solved, '| target 0x1b powered:', powered, '| target unsolved:', unsolved, flush=True)
sys.exit(0 if solved and powered and unsolved else 1)
