# T2 check of the name tags, run with Windows Python. A is the viewer, B the player A sees (B was started with a name). B stands 5 m ahead of A: A's proxy must list B by
# its name with a measured ping and say its tag is on screen; with B 5 m behind A the tag is off screen; the ping stays in a sane range for a loopback link.
# usage: wtags.py <pidA> <dirA> <pidB> <dirB> <B's name>
import os, sys, time, math, re
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
                m = re.search(r'tagstat: id (\d+) name (.*) ping (-?\d+) onscreen (\d) dist ([0-9.]+)', l)
                if m: rows.append(dict(id=int(m[1]), name=m[2], ping=int(m[3]), onscreen=int(m[4]), dist=float(m[5])))
            return rows
        time.sleep(0.2)
    return None
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-62s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
(cx, cy, cz), (th, ph) = A.camera(); fx, fy = math.cos(th), math.sin(th)
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, cz - 1.69, th, 0.0)], 1.0)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx + fx * 5, cy + fy * 5, cz - 1.69, th, 0.0)], 2.5)
time.sleep(6)      # pings go out once a second: a few have come back
t1 = tags(); time.sleep(1.0); t1 = tags()
row = t1[0] if t1 else None
check('A lists B by its name', row and row['name'] == want, row)
check('A has a measured ping for B (0..500 ms on loopback)', row and 0 <= row['ping'] <= 500, row)
check('B 5 m ahead has its tag on screen', row and row['onscreen'] == 1 and 4 < row['dist'] < 6, row)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx - fx * 5, cy - fy * 5, cz - 1.69, th, 0.0)], 2.5)       # B moves to 5 m behind A
time.sleep(1.0); t2 = tags(); r2 = t2[0] if t2 else None
check('with B behind A the tag is off screen', r2 and r2['onscreen'] == 0 and 4 < r2['dist'] < 6, r2)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
