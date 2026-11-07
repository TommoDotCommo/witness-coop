"""T2 a remote solve does what the solver's solve did. usage: wreact.py <pidA> <dirA> <pidB> <dirB> line|react
  line:  A has traced panel 0x64 (the bot drew it before this runs): B's trace buffer holds the same nodes (the line the game's reactions read),
         and B's overlay does not draw it a second time
  react: A solves the Swamp rotating bridge (0x181F5), the Town RGB light control (0x334D8) and a Desert water level (0x1831C) the game's own way
         (`nativesolve`): B's bridge turns to the same angle and B's lights change as A's, and B sends nothing back (the water is printed only)
Read-only probes of both games' memory, apart from the commands."""
import os, sys, time, struct, math
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, mode = attach(int(sys.argv[1])), sys.argv[2], attach(int(sys.argv[3])), sys.argv[4], sys.argv[5]
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def ent(p, i): return p.entity_table()[i]
def f32(p, a, n=1): return struct.unpack('<%df' % n, p.rd(a, 4 * n))
def trace(p, i):
    e = ent(p, i); n = p.u32(e + 0x228); ptr = p.u64(e + 0x230)
    if not n or n > 600 or not ptr: return []
    raw = p.rd(ptr, n * 52); return [struct.unpack_from('<2I', raw, k * 52) for k in range(n)]
def log_since(d, name, pos):
    try:
        with open(os.path.join(d, name), 'rb') as f: f.seek(pos); return f.read().decode('latin1', 'replace')
    except OSError: return ''
def size(d, name):
    try: return os.path.getsize(os.path.join(d, name))
    except OSError: return 0
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-84s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
if mode == 'line':
    t = time.time(); ta = tb = []
    while time.time() - t < 10:
        ta, tb = trace(pa, 0x64), trace(pb, 0x64)
        if ta and ta == tb: break
        time.sleep(0.5)
    check('A traced panel 0x64 (precondition)', len(ta) >= 1, '%d segments' % len(ta))
    check("B's panel holds the same traced line (node pairs)", ta and ta == tb, 'A %s | B %s' % (ta[:6], tb[:6]))
    pos = size(db, 'witness_proxy.log'); send(db, ['linestat'], 1.0)
    st = [l for l in log_since(db, 'witness_proxy.log', pos).splitlines() if 'linestat: lines' in l]
    drawn = int(st[-1].split('linestat: lines')[1].split()[0]) if st else -1
    check("B's overlay does not draw that line again (the game draws it)", drawn == 0, 'remote lines drawn by the overlay: %d' % drawn)
else:
    eb = size(db, 'witness_events.log')
    for pid_ in (0x181F5, 0x334D8, 0x1831C):
        for d in (da, db): send(d, ['menusel %x' % pid_, 'menudo teleport'], 0.0)
        time.sleep(3.0); send(da, ['nativesolve %x' % pid_], 0.5)
    time.sleep(12.0)      # the bridge turns and the water drains over several seconds
    qa_, qb_ = f32(pa, ent(pa, 0x181F5) + 0x34, 4), f32(pb, ent(pb, 0x181F5) + 0x34, 4)
    turn = 2 * math.degrees(math.acos(min(1.0, abs(sum(x * y for x, y in zip(qa_, qb_))))))
    check("B's rotating bridge turned to A's angle (panel 0x181F5 on it)", turn < 1.0, '%.2f degrees apart' % turn)
    la = [p.rd(ent(p, i) + o, 8) for p in (pa,) for i, o in ((0x3E23, 0x2BC), (0x3E24, 0x124), (0x187DB, 0x124), (0x2DC76, 0x2BC))]
    lb = [p.rd(ent(p, i) + o, 8) for p in (pb,) for i, o in ((0x3E23, 0x2BC), (0x3E24, 0x124), (0x187DB, 0x124), (0x2DC76, 0x2BC))]
    check("B's Town RGB house lights changed as A's did (4 lights)", la == lb, '%d of 4 match' % sum(x == y for x, y in zip(la, lb)))
    za, zb = f32(pa, ent(pa, 0x1027) + 0x2C)[0], f32(pb, ent(pb, 0x1027) + 0x2C)[0]
    print('  (information: flood-room water A %.3f B %.3f)' % (za, zb), flush=True)
    echoes = [l for l in log_since(db, 'witness_events.log', eb).splitlines() if '-> event' in l]
    check('B sent nothing back', not echoes, '; '.join(l.split('state: ')[-1] for l in echoes[:4]))
print('ALL OK' if ok else 'FAILED', flush=True)
sys.exit(0 if ok else 1)
