# T2 check of the pause menu, run with Windows Python. A is the viewer, B the player A sees (a puppet in front of A). Esc on A opens the game's pause menu: the proxy's pause
# flag (read here from the game's memory too) is 1 and the puppet is not drawn; Esc again closes it and the puppet is drawn again.
# usage: wpause.py <pidA> <dirA> <pidB> <dirB>
import os, sys, time, math, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    time.sleep(settle)
log = os.path.join(da, 'witness_proxy.log')
def stat():
    n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('modelstat:')); send(da, ['modelstat']); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('modelstat:')]
        if len(ls) > n0:
            m = re.search(r'drawn (\d)', ls[-1]); return int(m[1])
        time.sleep(0.1)
    return None
import struct
def flag():      # the two dim floats the proxy reads: 1 only while the pause menu is open
    f = [struct.unpack('<f', PAUSED_P.rd(PAUSED_P.base + a, 4))[0] for a in (0x630070, 0x62FF3C)]; return 1 if f[0] > 0.01 and f[1] > 0.01 else 0
PAUSED_P = Proc(pa)
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-66s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
(cx, cy, cz), (th, ph) = A.camera(); fx, fy = math.cos(th), math.sin(th)
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, cz - 1.69, th, 0.0)], 1.0)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx + fx * 5, cy + fy * 5, cz - 1.69, th + math.pi, 0.0)], 2.5)
d0 = stat(); check('playing: the puppet is drawn and the pause flag is 0', d0 == 1 and flag() == 0, 'drawn %s, flag %s' % (d0, flag()))
# what is NOT the pause menu must not hide anything: the model keys (the first flag candidate flipped with them) and a puzzle (it flipped with that too)
def press(k): send(da, ['key %s down' % k], 0.15); send(da, ['key %s up' % k], 1.5)
press('F7'); d_f7 = stat(); check('after F7 (model switch): still drawn, not paused', d_f7 == 1 and flag() == 0, 'drawn %s, flag %s' % (d_f7, flag())); press('F6')
send(da, ['menusel 293', 'menudo teleport'], 4.0); send(da, ['button L down'], 0.2); send(da, ['button L up'], 1.5)
d_pz = stat(); check('inside a puzzle: still drawn, not paused', d_pz == 1 and flag() == 0, 'drawn %s, flag %s' % (d_pz, flag())); send(da, ['button R down'], 0.2); send(da, ['button R up'], 1.5)
def esc(): send(da, ['key ESC down'], 0.15); send(da, ['key ESC up'], 2.5)
esc(); d1 = stat(); check('pause menu open: the game says paused (flag 1) and no puppet is drawn', flag() == 1 and d1 == 0, 'flag %s, drawn %s' % (flag(), d1))
esc(); time.sleep(1.0); d2 = stat(); check('pause menu closed: the puppet is back', flag() == 0 and d2 == 1, 'flag %s, drawn %s' % (flag(), d2))
esc(); d3 = stat(); check('opened a second time: hidden again', flag() == 1 and d3 == 0, 'flag %s, drawn %s' % (flag(), d3)); esc()
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
