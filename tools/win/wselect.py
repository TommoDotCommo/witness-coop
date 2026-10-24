# T2 check of the in-game model selector, run with Windows Python. A presses F6 / F7 (injected as the game's own key messages) and
# uses the `model <name>` command; the proxy's `modelstat` line says which model is drawn and how many of its pixels are visible.
# usage: wselect.py <pidA> <dirA> <pidB> <dirB>
import os, sys, time, math, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def stat():
    log = os.path.join(da, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('modelstat:'))
    send(da, ['modelstat']); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('modelstat:')]
        if len(ls) > n0:
            m = re.search(r'visible_px (\d+) .* model (\S+) (\d+)/(\d+)', ls[-1]); return dict(vis=int(m[1]), model=m[2], at=int(m[3]), of=int(m[4]))
        time.sleep(0.2)
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-62s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
def key(k): send(da, ['key %s down' % k], 0.15); send(da, ['key %s up' % k], 1.0)
(cx, cy, cz), (th, ph) = A.camera()
send(da, ['tp %.4f %.4f %.4f %.5f 0' % (cx, cy, cz - 1.69, th)], 1.0)
send(db, ['tp %.4f %.4f %.4f %.5f 0' % (cx + math.cos(th) * 3.2, cy + math.sin(th) * 3.2, cz - 1.69, th + math.pi)], 2.5)
s0 = stat(); check('the game starts with the configured model (cesium-man, 1 of 3)', s0 and s0['model'] == 'cesium-man' and s0['at'] == 1 and s0['of'] == 3 and s0['vis'] > 3000, s0)
key('F7'); s1 = stat(); check('F7 selects the next model, and it is drawn', s1 and s1['model'] == 'fox' and s1['vis'] > 500, s1)
key('F7'); s2 = stat(); check('F7 again: the third model, drawn', s2 and s2['model'] == 'rigged-figure' and s2['vis'] > 3000, s2)
key('F7'); s3 = stat(); check('F7 at the end wraps to the first', s3 and s3['model'] == 'cesium-man' and s3['at'] == 1, s3)
key('F6'); s4 = stat(); check('F6 goes back (wraps to the last)', s4 and s4['model'] == 'rigged-figure' and s4['at'] == 3, s4)
send(da, ['model fox'], 1.0); s5 = stat(); check('the model command selects by name', s5 and s5['model'] == 'fox' and s5['vis'] > 500, s5)
send(da, ['shot selector_fox'], 0.5)
sys.exit(0 if ok else 1)
