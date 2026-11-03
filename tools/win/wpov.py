# T2 check of the POV screens, run with Windows Python. A is the viewer, B the player A looks at (5 m ahead). Nothing is streamed until A asks; Watch in the panel starts
# B's pictures to A only (Watch in the hold-Tab player panel; V is the ping now); ] and [ cycle, Backspace in the panel clears; B stops sending when nobody asks; Enter watches.
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
def pstat(d):
    """One `panelstat` line from the proxy log of d, as a dict of numbers."""
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('panelstat:'))
    send(d, ['panelstat'], 0.0); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('panelstat:')]
        if len(ls) > n0: return {k: int(float(v)) for k, v in re.findall(r'(\w+) (-?\d+(?:\.\d+)?)', ls[-1])}
        time.sleep(0.2)
    return None
def click_at(d, x, y):
    """Move the panel's cursor to (x, y) with raw mouse motion and click the left button."""
    p = pstat(d); send(d, ['mouse %d %d' % (x - p['cursorx'], y - p['cursory'])], 0.3); send(d, ['button L down'], 0.1); send(d, ['button L up'], 0.4)
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
send(da, ['key TAB down'], 0.5); p0 = pstat(da)
check('holding Tab opens the player panel with B in it', p0 and p0['open'] == 1 and p0['rows'] == 1 and p0['first_id'] == bid, p0)
click_at(da, p0['watchx'], p0['watchy'])
a1 = wait_for(lambda: stat(da), lambda s: s and s['decoded'] >= 3 and s['pic'] >= 3, 15); b1 = stat(db)
check('clicking Watch on B\'s row watches B', a1 and a1['watching'] == bid, a1)
check('B has one viewer and sends pictures', b1 and b1['subscribers'] == 1 and b1['pictures_sent'] >= 3, b1)
check('A receives and decodes them', a1 and a1['received'] >= 3 and a1['decoded'] >= 3, a1)
check('and A DRAWS them: B\'s screen is on A\'s screen, with a picture in it (Watch alone, F9 not pressed)', a1 and a1['box'] >= 3 and a1['pic'] >= 3, a1 and (a1['box'], a1['pic']))
send(da, ['key TAB up'], 0.5); pc = pstat(da)
check('releasing Tab closes the panel and keeps the watch', pc and pc['open'] == 0 and stat(da)['watching'] == bid, pc)
send(da, ['key TAB down'], 0.4); key(da, 'BACK', 0.4); send(da, ['key TAB up'], 0.4)
a2 = stat(da); b2a = wait_for(lambda: stat(db), lambda s: s and s['subscribers'] == 0, 10); time.sleep(1.5); b2b = stat(db)      # the request lapses 3 s after the last one
check('Backspace in the panel clears the watch', a2 and a2['watching'] == 0, a2)
time.sleep(0.5); a2b = stat(da); time.sleep(1.0); a2c = stat(da)
check('and the picture is no longer drawn', a2b and a2c and a2c['box'] <= a2b['box'] + 1, (a2b and a2b['box'], a2c and a2c['box']))
check('B stops sending once nobody asks', b2a and b2b and b2b['pictures_sent'] <= b2a['pictures_sent'] + 1 and b2b['subscribers'] == 0, (b2a, b2b))
key(da, 'RB', 0.5); a3 = stat(da)
check('] goes to the first (only) player', a3 and a3['watching'] == bid, a3)
key(da, 'RB', 0.5); a4 = stat(da)
check('] after the last player goes off', a4 and a4['watching'] == 0, a4)
key(da, 'LB', 0.5); a5 = stat(da)
check('[ from off goes to the last player', a5 and a5['watching'] == bid, a5)
key(da, 'BACK', 0.3); a6 = stat(da)      # no panel: Backspace does nothing to the game
send(da, ['key TAB down'], 0.5); a7 = stat(da)
check('Tab held: the panel is open', a7 and a7['roster'] == 1, a7)
key(da, 'BACK', 0.5); a8 = stat(da)
check('Backspace in the panel clears the watch', a8 and a8['watching'] == 0 and a8['roster'] == 1, a8)
key(da, 'ENTER', 0.5); a9 = stat(da)
check('Enter in the panel watches the selected player (keyboard only)', a9 and a9['watching'] == bid and a9['roster'] == 1, a9)
send(da, ['key TAB up'], 0.5); a9b = stat(da)
check('releasing Tab closes the panel', a9b and a9b['roster'] == 0 and a9b['watching'] == bid, a9b)
send(da, ['key TAB down'], 0.4); key(da, 'ESC', 0.4); a9c = stat(da); send(da, ['key TAB up'], 0.3)
check('Esc closes the panel while Tab is still held', a9c and a9c['roster'] == 0, a9c)
send(da, ['key TAB down'], 0.4); key(da, 'BACK', 0.4); send(da, ['key TAB up'], 0.4); a10 = stat(da); key(da, 'V', 0.4); a11 = stat(da)
check('(the watch was cleared in the panel first)', a10 and a10['watching'] == 0, a10)
check('V is the ping now: it does not watch anybody', a11 and a11['watching'] == 0, a11)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
