"""Record a person playing the panels the bot cannot do yet, one task after another (tools/record.sh starts the game and this).

Usage: Windows Python wrecord.py <pid> <game folder> <out folder> <task,task,...>
A task is a panel id (hex). For each one that is powered and not solved yet: the player is put in front of it (the dev menu's teleport,
the owner's OK), a message on the screen says what to do, and everything needed to teach the bot is written to <out>/session.jsonl
twenty times a second while it changes: where the player stands and looks, the panel's state, how many nodes are traced and how far along
the current edge (+0x228, the last record's t), the raw traced records (world positions of the line), and when solved the line the game
keeps (+0x454/+0x458). The walk between and around is kept as well, and added to the walking map (<game folder>/wnav_map.bin).
Solving the panel goes on to the next task; walking more than 15 m away from it skips it. Prints one JSON line per task."""
import os, sys, json, math, struct, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
import wnav
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))

pid, gdir, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
tasks = [int(h, 16) for h in sys.argv[4].split(',') if h]
os.makedirs(out, exist_ok=True)
p = attach(pid); c = Control(gdir); log = open(os.path.join(out, 'session.jsonl'), 'a')
nav = wnav.Map(); nav_path = os.path.join(gdir, 'wnav_map.bin'); nav.load(nav_path)
NAMES = json.loads(os.environ.get('WRECORD_NAMES') or '{}')


def write(**kv): log.write(json.dumps(dict(t=round(time.time(), 3), **kv)) + '\n'); log.flush()
def f32(a): return struct.unpack('<f', p.rd(a, 4))[0]
def say(text): c.send(['notice ' + text[:190]])


def panel_state(e):
    n = p.u32(e + 0x228); ptr = p.u64(e + 0x230)
    recs = p.rd(ptr, 0x34 * n) if ptr and 0 < n < 400 else b''
    last_t = struct.unpack('<f', recs[-0x34 + 0x10:-0x34 + 0x14])[0] if recs else None
    return {'traced': n, 'state': p.u32(e + 0x280), 'solved': f32(e + 0x294) == 1.0 and p.u32(e + 0x298) == 1, 'powered': f32(e + 0x2A4) == 1.0,
            'last_t': None if last_t is None else round(last_t, 3), 'records': recs.hex() if recs else ''}


def seq(e):
    n = p.u32(e + 0x454); ptr = p.u64(e + 0x458)
    return list(struct.unpack('<%dI' % n, p.rd(ptr, 4 * n))) if 0 < n < 500 and ptr else []


def pose():
    (x, y, z), (yaw, pitch) = p.camera(); return [round(x, 3), round(y, 3), round(z, 3), round(yaw, 4), round(pitch, 4)]


def place(h, e, px, py, pz):
    """The menu's teleport leaves the player in a wall or under the ground at some panels (Quarry Intro Left, the boat spawn; owner,
    2026-10-30): stand 1.4 m straight out from the panel's face instead, a little above the floor (the player drops onto it), facing it.
    The floor's height comes from the walking map (the surface nearest 1.3 m under the panel); with none known, from where the menu's
    teleport left the eye, else 1.5 m under the panel (Quarry Lower Ramp Control sits at floor level: that guess put the player under the
    ground and the game threw them out). Other distances are tried when the game does not keep the player there; True when one held."""
    qx, qy, qz, qw = struct.unpack('<4f', p.rd(e + 0x34, 16))
    tx, ty, tz = 0.0, 2 * qz, -2 * qy      # the panel's local +x (its face) rotated by its quaternion, as in wdraw.py
    nx, ny = 1.0 + qw * tx + (qy * tz - qz * ty), qw * ty + (qz * tx - qx * tz)
    nn = math.hypot(nx, ny) or 1.0; nx, ny = nx / nn, ny / nn
    land = p.camera()[0]
    for d in (1.4, 2.0, 1.0, 2.8):
        sx, sy = px + d * nx, py + d * ny
        floors = [f for f in (nav.surfaces(wnav.cell_of(sx, sy)) or []) if f is not None and pz - 2.4 < f < pz + 0.3]
        if floors: feet = min(floors, key=lambda f: abs(f - (pz - 1.3)))
        elif math.dist(land[:2], (px, py)) < 6: feet = land[2] - wnav.EYE
        else: feet = pz - 1.5
        feet += 0.15      # a little above it: the player drops onto the floor
        c.send(['tp %.4f %.4f %.4f %.5f %.5f' % (sx, sy, feet, math.atan2(-ny, -nx), math.atan2(pz - feet - wnav.EYE, d))], 2.0)
        cam = p.camera()[0]
        if math.dist(cam[:2], (sx, sy)) < 1.0 and abs(cam[2] - (feet + wnav.EYE)) < 1.0: return True
    c.send(['menusel %x' % h, 'menudo teleport'], 3.2)
    return math.dist(p.camera()[0][:2], (px, py)) < 6


write(event='start', tasks=['%#x' % h for h in tasks])
for idx, h in enumerate(tasks):      # (not k: wlib keeps the kernel handle in k)
    e = p.entity(h); name = NAMES.get('%#x' % h, '%#x' % h)
    if not e or p.type_of(e) != TI_PANEL: print(json.dumps({'panel': '%#x' % h, 'result': 'not a panel here'}), flush=True); continue
    st = panel_state(e)
    if st['solved'] or not st['powered']:
        why = 'already solved' if st['solved'] else 'not powered in this save'
        write(event='skip', panel='%#x' % h, why=why); print(json.dumps({'panel': '%#x' % h, 'name': name, 'result': why}), flush=True); continue
    c.send(['menusel %x' % h, 'menudo teleport'], 3.2)
    px, py, pz = struct.unpack('<3f', p.rd(e + 0x24, 12))
    if not place(h, e, px, py, pz):
        write(event='skip', panel='%#x' % h, why='could not be placed in front of it', pose=pose())
        print(json.dumps({'panel': '%#x' % h, 'name': name, 'result': 'could not be placed in front of it'}), flush=True)
        say('Could not put you at %s - skipping it.' % name); time.sleep(3); continue
    write(event='task', panel='%#x' % h, name=name, panel_pos=[px, py, pz], pose=pose())
    msg = 'RECORDING %d/%d: solve %s (in front of you). Walk 15 m away to skip it.' % (idx + 1, len(tasks), name)
    t0 = time.time(); said = 0; last = None; result = 'skipped'; trail = []
    while True:
        now = time.time()
        if now - said > 5: say(msg); said = now
        st = panel_state(e); ps = pose(); key = (st['traced'], st['state'], st['solved'], st['last_t'], tuple(round(v, 2) for v in ps))
        if key != last: write(event='sample', panel='%#x' % h, pose=ps, **st); last = key
        if not trail or math.dist(ps[:2], trail[-1][:2]) > 0.5:
            trail.append(ps); nav.stand(ps[0], ps[1], ps[2] - wnav.EYE)
        if st['solved']:
            write(event='solved', panel='%#x' % h, line=seq(e), seconds=round(now - t0, 1), pose=ps); result = 'solved'
            say('Solved %s, thank you! Next task in a moment...' % name); time.sleep(3); break
        if math.dist(ps[:2], (px, py)) > 15: write(event='skipped', panel='%#x' % h, seconds=round(now - t0, 1)); say('Skipped %s.' % name); time.sleep(2); break
        time.sleep(0.05)
    write(event='trail', panel='%#x' % h, points=trail)
    try: nav.save(nav_path)
    except OSError: pass
    print(json.dumps({'panel': '%#x' % h, 'name': name, 'result': result, 'seconds': round(time.time() - t0, 1)}), flush=True)
say('All recording tasks done - thank you! You can close the game.'); write(event='done')
