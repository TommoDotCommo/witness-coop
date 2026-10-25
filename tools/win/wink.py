# T2 check of free drawing, run with Windows Python. A turns the canvas on (the mouse then moves a pen, the camera stays), holds X and moves the pen over the scene; B stands next to A and must
# hold the same stroke (same points, to a millimetre), see it drawn, and lose it when A undoes it or clears everything. The ink must sit on the surface under the pen (the depth the
# game rendered at that pixel): the point at the middle of the screen is as far away as a ping at the middle, and the last point projects back to the pen's pixel.
# usage: wink.py <pidA> <dirA> <pidB> <dirB>
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
def count(d, prefix): return sum(1 for l in open(os.path.join(d, 'witness_proxy.log'), errors='ignore') if l.startswith(prefix))
def stat(d, cmd, head, tail_re):
    """send a stat command; the block of lines it logs after its header line"""
    log = os.path.join(d, 'witness_proxy.log'); n0 = count(d, head); send(d, [cmd]); t = time.time()
    while time.time() - t < 6:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith(head)]
        if len(idx) > n0:
            time.sleep(0.15); ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith(head)]
            blk = [ls[idx[-1]]]
            for l in ls[idx[-1] + 1:]:
                if not re.match(tail_re, l): break
                blk.append(l)
            return blk
        time.sleep(0.05)
FL = r'(-?[0-9.]+)'
def draw(d):
    b = stat(d, 'drawstat', 'drawstat: strokes', r'drawstat: stroke ')
    m = re.match(r'drawstat: strokes (\d+) points (\d+) segs (\d+) pen (\d) canvas (\d) reticle ' + FL + ' ' + FL + r' color (\d+) screen ' + FL + ' ' + FL, b[0])
    r = dict(strokes=int(m[1]), points=int(m[2]), segs=int(m[3]), pen=int(m[4]), canvas=int(m[5]), rx=float(m[6]), ry=float(m[7]), color=int(m[8]), sw=float(m[9]), sh=float(m[10]), list=[])
    for l in b[1:]:
        m = re.match(r'drawstat: stroke from (\d+) id (\d+) color (\d+) points (\d+) first ' + ' '.join([FL] * 3) + ' last ' + ' '.join([FL] * 3) + ' lastscreen ' + FL + ' ' + FL + ' lastdist ' + FL, l)
        r['list'].append(dict(frm=int(m[1]), id=int(m[2]), color=int(m[3]), n=int(m[4]), first=tuple(float(m[i]) for i in (5, 6, 7)), last=tuple(float(m[i]) for i in (8, 9, 10)), sx=float(m[11]), sy=float(m[12]), dist=float(m[13])))
    return r
def marks(d):
    b = stat(d, 'socialstat', 'socialstat: open', r'socialstat: (chat|mark) ')
    return [tuple(float(x) for x in re.match(r'socialstat: mark from \d+ kind \d+ pos ' + ' '.join([FL] * 3), l).groups()) for l in b if l.startswith('socialstat: mark')]
def close(a, b, tol): return all(abs(x - y) <= tol for x, y in zip(a, b))
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-76s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
send(da, ['menusel c335', 'menudo teleport'], 4.0)
(cx, cy, cz), (th, ph) = A.camera()
send(db, ['tp %.4f %.4f %.4f %.5f 0' % (cx - math.cos(th) * 1.2, cy - math.sin(th) * 1.2, cz - 1.69, th)], 3.0)
check('start: nothing drawn in either game', draw(da)['strokes'] == 0 and draw(db)['strokes'] == 0, (draw(da)['strokes'], draw(db)['strokes']))
# 1. the canvas: the pen moves with the mouse, the camera stays
send(da, ['canvas on', 'reticle 0.5 0.5'], 0.5); send(da, ['mouse 40 0', 'mouse -40 0'], 0.5)
(_, (th1, ph1)) = A.camera(); sa = draw(da)
check('canvas on: the pen is at the middle of the screen and the mouse did not turn the camera', sa['canvas'] == 1 and abs(th1 - th) < 0.005 and abs(ph1 - ph) < 0.005 and abs(sa['rx'] - sa['sw'] / 2) < 40, (sa['canvas'], round(th1 - th, 4), round(sa['rx'])))
# 2. a ping at the middle of the screen measures the surface; a one-point stroke there must be as far
send(da, ['ping'], 1.5); mk = marks(da); cam = A.camera()[0]
pd = math.dist(mk[-1], cam) if mk else -1
send(da, ['reticle 0.5 0.5', 'key X down'], 0.9); send(da, ['key X up'], 0.8); sa = draw(da)
st = sa['list'][-1] if sa['list'] else None
check('ink at the middle of the screen sits as far away as a ping there', st and st['n'] >= 1 and abs(st['dist'] - pd) < 0.25, (pd and round(pd, 2), st and round(st['dist'], 2)))
# 3. a line across the scene: pen at 30%, X down, the mouse moves right in steps
send(da, ['reticle 0.3 0.45', 'key X down'], 0.5)
for i in range(10): send(da, ['mouse 28 0'], 0.22)
send(da, ['key X up'], 1.2); sa = draw(da); sb = draw(db)
mine = [s for s in sa['list'] if s['n'] >= 6]; st = mine[-1] if mine else None
check('A holds the line: several points on the surface, ending where the pen is', st and st['n'] >= 6 and abs(st['sx'] - sa['rx']) < 8 and abs(st['sy'] - sa['ry']) < 8, (st and st['n'], st and (round(st['sx']), round(st['sy'])), (round(sa['rx']), round(sa['ry']))))
bt = [s for s in sb['list'] if st and s['id'] == st['id'] and s['frm'] == 1]
check('B holds the same stroke (same points, from player 1)', st and bt and bt[0]['n'] == st['n'] and close(bt[0]['first'], st['first'], 0.002) and close(bt[0]['last'], st['last'], 0.002), (bt and bt[0]['n'], st and st['n']))
check('B counts the same strokes and points as A', sa['strokes'] == sb['strokes'] and sa['points'] == sb['points'], (sa['strokes'], sa['points'], sb['strokes'], sb['points']))
check('B draws the ink (line segments on its screen)', sb['segs'] >= 3, sb['segs'])
# 4. a colour
send(da, ['key B down'], 0.15); send(da, ['key B up'], 0.2); send(da, ['reticle 0.6 0.6', 'key X down'], 0.4)
for i in range(6): send(da, ['mouse -20 0'], 0.22)
send(da, ['key X up'], 1.0); sa = draw(da); sb = draw(db)
check('the next stroke has the next colour in both games', sa['list'] and sb['list'] and sa['list'][-1]['color'] == 1 and sb['list'][-1]['color'] == 1 and sa['strokes'] == sb['strokes'] == 3, (sa['list'] and sa['list'][-1]['color'], sb['list'] and sb['list'][-1]['color'], sa['strokes'], sb['strokes']))
# 5. undo (Backspace on the canvas) takes the last stroke back in both games
send(da, ['key BACK down'], 0.15); send(da, ['key BACK up'], 1.0); sa = draw(da); sb = draw(db)
check('Backspace undoes my last stroke, for both', sa['strokes'] == 2 and sb['strokes'] == 2, (sa['strokes'], sb['strokes']))
# 6. B draws too: A must see it, from player 2
send(db, ['canvas on', 'reticle 0.7 0.5', 'key X down'], 0.4)
for i in range(6): send(db, ['mouse -20 0'], 0.22)
send(db, ['key X up', 'canvas off'], 1.2); sa = draw(da); sb = draw(db)
check('B draws a stroke and A holds it (from player 2)', sa['strokes'] == 3 and any(s['frm'] == 2 for s in sa['list']) and sa['points'] == sb['points'], (sa['strokes'], sb['strokes'], sa['points'], sb['points']))
# 7. Shift + X clears all of mine (not B's), in both games
send(da, ['key SHIFT down'], 0.15); send(da, ['key X down'], 0.15); send(da, ['key X up'], 0.15); send(da, ['key SHIFT up'], 1.2); sa = draw(da); sb = draw(db)
check('Shift+X clears my strokes in both games and leaves the other player\'s', sa['strokes'] == 1 and sb['strokes'] == 1 and all(s['frm'] == 2 for s in sa['list'] + sb['list']), (sa['strokes'], sb['strokes']))
send(da, ['canvas off'], 0.3)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
