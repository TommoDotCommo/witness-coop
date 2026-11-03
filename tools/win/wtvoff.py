"""T2 experimental_tv gating, state read from both game processes (witness_tv.json), no logs, no pictures.
Usage: wtvoff.py <pidA> <dirA> <pidB> <dirB> <mode>. mode 'off': neither game has experimental_tv (the default); 'mixed': A has it on, B off.
"""
import json, math, os, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, mode = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
A, B = attach(pa), attach(pb)
def send(d, lines):
    p = os.path.join(d, 'witness_cmd.txt')
    end = time.monotonic() + 10
    while os.path.exists(p) and time.monotonic() < end: time.sleep(.02)
    assert not os.path.exists(p), 'command queue did not drain'
    with open(p + '.tmp', 'w') as f: f.write('\n'.join(lines) + '\n')
    os.replace(p + '.tmp', p)
    while os.path.exists(p) and time.monotonic() < end: time.sleep(.02)
    assert not os.path.exists(p), 'command was not consumed'
def dump(d, command='tvstate', filename='witness_tv.json'):
    path = os.path.join(d, filename)
    if os.path.exists(path): os.unlink(path)
    send(d, [command])
    end = time.monotonic() + 10
    while time.monotonic() < end:
        try:
            with open(path) as f: return json.load(f)
        except (FileNotFoundError, json.JSONDecodeError, PermissionError): time.sleep(.05)
    raise AssertionError('render bridge timeout: ' + command)
def wait(fn, predicate, timeout=20, failure='state did not converge'):
    end = time.monotonic() + timeout
    while True:
        v = fn()
        if predicate(v): return v
        assert time.monotonic() < end, '%s: %s' % (failure, v)
        time.sleep(.15)
def key(d, k): send(d, ['key %s down' % k, 'key %s up' % k])
def check(name, condition):
    assert condition, name
    print('PASS:', name, flush=True)
def silent(s): return not (s['enabled'] or s['consent'] or s['target'] or s['deploy_target'] or s['subscribers'] or s['sent'] or s['decoded'] or s['draws']
                          or s['visible_pixels'] or s['audience'] or s['probing'] or s['seated'] or s['audience_streams'])
for d in (da, db): wait(lambda: dump(d, 'netstate', 'witness_net.json'), lambda s: s['ready'], 180)
check('both start with nothing TV running', silent(dump(da)) and silent(dump(db)))
send(da, ['menusel c335', 'menudo teleport'])
time.sleep(4)
(ax, ay, az), (yaw, _) = A.camera()
send(db, ['hover %.5f %.5f %.5f %.6f 0' % (ax-math.cos(yaw)*2.5, ay-math.sin(yaw)*2.5, az, yaw)])
wait(B.camera, lambda c: abs(c[0][0]-(ax-math.cos(yaw)*2.5)) < .3 and abs(c[0][1]-(ay-math.sin(yaw)*2.5)) < .3)
def poke(d):      # every way to start a TV feature: its keys and its bridge commands (one at a time, each followed by a state read: a crash names the step)
    for k in ('F10', 'F11', 'J', 'K'):
        key(d, k); dump(d); print('poked key', k, flush=True)
    for c in ('tv 1', 'tvconsent 1', 'tvdeploy', 'tvseat'):
        send(d, [c]); dump(d); print('poked command', c, flush=True)
if mode == 'off':
    poke(da); poke(db); time.sleep(6)
    a, b = dump(da), dump(db)
    check('off: keys and commands start nothing on A (no viewer, consent, screen, seat, draw or packet)', silent(a))
    check('off: keys and commands start nothing on B', silent(b))
else:
    key(da, 'F10'); key(da, 'F11')
    a1 = wait(lambda: dump(da), lambda s: s['enabled'] and s['consent'] and s['target'], failure='A (experimental_tv on) viewer did not start')
    send(da, ['tvdeploy %d' % a1['target']])
    wait(lambda: dump(da), lambda s: s['deploy_target'] == a1['target'] and len(s['audience']) >= 1, timeout=40, failure='A (on) could not deploy a sky TV')
    poke(db); time.sleep(6)
    a, b = dump(da), dump(db)
    check('mixed: A (on) still runs its own viewer and sky TV', a['enabled'] and a['deploy_target'] == a1['target'] and len(a['audience']) >= 1)
    check('mixed: A (on) gets no picture and no subscriber from B (off)', a['decoded'] == 0 and not a['drawn_live'] and a['subscribers'] == 0)
    check('mixed: B (off) ignores A: no screen, no stool, no seat, no stream, nothing drawn or sent', silent(b))
print('ALL OK: tvoff ' + mode)
