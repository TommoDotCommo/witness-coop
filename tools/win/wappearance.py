# Two real clients: compare received and rendered appearance records and capture both views.
import json, math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
exec(compile(open(os.path.join(HERE, 'wimg.py')).read(), 'wimg.py', 'exec'))
pa, da, pb, db, out = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
os.makedirs(out, exist_ok=True)
ok = True

def check(name, condition, detail=''):
    global ok
    ok = ok and bool(condition)
    print('CHECK %s %s | %s' % ('ok' if condition else 'FAILED', name, detail), flush=True)

def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); start = time.time()
    while os.path.exists(cmd) and time.time() - start < 10: time.sleep(0.02)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    start = time.time()
    while os.path.exists(cmd) and time.time() - start < 10: time.sleep(0.02)
    time.sleep(settle)

def read_state(d, name):
    try: return json.load(open(os.path.join(d, name)))
    except (OSError, ValueError): return {}

def appearance(d, remote):
    net = read_state(d, 'witness_net.json'); models = read_state(d, 'witness_models.json')
    nr = next((p for p in net.get('remotes', []) if p.get('id') == remote), None)
    mr = next((p for p in models.get('players', []) if p.get('id') == remote), None)
    return nr and nr.get('appearance'), mr and mr.get('appearance')

def expect(d, remote, wanted, label):
    deadline = time.time() + 15; got = None
    while time.time() < deadline:
        send(d, ['netstate', 'modelstate'], 0.4)
        got = appearance(d, remote)
        if got == (wanted, wanted): break
        time.sleep(0.4)
    check(label, got == (wanted, wanted), 'received/submitted %s, wanted %s' % (got, wanted))

def shot(d, name):
    bmp = os.path.join(d, 'shots', name + '.bmp')
    try: os.remove(bmp)
    except OSError: pass
    send(d, ['shot ' + name]); deadline = time.time() + 10; last = -1
    while time.time() < deadline:
        try: size = os.path.getsize(bmp)
        except OSError: size = -1
        if size > 0 and size == last: break
        last = size; time.sleep(0.15)
    else:
        check('in-game capture ' + name, False, 'no stable BMP'); return
    png = os.path.join(out, name + '.png'); bmp_to_png(bmp, png)
    check('in-game capture ' + name, os.path.getsize(png) > 10000, png)

send(da, ['netstate', 'modelstate']); send(db, ['netstate', 'modelstate'], 1.0)
expect(da, 2, [2] * 8, 'A receives and submits player B palette')
expect(db, 1, [1] * 8, 'B receives and submits player A palette')

(cx, cy, cz), (heading, pitch) = __import__('wlib').attach(pa).camera() if False else ((None, None, None), (None, None))
# Read the camera through the shared probe without desktop input.
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
(cx, cy, cz), (heading, pitch) = attach(pa).camera()
ground = cz - 1.69
send(da, ['tp %.5f %.5f %.5f %.5f 0' % (cx, cy, ground, heading)])
send(db, ['tp %.5f %.5f %.5f %.5f 0' % (cx + 3.4 * math.cos(heading), cy + 3.4 * math.sin(heading), ground, heading + math.pi)], 3.0)
shot(da, 'appearance_a')
shot(db, 'appearance_b')

def appearance_command(d, feature, value):
    send(d, ['typing on', 'typetext /appearance %s %s' % (feature, value),
             'key ENTER down', 'key ENTER up'], 0.5)

appearance_command(da, 'coat', 3)
appearance_command(db, 'scarf', 3)
want_a = [1, 1, 1, 3, 1, 1, 1, 1]; want_b = [2, 2, 2, 2, 3, 2, 2, 2]
expect(db, 1, want_a, 'session coat change reaches and renders on B')
expect(da, 2, want_b, 'session scarf change reaches and renders on A')
appearance_command(da, 'coat', 9)
expect(db, 1, want_a, 'invalid session choice is rejected without changing appearance')
shot(da, 'appearance_a2')
shot(db, 'appearance_b2')
print('ALL OK' if ok else 'FAILED', flush=True)
sys.exit(0 if ok else 1)
