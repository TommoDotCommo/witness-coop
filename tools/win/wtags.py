# T2 check of the name tags, run with Windows Python. A is the viewer, B the player A sees (B was started with a name). B stands 2 m ahead of A in the open tutorial tunnel: A's proxy must list B by
# its name with a measured ping and say its tag is on screen; with B 2 m behind A the tag is off screen; the ping stays in a sane range for a loopback link.
# usage: wtags.py <pidA> <dirA> <pidB> <dirB> <B's name>
import os, sys, time, math, re, struct
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, want = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def tags():
    """A's `tagstat` lines: a list of dicts, one per remote player."""
    log = os.path.join(da, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('tagstat: players'))
    send(da, ['tagstat'], 0.0); t = time.time()
    while time.time() - t < 6:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('tagstat: players')]
        if len(idx) > n0:
            n = int(ls[idx[-1]].split()[-1]); rows = []
            for l in ls[idx[-1] + 1: idx[-1] + 1 + n]:
                m = re.search(r'tagstat: id (\d+) name (.*) ping (-?\d+) onscreen (\d) dist ([0-9.]+) visible_px (\d+) query_ready (\d) scene_depth (-?\d+)', l)
                if m: rows.append(dict(id=int(m[1]), name=m[2], ping=int(m[3]), onscreen=int(m[4]), dist=float(m[5]), pixels=int(m[6]), ready=int(m[7]), scene=int(m[8])))
            return rows
        time.sleep(0.2)
    return 'no tagstat answer'
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-62s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
# Keep the open control in front of the closed door rather than projecting a tag through it.
e = A.entity(0x64); px, py, _ = struct.unpack('<3f', A.rd(e + 0x24, 12))
cx, cy, cz = px - 0.15, py + 3.34, 3.727
th = math.atan2(py - cy, px - cx); fx, fy = math.cos(th), math.sin(th)
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, cz - 1.69, th, 0.0)], 0.3)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx + fx * 2, cy + fy * 2, cz - 1.69, th, 0.0)], 0.5)
def wait_for(fn, ok, timeout):      # poll until ok(fn()) or the time is up; returns the last value
    t = time.time(); v = fn()
    while not ok(v) and time.time() - t < timeout: time.sleep(0.25); v = fn()
    return v
t1 = wait_for(tags, lambda t: isinstance(t, list) and t and t[0]['name'] == want and t[0]['ping'] >= 0 and t[0]['onscreen'] == 1 and t[0]['ready'] and t[0]['pixels'] > 100, 60)      # pings go out once a second; B needs to have connected first, which is slow when several pairs of games start at once
row = t1[0] if isinstance(t1, list) and t1 else None
if row is None: print('  A has no tag row for B: %s' % ('tagstat listed no players (B not connected)' if t1 == [] else t1), flush=True)
check('A lists B by its name', row and row['name'] == want, row)
check('A has a measured ping for B (0..500 ms on loopback)', row and 0 <= row['ping'] <= 500, row)
check('B 2 m ahead has visible tag pixels', row and row['onscreen'] == 1 and row['ready'] and row['pixels'] > 100 and row['scene'] in (1,2) and 1.5 < row['dist'] < 2.5, row)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx - fx * 2, cy - fy * 2, cz - 1.69, th, 0.0)], 0.5)       # B moves to 2 m behind A
t2 = wait_for(tags, lambda t: isinstance(t, list) and t and t[0]['onscreen'] == 0, 10); r2 = t2[0] if isinstance(t2, list) and t2 else None
check('with B behind A the tag is off screen', r2 and r2['onscreen'] == 0 and 1.5 < r2['dist'] < 2.5, r2)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
