"""Teleport tour for manual testing: numbered spots across the island, each in front of a real panel.
Usage (Windows Python): wtour.py <pid> <game folder> <state file> list | go N | next | prev | here
A spot is a 80 m map cell with panels in it; its panel is the one nearest the cell's centre, and you land 3 m in front of it, facing it.
Spots are numbered by distance from the first tutorial panel (0x64). Some places cannot be entered by teleport (the game returns the
player to the spawn point): that is reported, and a drop from above and other distances are tried first. Both games can be sent the same spot."""
import os, sys, math, struct, time, collections
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir, state_file, cmd = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
p = attach(pid); c = Control(gdir); tab = p.entity_table()
panels = {}
for i, e in enumerate(tab):
    if e and p.type_of(e) == TI_PANEL:
        x, y, z = struct.unpack('<3f', p.rd(e + 0x24, 12)); qz, qw = struct.unpack('<2f', p.rd(e + 0x3c, 8))
        if abs(x) + abs(y) > 1: panels[i] = (x, y, z, 2 * math.atan2(qz, qw), p.u32(e + 0x298) == 1)
cells = collections.defaultdict(list)
for i, (x, y, z, rz, s) in panels.items(): cells[(int(x // 80), int(y // 80))].append(i)
ox, oy = panels[0x64][:2]
spots = []
for key, ids in cells.items():
    cx, cy = (key[0] + .5) * 80, (key[1] + .5) * 80
    best = min(ids, key=lambda i: math.hypot(panels[i][0] - cx, panels[i][1] - cy))
    spots.append((math.hypot(panels[best][0] - ox, panels[best][1] - oy), best, len(ids), sum(panels[i][4] for i in ids)))
spots.sort(); spots = [(k + 1,) + s[1:] for k, s in enumerate(spots)]
def cur():
    try: return int(open(state_file).read())
    except Exception: return 0
def go(n):
    n = max(1, min(len(spots), n)); _, pid_, count, solved = spots[n - 1]; x, y, z, rz, _ = panels[pid_]
    for dist, dz in ((3, .3), (3, 8), (2, 8), (4, 8), (5, 12)):
        sx, sy = x + dist * math.cos(rz), y + dist * math.sin(rz)
        c.send(['tp %.3f %.3f %.3f %.5f 0.0' % (sx, sy, z + dz, math.remainder(rz + math.pi, 2 * math.pi))], 2.5)
        pos, _ = p.camera()
        if math.dist(pos[:2], (sx, sy)) < 1.0: break
    else:
        print('spot %d (panel %#x): the game refused every try and put the player back at %.0f %.0f. This place cannot be entered by teleport.' % (n, pid_, pos[0], pos[1])); open(state_file, 'w').write(str(n)); return
    open(state_file, 'w').write(str(n))
    print('spot %d of %d: in front of panel %#x (%d panels in this area, %d solved in this game)' % (n, len(spots), pid_, count, solved))
if cmd == 'list':
    for n, pid_, count, solved in spots: print('%3d  panel %#07x  at %7.0f %7.0f %6.0f  %3d panels, %3d solved here' % (n, pid_, panels[pid_][0], panels[pid_][1], panels[pid_][2], count, solved))
elif cmd == 'go': go(int(sys.argv[5]))
elif cmd == 'next': go(cur() + 1)
elif cmd == 'prev': go(cur() - 1)
elif cmd == 'here':
    pos, _ = p.camera(); near = min(panels, key=lambda i: math.dist(pos[:2], panels[i][:2])); print('player at %.1f %.1f %.1f, nearest panel %#x (%.1f m)' % (*pos, near, math.dist(pos[:2], panels[near][:2])))
