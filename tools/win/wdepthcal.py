# Calibrates the game's depth buffer against known distances: A stands at several distances from panel 0x64, looking at it, and the proxy
# logs the scene depth at the screen centre (command `probe`). usage: wdepthcal.py <pidA> <dirA>
import os, sys, time, math, struct, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir = int(sys.argv[1]), sys.argv[2]; p = attach(pid); CMD = os.path.join(gdir, 'witness_cmd.txt'); LOG = os.path.join(gdir, 'witness_proxy.log')
def send(lines, settle=0.0):
    t = time.time()
    while os.path.exists(CMD) and time.time() - t < 8: time.sleep(0.05)
    tmp = CMD + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, CMD)
    t = time.time()
    while os.path.exists(CMD) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
e = p.entity(0x64); px, py, pz = struct.unpack('<3f', p.rd(e + 0x24, 12))
rows = []
for D in (1.5, 2.5, 3.34, 5.0, 8.0, 12.0):
    sx, sy, sz = px - 0.15, py + D, 3.727
    yaw = math.atan2(py - sy, px - sx); pitch = math.atan2(pz - sz, math.hypot(px - sx, py - sy))
    send(['tp %.4f %.4f %.4f %.5f %.5f' % (sx, sy, sz - 1.69, yaw, pitch)], 2.0)
    (cx, cy, cz), _ = p.camera(); dist = math.dist((cx, cy, cz), (px, py, pz))
    n0 = sum(1 for l in open(LOG, errors='ignore') if l.startswith('probe:'))
    send(['probe'], 1.5)
    lines = [l for l in open(LOG, errors='ignore') if l.startswith('probe:')]
    if len(lines) <= n0: print('no probe line'); continue
    v = [float(x) for x in re.findall(r'[-0-9.e+]+', lines[-1].split('|')[0].split('samples')[1])[:4]]
    rows.append((dist, v[0])); print('distance %.3f  centre depth %.7g  (z*d = %.5f, 1/(z*d)... )' % (dist, v[0], v[0] * dist), flush=True)
if len(rows) >= 3:   # fit z = a / d + b by least squares
    xs = [1 / d for d, _ in rows]; ys = [z for _, z in rows]; n = len(xs); mx = sum(xs) / n; my = sum(ys) / n
    a = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs); b = my - a * mx
    print('fit z = a/d + b: a = %.6f  b = %.6g   max residual %.2e' % (a, b, max(abs(a * x + b - y) for x, y in zip(xs, ys))))
