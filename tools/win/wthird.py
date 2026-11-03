"""T2 third-person view while emoting. A emotes: the world is drawn from a camera in front of A (the mod's state bridge and the game's
own camera memory, read from outside, say so), A's own figure is drawn doing the emote (A's model bridge lists A's own id), and the view
returns to first person when the emote ends, when A moves and when A clicks. B sees nothing different: the pose B receives is A's real eye,
and B's view never leaves first person. No log lines or screenshots are asserted (the shots are kept for a person to look at).
Run through tools/qa.sh thirdperson (clean build and the mutant tp_never_return=1, which must fail).
"""
import json, math, os, struct, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
A, B = attach(pa), attach(pb)
RENDER, CALLS, VIEW_POS = 0x1C8970, (0x696BD, 0x69A17), 0x62D578

def send(d, lines, settle=0):
    path = os.path.join(d, 'witness_cmd.txt'); deadline = time.monotonic() + 10
    while os.path.exists(path):
        assert time.monotonic() < deadline, 'command bridge stalled'
        time.sleep(.02)
    with open(path + '.tmp', 'w') as f: f.write('\n'.join(lines) + '\n')
    os.replace(path + '.tmp', path)
    while os.path.exists(path):
        assert time.monotonic() < deadline, 'command not consumed'
        time.sleep(.02)
    time.sleep(settle)

def bridge(d, cmd, name):
    p = os.path.join(d, name)
    try: os.remove(p)
    except FileNotFoundError: pass
    send(d, [cmd]); deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            with open(p) as f: return json.load(f)
        except (OSError, ValueError): time.sleep(.03)
    raise AssertionError('%s bridge timed out' % name)

tp = lambda d: bridge(d, 'tpstate', 'witness_tp.json')
models = lambda d: bridge(d, 'modelstate', 'witness_models.json')
net = lambda d: bridge(d, 'netstate', 'witness_net.json')

def expect(label, test, d=da, timeout=5):
    deadline = time.monotonic() + timeout; last = None
    while time.monotonic() < deadline:
        last = tp(d)
        if test(last): print('PASS ' + label, flush=True); return last
        time.sleep(.05)
    raise AssertionError('%s: %r' % (label, last))

def calls_patched(p):
    out = []
    for site in CALLS:
        b = p.rd(p.base + site, 5); rel = struct.unpack('<i', b[1:])[0]
        out.append(b[0] == 0xE8 and p.base + site + 5 + rel != p.base + RENDER)
    return out

def game_view(p):      # the game's own camera copies, read from outside between frames: the player's eye, never the third-person one
    (x, y, z), _ = p.camera(); vx, vy, vz = struct.unpack('<3f', p.rd(p.base + VIEW_POS, 12))
    return (x, y, z), (vx, vy, vz)

dist = lambda a, b: math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))

s = tp(da); assert s['phase'] == 'off' and s['patched'] == 0 and s['enabled'] == 1, s
print('PASS nothing patched before the first emote', flush=True)
assert calls_patched(A) == [False, False], 'the game code changed before any emote'
send(da, ['menusel c335', 'menudo teleport'], 4)
(cx, cy, cz), (heading, _) = A.camera()
# B stands 6 m behind A, looking at A; A tries headings until one has room in front (the probe may refuse a wall: that is the feature)
send(db, ['tp %.5f %.5f %.5f %.5f 0' % (cx - 6 * math.cos(heading), cy - 6 * math.sin(heading), cz - 1.69, heading)], 3)
chosen = None
for hi, dh in enumerate((math.pi, math.pi / 2, -math.pi / 2, 0.0, 3 * math.pi / 4, -3 * math.pi / 4)):
    h = heading + dh
    send(da, ['tp %.5f %.5f %.5f %.5f 0' % (cx, cy, cz - 1.69, h)], 1.5)
    send(da, ['emote 13'], .1)      # Thinking: loops until something ends it
    s = expect('heading %d: the probe answered' % hi, lambda s: s['phase'] != 'probe', timeout=3)
    if s['phase'] in ('in', 'hold'): chosen = h; break
    print('  heading %d refused: %s (probe depths %s)' % (hi, s['reason'], s['probe']), flush=True)
    send(da, ['emote 0'], .8)
assert chosen is not None, 'no heading with room in front of the player'
s = expect('the view swings out (hold)', lambda s: s['phase'] == 'hold' and s['frame_view'] == 1 and s['offset'] > 1.0)
send(da, ['shot tp_out'], .5)
assert calls_patched(A) == [True, True], 'the world drawing is not routed through the mod'
print('PASS both calls of the world drawing go through the mod', flush=True)
# the camera is in front of A, above the eye, looking back at A; never further than the measured free space allows
eye, view = s['eye'], s['view']
fwd = (math.cos(eye[3]), math.sin(eye[3])); off = [view[i] - eye[i] for i in range(3)]
assert off[0] * fwd[0] + off[1] * fwd[1] > 1.0 and off[2] > 0, s
look = (math.cos(view[4]) * math.cos(view[3]), math.cos(view[4]) * math.sin(view[3]), math.sin(view[4]))
assert -sum(look[i] * off[i] for i in range(3)) / math.sqrt(sum(o * o for o in off)) > 0.9, 'the camera does not look back at the player: %r' % s
probe0, L = s['probe'][0], math.sqrt(sum(o * o for o in off))
if probe0 > 0:      # the free length along the camera's ray, from the depth the game drew in first person (0.45 m kept free)
    f3 = (math.cos(eye[4]) * math.cos(eye[3]), math.cos(eye[4]) * math.sin(eye[3]), math.sin(eye[4]))
    along = probe0 / max(.05, sum(f3[i] * off[i] for i in range(3)) / L)
    assert L <= along - 0.45 + 0.05, 'the camera is not clear of the first surface: %.2f m of %.2f free: %r' % (L, along, s)
print('PASS the camera stands %.2f m in front of the player, %.2f m up, looking back (scale %.2f)' % (math.hypot(off[0], off[1]), off[2], s['scale']), flush=True)
assert s['qerr'] < 0.01, 'the quaternion convention does not match the game: %r' % s['qerr']
print('PASS the camera quaternion matches the game\'s own (difference %.5f)' % s['qerr'], flush=True)
# The game's camera memory holds the third-person camera only while the game draws its world (by design); its logic runs outside that
# window. What it moves must not move: the player entity (0x1E465, feet at +0x24) stays at the eye, and every outside read of the
# camera is either the eye or the view, nothing in between.
player = A.entity(0x1E465); odd = []; feet_bad = []; seen = {'eye': 0, 'view': 0}
for i in range(60):
    real, copy = game_view(A); fx, fy, fz = struct.unpack('<3f', A.rd(player + 0x24, 12))
    if dist((fx, fy, fz + 1.69), eye) > 0.05: feet_bad.append((fx, fy, fz))
    for c in (real, copy):
        if dist(c, eye) < 0.05: seen['eye'] += 1
        elif dist(c, view) < 0.05: seen['view'] += 1
        else: odd.append(c)
    time.sleep(.007)
assert not feet_bad, 'the player moved with the camera: %r' % feet_bad[:3]
assert not odd, 'the game camera held something else: %r' % odd[:3]
print('PASS the player stays where it stood (60 reads); the game camera is the eye %d times, the view %d times (while drawing)' % (seen['eye'], seen['view']), flush=True)
# A's own figure is drawn, doing the emote
deadline = time.monotonic() + 3; me = None
while time.monotonic() < deadline:
    m = models(da); me = next((p for p in m['players'] if dist(p['pos'], eye) < 0.3), None)
    if me and me.get('emote') == 13: break
    time.sleep(.05)
assert me and me.get('emote') == 13, 'own figure not drawn with the emote: %r' % (m,)
s2 = tp(da); assert s2['self_draws'] > s['self_draws'] or s2['self_draws'] > 5, s2
print('PASS A draws its own figure (id %s) doing the emote, %d frames' % (me['id'], s2['self_draws']), flush=True)
# B: nothing different. The pose B has of A is A's eye; B's view never leaves first person
nb = net(db); ra = [r for r in nb['remotes']]
assert ra and min(dist(r['pos'], eye) for r in ra) < 0.3, 'B does not see A where A really is: %r' % ra
sb = tp(db); assert sb['phase'] == 'off' and sb['swapped'] == 0 and sb['frame_view'] == 0, sb
assert calls_patched(B) == [False, False], 'B\'s game code was changed'
print('PASS B receives A\'s real eye position and stays first person (nothing patched in B)', flush=True)
# 1) movement brings the view back
send(da, ['key W down'], .6); send(da, ['key W up'])
s = expect('walking brings the view back', lambda s: s['phase'] in ('out', 'off') and s['reason'] == 'moved')
s = expect('back in first person', lambda s: s['phase'] == 'off' and s['frame_view'] == 0, timeout=3)
send(da, ['shot tp_back'], .3)
real, copy = game_view(A); assert dist(real, s['eye']) < 0.3, (real, s['eye'])
print('PASS first person again after walking', flush=True)
# 2) a click (a puzzle) brings it back
send(da, ['tp %.5f %.5f %.5f %.5f 0' % (cx, cy, cz - 1.69, chosen)], 1.5)
send(da, ['emote 13'])
expect('out again', lambda s: s['phase'] == 'hold')
send(da, ['button R down', 'button R up'])
expect('a click brings the view back', lambda s: s['phase'] in ('out', 'off') and s['reason'] == 'clicked')
expect('back in first person', lambda s: s['phase'] == 'off' and s['frame_view'] == 0, timeout=3)
# 3) the emote ending brings it back (a one-shot: Wave, 2.6 s)
send(da, ['emote 1'])
expect('out for a one-shot', lambda s: s['phase'] in ('in', 'hold') and s['frame_view'] == 1)
s = expect('the emote ends: back in first person', lambda s: s['phase'] == 'off' and s['frame_view'] == 0 and s['reason'] == 'emote_ended', timeout=6)
for i in range(30):
    real, copy = game_view(A); assert dist(real, s['eye']) < 0.05 and dist(copy, s['eye']) < 0.05, (real, copy, s['eye']); time.sleep(.007)
print('PASS after the emote the game draws from the player\'s eye again', flush=True)
# 4) the setting off: the view stays first person
send(da, ['tpforce off'])
starts = tp(da)['starts']; send(da, ['emote 2'], 1.5)
s = tp(da); assert s['phase'] == 'off' and s['starts'] == starts and s['frame_view'] == 0, s
send(da, ['tpforce cfg', 'emote 0'])
print('PASS with the setting off the view stays first person', flush=True)
print('ALL OK', flush=True)
