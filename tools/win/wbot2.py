"""Solve Tutorial Bend (0x182) through in-process input, after wbot.py solves the first door.

Usage: Windows Python wbot2.py <pid> <game folder>.
Standing point and input path verified on build 11370412 at the resource provider's 1920x1080 back buffer.
The panel's three graph nodes are (0.8,0.25), (0.8,0.75), (0.2,0.75): up, then left.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); c = Control(sys.argv[2])
e = p.entity(0x182)
if not e or p.type_of(e) != TI_PANEL: raise SystemExit('Tutorial Bend is missing or wrong type')
if p.u32(e + 0x298): raise SystemExit('Tutorial Bend must start unsolved')
c.send(['tp -182.71 -164.90 3.727 0.303 -0.066'], 2)
c.click(settle=2.5)
# Cursor starts at the back-buffer centre. Free cursor motion is 2 counts per back-buffer pixel here.
c.send(['mouse 14 0'] * 8 + ['mouse 0 10'] * 20, 1.5)
c.click(settle=0.5)
# Overshoot each segment to its end: short free-cursor distances do not complete a constrained trace.
c.send(['mouse 0 -18'] * 40, 2)
c.send(['mouse -18 0'] * 40, 2)
c.click(settle=4)
c.send(['shot solved_bend_by_bot'], 1)
solved = p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
print('panel 0x182 solved:', solved, flush=True)
sys.exit(0 if solved else 1)
