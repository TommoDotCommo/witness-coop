"""Draw a given path on a panel through in-process input: teleport with the dev menu, enter the puzzle, trace the node list, finish.

Usage: Windows Python wdraw.py <pid> <game folder> <panel id hex> <node,node,...> [--no-teleport]
The path is a list of panel node indices from a start node to an end node (the end stub included). Prints one JSON line:
{"id", "solved", "why", "traced", "dist"}. Same geometry as wbot_maze.py (222 px per panel unit at 3 m, 2 counts per pixel, closed loop on
the traced-node count at +0x228); the distance actually reached after the menu teleport scales the pixel size."""
import os, sys, math, struct, time, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir, eid = int(sys.argv[1]), sys.argv[2], int(sys.argv[3], 16)
path = [int(v) for v in sys.argv[4].split(',')]
PX_PER_UNIT_3M, COUNTS_PER_PX, STEP = 222.0, 2.0, 16
p = attach(pid); c = Control(gdir); e = p.entity(eid)
import threading
class Hover:
    """Re-arms the proxy's 12-frame teleport every 0.2 s so the player stays at a point with no ground under it (the world around
    far panels is not loaded: a released player falls and the game puts it back at the spawn point)."""
    def __init__(self): self.line = None; self.lock = threading.Lock(); threading.Thread(target=self.run, daemon=True).start()
    def run(self):
        while True:
            time.sleep(0.2)
            if self.line:
                with self.lock: c_send(self.line, 0)
hover = Hover(); c_send = c.send
def locked_send(lines, settle=0.0):
    with hover.lock: c_send(lines, 0)
    time.sleep(settle)
c.send = locked_send

def done(solved, why, traced=0, dist=0.0):
    if not solved and 'hover' in globals() and hover.line: c.send(['shot fail_%x' % eid], 1.0)      # what the game showed, taken while the player is still held
    print(json.dumps({'id': eid, 'solved': solved, 'why': why, 'traced': traced, 'dist': round(dist, 2)}), flush=True); sys.exit(0 if solved else 1)

def is_solved(): return p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
def traced(): return p.u32(e + 0x228)
if not e or p.type_of(e) != TI_PANEL: done(False, 'not a panel')
if is_solved(): done(True, 'already solved')
n = p.u32(e + 0x3b4)
dots = struct.unpack('<%df' % (2 * n), p.rd(p.u64(e + 0x3c0), 8 * n))
px_, py_, pz_ = struct.unpack('<3f', p.rd(e + 0x24, 12)); qz, qw = struct.unpack('<2f', p.rd(e + 0x3c, 8)); rz = 2 * math.atan2(qz, qw)

def standing(pos):      # distance along the panel's normal and the sideways offset of a camera position
    ux, uy = pos[0] - px_, pos[1] - py_; return ux * math.cos(rz) + uy * math.sin(rz), -ux * math.sin(rz) + uy * math.cos(rz)

def menu_teleport():
    """The dev menu's teleport (the feature under test). The player is held at once (the hover thread), because a released player over
    ground that is not loaded falls and is put back at the spawn point a couple of seconds later."""
    for attempt in range(3):
        c.send(['menusel %x' % eid, 'menudo teleport'], 1.3)
        pos, ang = p.camera()
        if math.dist(pos[:2], (px_, py_)) < 6: return pos
        time.sleep(3)
    done(False, 'teleport refused: camera at %.1f %.1f' % pos[:2])

pscale = struct.unpack('<f', p.rd(e + 0x44, 4))[0]      # panel scale: 0.484 is the tutorial panel, seen at 3 m
if not 0.05 < pscale < 20: pscale = 0.484
yaw = math.remainder(rz + math.pi, 2 * math.pi)
pos = menu_teleport() if '--no-teleport' not in sys.argv else p.camera()[0]
hover.line = ['tp %.4f %.4f %.4f %.5f 0.0' % (pos[0], pos[1], pos[2], yaw)]      # held where the menu put it (it drops in from above the panel)
dist = max(2.0, 3.0 * pscale / 0.484)      # farther back for a big panel; the player floats, so there is always room
hover.line = ['tp %.4f %.4f %.4f %.5f 0.0' % (px_ + dist * math.cos(rz), py_ + dist * math.sin(rz), pz_ + 0.3, yaw)]      # head-on at eye level
c.send([], 2.5)
scale = PX_PER_UNIT_3M * 3.0 / dist * pscale / 0.484
def node_px(i): return (dots[2 * i] - 0.5) * scale, -(dots[2 * i + 1] - 0.5) * scale

c.click(settle=2.5)                                       # enter the puzzle: the cursor appears at the screen centre
dx, dy = node_px(path[0])
steps = max(1, int(math.ceil(max(abs(dx), abs(dy)) * COUNTS_PER_PX / 10)))
c.send(['mouse %d %d' % (round(dx * COUNTS_PER_PX / steps), round(dy * COUNTS_PER_PX / steps))] * steps, 1.0)
c.click(settle=0.8)                                       # press and release on the start circle: the line starts
if traced() < 1: done(False, 'the line did not start', 0, dist)
end = path[-1]
for i in range(1, len(path)):
    (x0, y0), (x1, y1) = node_px(path[i - 1]), node_px(path[i])
    ux, uy = x1 - x0, y1 - y0; norm = math.hypot(ux, uy) or 1.0
    mx, my = round(STEP * ux / norm), round(STEP * uy / norm)
    for tries in range(80):
        if i == len(path) - 1 and tries >= 4: break
        if i < len(path) - 1 and traced() >= i + 1: break
        c.send(['mouse %d %d' % (mx, my)], 0.1)
    else: done(False, 'stuck before node %d (path index %d)' % (path[i], i), traced(), dist)
c.send(['mouse 0 0'], 1.0)
c.click(hold=0.12, settle=3)
done(is_solved(), '' if is_solved() else 'finished but the game did not accept it', traced(), dist)
