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
def edge_progress():
    """(progress t, end node) of the edge the line is on: the last traced-edge record (count - 1), 0x34 bytes each (index_a, index_b, id_a, id_b
    as ints, then t). None when there is no such record."""
    n_ = traced(); ptr = p.u64(e + 0x230)
    if not ptr or n_ < 1 or n_ > 500: return None
    raw = p.rd(ptr + (n_ - 1) * 0x34, 0x14)
    if not raw: return None
    return struct.unpack('<f', raw[16:20])[0], struct.unpack('<2I', raw[:8])[1]
def first_t():      # how far the line has got along its first edge (traced-edge records at [+0x230], 0x34 bytes each, t at +0x10): 1.0 on a finished line, near 0 on a new one
    ptr = p.u64(e + 0x230)
    return struct.unpack('<f', p.rd(ptr + 0x10, 4))[0] if ptr else 1.0
def state(): return p.u32(e + 0x280)
if not e or p.type_of(e) != TI_PANEL: done(False, 'not a panel')
REDRAW = bool(os.environ.get('WDRAW_REDRAW'))      # draw again over a solved panel (another solution: a panel whose other exit powers something else)
if is_solved() and not REDRAW: done(True, 'already solved')

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
FLIP = bool(os.environ.get('WDRAW_FLIP_SIDE'))      # stand on the other side: for a panel whose start was not found from the front (seen from behind, or through a cage)
if FLIP: NORMAL = (-NORMAL[0], -NORMAL[1], -NORMAL[2])
def standing(pos):      # distance along the (horizontal part of the) panel's normal and the sideways offset of a camera position
    nx, ny = NORMAL[0], NORMAL[1]; nn = math.hypot(nx, ny) or 1.0; nx, ny = nx / nn, ny / nn
    ux, uy = pos[0] - px_, pos[1] - py_; return ux * nx + uy * ny, -ux * ny + uy * nx

# ---- teleport -------------------------------------------------------------------------------------------------------------------------------
def menu_teleport():
    """The dev menu's teleport (the feature under test): 3 m in front of the panel, held 2 s while the player settles on the ground there."""
    if os.environ.get('WDRAW_NO_TELEPORT'): return p.camera()[0]      # a human is standing in front of the panel: draw from where they are (the menu teleport may land off the platform they are on)
    for attempt in range(3):
        c.send(['menusel %x' % eid, 'menudo teleport'], 3.2)
        pos, ang = p.camera(); dn_, lat_ = standing(pos)
        if 1.0 < dn_ < 8 and abs(lat_) < 2.0 and abs(pos[2] - pz_) < 2.0: return pos      # near enough: everything below is worked out from where we really stand
        time.sleep(3)
    # refused three times: no collision is loaded there (the game keeps putting the player back at the spawn point). Hold the player in the air in front of the
    # panel for the whole run instead (the hover command holds for 10 minutes; `hover off` in done() lets go).
    nx, ny = NORMAL[0], NORMAL[1]; nn = math.hypot(nx, ny) or 1.0
    hx, hy = px_ + 3.0 * nx / nn, py_ + 3.0 * ny / nn
    for attempt in range(3):
        c.send(['hover %.4f %.4f %.4f %.5f 0.0' % (hx, hy, pz_, math.atan2(-ny, -nx))], 2.5)
        pos, ang = p.camera(); dn_, lat_ = standing(pos)
        if 1.0 < dn_ < 8 and abs(lat_) < 2.0 and abs(pos[2] - pz_) < 2.0: dbg('held in the air in front of the panel'); return pos
    done(False, 'teleport refused and the player could not be held there: camera at %.1f %.1f %.1f (panel at %.1f %.1f %.1f)' % (*pos, px_, py_, pz_))

stamp('start')
FLAT = abs(NORMAL[2]) > 0.7      # a panel lying flat (the Desert laser redirect control on the town's roof faces the sky): stand beside it, look down
def stand_beside_flat():
    for ax_ in ((0.0, 1.0, 0.0), (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)):      # the panel's own sides, made horizontal
        sx_, sy_, _ = quat_rot(ax_); sn_ = math.hypot(sx_, sy_)
        if sn_ < 0.3: continue
        hx_, hy_ = px_ + 1.6 * sx_ / sn_, py_ + 1.6 * sy_ / sn_
        c.send(['hover %.4f %.4f %.4f %.5f -0.8' % (hx_, hy_, pz_ + 1.65, math.atan2(py_ - hy_, px_ - hx_))], 2.4)
        aim(); r_ = depth_ratio(); stamp('flat panel: beside it at (%.1f, %.1f), depth ratio %.2f' % (hx_, hy_, r_))
        if 0.7 < r_ < 1.45: return p.camera()[0]
    return p.camera()[0]
pos = p.camera()[0] if FLAT else menu_teleport()      # (a flat panel: stood beside it below, once aim and the depth check exist)
STAND = os.environ.get('WDRAW_STAND')      # "sideways,distance" in metres: stand there instead (part of the panel was hidden from the menu's spot: the
if STAND and not FLIP:                     # cursor only reaches what the camera sees, so a line stops at the edge of a wall in front of the panel)
    lat_, dd_ = (float(v) for v in STAND.split(',')); nx_, ny_ = NORMAL[0], NORMAL[1]; nn_ = math.hypot(nx_, ny_) or 1.0; nx_, ny_ = nx_ / nn_, ny_ / nn_
    hx_, hy_ = px_ + dd_ * nx_ - lat_ * ny_, py_ + dd_ * ny_ + lat_ * nx_
    c.send(['hover %.4f %.4f %.4f %.5f 0.0' % (hx_, hy_, pz_ + 0.2, math.atan2(py_ - hy_, px_ - hx_))], 2.4); c.send(['hover off'], 1.5)
    pos = p.camera()[0]; stamp('stood at %.1f m sideways, %.1f m out' % (lat_, dd_))
if FLIP:      # the menu's teleport puts us on its side: move to the other one (3 m along the flipped normal), then let the player settle there
    nx_, ny_ = NORMAL[0], NORMAL[1]; nn_ = math.hypot(nx_, ny_) or 1.0; hx_, hy_ = px_ + 3.0 * nx_ / nn_, py_ + 3.0 * ny_ / nn_
    c.send(['hover %.4f %.4f %.4f %.5f 0.0' % (hx_, hy_, pz_ + 0.2, math.atan2(py_ - hy_, px_ - hx_))], 2.4); c.send(['hover off'], 1.5)
    pos = p.camera()[0]; stamp('moved to the other side of the panel')
dist = math.dist(pos, (px_, py_, pz_))
stamp('teleported')

def aim():
    """Point the camera at the middle of the panel (the angles are set by a teleport to where we already stand)."""
    cx, cy, cz = p.camera()[0]; hd = math.hypot(px_ - cx, py_ - cy) or 1.0
    c.send(['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, cz, math.atan2(py_ - cy, px_ - cx), math.atan2(pz_ - cz, hd))], 0.4)
aim()

def depth_ratio():
    """The game's scene depth at the screen centre against what an unobstructed panel at this distance gives (stored depth = 0.0597 / metres, measured):
    about 1 when the panel is what we are looking at, far from it when a wall or the ground is in the way."""
    log = os.path.join(gdir, 'witness_proxy.log')
    try: n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('probe:'))
    except OSError: return 1.0
    c.send(['probe'], 0.0)
    for _ in range(20):
        time.sleep(0.15)
        lines = [l for l in open(log, errors='ignore') if l.startswith('probe:')]
        if len(lines) > n0:
            v = float(lines[-1].split('centre samples ')[1].split(' ')[0]); cx, cy, cz = p.camera()[0]
            return v / (0.0597 / max(0.3, math.dist((cx, cy, cz), (px_, py_, pz_))))
    return 1.0

def other_standing_points():
    """Where else to stand: other distances along the panel's normal (the first try, 3 m, may be inside a wall or over a drop)."""
    nx, ny = NORMAL[0], NORMAL[1]; nn = math.hypot(nx, ny) or 1.0; nx, ny = nx / nn, ny / nn
    return [(px_ + d_ * nx, py_ + d_ * ny) for d_ in (2.0, 4.0, 1.6, 5.0, 2.5, 3.5)]

def centre_on_panel():
    """The panel's middle may not be its entity's origin (Quarry Boat Spawn sits well below where the camera aims): find it on the screen in the
    scene depth (an 11 x 7 grid over the middle of the screen, cells at about the panel's distance) and turn the camera onto it."""
    log = os.path.join(gdir, 'witness_proxy.log')
    try: n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('probegrid:'))
    except OSError: return
    c.send(['probegrid'], 0.0); line = None
    for _ in range(30):
        time.sleep(0.15)
        ls_ = [l for l in open(log, errors='ignore') if l.startswith('probegrid:')]
        if len(ls_) > n0: line = ls_[-1]; break
    if not line or 'failed' in line or 'no scene' in line: return
    v_ = [float(x) for x in line.split()[3:]]; cpos, cang = p.camera(); d_ = math.dist(cpos, (px_, py_, pz_)); want = 0.0597 / max(0.3, d_)
    cells = [(0.2 + 0.6 * (k % 11) / 10, 0.2 + 0.6 * (k // 11) / 6) for k, dv in enumerate(v_) if dv > 0 and 0.75 < dv / want < 1.35]
    if len(cells) < 2: stamp('panel not found in the depth grid'); return
    fx = sum(x for x, _ in cells) / len(cells); fy = sum(y for _, y in cells) / len(cells)
    if abs(fx - 0.5) < 0.04 and abs(fy - 0.5) < 0.04: return
    dyaw = -math.atan((fx - 0.5) * 1920 / FOCAL); dpitch = -math.atan((fy - 0.5) * 1080 / FOCAL)      # screen right: yaw down; screen down: pitch down
    c.send(['tp %.4f %.4f %.4f %.5f %.5f' % (cpos[0], cpos[1], cpos[2], cang[0] + dyaw, cang[1] + dpitch)], 0.4)
    stamp('re-aimed on the panel found in the depth grid (%.2f, %.2f), %d cells' % (fx, fy, len(cells)))
if FLAT: pos = stand_beside_flat(); dist = math.dist(pos, (px_, py_, pz_)); stamp('stood beside the flat panel')
r_ = depth_ratio(); stamp('view check: depth ratio %.2f' % r_)
if not 0.7 < r_ < 1.45:
    ok_view = False
    for (hx, hy) in other_standing_points():
        c.send(['hover %.4f %.4f %.4f %.5f 0.0' % (hx, hy, pz_ + 0.2, math.atan2(py_ - hy, px_ - hx))], 2.4)
        c.send(['hover off'], 1.5)
        cx, cy, cz = p.camera()[0]
        if math.hypot(cx - px_, cy - py_) > 12: continue            # put back at the spawn point: nothing to stand on there
        aim(); r_ = depth_ratio(); stamp('view check at (%.1f, %.1f): depth ratio %.2f' % (hx, hy, r_))
        if 0.7 < r_ < 1.45: ok_view = True; break
    if not ok_view:
        # glass panels (the symmetry strips on the shore) read the depth of what is behind the glass, so the check says "blocked" for a panel that is in plain view;
        # a camera that really stands in front of the panel (within 12 m, checked above) draws anyway, and a line that does not start is reported as that
        cx, cy, cz = p.camera()[0]
        if math.hypot(cx - px_, cy - py_) > 12 or os.environ.get('WDRAW_STRICT_VIEW'):
            done(False, 'the panel cannot be seen from any of the places tried (something is in the way, or there is nothing to stand on)', 0, dist)
        stamp('depth check failed everywhere; drawing anyway (glass or a thin cover?)')
    dist = math.dist(p.camera()[0], (px_, py_, pz_))
if not os.environ.get('WDRAW_NO_RECENTRE'): centre_on_panel()

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
AX = 1.0       # width/height of a panel unit on the screen: 1 for most panels, found by the scan on the others
SX, SY = 1.0, 1.0      # -1 where the panel is shown mirrored or turned (1cd3: its start, node data bottom right, is the circle at the top left)
ROLL = math.radians(float(os.environ.get('WDRAW_ROLL', '0')))      # some panels (290) draw their grid turned in the panel's plane, with nothing in the rotation fields: found by the scan, or forced
def node_world(i):
    lx, ly = (dots[2 * i] - 0.5) * W, (dots[2 * i + 1] - 0.5) * W
    if ROLL: lx, ly = lx * math.cos(ROLL) - ly * math.sin(ROLL), lx * math.sin(ROLL) + ly * math.cos(ROLL)
    loc = quat_rot((0.0, lx, ly))
    return (px_ + loc[0], py_ + loc[1], pz_ + loc[2])
def node_px(i):
    ox, oy = to_screen((px_, py_, pz_)); x, y = to_screen(node_world(i)); return (x - ox) * AX * SX, (y - oy) * SY      # relative to the panel's middle (which is at the screen centre); AX: some panels are not square

# ---- entering and finding the start circle --------------------------------------------------------------------------------------------------
def probe_free_look():
    """4 counts of mouse: the view turns (the yaw changes) only when the puzzle has not been entered."""
    y0 = p.camera()[1][0]; c.send(['mouse 4 0'], 0.15); y1 = p.camera()[1][0]
    c.send(['mouse -4 0'], 0.1)
    if abs(y1 - y0) > 1e-5: aim(); return True
    return False

W_LIST = [0.567, 0.645, 0.76, 0.88, 0.50, 0.70, 0.82, 0.60, 0.95, 0.45, 1.05, 1.2, 1.35, 1.5]
DY_LIST = [0, 12, -12, 24, -24, 36, -36, 48, -48]
kcache_path = os.environ.get('WDRAW_KCACHE'); kcache = {}
if kcache_path and os.path.exists(kcache_path):
    try: kcache = json.load(open(kcache_path))
    except Exception: kcache = {}
cands = [(w_, dy_) for dy_ in DY_LIST for w_ in W_LIST]
cached_ax = None; cached_flip = (1, 1)
if 'w%x' % eid in kcache:
    cands.insert(0, tuple(kcache['w%x' % eid][:2])); kc_ = kcache['w%x' % eid]; cached_ax = kc_[2] if len(kc_) > 2 else 1.0; cached_flip = (kc_[3], kc_[4]) if len(kc_) > 4 else (1, 1)
pskey = 'ws%.3f' % pscale
if pskey in kcache: cands.insert(0, (kcache[pskey], 0))
# panels of one kind (same scale, grid size and symmetry) are found at the same place: the class remembers the whole answer
ckey = 'c%.3f|%d|%d|%d' % (pscale, p.u32(e + 0x440), p.u32(e + 0x444), 1 if p.u64(e + 0x438) > 0x10000 else 0)
if ckey in kcache:
    kc_ = kcache[ckey]; cached_ax = kc_[2]; cached_flip = (kc_[3], kc_[4])
    class_cands = [(kc_[0], kc_[1] + d_) for d_ in (0, 12, -12, 24, -24)]      # round 0 tries only these (with the class's own flips): a wrong class guess must not eat the time of the real search
else: class_cands = None
started = None
FORCE = os.environ.get('WDRAW_FORCE')      # test: W,dy,AX,SX,SY, only this one
ROLL0 = ROLL; ROLL_CANDS = [(w2, dy2) for dy2 in (0, 12, -12, 24, -24) for w2 in (1.88, 1.75, 2.0, 1.6, 2.15, 1.45, 1.3)]
if 'r%x' % eid in kcache: ROLL0 = ROLL = math.radians(kcache['r%x' % eid]); cands.insert(0, tuple(kcache['w%x' % eid][:2])) if 'w%x' % eid in kcache else None
ROUNDS = [(1.0, 1, 1), (1.0, 1, 1), (1.4, -1, -1), (1.0, 1, 1, 45), (1.0, -1, -1, 45)] + [(ax_, sx_, sy_) for ax_ in (1.0, 1.45, 0.7, 2.0, 0.5) for (sx_, sy_) in ((-1, -1), (1, 1), (1, -1), (-1, 1)) if (ax_, sx_, sy_) != (1.0, 1, 1)]
if FORCE:
    fw, fdy, fax, fsx, fsy = [float(v) for v in FORCE.split(',')]; ROUNDS = [(fax, int(fsx), int(fsy))] * 2; cands = [(fw, int(fdy))]; cached_ax = None
OLD_TRACED = traced()      # (a redraw: the solved line's count)
for attempt, rd_ in enumerate(ROUNDS):
    ax_, sx_, sy_ = rd_[:3]; ROLL = math.radians(rd_[3]) if len(rd_) > 3 else ROLL0
    ROUND_END = (time.time() - T_START) + 70      # each round gets its own budget; the whole search is capped below
    if time.time() - T_START > float(os.environ.get('WDRAW_START_BUDGET') or 230) and attempt > 2: break      # the whole search for a start circle is capped: a panel without one is reported, not chased (WDRAW_START_BUDGET seconds)
    AX = cached_ax if (attempt == 0 and cached_ax) else ax_; SX, SY = cached_flip if attempt == 0 else (sx_, sy_)
    for _ in range(3):
        c.click(settle=1.0)                          # enter: the cursor appears at the screen centre
        if not probe_free_look(): break              # entered (the cursor moved, the view did not)
        rclick()                                     # not entered: leave and click again
    cur = (0.0, 0.0)
    for w_, dy_ in (class_cands if (attempt == 0 and class_cands) else cands if (attempt < 2 or FORCE) else ROLL_CANDS if len(rd_) > 3 else [(w2, dy2) for dy2 in (0, 12, -12, 24, -24, 36, -36) for w2 in ((0.70, 0.66, 0.74, 0.62, 0.78, 0.58, 0.82) if attempt == 2 else (0.567, 0.645, 0.70, 0.76, 0.82, 0.88, 0.95, 1.05))]):
        if time.time() - T_START > ROUND_END: break
        W = w_
        sx, sy = node_px(path[0]); sy += dy_
        ddx, ddy = (sx - cur[0]) * COUNTS_PER_PX, (sy - cur[1]) * COUNTS_PER_PX
        n_ = max(1, int(math.ceil(max(abs(ddx), abs(ddy)) / 14)))
        if abs(ddx) + abs(ddy) >= 1: c.send(['mouse %d %d' % (round(ddx / n_), round(ddy / n_))] * n_, 1.1 * c.frame * n_ + 0.02)
        cur = (cur[0] + round(ddx / n_) * n_ / COUNTS_PER_PX, cur[1] + round(ddy / n_) * n_ / COUNTS_PER_PX)
        if os.environ.get('WDRAW_KSHOTS'): c.send(['shot k_%d_%d' % (round(w_ * 1000), dy_)], 0.4)
        c.send(['button L down', 'button L up'], 0.14)      # press and release on the start circle: the line starts
        # A redraw: the solved line's count stays until a new line starts, and state 3 shows up just from entering a solved panel, so only the count
        # changing from what the old line had (a new line counts 1) says the click started it.
        if (not REDRAW and (traced() >= 1 or state() == 3)) or (REDRAW and traced() >= 1 and (traced() != OLD_TRACED or first_t() < 0.99)): started = (w_, dy_); break
    if started: break
    rclick()
if not started: done(False, 'the line did not start with any panel size or offset tried', traced(), dist)
W = started[0]
stamp('line started with W %.3f m, vertical offset %d px, width factor %.2f, flips %d %d' % (started + (AX, SX, SY)))
if kcache_path:
    kcache['w%x' % eid] = list(started) + [AX, SX, SY]; kcache['r%x' % eid] = round(math.degrees(ROLL)); kcache[pskey] = started[0]; kcache[ckey] = list(started) + [AX, SX, SY]
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
    if lines: c.send(lines, 1.15 * c.frame * len(lines) + 0.015)      # one event per frame: the next look at the traced count comes a frame after the last event
def straight_on(node, vx, vy):      # does an edge leave this node in (about) the direction we arrived in? (screen directions)
    x0_, y0_ = node_px(node)
    for w_ in neigh.get(node, []):
        x1_, y1_ = node_px(w_); wx, wy = x1_ - x0_, y1_ - y0_; wn = math.hypot(wx, wy) or 1.0
        if (wx * vx + wy * vy) / wn > 0.9: return True
    return False

axflip = {}        # axes found running the other way on this panel ('h', 'v': -1)
path_nodes_all = range(10 ** 6)
per_px = None      # (kept for the end stub) mouse counts per screen pixel on the last edge walked
gain = {}          # counts per pixel, learned per direction (h / v): when the panel's width and height are scaled differently from what the projection assumed (the swamp panels), one number overshoots the other direction
def heading(i):
    """The screen direction to push in from path[i-1] towards path[i]. Some panels put extra nodes a few millimetres apart on a track (the
    tutorial gate's diamond lattice: pairs 4 mm apart beside each crossing); between two of them the projected direction is rounding noise, and
    at a turn it pushed the line the wrong way. So a short step heads for the first node along the path that is far enough to give a direction."""
    x0_, y0_ = node_px(path[i - 1])
    for j in range(i, len(path)):
        x1_, y1_ = node_px(path[j])
        if math.hypot(x1_ - x0_, y1_ - y0_) >= 6: return x1_ - x0_, y1_ - y0_
    x1_, y1_ = node_px(path[i]); return x1_ - x0_, y1_ - y0_
TRACER = bool(os.environ.get('WDRAW_TRACER'))      # hand the edges after the first to the game's own tracer (proxy/tracer.cpp): one frame per step, no round trips
def run_tracer(i0):
    """Draw path[i0:] with the in-game tracer: per edge its screen direction, the planned push (90 % of what the gain learned on the first
    edge says), the traced count it must reach, and a hair past the node where the line turns; the end stub by dead reckoning."""
    items = []
    for j in range(i0, len(path)):
        (xa, ya), (xb, yb) = node_px(path[j - 1]), node_px(path[j]); nrm = math.hypot(xb - xa, yb - ya) or 1.0
        ux_, uy_ = heading(j); un = math.hypot(ux_, uy_) or 1.0; vx_, vy_ = ux_ / un, uy_ / un
        g_ = gain.get('h' if abs(vx_) >= abs(vy_) else 'v') or per_px or 2.9
        if j == len(path) - 1: items.append((vx_, vy_, g_ * nrm + float(os.environ.get('WDRAW_STUB_EXTRA') or 40), 0, 0))
        else: items.append((vx_, vy_, 0.9 * g_ * nrm if nrm > 4 else 0, j + 1, 0 if straight_on(path[j], vx_, vy_) else 2))
    res = os.path.join(gdir, 'witness_trace.json')
    if os.path.exists(res): os.remove(res)
    c.send(['trace %x %d %s' % (eid, len(items), ' '.join('%.4f %.4f %.1f %d %.1f' % it for it in items))], 0.05)
    t_ = time.time()
    while time.time() - t_ < 60 and not os.path.exists(res): time.sleep(0.05)
    try: r_ = json.load(open(res))
    except (OSError, ValueError): done(False, 'the in-game tracer gave no result', traced(), dist)
    stamp('tracer: %d of %d edges' % (r_['done'], len(items)))
    if not r_['ok']: done(False, 'tracer: %s (edge %d)' % (r_['why'], i0 + r_['done']), traced(), dist)
for i in range(1, len(path)):
    if TRACER and i >= 2 and (gain or per_px): run_tracer(i); break
    (x0, y0), (x1, y1) = node_px(path[i - 1]), node_px(path[i])
    norm = math.hypot(x1 - x0, y1 - y0) or 1.0; ux, uy = heading(i); vx, vy = ux / (math.hypot(ux, uy) or 1.0), uy / (math.hypot(ux, uy) or 1.0)
    vx *= axflip.get('h', 1); vy *= axflip.get('v', 1)      # an axis found reversed on an earlier edge stays reversed
    last = i == len(path) - 1
    if last:      # the end stub: its length by dead reckoning and a little more (a cursor pushed past the panel's edge cancels the line)
        stub_gain = gain.get('h' if abs(vx) >= abs(vy) else 'v') or per_px or 2.9      # the gain of the stub's own axis (swamp panels: 2.4 sideways, 5.5 up)
        total = stub_gain * norm + float(os.environ.get('WDRAW_STUB_EXTRA') or 40); hold = float(os.environ.get('WDRAW_HOLD_FRACTION') or 0)
        if hold:      # a test that wants to look at a line half drawn: stop part of the way, say so in a file, wait, then go on
            mouse(vx, vy, total * hold); open(os.path.join(gdir, 'wdraw_held.txt'), 'w').write('held at %.2f' % hold); time.sleep(float(os.environ.get('WDRAW_HOLD_SECS') or 8)); mouse(vx, vy, total * (1 - hold))
        else: mouse(vx, vy, total)
        dbg('end stub pushed', round(stub_gain * norm + 40), 'counts, traced', traced())
        continue
    used = 0.0; axis_ = 'h' if abs(vx) >= abs(vy) else 'v'
    if gain.get(axis_) and norm > 4:
        first = float(os.environ.get("WDRAW_FIRST") or 0.9) * gain[axis_] * norm; mouse(vx, vy, first); used += first
    stalled_at = traced(); since = 0
    for _ in range(250):
        if traced() >= i + 1: break
        # No progress for a while: the axis may run the other way on this panel (a vertical flip the start search did not catch; Jungle Second
        # Row 1 went nowhere going up). Push the main axis the other way, and keep that flip for the edges after if it works.
        if traced() == stalled_at: since += 1
        else: stalled_at = traced(); since = 0
        if since == 60 and not os.environ.get('WDRAW_NO_FLIP_RETRY'):
            if abs(vy) >= abs(vx): vy = -vy; axflip['v'] = -axflip.get('v', 1)
            else: vx = -vx; axflip['h'] = -axflip.get('h', 1)
            dbg('no progress on segment', path[i - 1], '->', path[i], ': main axis reversed')
        # WDRAW_PROGRESS_STEPS: on a long edge, the line's own progress along it says how much is left: push most of that (capped), then small
        # steps. (Experimental: on short edges the estimate is noise; an uncapped version overshot the tutorial gate's first segment.)
        step = 2
        if os.environ.get('WDRAW_PROGRESS_STEPS') and norm > 40 and used > 0:
            ep = edge_progress()
            if ep and ep[1] == path[i] and 0.1 < ep[0] < 0.9: step = min(40.0, max(2.0, 0.7 * (1 - ep[0]) * used / ep[0]))
        mouse(vx, vy, step); used += step
    else: done(False, 'stuck before node %d (path index %d)' % (path[i], i), traced(), dist)
    if traced() > i + 1: done(False, 'overshot node %d: the line ran past it (traced %d, wanted %d)' % (path[i], traced(), i + 1), traced(), dist)
    ep_ = edge_progress()
    if ep_ and ep_[1] != path[i] and ep_[1] < len(path_nodes_all) and axflip: done(False, 'took the wrong branch after reversing an axis: reached node %d, wanted %d' % (ep_[1], path[i]), traced(), dist)
    if not straight_on(path[i], vx, vy): mouse(vx, vy, 2)      # the count flips as the tip reaches the node: a hair further so a turn is taken at the node, not before it
    if gain.get(axis_) is None and norm > 14: gain[axis_] = used / norm
    if norm > 14: per_px = used / norm
    dbg('segment', path[i - 1], '->', path[i], 'counts %.0f' % used, 'per px', per_px and round(per_px, 2), 'traced', traced())

# ---- finishing ------------------------------------------------------------------------------------------------------------------------------
stamp('path drawn'); c.send(['mouse 0 0'], 0.6)
dbg('before the finishing click: state', state(), 'traced', traced())
c.send(['button L down'], 0.12); c.send(['button L up'], 0.12)
t_end = time.time()
while time.time() - t_end < 8 and not is_solved() and state() != 2: time.sleep(0.2)      # solved flags, or the game's verdict "wrong" (state 2)
stamp('finished (state %d)' % state())
if REDRAW:      # the panel was solved before; starting a new line cleared that (the walk above saw it traced from the start), so solved again
    # now means the game accepted this line. (The stored-line field +0x454 is empty after a redraw: not usable as the verdict.)
    done(is_solved(), '' if is_solved() else 'redrawn, but the game did not accept it (state %d)' % state(), traced(), dist)
done(is_solved(), '' if is_solved() else 'finished but the game did not accept it (state %d)' % state(), traced(), dist)
