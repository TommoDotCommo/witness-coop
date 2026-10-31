# T2 check of the occlusion by the open world's terrain, run with Windows Python. A stands at several tour spots (3 m in front of a panel) and looks away
# from the panel along open ground; B's figure is drawn on A (`fakepose`) 8 m ahead standing on A's level (it must show) and 4 m below A's eye (buried 2.3 m
# under the ground: the terrain between must hide it). Spots where the standing figure does not show are inconclusive (a slope, a tree) and skipped; at least 3 must
# be conclusive. usage: wocc_out.py <pidA> <dirA> <pidB> <dirB> [label]      prints  RESULT ...  and exits 0 only if every conclusive spot held and there were 3
import os, sys, time, math, struct, re, collections
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]; LABEL = sys.argv[5] if len(sys.argv) > 5 else ''
A = attach(pa); tab = A.entity_table()
MIN_OPEN = 1500; MAX_FRACTION = 0.02
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
def settle_stat(wait):
    time.sleep(wait); stat(); time.sleep(0.5); r = stat()
    for _ in range(3):
        if r is not None: break
        time.sleep(1.0); r = stat()
    return r
panels = {}
for i, e in enumerate(tab):
    if e and A.type_of(e) == TI_PANEL:
        x, y, z = struct.unpack('<3f', A.rd(e + 0x24, 12)); qz, qw = struct.unpack('<2f', A.rd(e + 0x3c, 8))
        if abs(x) + abs(y) > 1: panels[i] = (x, y, z, 2 * math.atan2(qz, qw))
cells = collections.defaultdict(list)
for i, p in panels.items(): cells[(int(p[0] // 80), int(p[1] // 80))].append(i)
ox, oy = panels[0x64][:2]; spots = []
for key, ids in cells.items():
    cx, cy = (key[0] + .5) * 80, (key[1] + .5) * 80
    best = min(ids, key=lambda i: math.hypot(panels[i][0] - cx, panels[i][1] - cy)); spots.append((math.hypot(panels[best][0] - ox, panels[best][1] - oy), best))
spots.sort()
ok = True; conclusive = 0; rows = []
for n in range(3, min(len(spots), 16)):
    if conclusive >= 4: break
    x, y, z, rz = panels[spots[n][1]]
    for dist, dz in ((3, .3), (3, 8)):
        sx, sy = x + dist * math.cos(rz), y + dist * math.sin(rz); th = math.remainder(rz, 2 * math.pi)      # looking away from the panel
        send(da, ['tp %.3f %.3f %.3f %.5f 0.0' % (sx, sy, z + dz, th)], 2.5)
        (cx, cy, cz), (cth, cph) = A.camera()
        if math.dist((cx, cy), (sx, sy)) < 1.0: break
    else: rows.append((n, 'refused')); continue
    fx, fy = math.cos(cth), math.sin(cth)
    send(da, ['fakepose %.4f %.4f %.4f %.5f' % (cx + 8 * fx, cy + 8 * fy, cz, cth + math.pi)], 1.0)
    s_open = settle_stat(0.5)
    send(da, ['fakepose %.4f %.4f %.4f %.5f' % (cx + 8 * fx, cy + 8 * fy, cz - 4.0, cth + math.pi)], 1.0)
    s_bur = settle_stat(0.5)
    if not (s_open and s_bur and s_open['drawn'] and s_open['vis'] > MIN_OPEN): rows.append((n, 'inconclusive', s_open and s_open['vis'])); continue
    conclusive += 1; good = s_bur['vis'] < MAX_FRACTION * s_open['vis'] and s_open['scene'] in (1, 2); ok = ok and good
    rows.append((n, 'ok' if good else 'FAILED', s_open['vis'], s_bur['vis'], s_open['scene']))
    print('  spot %d: standing figure %d px, buried figure %d px, scene_depth %d  %s' % (n, s_open['vis'], s_bur['vis'], s_open['scene'], 'ok' if good else 'FAILED'), flush=True)
send(da, ['fakepose off'], 0.5)
print('  conclusive spots: %d (need 3)' % conclusive, flush=True); ok = ok and conclusive >= 3
print('RESULT ' + ' '.join('%s=%s' % kv for kv in [('profile', LABEL), ('conclusive', conclusive), ('rows', rows)]))
sys.exit(0 if ok else 1)
