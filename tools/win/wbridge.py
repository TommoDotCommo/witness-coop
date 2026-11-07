"""T2 Treehouse bridge: A solves Yellow Bridge 1 (0x17D72) with the `solve` test command (fields, dispatcher and the panel's Bridge, as a native solve);
A's bridge grows a section and carries Yellow Bridge 2 (0x17D8F) out. B gets the solve by sync and must grow the same section: its 0x17D8F ends where A's is.
Read-only probes of both games (the panel's position at +0x24); usage: wbridge.py <pidA> <dirA> <pidB> <dirB>"""
import os, sys, time, struct, math
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = attach(int(sys.argv[1])), sys.argv[2], attach(int(sys.argv[3])), sys.argv[4]
FIRST, NEXT = 0x17D72, 0x17D8F
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def pos(p, i):
    e = p.entity_table()[i]; return struct.unpack('<3f', p.rd(e + 0x24, 12)) if e else None
def dist(a, b): return math.dist(a, b) if a and b else 1e9
def log_has(d, text):
    try: return text in open(os.path.join(d, 'witness_events.log'), errors='replace').read()
    except OSError: return False
def settle(p, i, timeout):      # the bridge grows over a few seconds: wait until the panel has stopped moving
    t = time.time(); last = pos(p, i); still = 0
    while time.time() - t < timeout:
        time.sleep(0.5); now = pos(p, i)
        still = still + 1 if dist(now, last) < 0.002 else 0; last = now
        if still >= 4: break
    return last
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-80s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
for d in (da, db): send(d, ['menusel %x' % FIRST, 'menudo teleport'], 2.0)      # both players by the bridge, so its section is loaded and drawn in both
a0, b0 = pos(pa, NEXT), pos(pb, NEXT)
check('before: Yellow Bridge 2 is at the same place in both games', dist(a0, b0) < 0.05, '%.3f m' % dist(a0, b0))
send(da, ['solve %x' % FIRST], 0.5)
a1 = settle(pa, NEXT, 20)
check('A: solving Yellow Bridge 1 grows the bridge (Yellow Bridge 2 moves out)', dist(a0, a1) > 0.5, 'moved %.2f m' % dist(a0, a1))
t = time.time()
while time.time() - t < 20 and not log_has(db, 'remote panel %#x applied as solved' % FIRST): time.sleep(0.25)
check('B: the solve arrived', log_has(db, 'remote panel %#x applied as solved' % FIRST))
b1 = settle(pb, NEXT, 20)
check('B: its bridge grew the same section (Yellow Bridge 2 where A has it)', dist(a1, b1) < 0.05, 'B moved %.2f m, %.3f m from A' % (dist(b0, b1), dist(a1, b1)))
print('ALL OK' if ok else 'FAILED', flush=True)
sys.exit(0 if ok else 1)
