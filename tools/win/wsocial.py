# T2 check of chat, the typing indicator and location pings, run with Windows Python. A and B face each other. A opens the chat box with the real key, types with real character
# messages and sends with Enter; B must see three dots while A types, then A's line in its log and in a bubble over A. A pings a point (by key, from the depth under the cross-hair,
# and at an exact point); B must hold the marker and draw it (on screen, or as an edge arrow when it is out of view).
# usage: wsocial.py <pidA> <dirA> <pidB> <dirB> [number of lines for the reliability check]
import os, sys, time, math, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]; NLINES = int(sys.argv[5]) if len(sys.argv) > 5 else 8
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    time.sleep(settle)
def stat(d):
    """The block of 'socialstat:' lines the game just logged: the header's numbers, the chat lines, the marks and the remote players."""
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('socialstat: open'))
    send(d, ['socialstat']); t = time.time()
    while time.time() - t < 6:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('socialstat: open')]
        if len(idx) > n0:
            h = ls[idx[-1]]; blk = []
            for l in ls[idx[-1] + 1:]:
                if not l.startswith('socialstat:') or l.startswith('socialstat: open'): break
                blk.append(l)
            g = lambda k: int(re.search(k + r' (\d+)', h)[1])
            return dict(open=g('open'), log=g('log'), marks=g('marks'), dots=g(r'dots'), bubbles=g(r'drawn dots \d+ bubbles'), marks_drawn=int(re.search(r'drawn dots \d+ bubbles \d+ marks (\d+) arrows (\d+)', h)[1]),
                        arrows=int(re.search(r'arrows (\d+)', h)[1]), chat=[(int(m[1]), m[2], m[3]) for m in (re.match(r'socialstat: chat from (\d+) name (.*?) text (.*)', l) for l in blk) if m],
                        marks_list=[(int(m[1]), int(m[2]), float(m[3]), float(m[4]), float(m[5])) for m in (re.match(r'socialstat: mark from (\d+) kind (\d+) pos (\S+) (\S+) (\S+)', l) for l in blk) if m],
                        remote=[(int(m[1]), int(m[2]), int(m[3])) for m in (re.match(r'socialstat: remote (\d+) typing (\d+) speaking (\d+)', l) for l in blk) if m])
        time.sleep(0.1)
    return None
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-70s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
def press(d, k, hold=0.12, settle=0.4): send(d, ['key %s down' % k], hold); send(d, ['key %s up' % k], settle)
# A in front of the stone pillar (the dev menu's teleport puts it 3 m from the panel, facing it), B a little to the side facing A
send(da, ['menusel c335', 'menudo teleport'], 4.0)
(cx, cy, cz), (th, ph) = A.camera(); fx, fy = math.cos(th), math.sin(th)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx - fx * 5.0, cy - fy * 5.0, cz - 1.69, th, 0.0)], 3.0)      # B 5 m behind A... then turns to look at A
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx - fx * 5.0, cy - fy * 5.0, cz - 1.69, th, 0.0)], 0.5)
# B's view of A: B looks along the same heading as A, A is in front of B
s = stat(db); a0 = stat(da)
check('before anything: nobody is typing, no chat, no marks', s and a0 and not any(r[1] for r in s['remote']) and s['log'] == 0 and a0['log'] == 0, (s['remote'] if s else None))
press(da, 'T', settle=1.2)
sa = stat(da); sb = stat(db)
check('T opens the chat box on A', sa and sa['open'] == 1, sa and sa['open'])
check('B is told A is typing and draws three dots over A', sb and any(r[0] == 1 and r[1] for r in sb['remote']) and sb['dots'] >= 1, (sb['remote'], sb['dots']) if sb else None)
send(da, ['typetext hello there'], 1.0); sa = stat(da)
send(da, ['key ENTER down'], 0.12); send(da, ['key ENTER up'], 2.5)
sa = stat(da); sb = stat(db)
check('Enter sends the line and closes the box', sa and sa['open'] == 0 and any(c[2] == 'hello there' for c in sa['chat']), sa and (sa['open'], sa['chat']))
check('B has the line, from A, in its chat log', sb and any(c[0] == 1 and c[2] == 'hello there' for c in sb['chat']), sb and sb['chat'])
check('B no longer sees A typing', sb and not any(r[1] for r in sb['remote']) and sb['dots'] == 0, sb and (sb['remote'], sb['dots']))
check('B draws the line as a bubble over A', sb and sb['bubbles'] >= 1, sb and sb['bubbles'])
# reliability: a run of lines, with the loss the cfg asks for (drop_pct), all arrive once and in order
for i in range(NLINES): send(da, ['chat line %d' % i], 0.25)
time.sleep(12.0); sb = stat(db)
got = [c[2] for c in sb['chat'] if c[2].startswith('line ')] if sb else []
check('%d more lines in a row arrive once each and in order' % NLINES, got == ['line %d' % i for i in range(NLINES)], got)
# pings: from the depth under A's cross-hair (it looks at the pillar 3 m away), then at an exact point
send(da, ['key P down'], 0.12); send(da, ['key P up'], 2.0)
sa = stat(da); sb = stat(db)
mine = [m for m in sa['marks_list'] if m[0] == 2 or m[0] == 1] if sa else []
d = math.dist((mine[0][2], mine[0][3], mine[0][4]), (cx, cy, cz)) if mine else -1
check('P: a marker lands on the pillar under the cross-hair (1.5 to 4.5 m away)', mine and 1.5 < d < 4.5, 'distance %.2f m' % d)
check('B holds A\'s marker', sb and any(m[0] == 1 for m in sb['marks_list']), sb and sb['marks_list'])
send(da, ['pingat -140.0 -150.0 9.0 1'], 2.0); sb = stat(db)
check('a ping at an exact point arrives with its position intact', sb and any(abs(m[2] + 140.0) < 0.01 and abs(m[3] + 150.0) < 0.01 and abs(m[4] - 9.0) < 0.01 and m[1] == 1 for m in sb['marks_list']), sb and sb['marks_list'])
check('B draws the markers (on screen or as an arrow at the screen edge)', sb and sb['marks_drawn'] + sb['arrows'] >= 2, sb and (sb['marks_drawn'], sb['arrows']))
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
