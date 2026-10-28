"""T2 back TV checks: process state and render-thread bridge, never logs/pixels.
Usage: wtv.py <pidA> <dirA> <pidB> <dirB>. Captures are review artifacts only.
"""
import json, math, os, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
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
for d in (da, db): wait(lambda: dump(d, 'netstate', 'witness_net.json'), lambda s: s['ready'], 180)
a0, b0 = dump(da), dump(db)
check('both viewer and consent default off', not any(s['enabled'] or s['consent'] or s['sent'] or s['decoded'] for s in (a0, b0)))
# Known reachable outdoor panel, then B stands two metres behind A, looking at A's back.
send(da, ['menusel c335', 'menudo teleport'])
time.sleep(4)
(ax, ay, az), (yaw, _) = A.camera()
send(db, ['hover %.5f %.5f %.5f %.6f 0' % (ax-math.cos(yaw)*2.5, ay-math.sin(yaw)*2.5, az, yaw)])
wait(B.camera, lambda c: abs(c[0][0]-(ax-math.cos(yaw)*2.5)) < .3 and abs(c[0][1]-(ay-math.sin(yaw)*2.5)) < .3)
key(db, 'F10')
wait(lambda: dump(db), lambda s: s['enabled'] and s['target'])
time.sleep(2)
a1, b1 = dump(da), dump(db)
check('no consent: no source pictures, viewer shows static', a1['sent']==0 and b1['decoded']==0 and not b1['fresh'] and b1['draws']>0)
# A may also view a TV; that must not imply consent, as checked above for B.
key(da, 'F10'); key(da, 'F11')
b2 = wait(lambda: dump(db), lambda s: s['decoded']>=3 and s['drawn_live'] and s['power']>.95 and s['visible_pixels']>0,
          failure='TV_ASSERTION: consenting stream reaches viewer')
a2 = dump(da)
check('consenting A sends only to subscribed B', a2['consent'] and a2['subscribers']==1 and a2['sent']>=3)
check('screen samples A snapshot, using exact RGB332 frame hash', dict(a2['sent_hashes']).get(b2['uploaded_sequence'])==b2['upload_hash'] and b2['upload_hash']!=0)
check('bounded draw and motion', 0<b2['vertices']<=1024 and abs(b2['sway'])<=.16)
send(db, ['shot tv_live'])
# Camera turn must change A's image content and arrive at B's TV texture.
old = b2['upload_hash']
send(da, ['hover %.5f %.5f %.5f %.6f .15' % (ax, ay, az, yaw+.7)])
wait(A.camera, lambda c: abs(c[1][0]-(yaw+.7))<.1)
wait(lambda: dump(db), lambda s: s['fresh'] and s['upload_hash'] and s['upload_hash']!=old)
check('source camera change reaches the TV', True)
# Per-viewer off removes the prop after its CRT closing animation and cancels traffic.
key(db, 'F10'); time.sleep(2)
b3, a3 = dump(db), dump(da); time.sleep(.6); b4, a4 = dump(db), dump(da)
check('disabled viewer sees plain back and receives nothing', not b4['enabled'] and b4['target']==0 and b4['draws']==b3['draws'] and b4['decoded']==b3['decoded'])
check('no watchers: source stops', a4['subscribers']==0 and a4['sent']==a3['sent'])
send(db, ['shot tv_plain'])
key(db, 'F10'); wait(lambda: dump(db), lambda s: s['fresh'] and s['drawn_live'])
key(da, 'F11')
b5 = wait(lambda: dump(db), lambda s: not s['fresh'] and not s['drawn_live'] and s['upload_hash']==0)
a5 = dump(da); time.sleep(.5); a6 = dump(da)
check('revocation wipes display, source stops without consent', not a6['consent'] and not a6['subscribers'] and a5['sent']==a6['sent'])
send(db, ['shot tv_revoked', 'hover off']); send(da, ['hover off'])
print('CPU encode/send maximum (us):', a6['capture_cpu_max_us'])
print('ALL OK', flush=True)
