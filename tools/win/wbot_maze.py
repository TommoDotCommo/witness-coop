"""Solve a plain maze panel (no decorations) by breadth-first search over its own graph, through in-process input.

Usage: Windows Python wbot_maze.py <pid> <game folder> <panel id hex> [--no-finish]
The panel graph (positions, flags, connections) is read from the game. Node flag 0x2 is the start, 0x1 the end. The bot stands
3 m in front of the panel (head-on: yaw = panel rotation about z + pi), enters the puzzle, puts the cursor on the start circle and
walks the shortest path in small mouse steps, checking the game's traced-node count (panel +0x228) after every step instead of
trusting a distance. Calibrated on build 11370412 at the lab's 1920x1080 back buffer: 3 m away the panel spans about 222 px per
unit of its 0..1 coordinates, and free cursor motion is 2 counts per pixel.
"""
import os, sys, math, struct, time, collections
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir, eid = int(sys.argv[1]), sys.argv[2], int(sys.argv[3], 16)
p = attach(pid); c = Control(gdir); e = p.entity(eid)
if not e or p.type_of(e) != TI_PANEL: raise SystemExit('not a Machine_Panel')
if p.u32(e + 0x298): raise SystemExit('panel must start unsolved')
PX_PER_UNIT, COUNTS_PER_PX, DIST, STEP = 222.0, 2.0, 3.0, 16

def array(off, fmt, count):
    return list(struct.unpack('<' + fmt * count, p.rd(p.u64(e + off), 4 * count * (2 if fmt == 'Q' else 1)))) if count else []
n, m = p.u32(e + 0x3b4), p.u32(e + 0x3b8)
dots, flags = array(0x3c0, 'f', n * 2), array(0x3c8, 'I', n)
adj = collections.defaultdict(list)
for a, b in zip(array(0x3d0, 'I', m), array(0x3d8, 'I', m)): adj[a].append(b); adj[b].append(a)
start = [i for i in range(n) if flags[i] & 0x2 and not flags[i] & 0x1]; end = [i for i in range(n) if flags[i] & 0x1]
if len(start) < 1 or len(end) != 1: raise SystemExit('need a start and one end, found %s %s' % (start, end))
start = start[0]; end = end[0]
prev = {start: None}; queue = collections.deque([start])
while queue:
    v = queue.popleft()
    for w in adj[v]:
        if w not in prev: prev[w] = v; queue.append(w)
if end not in prev: raise SystemExit('no path from start to end')
path = [end]
while prev[path[-1]] is not None: path.append(prev[path[-1]])
path.reverse()
print('panel %#x: %d nodes, path %s' % (eid, n, path), flush=True)

# stand in front of it, looking straight at it from eye height
px_, py_, pz_ = struct.unpack('<3f', p.rd(e + 0x24, 12)); qz, qw = struct.unpack('<2f', p.rd(e + 0x3c, 8))
rz = 2 * math.atan2(qz, qw); sx, sy = px_ + DIST * math.cos(rz), py_ + DIST * math.sin(rz)
yaw = math.remainder(rz + math.pi, 2 * math.pi)
for attempt in range(3):
    c.send(['tp %.4f %.4f %.4f %.5f 0.0' % (sx, sy, pz_ + 0.3, yaw)], 2.5)
    pos, _ = p.camera()
    if math.dist(pos[:2], (sx, sy)) < 0.15 and abs(pos[2] - pz_) < 0.3: break
else: raise SystemExit('did not reach the standing point: %s' % (pos,))
def node_px(i): return (dots[2 * i] - 0.5) * PX_PER_UNIT, -(dots[2 * i + 1] - 0.5) * PX_PER_UNIT      # offset from the screen centre
c.click(settle=2.5)                                  # enter the puzzle: the cursor appears at the screen centre
dx, dy = node_px(start)
steps = int(math.ceil(max(abs(dx), abs(dy)) * COUNTS_PER_PX / 10))
c.send(['mouse %d %d' % (round(dx * COUNTS_PER_PX / steps), round(dy * COUNTS_PER_PX / steps))] * steps, 1.0)
c.click(settle=0.8)                                  # press and release on the start circle: the line starts
def traced(): return p.u32(e + 0x228)
if traced() < 1: raise SystemExit('the line did not start')
for i in range(1, len(path)):
    (x0, y0), (x1, y1) = node_px(path[i - 1]), node_px(path[i])
    ux, uy = x1 - x0, y1 - y0; norm = math.hypot(ux, uy)
    mx, my = round(STEP * ux / norm), round(STEP * uy / norm)
    for tries in range(60):
        if path[i] == end and tries >= 4: break         # the end stub does not add to the traced count: push past it and finish
        if path[i] != end and traced() >= i + 1: break
        c.send(['mouse %d %d' % (mx, my)], 0.1)
    else: raise SystemExit('stuck before node %d (path index %d): traced %d' % (path[i], i, traced()))
c.send(['mouse 0 0'], 1.5)
if '--no-finish' not in sys.argv: c.click(hold=0.12, settle=4)
c.send(['shot maze_%x' % eid], 1)
solved = p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
print('panel %#x solved: %s' % (eid, solved), flush=True)
sys.exit(0 if solved else 1)
