# T2 check of the POV screens, run with Windows Python. A is the viewer, B the player A looks at (5 m ahead). Nothing is streamed until A asks; V (on the aimed player) starts
# B's pictures to A only; ] and [ cycle, V again clears; B stops sending when nobody asks; F5 opens the list, Enter watches, Backspace clears.
# usage: wpov.py <pidA> <dirA> <pidB> <dirB> <B's id>
import os, sys, time, math, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, bid = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], int(sys.argv[5])
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def stat(d):
    """One `povstat` line from the proxy log of d, as a dict of numbers."""
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('povstat:'))
    send(d, ['povstat'], 0.0); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('povstat:')]
        if len(ls) > n0: return {k: int(v) for k, v in re.findall(r'(\w+) (-?\d+)', ls[-1])}
        time.sleep(0.2)
    return None
def key(d, name, settle=0.4): send(d, ['key %s down' % name, 'key %s up' % name], settle)
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-66s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
(cx, cy, cz), (th, ph) = A.camera(); fx, fy = math.cos(th), math.sin(th)
def wait_for(fn, ok, timeout):      # poll until ok(fn()) or the time is up; returns the last value
    t = time.time(); v = fn()
    while not ok(v) and time.time() - t < timeout: time.sleep(0.25); v = fn()
    return v
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, cz - 1.69, th, 0.0)], 0.3)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx + fx * 5, cy + fy * 5, cz - 1.69, th, 0.0)], 0.5)
a0 = wait_for(lambda: stat(da), lambda s: s and s['aimed'] == bid, 15); b0 = stat(db)
check('before anyone asks, nothing is watched or sent', a0 and b0 and a0['watching'] == 0 and b0['subscribers'] == 0 and b0['pictures_sent'] == 0, (a0, b0))
check('A looks at B: the prompt target is B', a0 and a0['aimed'] == bid, a0)
key(da, 'V', 0.2)
a1 = wait_for(lambda: stat(da), lambda s: s and s['decoded'] >= 3, 15); b1 = stat(db)
check('V on the aimed player watches B', a1 and a1['watching'] == bid, a1)
check('B has one viewer and sends pictures', b1 and b1['subscribers'] == 1 and b1['pictures_sent'] >= 3, b1)
check('A receives and decodes them', a1 and a1['received'] >= 3 and a1['decoded'] >= 3, a1)
key(da, 'V', 0.2)
a2 = stat(da); b2a = wait_for(lambda: stat(db), lambda s: s and s['subscribers'] == 0, 10); time.sleep(1.5); b2b = stat(db)      # the request lapses 3 s after the last one
check('V again clears the watch', a2 and a2['watching'] == 0, a2)
check('B stops sending once nobody asks', b2a and b2b and b2b['pictures_sent'] <= b2a['pictures_sent'] + 1 and b2b['subscribers'] == 0, (b2a, b2b))
key(da, 'RB', 0.5); a3 = stat(da)
check('] goes to the first (only) player', a3 and a3['watching'] == bid, a3)
key(da, 'RB', 0.5); a4 = stat(da)
check('] after the last player goes off', a4 and a4['watching'] == 0, a4)
key(da, 'LB', 0.5); a5 = stat(da)
check('[ from off goes to the last player', a5 and a5['watching'] == bid, a5)
key(da, 'BACK', 0.3); a6 = stat(da)      # no roster: Backspace does nothing to the game
key(da, 'F5', 0.5); a7 = stat(da)
check('F5 opens the player list', a7 and a7['roster'] == 1, a7)
key(da, 'BACK', 0.5); a8 = stat(da)
check('Backspace in the list clears the watch', a8 and a8['watching'] == 0 and a8['roster'] == 1, a8)
key(da, 'ENTER', 0.5); a9 = stat(da)
check('Enter in the list watches the selected player and closes it', a9 and a9['watching'] == bid and a9['roster'] == 0, a9)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
