"""Walking benchmark: walk to the place in front of each panel of a fixed list, as wdraw.py does before drawing, and measure it.

Usage: Windows Python wbench.py <pid> <game folder> <out.jsonl> <panel,panel,...>      (tools/test_walk_bench.sh starts the game and this)
Each leg starts where the previous panel's place is (put there by the dev menu's teleport when the walk before did not get there, so the
legs do not depend on each other) and walks (tools/win/wnav.py) to 3 m straight out from the panel, the spot wdraw.py's walk aims at. Result
per leg, one JSON line: "result" walked (there on foot) / teleported (the walk failed, the dev menu's teleport got there: a last resort the
bot would have used) / failed (neither got there); metres walked, seconds, stucks, the walker's own reason, where it started and ended.
Nothing is drawn and nothing in the world is changed."""
import os, sys, json, math, struct, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
import wnav
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))

pid, gdir, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
panels = [int(h, 16) for h in sys.argv[4].split(',') if h]
p = attach(pid); c = Control(gdir); log = open(out, 'a')


def front(h, d=3.0):
    """(spot 3 m straight out from the panel's face (x, y), panel position): the panel's local +x rotated by its quaternion, as wdraw.py."""
    e = p.entity(h)
    if not e or p.type_of(e) != TI_PANEL: return None, None
    px, py, pz = struct.unpack('<3f', p.rd(e + 0x24, 12)); qx, qy, qz, qw = struct.unpack('<4f', p.rd(e + 0x34, 16))
    tx, ty, tz = 0.0, 2 * qz, -2 * qy
    nx, ny = 1.0 + qw * tx + (qy * tz - qz * ty), qw * ty + (qz * tx - qx * tz); nn = math.hypot(nx, ny) or 1.0
    return (px + d * nx / nn, py + d * ny / nn), (px, py, pz)


def there(spot, pz):
    q = p.camera()[0]; return math.dist(q[:2], spot) < 1.0 and abs(q[2] - pz) < 2.0


def teleport(h, spot, pz, ppos=None):
    for _ in range(2):
        c.send(['menusel %x' % h, 'menudo teleport'], 3.4)
        if math.dist(p.camera()[0][:2], spot) < 3.0 and abs(p.camera()[0][2] - pz) < 2.5: return True
    # the menu put the player at the spawn instead (some places refuse it): the command that takes coordinates, facing the panel
    x, y, _ = p.camera()[0]; yaw = math.atan2(ppos[1] - spot[1], ppos[0] - spot[0]) if ppos else 0.0
    c.send(['tp %.4f %.4f %.4f %.5f 0.0' % (spot[0], spot[1], pz + 0.3, yaw)], 3.0)
    return math.dist(p.camera()[0][:2], spot) < 3.0 and abs(p.camera()[0][2] - pz) < 2.5


t_all = time.monotonic(); walker = wnav.Walker(p, c, gdir, log=lambda m: print(m, file=sys.stderr, flush=True))
prev = None; prev_pos = None; only = {int(v) for v in os.environ.get('WBENCH_ONLY', '').split(',') if v}      # (WBENCH_ONLY: walk only these legs)
for i, h in enumerate(panels):
    spot, ppos = front(h)
    if spot is None: log.write(json.dumps({'leg': i, 'panel': '%#x' % h, 'result': 'failed', 'why': 'not a panel'}) + '\n'); log.flush(); continue
    if only and i not in only: prev = (h, spot, ppos[2]); prev_pos = ppos; continue
    if prev is not None and not there(*prev[1:]): teleport(*prev, ppos=prev_pos)      # the leg starts at the previous panel's place, whatever happened before
    walker.tag = 'leg%d_' % i; walker.walked = 0.0; walker.stuck = 0; walker.replans = 0; walker.looks = 0; walker.avoid = set(); walker.stuck_at = []; walker.backouts = 0; walker.sidesteps = 0
    start = p.camera()[0]; d = math.dist(start[:2], spot)
    r = walker.go((spot[0], spot[1], ppos[2]), 1.0, min(240.0, 60.0 + 1.5 * d), ztol=1.8)
    ok = r['ok'] and there(spot, ppos[2])
    result = 'walked' if ok else ('teleported' if teleport(h, spot, ppos[2], ppos) else 'failed')
    rec = {'leg': i, 'panel': '%#x' % h, 'result': result, 'metres': r['walked'], 'seconds': r['seconds'], 'straight_m': round(d, 1), 'stuck': r['stuck'],
           'replans': r['replans'], 'why': r['why'], 'backouts': r.get('backouts', 0), 'sidesteps': r.get('sidesteps', 0), 'from': [round(v, 2) for v in start], 'to': r['to'],
           'end': [round(v, 2) for v in p.camera()[0]], 'spot': [round(spot[0], 2), round(spot[1], 2), round(ppos[2], 2)]}
    log.write(json.dumps(rec) + '\n'); log.flush(); print(json.dumps(rec), flush=True)
    prev = (h, spot, ppos[2]); prev_pos = ppos
log.write(json.dumps({'done': True, 'seconds': round(time.monotonic() - t_all, 1)}) + '\n'); log.flush()
