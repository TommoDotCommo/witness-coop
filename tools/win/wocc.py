# T2 check of the character's occlusion by the game's own geometry, run with Windows Python. A is the viewer; B's figure is drawn on A at places
# chosen with `fakepose`: in front of the closed door at the end of the start tunnel (open), and behind it (hidden by the wall). Reads the proxy's
# `modelstat` line: visible_px (a GPU occlusion query: pixels of the figure that passed the scene depth test) and scene_depth (0 the game's depth
# buffer was not found, 1 single sample, 2 multisampled, -1 switched off by the no_model_depth test mutant).
# usage: wocc.py <pidA> <dirA> <pidB> <dirB> [profile label]      prints  RESULT key=value ...  and exits 0 only if every assertion held
import os, sys, time, math, struct, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]; LABEL = sys.argv[5] if len(sys.argv) > 5 else ''
A = attach(pa)
MIN_OPEN = 3000          # pixels of the figure that must show 2 m in front of the door at 1920x1080
MAX_FRACTION = 0.02      # behind the wall: at most this share of the open figure's pixels
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def stat():
    log = os.path.join(da, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('modelstat:'))
    send(da, ['modelstat'], 0.0); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('modelstat:')]
        if len(ls) > n0:
            m = re.search(r'visible_px (\d+) drawn (\d) .* scene_depth (-?\d+)', ls[-1])
            if m: return dict(vis=int(m[1]), drawn=int(m[2]), scene=int(m[3]))
        time.sleep(0.2)
    return None
def settle_stat(wait):      # the occlusion query needs a couple of frames after the pose changes
    time.sleep(wait); stat(); time.sleep(0.5); r = stat()
    for _ in range(3):
        if r is not None: break
        time.sleep(1.0); r = stat()
    return r
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-62s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)

e = A.entity(0x64); px, py, pz = struct.unpack('<3f', A.rd(e + 0x24, 12))
sx, sy = px - 0.15, py + 3.34
yaw = math.atan2(py - sy, px - sx)
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (sx, sy, 3.727 - 1.69, yaw, 0.0)], 1.5)
send(da, ['fakepose %.4f %.4f %.4f %.5f' % (sx, sy - 2.0, 3.727, yaw + math.pi)], 1.0)          # in front of the door, facing A (eye position)
s_front = settle_stat(0.5)
check('the scene depth buffer was found by the proxy (%s)' % LABEL, s_front and s_front['scene'] in (1, 2), s_front)
check('a player in the open is visible (> %d px)' % MIN_OPEN, s_front and s_front['drawn'] and s_front['vis'] > MIN_OPEN, s_front)
send(da, ['fakepose %.4f %.4f %.4f %.5f' % (sx, py - 3.0, 3.727, yaw + math.pi)], 1.0)         # 3 m behind the door
s_back = settle_stat(0.5)
check('a player behind the wall shows < 2% of the open pixels', s_front and s_back and s_back['drawn'] and s_back['vis'] < MAX_FRACTION * s_front['vis'], (s_front and s_front['vis'], s_back))
send(da, ['fakepose off'], 0.5)
print('RESULT ' + ' '.join('%s=%s' % kv for kv in [('profile', LABEL), ('scene_depth', s_front and s_front['scene']), ('open', s_front and s_front['vis']), ('behind', s_back and s_back['vis'])]))
sys.exit(0 if ok else 1)
