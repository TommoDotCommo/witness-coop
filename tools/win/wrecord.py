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
