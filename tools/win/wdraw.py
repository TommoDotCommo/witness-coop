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
T_START = time.time()
def stamp(what):
    if os.environ.get('WDRAW_TIMING'): print('%6.1f s  %s' % (time.time() - T_START, what), file=sys.stderr)

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
        c.send(['menusel %x' % eid, 'menudo teleport'], 3.2)
        pos, ang = p.camera()
        dn_, lat_ = standing(pos)
        if 1.2 < dn_ < 7 and abs(lat_) < 1.6 and abs(pos[2] - pz_) < 1.6: return pos      # near enough: the pixel size and the shift of the panel on the screen are worked out from where we really stand
        time.sleep(3)
    done(False, 'teleport refused: camera at %.1f %.1f %.1f (wanted %.1f %.1f %.1f)' % (*pos, *stand_point(3.0), pz_))

pscale = struct.unpack('<f', p.rd(e + 0x44, 4))[0]      # panel scale: 0.484 is the tutorial panel, seen at 3 m
if not 0.05 < pscale < 20: pscale = 0.484
yaw = math.remainder(rz + math.pi, 2 * math.pi)
stamp('start'); pos = menu_teleport() if '--no-teleport' not in sys.argv else p.camera()[0]
stamp('teleported'); dist = math.dist(pos[:2], (px_, py_))
hz = [pos[0], pos[1], pos[2]]
def look_back():      # face the panel again (free look moves the yaw): the angles are set by a teleport to where we already stand
    c.send(['tp %.4f %.4f %.4f %.5f 0.0' % (pos[0], pos[1], pos[2], yaw)], 1.0)
def rclick(): c.send(['button R down'], 0.12); c.send(['button R up'], 0.7)
def in_puzzle():
    """True while the mouse moves the puzzle cursor instead of the view: send a little mouse motion and see whether the camera yaw changed."""
    y0 = p.camera()[1][0]; c.send(['mouse 40 0'], 0.4); y1 = p.camera()[1][0]
    if abs(y1 - y0) > 1e-4: look_back(); return False      # free look: put the view back
    return True

# How big a panel unit looks is not a function of anything stored in the panel that I know of: px_per_unit x distance was 585 on garden panel 0x139,
# 666 on the tutorial panel and on 0x293, 1165 x scale for others. The start circle is found by trying sizes: the cursor (which starts at the
# screen centre) is moved on from where it is, relative to the previous try, and clicked; a click that misses the circle does nothing.
# The click lands on the circle only within a few percent of the true size for a start far from the centre: scan in 3.5 % steps, nearest first.
K_CANDIDATES = [585, 666] + sorted((k_ for k_ in range(430, 1500, 25)), key=lambda k_: abs(k_ - 650))
kcache_path = os.environ.get('WDRAW_KCACHE'); kcache = {}
if kcache_path and os.path.exists(kcache_path):
    try: kcache = json.load(open(kcache_path))
    except Exception: kcache = {}
if '%x' % eid in kcache: K_CANDIDATES = [kcache['%x' % eid]] + K_CANDIDATES      # a previous run found the size of this panel
# The panel centre is not always at the screen centre: the eye rests 1.69 m above the ground, wherever the panel is mounted, and the player may stand
# a little to the side. Both shift the panel on the screen by (offset in metres) x 1033 / distance pixels (measured: 1033 px per unit-metre at 1 m).
pos_now = p.camera()[0]; lat_m = -standing(pos_now)[1]; up_m = pz_ - pos_now[2]
centre_shift = (lat_m * 1033.0 / dist, -up_m * 1033.0 / dist)
if os.environ.get('WDRAW_SHOTS'): print('panel centre shifted on screen by %.1f, %.1f px (camera %.2f m below the panel centre, %.2f m aside)' % (*centre_shift, up_m, lat_m), file=sys.stderr)
def start_off(sc): return (centre_shift[0] + (dots[2 * path[0]] - 0.5) * sc, centre_shift[1] - (dots[2 * path[0] + 1] - 0.5) * sc)
for attempt in range(3):
    # Every run leaves the puzzle at its end (a right click), so the panel starts un-entered: one click enters it and puts the cursor at the
    # centre. If the scan finds no start, the state is doubtful: leave with a right click and enter again.
    c.click(settle=1.0)
    cur = (0.0, 0.0); started = None
    for kc in K_CANDIDATES:
        sc = kc / dist; tx, ty = start_off(sc)
        ddx, ddy = (tx - cur[0]) * COUNTS_PER_PX, (ty - cur[1]) * COUNTS_PER_PX
        n_ = max(1, int(math.ceil(max(abs(ddx), abs(ddy)) / 10)))
        if abs(ddx) + abs(ddy) >= 1: c.send(['mouse %d %d' % (round(ddx / n_), round(ddy / n_))] * n_, 0.3)
        cur = (cur[0] + round(ddx / n_) * n_ / COUNTS_PER_PX, cur[1] + round(ddy / n_) * n_ / COUNTS_PER_PX)
        stamp('try K %d (attempt %d)' % (kc, attempt))
        if os.environ.get('WDRAW_KSHOTS'): c.send(['shot k_%d' % kc], 0.5)
        c.click(settle=0.5)                                   # press and release on the start circle: the line starts
        if traced() >= 1: started = kc; break
    if started: break
    rclick()
else: done(False, 'the line did not start with any panel size tried', traced(), dist)
stamp('line started with K %s' % started); scale = started / dist
if kcache_path:
    kcache['%x' % eid] = started
    try: json.dump(kcache, open(kcache_path, 'w'))
    except Exception: pass
if os.environ.get('WDRAW_SHOTS'): print('line started with K = %d (px per unit x distance), %.1f px per unit' % (started, scale), file=sys.stderr)
if os.environ.get('WDRAW_SHOTS'): c.send(['shot draw_start'], 0.8)
def node_px(i): return (dots[2 * i] - 0.5) * scale, -(dots[2 * i + 1] - 0.5) * scale      # (the shift of the panel centre cancels in every difference)      # offset from the screen centre, in pixels
# The line moves along the track by mouse counts at a gain that is the same on both axes (measured on panel 0x139 with a steady camera: 557
# counts per panel unit, 2.86 per pixel at 195 px per unit, so not the 2 per pixel of the free cursor) and that is learned again on the first edge
# of every panel. The traced-node count (+0x228) steps up exactly when the line arrives at a node (1 at the start node, i + 1 at path[i]), which
# closes the loop: each edge moves 85 % of its expected counts at once and finishes one count at a time, so the line stops at the node it is
# aiming at (an overshoot onto a straight-on edge would be the next node's count). The end stub counts too once the line is at the end.
carry = [0.0, 0.0]
def mouse_lines(vx, vy, counts, chunk=14):
    """'mouse dx dy' lines for a move of `counts` along (vx, vy): the rounding error is carried to the next move."""
    n_ = max(1, int(math.ceil(counts / chunk))); lines = []
    for _ in range(n_):
        carry[0] += vx * counts / n_; carry[1] += vy * counts / n_
        mx, my = round(carry[0]), round(carry[1]); carry[0] -= mx; carry[1] -= my
        if mx or my: lines.append('mouse %d %d' % (mx, my))
    return lines
def mouse(vx, vy, counts):
    """One batch: the proxy takes a file's lines in order, one event per frame, so a whole edge goes in one file."""
    lines = mouse_lines(vx, vy, counts)
    if lines: c.send(lines, 0.04 * len(lines) + 0.05)
def move_edge(vx, vy, counts): mouse(vx, vy, counts)
m_ = p.u32(e + 0x3b8)
conn_a = struct.unpack('<%dI' % m_, p.rd(p.u64(e + 0x3d0), 4 * m_)); conn_b = struct.unpack('<%dI' % m_, p.rd(p.u64(e + 0x3d8), 4 * m_))
neigh = {}
for a_, b_ in zip(conn_a, conn_b): neigh.setdefault(a_, []).append(b_); neigh.setdefault(b_, []).append(a_)
def straight_on(node, vx, vy):      # does an edge leave this node in (about) the direction we arrived in?
    for w_ in neigh.get(node, []):
        wx, wy = dots[2 * w_] - dots[2 * node], -(dots[2 * w_ + 1] - dots[2 * node + 1]); wn = math.hypot(wx, wy) or 1.0
        if (wx * vx + wy * vy) / wn > 0.9: return True
    return False
per_unit = None      # counts per panel unit
for i in range(1, len(path)):
    (x0, y0), (x1, y1) = node_px(path[i - 1]), node_px(path[i])
    ux, uy = x1 - x0, y1 - y0; norm = math.hypot(ux, uy) or 1.0; vx, vy = ux / norm, uy / norm
    units = norm / scale; last = i == len(path) - 1
    if last:      # the end stub: its length by dead reckoning, and only a little more. The count does not show the end, and a cursor pushed on
        # beyond the panel's edge (an end stub near the top edge leaves 11 px) cancels the line: the game then rejects the finish.
        move_edge(vx, vy, (per_unit or 560.0) * units + float(os.environ.get('WDRAW_STUB') or 10))
        if os.environ.get('WDRAW_SHOTS'): print('end stub pushed', round((per_unit or 560.0) * units + float(os.environ.get('WDRAW_STUB') or 10)), 'counts, traced', traced(), file=sys.stderr)
        continue
    used = 0.0
    if per_unit:
        first = 0.9 * per_unit * units; move_edge(vx, vy, first); used += first
    for _ in range(250):
        if traced() >= i + 1: break
        mouse(vx, vy, 3); used += 3
    else: done(False, 'stuck before node %d (path index %d)' % (path[i], i), traced(), dist)
    if traced() > i + 1: done(False, 'overshot node %d: the line ran past it (traced %d, wanted %d)' % (path[i], traced(), i + 1), traced(), dist)
    if not straight_on(path[i], vx, vy): mouse(vx, vy, 2)      # the count flips as the tip reaches the node: go a hair further so a turn is taken at the node, not before it (only where no edge goes straight on)
    if per_unit is None and units > 0.12: per_unit = used / units      # not from the start stub: the click that started the line leaves the cursor a few pixels off along it
    if os.environ.get('WDRAW_SHOTS'): print('segment', path[i - 1], '->', path[i], 'counts %.0f' % used, 'per unit', per_unit and round(per_unit), 'traced', traced(), file=sys.stderr)
stamp('path drawn'); c.send(['mouse 0 0'], 1.0)
if os.environ.get('WDRAW_SHOTS'): c.send(['shot draw_end'], 0.8); print('before the finishing click: state +0x280 =', p.u32(e + 0x280), ' +0x37c =', p.u32(e + 0x37c), ' traced', traced(), file=sys.stderr)
c.send(['button L down'], 0.12)
if os.environ.get('WDRAW_SHOTS'): c.send(['shot draw_down'], 0.05)
c.send(['button L up'], 0.12)
if os.environ.get('WDRAW_SHOTS'): c.send(['shot draw_up'], 0.05)
time.sleep(0.3)
t_end = time.time()
while time.time() - t_end < 8 and not is_solved(): time.sleep(0.25)      # the solve animation runs a few seconds before the flags change
if os.environ.get('WDRAW_SHOTS'): c.send(['shot draw_after'], 0.8); print('state +0x280', p.u32(e + 0x280), 'traced', traced(), file=sys.stderr)
stamp('finished'); done(is_solved(), '' if is_solved() else 'finished but the game did not accept it', traced(), dist)
