# T2 check of body-overlap transparency, run with Windows Python. B reads the alpha it draws A with (`ghoststat`).
# A stays solid between B and a puzzle, at shoulder-to-shoulder distance, and on another level; only intersecting B's body fades A.
# Positions are set with hover and read back from both games' memory.
# usage: wghost.py <pidA> <dirA> <pidB> <dirB> <see_through 1|0>
import os, sys, time, math, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, on = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5] == '1'
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    time.sleep(settle)
def ghost(d, pid=1):
    """(alpha, target, panels) that B's draw has for player `pid`, from a fresh modelstat"""
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('ghoststat: %d ' % pid)); send(d, ['modelstat']); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('ghoststat: %d ' % pid)]
        if len(ls) > n0:
            m = re.match(r'ghoststat: \d+ alpha ([0-9.]+) target ([0-9.]+) panels (\d+)', ls[-1]); return float(m[1]), float(m[2]), int(m[3])
        time.sleep(0.05)
    return None
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-100s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
def settle(want_low, secs=4.0):
    end = time.time() + secs; g = None
    while time.time() < end:
        g = ghost(db)
        if g and ((want_low and g[0] < 0.3) or (not want_low and g[0] > 0.95)): return g
        time.sleep(0.2)
    return g
send(da, ['menusel c335', 'menudo teleport'], 4.0)
(ax, ay, az), (ath, aph) = A.camera()
send(db, ['tp %.4f %.4f %.4f %.5f 0' % (ax - math.cos(ath) * 2.0, ay - math.sin(ath) * 2.0, az, ath)], 3.0)
(bx, by, bz), (bth, bph) = B.camera()
def put_a(fwd, side, dz=0.0):
    """A's eye relative to B's camera: forward, right and vertical metres."""
    x = bx + fwd * math.cos(bth) + side * math.sin(bth); y = by + fwd * math.sin(bth) - side * math.cos(bth)
    send(da, ['hover %.3f %.3f %.3f %.4f 0' % (x, y, bz + dz, bth)], 1.0)
put_a(2.0, 0.0); g = settle(False)
check('1. A between B and the panel stays fully solid', g and g[0] > 0.95 and g[1] > 0.95, g)
check('   B knows where the panels are (the world table is present)', g and g[2] >= 300, g and g[2])
put_a(0.0, 0.65); g = settle(False)
check('2. A at ordinary shoulder-to-shoulder distance (0.65 m) stays solid', g and g[0] > 0.95 and g[1] > 0.95, g)
put_a(0.0, 0.0); g = settle(True)
check('3. A intersecting B\'s body fades', g and (g[0] < 0.3 if on else g[0] > 0.95) and (g[1] < 0.3 if on else g[1] > 0.95), g)
put_a(0.0, 0.65); g = settle(False)
check('4. A restores opacity quickly after leaving overlap', g and g[0] > 0.95 and g[1] > 0.95, g)
put_a(0.0, 0.0, 2.2); g = settle(False)
check('5. A directly above B but vertically separated stays solid', g and g[0] > 0.95 and g[1] > 0.95, g)
put_a(0.0, 0.0); g = settle(True)
check('6. Returning to vertical and horizontal body overlap fades again', g and (g[0] < 0.3 if on else g[0] > 0.95), g)
send(da, ['hover off'])
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
