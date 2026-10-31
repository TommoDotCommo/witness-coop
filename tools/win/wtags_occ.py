# Name-tag occlusion oracle: the UI shader samples scene depth at the remote player's head.
# tagstat visible_px is a GPU occlusion query around the actual tag draw, after alpha and scene-depth discard.
# The known closed tutorial wall and open position match wocc.py; both camera positions are checked from process memory.
import os, sys, time, math, struct, re, json
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]; LABEL = sys.argv[5] if len(sys.argv) > 5 else ''
A, B = attach(pa), attach(pb)
MIN_OPEN = 100          # name-tag fragments that must survive in the open
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def stat():
    log = os.path.join(da, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('tagstat: id'))
    send(da, ['tagstat'], 0.0); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('tagstat: id')]
        if len(ls) > n0:
            m = re.search(r'visible_px (\d+) query_ready (\d) scene_depth (-?\d+)', ls[-1])
            if m: return dict(vis=int(m[1]), drawn=int(m[2]), scene=int(m[3]))
        time.sleep(0.2)
    return None
def settle_stat(wait):      # the occlusion query needs a couple of frames after the pose changes
    time.sleep(wait); stat(); time.sleep(0.5); r = stat()
    for _ in range(3):
        if r is not None: break
        time.sleep(1.0); r = stat()
    return r
ok = True; checks = {}
def check(name, cond, detail):
    global ok; checks[name] = bool(cond); ok = ok and bool(cond); print('  %-62s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)

e = A.entity(0x64); px, py, pz = struct.unpack('<3f', A.rd(e + 0x24, 12))
sx, sy = px - 0.15, py + 3.34
yaw = math.atan2(py - sy, px - sx)
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (sx, sy, 3.727 - 1.69, yaw, 0.0)], 1.5)
send(db, ['tp %.4f %.4f %.4f %.5f 0' % (sx, sy - 2.0, 3.727 - 1.69, yaw + math.pi)], 2.5)          # in front of the door, facing A (eye position)
check('A is at the wall viewpoint', math.dist(A.camera()[0], (sx,sy,3.727)) < 0.35, A.camera())
check('B is in the open in front of the wall', math.dist(B.camera()[0], (sx,sy-2,3.727)) < 0.35, B.camera())
s_front = settle_stat(0.5)
check('the scene depth buffer was found by the proxy (%s)' % LABEL, s_front and s_front['scene'] in (1, 2), s_front)
check('a name tag in the open is visible (> %d px)' % MIN_OPEN, s_front and s_front['drawn'] and s_front['vis'] > MIN_OPEN, s_front)
send(db, ['tp %.4f %.4f %.4f %.5f 0' % (sx, py - 3.0, 3.727 - 1.69, yaw + math.pi)], 2.5)         # 3 m behind the door
check('B is behind the closed wall', math.dist(B.camera()[0], (sx,py-3,3.727)) < 0.35, B.camera())
s_back = settle_stat(0.5)
check('a name tag behind the wall has zero visible pixels', s_front and s_back and s_back['drawn'] and s_back['vis'] == 0, (s_front and s_front['vis'], s_back))

print('RESULT ' + ' '.join('%s=%s' % kv for kv in [('profile', LABEL), ('scene_depth', s_front and s_front['scene']), ('open', s_front and s_front['vis']), ('behind', s_back and s_back['vis'])]))
print('TAG_OCC_RESULT ' + json.dumps(dict(checks=checks, front=s_front, behind=s_back)))
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
