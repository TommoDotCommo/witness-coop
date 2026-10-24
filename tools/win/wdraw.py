"""Draw a given path on a panel through in-process input: teleport with the dev menu, enter the puzzle, trace the node list, finish.

Usage: Windows Python wdraw.py <pid> <game folder> <panel id hex> <node,node,...>
The path is a list of panel node indices from a start node to an end node (the end stub included). Prints one JSON line:
{"id", "solved", "why", "traced", "dist", "seq", "want"}; the verdict is the game's own solved flag after the line was drawn and finished.
Environment: WDRAW_KCACHE (a json file that remembers what a panel needed), WDRAW_TIMING, WDRAW_SHOTS (debug output on stderr).

What it does, and what was measured (build 11370412, 1920x1080, see NOTES.md of the hub):
 1. teleport with the dev menu (it holds the player 2 s so it settles on the ground), then aim the camera at the middle of the panel;
 2. enter the puzzle with a click; the cursor appears at the screen centre, i.e. on the middle of the panel; a 4-count mouse move that does not turn the
    view proves it was entered;
 3. find the start circle: node positions are projected with a pinhole camera (1033 px at 1 m, the panel's full rotation, its size in metres W unknown), and
    W is scanned (a few typical sizes first) with small vertical shifts until a click on the circle starts the line (traced count >= 1 or state 3);
 4. walk the line: the line follows the mouse along the track at a gain that is the same in every direction but differs per panel, so each edge moves 96 %
    of the counts the first long edge needed per pixel, then 2 counts at a time until the traced-node count (+0x228) shows the node was reached (it steps up
    exactly on arrival: i + 1 at path[i]); the end stub does not count;
 5. click to finish and wait for the solved flags (+0x294 = 1.0 and +0x298 = 1) or the game's "wrong" (state 2)."""
import os, sys, math, struct, time, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir, eid = int(sys.argv[1]), sys.argv[2], int(sys.argv[3], 16)
path = [int(v) for v in sys.argv[4].split(',')]
COUNTS_PER_PX, FOCAL = 2.0, 1033.0       # free cursor: 2 counts per pixel; projection: pixels at 1 m for a 1 m long thing
p = attach(pid); c = Control(gdir); e = p.entity(eid)
T_START = time.time()
def stamp(what):
    if os.environ.get('WDRAW_TIMING'): print('%6.1f s  %s' % (time.time() - T_START, what), file=sys.stderr)
def dbg(*a):
    if os.environ.get('WDRAW_SHOTS'): print(*a, file=sys.stderr)

def sequence():      # the solution line the game ships (panel +0x454 length, +0x458 nodes)
    n_ = p.u32(e + 0x454); ptr = p.u64(e + 0x458)
    return list(struct.unpack('<%dI' % n_, p.rd(ptr, 4 * n_))) if 0 < n_ < 500 and ptr else []

def rclick(): c.send(['button R down'], 0.12); c.send(['button R up'], 0.7)

def done(solved, why, traced=0, dist=0.0):
    if not solved: c.send(['shot fail_%x' % eid], 0.8)      # what the game showed, taken while still there
    rclick()                                                 # leave the puzzle if it is still entered (nothing happens otherwise)
    c.send(['hover off'], 0.2)
    print(json.dumps({'id': eid, 'solved': solved, 'why': why, 'traced': traced, 'dist': round(dist, 2), 'seq': sequence(), 'want': path}), flush=True); sys.exit(0 if solved else 1)

def is_solved(): return p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
def traced(): return p.u32(e + 0x228)
def state(): return p.u32(e + 0x280)
if not e or p.type_of(e) != TI_PANEL: done(False, 'not a panel')
if is_solved(): done(True, 'already solved')

# ---- the panel ------------------------------------------------------------------------------------------------------------------------------
n = p.u32(e + 0x3b4); m_ = p.u32(e + 0x3b8)
dots = struct.unpack('<%df' % (2 * n), p.rd(p.u64(e + 0x3c0), 8 * n))
conn_a = struct.unpack('<%dI' % m_, p.rd(p.u64(e + 0x3d0), 4 * m_)); conn_b = struct.unpack('<%dI' % m_, p.rd(p.u64(e + 0x3d8), 4 * m_))
neigh = {}
for a_, b_ in zip(conn_a, conn_b): neigh.setdefault(a_, []).append(b_); neigh.setdefault(b_, []).append(a_)
px_, py_, pz_ = struct.unpack('<3f', p.rd(e + 0x24, 12))
qx, qy, qz, qw = struct.unpack('<4f', p.rd(e + 0x34, 16))
pscale = struct.unpack('<f', p.rd(e + 0x44, 4))[0]

def quat_rot(v):      # rotate a local vector by the panel's quaternion
    x, y, z = v; tx = 2 * (qy * z - qz * y); ty = 2 * (qz * x - qx * z); tz = 2 * (qx * y - qy * x)
    return (x + qw * tx + (qy * tz - qz * ty), y + qw * ty + (qz * tx - qx * tz), z + qw * tz + (qx * ty - qy * tx))
NORMAL = quat_rot((1.0, 0.0, 0.0))      # the panel faces along its local +x (a yaw-only panel at rotation rz faces (cos rz, sin rz, 0))
def standing(pos):      # distance along the (horizontal part of the) panel's normal and the sideways offset of a camera position
    nx, ny = NORMAL[0], NORMAL[1]; nn = math.hypot(nx, ny) or 1.0; nx, ny = nx / nn, ny / nn
    ux, uy = pos[0] - px_, pos[1] - py_; return ux * nx + uy * ny, -ux * ny + uy * nx

# ---- teleport -------------------------------------------------------------------------------------------------------------------------------
def menu_teleport():
    """The dev menu's teleport (the feature under test): 3 m in front of the panel, held 2 s while the player settles on the ground there."""
    for attempt in range(3):
        c.send(['menusel %x' % eid, 'menudo teleport'], 3.2)
        pos, ang = p.camera(); dn_, lat_ = standing(pos)
        if 1.0 < dn_ < 8 and abs(lat_) < 2.0 and abs(pos[2] - pz_) < 2.0: return pos      # near enough: everything below is worked out from where we really stand
        time.sleep(3)
    done(False, 'teleport refused: camera at %.1f %.1f %.1f (panel at %.1f %.1f %.1f)' % (*pos, px_, py_, pz_))

stamp('start')
pos = menu_teleport()
dist = math.dist(pos, (px_, py_, pz_))
stamp('teleported')

def aim():
    """Point the camera at the middle of the panel (the angles are set by a teleport to where we already stand)."""
    cx, cy, cz = p.camera()[0]; hd = math.hypot(px_ - cx, py_ - cy) or 1.0
    c.send(['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, cz, math.atan2(py_ - cy, px_ - cx), math.atan2(pz_ - cz, hd))], 1.2)
aim()

# ---- projection -----------------------------------------------------------------------------------------------------------------------------
def cam_basis():
    (cx, cy, cz), (yaw, pitch) = p.camera()
    F = (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch))
    R = (math.sin(yaw), -math.cos(yaw), 0.0)
    U = (R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0])      # up = R x F
    return (cx, cy, cz), F, R, U
CAM = cam_basis()
def to_screen(world):      # offset from the screen centre in pixels
    (cx, cy, cz), F, R, U = CAM; d = (world[0] - cx, world[1] - cy, world[2] - cz)
    zc = d[0] * F[0] + d[1] * F[1] + d[2] * F[2]
    if zc < 0.2: zc = 0.2
    return FOCAL * (d[0] * R[0] + d[1] * R[1] + d[2] * R[2]) / zc, -FOCAL * (d[0] * U[0] + d[1] * U[1] + d[2] * U[2]) / zc
W = 0.567      # the panel's size in metres (unit 0..1): scanned below
def node_world(i):
    loc = quat_rot((0.0, (dots[2 * i] - 0.5) * W, (dots[2 * i + 1] - 0.5) * W))
    return (px_ + loc[0], py_ + loc[1], pz_ + loc[2])
def node_px(i):
    ox, oy = to_screen((px_, py_, pz_)); x, y = to_screen(node_world(i)); return x - ox, y - oy      # relative to the panel's middle (which is at the screen centre)

# ---- entering and finding the start circle --------------------------------------------------------------------------------------------------
def probe_free_look():
    """4 counts of mouse: the view turns (the yaw changes) only when the puzzle has not been entered."""
    y0 = p.camera()[1][0]; c.send(['mouse 4 0'], 0.15); y1 = p.camera()[1][0]
    c.send(['mouse -4 0'], 0.1)
    if abs(y1 - y0) > 1e-5: aim(); return True
    return False

W_LIST = [0.567, 0.645, 0.76, 0.88, 0.50, 0.70, 0.82, 0.60, 0.95, 0.45, 1.05, 1.2]
DY_LIST = [0, 12, -12, 24, -24, 36, -36, 48, -48]
kcache_path = os.environ.get('WDRAW_KCACHE'); kcache = {}
if kcache_path and os.path.exists(kcache_path):
    try: kcache = json.load(open(kcache_path))
    except Exception: kcache = {}
cands = [(w_, dy_) for dy_ in DY_LIST for w_ in W_LIST]
if 'w%x' % eid in kcache: cands.insert(0, tuple(kcache['w%x' % eid]))
pskey = 'ws%.3f' % pscale
if pskey in kcache: cands.insert(0, (kcache[pskey], 0))
started = None
for attempt in range(2):
    for _ in range(3):
        c.click(settle=1.0)                          # enter: the cursor appears at the screen centre
        if not probe_free_look(): break              # entered (the cursor moved, the view did not)
        rclick()                                     # not entered: leave and click again
    cur = (0.0, 0.0)
    for w_, dy_ in cands:
        W = w_
        sx, sy = node_px(path[0]); sy += dy_
        ddx, ddy = (sx - cur[0]) * COUNTS_PER_PX, (sy - cur[1]) * COUNTS_PER_PX
        n_ = max(1, int(math.ceil(max(abs(ddx), abs(ddy)) / 14)))
        if abs(ddx) + abs(ddy) >= 1: c.send(['mouse %d %d' % (round(ddx / n_), round(ddy / n_))] * n_, 0.035 * n_ + 0.06)
        cur = (cur[0] + round(ddx / n_) * n_ / COUNTS_PER_PX, cur[1] + round(ddy / n_) * n_ / COUNTS_PER_PX)
        c.send(['button L down', 'button L up'], 0.14)      # press and release on the start circle: the line starts
        if traced() >= 1 or state() == 3: started = (w_, dy_); break
    if started: break
    rclick()
if not started: done(False, 'the line did not start with any panel size or offset tried', traced(), dist)
W = started[0]
stamp('line started with W %.3f m, vertical offset %d px' % started)
if kcache_path:
    kcache['w%x' % eid] = list(started); kcache[pskey] = started[0]
    try: json.dump(kcache, open(kcache_path, 'w'))
    except Exception: pass

# ---- walking the line -----------------------------------------------------------------------------------------------------------------------
carry = [0.0, 0.0]
def mouse_lines(vx, vy, counts, chunk=14):
    n_ = max(1, int(math.ceil(counts / chunk))); lines = []
    for _ in range(n_):
        carry[0] += vx * counts / n_; carry[1] += vy * counts / n_
        mx, my = round(carry[0]), round(carry[1]); carry[0] -= mx; carry[1] -= my
        if mx or my: lines.append('mouse %d %d' % (mx, my))
    return lines
def mouse(vx, vy, counts):      # one batch: the proxy takes a file's lines in order, one event per frame
    lines = mouse_lines(vx, vy, counts)
    if lines: c.send(lines, 0.04 * len(lines) + 0.05)
def straight_on(node, vx, vy):      # does an edge leave this node in (about) the direction we arrived in? (screen directions)
    x0_, y0_ = node_px(node)
    for w_ in neigh.get(node, []):
        x1_, y1_ = node_px(w_); wx, wy = x1_ - x0_, y1_ - y0_; wn = math.hypot(wx, wy) or 1.0
        if (wx * vx + wy * vy) / wn > 0.9: return True
    return False

per_px = None      # mouse counts the line needs per screen pixel along an edge, learned on the first long edge
for i in range(1, len(path)):
    (x0, y0), (x1, y1) = node_px(path[i - 1]), node_px(path[i])
    ux, uy = x1 - x0, y1 - y0; norm = math.hypot(ux, uy) or 1.0; vx, vy = ux / norm, uy / norm
    last = i == len(path) - 1
    if last:      # the end stub: its length by dead reckoning and a little more (a cursor pushed past the panel's edge cancels the line)
        mouse(vx, vy, (per_px or 2.9) * norm + float(os.environ.get('WDRAW_STUB_EXTRA') or 40))
        dbg('end stub pushed', round((per_px or 2.9) * norm + 40), 'counts, traced', traced())
        continue
    used = 0.0
    if per_px and norm > 4:
        first = 0.96 * per_px * norm; mouse(vx, vy, first); used += first
    for _ in range(250):
        if traced() >= i + 1: break
        mouse(vx, vy, 2); used += 2
    else: done(False, 'stuck before node %d (path index %d)' % (path[i], i), traced(), dist)
    if traced() > i + 1: done(False, 'overshot node %d: the line ran past it (traced %d, wanted %d)' % (path[i], traced(), i + 1), traced(), dist)
    if not straight_on(path[i], vx, vy): mouse(vx, vy, 2)      # the count flips as the tip reaches the node: a hair further so a turn is taken at the node, not before it
    if per_px is None and norm > 14: per_px = used / norm
    dbg('segment', path[i - 1], '->', path[i], 'counts %.0f' % used, 'per px', per_px and round(per_px, 2), 'traced', traced())

# ---- finishing ------------------------------------------------------------------------------------------------------------------------------
stamp('path drawn'); c.send(['mouse 0 0'], 0.6)
dbg('before the finishing click: state', state(), 'traced', traced())
c.send(['button L down'], 0.12); c.send(['button L up'], 0.12)
t_end = time.time()
while time.time() - t_end < 8 and not is_solved() and state() != 2: time.sleep(0.2)      # solved flags, or the game's verdict "wrong" (state 2)
stamp('finished (state %d)' % state())
done(is_solved(), '' if is_solved() else 'finished but the game did not accept it (state %d)' % state(), traced(), dist)
