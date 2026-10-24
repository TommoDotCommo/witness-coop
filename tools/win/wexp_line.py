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

def sequence():      # the nodes the game says are traced (panel +0x454 length, +0x458 array)
    n_ = p.u32(e + 0x454); ptr = p.u64(e + 0x458)
    return list(struct.unpack('<%dI' % n_, p.rd(ptr, 4 * n_))) if 0 < n_ < 500 and ptr else []

def done(solved, why, traced=0, dist=0.0):
    if not solved: c.send(['shot fail_%x' % eid], 1.0)      # what the game showed, taken while the player is still held
    c.send(['button R down'], 0.12); c.send(['button R up'], 0.5)      # leave the puzzle if it is still entered (a right click does nothing otherwise)
    c.send(['hover off'], 0.3)
    print(json.dumps({'id': eid, 'solved': solved, 'why': why, 'traced': traced, 'dist': round(dist, 2), 'seq': sequence(), 'want': path}), flush=True); sys.exit(0 if solved else 1)

def is_solved(): return p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
def traced(): return p.u32(e + 0x228)
if not e or p.type_of(e) != TI_PANEL: done(False, 'not a panel')
if is_solved(): done(True, 'already solved')
n = p.u32(e + 0x3b4)
dots = struct.unpack('<%df' % (2 * n), p.rd(p.u64(e + 0x3c0), 8 * n))
px_, py_, pz_ = struct.unpack('<3f', p.rd(e + 0x24, 12)); qz, qw = struct.unpack('<2f', p.rd(e + 0x3c, 8)); rz = 2 * math.atan2(qz, qw)

def standing(pos):      # distance along the panel's normal and the sideways offset of a camera position
    ux, uy = pos[0] - px_, pos[1] - py_; return ux * math.cos(rz) + uy * math.sin(rz), -ux * math.sin(rz) + uy * math.cos(rz)

def stand_point(d_): return (px_ + d_ * math.cos(rz), py_ + d_ * math.sin(rz))
def menu_teleport():
    """The dev menu's teleport (the feature under test): 3 m in front of the panel, held 2 s while the player settles on the ground there."""
    for attempt in range(3):
        c.send(['menusel %x' % eid, 'menudo teleport'], 4.0)
        pos, ang = p.camera()
        if math.dist(pos[:2], stand_point(3.0)) < 0.5 and abs(pos[2] - pz_) < 0.6: return pos
        time.sleep(3)
    done(False, 'teleport refused: camera at %.1f %.1f %.1f (wanted %.1f %.1f %.1f)' % (*pos, *stand_point(3.0), pz_))

pscale = struct.unpack('<f', p.rd(e + 0x44, 4))[0]      # panel scale: 0.484 is the tutorial panel, seen at 3 m
if not 0.05 < pscale < 20: pscale = 0.484
yaw = math.remainder(rz + math.pi, 2 * math.pi)
pos = menu_teleport() if '--no-teleport' not in sys.argv else p.camera()[0]
dist = math.dist(pos[:2], (px_, py_))
hz = [pos[0], pos[1], pos[2]]
def look_back():      # face the panel again (free look moves the yaw): the angles are set by a teleport to where we already stand
    c.send(['tp %.4f %.4f %.4f %.5f 0.0' % (pos[0], pos[1], pos[2], yaw)], 1.0)
def rclick(): c.send(['button R down'], 0.12); c.send(['button R up'], 0.7)
def in_puzzle():
    """True while the mouse moves the puzzle cursor instead of the view: send a little mouse motion and see whether the camera yaw changed."""
    y0 = p.camera()[1][0]; c.send(['mouse 40 0'], 0.4); y1 = p.camera()[1][0]
    if abs(y1 - y0) > 1e-4: look_back(); return False      # free look: put the view back
    return True


def rclick(): c.send(['button R down'], 0.12); c.send(['button R up'], 0.7)
def snapall():
    out = {}
    for o in range(0, 0x500, 8):
        v = p.u64(e + o)
        if v and 0x10000 < v < 0x7fffffffffff and v % 4 == 0 and not (p.base <= v < p.base + 0x6000000):
            raw = p.rd(v, 0x200)
            if raw: out[o] = raw
    out['self'] = p.rd(e, 0x500)
    return out
# enter, then move the free cursor in known steps and see which words follow it
if in_puzzle(): rclick()
if in_puzzle(): pass
c.click(settle=2.0)
if not in_puzzle(): print('could not enter', file=sys.stderr)
rclick(); c.click(settle=2.0)
snaps = [snapall()]
for dx_, dy_ in ((40, 0), (40, 0), (0, 40), (0, 40), (-80, -80)):
    c.send(['mouse %d %d' % (dx_, dy_)], 0.5); snaps.append(snapall())
for key in snaps[0]:
    base = snaps[0][key]
    for j in range(0, len(base), 4):
        vals = [struct.unpack('<f', sn[key][j:j + 4])[0] if key in sn else None for sn in snaps]
        if None not in vals and len(set(vals)) > 2 and all(abs(v) < 1e4 for v in vals) and key != 'self':
            print('ptr@+%s [%#x]' % (hex(key) if key != 'self' else key, j), ' '.join('%.4f' % v for v in vals), file=sys.stderr)
done(False, 'experiment')
