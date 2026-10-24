# T2 check of the character model, run with Windows Python. A is the viewer, B the player A sees. Reads the proxy's `modelstat` line
# (pixels of the model that passed the depth test, counted by a GPU occlusion query; the animation clip time; the measured speed).
# usage: wmodel.py <pidA> <dirA> <pidB> <dirB> [pixel scale, default 1: a small model covers fewer pixels] [walk clip index, default 0] [1: the idle clip animates (a model with its own idle clip), default 0: the first frame is held]      prints  RESULT key=value ...  and exits 0 only if every assertion held
import os, sys, time, math, struct, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
A, B = attach(pa), attach(pb)
PX = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0; WALK = int(sys.argv[6]) if len(sys.argv) > 6 else 0; IDLE_ANIMATES = len(sys.argv) > 7 and sys.argv[7] == '1'
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def stat():
    """One `modelstat` line from A's log: asks for a fresh one and waits for it."""
    log = os.path.join(da, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('modelstat:'))
    send(da, ['modelstat'], 0.0); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('modelstat:')]
        if len(ls) > n0:
            m = re.search(r'visible_px (\d+) drawn (\d) clip (-?\d+) speed ([-0-9.]+) anim ([-0-9.]+) frames (\d+)', ls[-1])
            return dict(vis=int(m[1]), drawn=int(m[2]), clip=int(m[3]), speed=float(m[4]), anim=float(m[5]))
        time.sleep(0.2)
    return None
def settle_stat(wait):      # the occlusion query needs a couple of frames after the pose changes
    time.sleep(wait); stat(); time.sleep(0.5); return stat()
res = {}; ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-62s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)

e = A.entity(0x64); px, py, pz = struct.unpack('<3f', A.rd(e + 0x24, 12))
# phase 1: the start tunnel. A stands still looking along it, B appears 5 m ahead facing away and walks off
(cx, cy, cz), (th, ph) = A.camera()
fx, fy = math.cos(th), math.sin(th)
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, cz - 1.69, th, 0.0)], 1.0)
bx, by = cx + fx * 5, cy + fy * 5
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (bx, by, cz - 1.69, th, 0.0)], 2.5)
s_still = settle_stat(0.5)
check('B standing 5 m ahead is drawn and visible', s_still and s_still['drawn'] and s_still['vis'] > 3000 * PX, s_still)
check('a standing player is idle (speed < 0.3)', s_still and s_still['speed'] < 0.3, s_still)
send(db, ['key W down'], 1.2)
s1 = stat(); time.sleep(0.4); s2 = stat()
check('a walking player has a measured speed of 1.5..5 m/s', s1 and s2 and 1.5 < s2['speed'] < 5.0, (s1, s2))
check('a walking player animates (clip time advances)', s1 and s2 and abs(s2['anim'] - s1['anim']) > 0.02, (s1, s2))
check('a walking player plays the walk clip', s2 and s2['clip'] == WALK, s2)
check('the walking figure is visible', s2 and s2['vis'] > 1000 * PX, s2)
send(db, ['key W up'], 2.0)
s3 = stat(); time.sleep(0.5); s4 = stat()
if IDLE_ANIMATES: check('after stopping the figure plays its idle clip', s3 and s4 and s4['speed'] < 0.3 and s4['clip'] != WALK and abs(s4['anim'] - s3['anim']) > 0.02, (s3, s4))
else: check('after stopping the figure is idle and still', s3 and s4 and s4['speed'] < 0.3 and abs(s4['anim'] - s3['anim']) < 1e-3, (s3, s4))
# phase 2: the closed door at the end of the tunnel hides a player who stands behind it. B's own game would not let it stand
# there (collision), so A is told to draw the peer at a chosen place (`fakepose`, a test hook)
sx, sy = px - 0.15, py + 3.34
yaw = math.atan2(py - sy, px - sx)
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (sx, sy, 3.727 - 1.69, yaw, 0.0)], 1.5)
send(da, ['fakepose %.4f %.4f %.4f %.5f' % (sx, sy - 2.0, 3.727, yaw + math.pi)], 1.0)          # in front of the door, facing A (eye position)
s_front = settle_stat(0.5)
check('a player in front of the door is visible', s_front and s_front['vis'] > 3000 * PX, s_front)
send(da, ['fakepose %.4f %.4f %.4f %.5f' % (sx, py - 3.0, 3.727, yaw + math.pi)], 1.0)         # 3 m behind the door
s_back = settle_stat(0.5)
check('a player behind the closed door is hidden (< 2% of the pixels in front)', s_front and s_back and s_back['drawn'] and s_back['vis'] < 0.02 * s_front['vis'], (s_front and s_front['vis'], s_back))
send(da, ['fakepose off'], 0.5)
print('RESULT ' + ' '.join('%s=%s' % kv for kv in [('still', s_still and s_still['vis']), ('walk_speed', s2 and s2['speed']), ('front', s_front and s_front['vis']), ('back', s_back and s_back['vis'])]))
sys.exit(0 if ok else 1)
