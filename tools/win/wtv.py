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
key(db, 'F10')
wait(lambda: dump(db), lambda s: not s['enabled'])

# Audience POV screen: B aims at A, deploys A's backpack TV into a giant in-world screen.
send(da, ['menusel c335', 'menudo teleport'])
time.sleep(3)
(ax, ay, az), (yaw, _) = A.camera()
send(db, ['hover %.5f %.5f %.5f %.6f -0.10' % (ax-math.cos(yaw)*3.5, ay-math.sin(yaw)*3.5, az, yaw)])
wait(B.camera, lambda c: abs(c[0][0]-(ax-math.cos(yaw)*3.5)) < .3 and abs(c[0][1]-(ay-math.sin(yaw)*3.5)) < .3)
time.sleep(2)

# Deploy aimed sky TV via key J
key(db, 'J')
time.sleep(.5)
if not dump(db).get('deploy_target') and not dump(db).get('probing'): send(db, ['tvdeploy'])     # a second press while the ground is being measured cancels it
aud_b1 = wait(lambda: dump(db), lambda s: s.get('deploy_target')!=0 and len(s.get('audience',[]))>=1, timeout=40, failure="deploy aimed sky TV failed")
check('deploy action sets deployment target and creates audience screen', aud_b1['deploy_target']==1)
scr1 = aud_b1['audience'][0]
check('audience screen size is 4.0m x 2.5m', abs(scr1['width']-4.0)<1e-3 and abs(scr1['height']-2.5)<1e-3)
check('screen bottom is 2.0m above ground', abs(scr1['bottom']-(scr1['ground']+2.0))<1e-3)
behind_dist = (scr1['center'][0]-ax)*math.cos(yaw) + (scr1['center'][1]-ay)*math.sin(yaw)
check('screen is anchored ~3.0m behind wearer deployment pose', abs(behind_dist+3.0)<0.35)
time.sleep(1)
aud_b1 = dump(db)
check('audience screen with no consent shows static noise', not aud_b1['audience'][0]['fresh'] and not aud_b1['audience'][0]['drawn_live'] and aud_b1['audience'][0]['draws']>0)
send(db, ['shot tv_audience_static'])

# Wearer A consents to share POV
key(da, 'F11')
aud_b2 = wait(lambda: dump(db), lambda s: s.get('audience') and s['audience'][0]['fresh'] and s['audience'][0]['drawn_live'] and s['audience'][0]['upload_hash']!=0, timeout=15, failure='live feed did not reach audience screen')
check('audience screen receives live source POV from consenting wearer', aud_b2['audience'][0]['fresh'] and aud_b2['audience'][0]['drawn_live'])
send(db, ['shot tv_audience_live'])

# Wearer moves around: screen stays stationary in-world at deployment pose while stream continues
orig_center = list(aud_b2['audience'][0]['center'])
orig_bottom = aud_b2['audience'][0]['bottom']
send(da, ['hover %.5f %.5f %.5f %.6f .15' % (ax+3.5, ay+3.5, az, yaw+.8)])
wait(A.camera, lambda c: abs(c[0][0]-(ax+3.5))<.3)
time.sleep(1)
aud_b3 = wait(lambda: dump(db), lambda s: s.get('audience') and s['audience'][0]['fresh'] and s['audience'][0]['drawn_live'])
scr3 = aud_b3['audience'][0]
check('audience screen stays stationary in-world at deployment pose while wearer moves',
      abs(scr3['center'][0]-orig_center[0])<1e-3 and abs(scr3['center'][1]-orig_center[1])<1e-3 and abs(scr3['bottom']-orig_bottom)<1e-3)
send(db, ['shot tv_audience_moved'])

# Wearer revokes consent: audience screen wipes live display back to static noise
key(da, 'F11')
aud_b4 = wait(lambda: dump(db), lambda s: s.get('audience') and not s['audience'][0]['fresh'] and not s['audience'][0]['drawn_live'] and s['audience'][0]['upload_hash']==0, timeout=15, failure='revocation did not wipe audience screen')
check('wearer revocation wipes audience screen back to static noise', True)
send(db, ['shot tv_audience_revoked'])

# Retract sky TV: key J again retracts the deployed screen
key(db, 'J')
aud_b5 = wait(lambda: dump(db), lambda s: s.get('deploy_target')==0 and len(s.get('audience',[]))==0, timeout=15, failure='retract sky TV failed')
check('retracting sky TV clears screen from world', len(aud_b5.get('audience',[]))==0)
send(db, ['shot tv_audience_retracted'])
send(da, ['hover off']); send(db, ['hover off'])

print('CPU encode/send maximum (us):', a6['capture_cpu_max_us'])
print('ALL OK', flush=True)
