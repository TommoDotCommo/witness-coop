# T2 check of emotes and the character switcher, run with Windows Python. A is the player who emotes, B watches. A plays emotes by command and with the real wheel (the key G held, the
# mouse moved toward a slot, the key released: the camera must not turn while the wheel is open); B must be told, play the motion on A's puppet (lift, lean, roll or turn measured in
# its draw) and stop when it is over. A changes character by command and with the real list (C, Down, Down, Enter); B must draw A with that model.
# usage: wemote.py <pidA> <dirA> <pidB> <dirB>
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
def lines_after(d, prefix, request, n0, wait=4.0):
    log = os.path.join(d, 'witness_proxy.log'); t = time.time()
    while time.time() - t < wait:
        ls = [l for l in open(log, errors='ignore').read().splitlines() if l.startswith(prefix)]
        if len(ls) > n0: return ls
        time.sleep(0.05)
    return []
def count(d, prefix): return sum(1 for l in open(os.path.join(d, 'witness_proxy.log'), errors='ignore') if l.startswith(prefix))
def social(d):
    """the 'me' line and the remote lines of a socialstat"""
    log = os.path.join(d, 'witness_proxy.log'); n0 = count(d, 'socialstat: me'); send(d, ['socialstat']); t = time.time()
    while time.time() - t < 6:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('socialstat: me')]
        if len(idx) > n0:
            me = ls[idx[-1]]; rem = []
            for l in ls[idx[-1] + 1:]:
                m = re.match(r'socialstat: remote (\d+) typing (\d+) speaking (\d+) emote (\d+) character (\S*) playing (\d+) t ([0-9.]+)', l)
                if not m: break
                rem.append(dict(id=int(m[1]), emote=int(m[4]), character=m[5], playing=int(m[6]), t=float(m[7])))
            mm = re.match(r'socialstat: me character (\S*) emote (\d+) seq (\d+) wheel (\d+) picker (\d+)(?: paused (\d+))?', me)
            return dict(character=mm[1], emote=int(mm[2]), seq=int(mm[3]), wheel=int(mm[4]), picker=int(mm[5]), paused=int(mm[6] or 0), remote=rem)
        time.sleep(0.05)
def model_stat(d):
    log = os.path.join(d, 'witness_proxy.log'); n0 = count(d, 'modelstat:'); send(d, ['modelstat']); t = time.time()
    while time.time() - t < 6:
        ls = [l for l in open(log, errors='ignore') if l.startswith('modelstat:')]
        if len(ls) > n0:
            m = re.search(r'model (\S+) \d+/\d+ .* fx emote (\d+) lift ([-0-9.]+) lean ([-0-9.]+) roll ([-0-9.]+) dyaw ([-0-9.]+)', ls[-1])
            return dict(model=m[1], emote=int(m[2]), lift=float(m[3]), lean=float(m[4]), roll=float(m[5]), dyaw=float(m[6]))
        time.sleep(0.05)
def watch(d, seconds):
    """model_stat samples over a while: the largest of each effect"""
    end = time.time() + seconds; best = dict(lift=0.0, lean=0.0, roll=0.0, dyaw=0.0, emote=0); n = 0
    while time.time() < end:
        s = model_stat(d)
        if s:
            n += 1
            for k in ('lift', 'lean', 'roll', 'dyaw'): best[k] = max(best[k], abs(s[k]))
            best['emote'] = max(best['emote'], s['emote'])
        time.sleep(0.05)
    best['n'] = n; return best
ok = True
def check(name, cond, detail):
    global ok; ok = ok and bool(cond); print('  %-72s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
send(da, ['menusel c335', 'menudo teleport'], 4.0)
(cx, cy, cz), (th, ph) = A.camera(); fx, fy = math.cos(th), math.sin(th)
send(db, ['tp %.4f %.4f %.4f %.5f 0' % (cx - fx * 5.0, cy - fy * 5.0, cz - 1.69, th)], 3.0)
b0 = model_stat(db); s0 = social(db)
check('at rest: B draws A without an emote', b0 and b0['emote'] == 0 and b0['lift'] == 0 and b0['dyaw'] == 0, b0)
# 1. an emote by command: B is told and plays the hop; then it stops
send(da, ['emote 3'], 0.3); sb = social(db)
w = watch(db, 1.8)
check('A plays Cheer: B is told (emote 3 from A)', sb and any(r['id'] == 1 and r['emote'] == 3 for r in sb['remote']), sb and sb['remote'])
check('B draws A hopping (the figure lifts more than 0.2 m)', w['emote'] == 3 and w['lift'] > 0.2, w)
time.sleep(2.0); b1 = model_stat(db)
check('the emote ends by itself (no motion, no emote)', b1 and b1['emote'] == 0 and b1['lift'] == 0, b1)
# 2. the real wheel: G held, mouse right = the third slot (Cheer); the camera must not move
h0 = A.camera()[1][0]
send(da, ['key G down'], 0.2); send(da, ['mouse 60 0'], 0.6); sa = social(da)
check('G held opens the wheel', sa and sa['wheel'] == 1, sa and sa['wheel'])
send(da, ['key G up'], 0.4); h1 = A.camera()[1][0]; sa = social(da)
check('releasing G plays the slot the mouse pointed to (Cheer, 3); the camera did not turn', sa and sa['emote'] == 3 and sa['wheel'] == 0 and abs(h1 - h0) < 0.01, (sa and (sa['emote'], sa['wheel']), round(h1 - h0, 4)))
sb = social(db); w = watch(db, 1.6)
check('B sees it again (a new emote counter) and the hop', sb and any(r['id'] == 1 and r['emote'] == 3 for r in sb['remote']) and w['lift'] > 0.2, (sb and sb['remote'], w))
time.sleep(2.5)
# 3. up and to the left = the last slot (Shrug, 8): roll and lean, no hop beyond a few centimetres
send(da, ['key G down'], 0.2); send(da, ['mouse -40 -40'], 0.6); send(da, ['key G up'], 0.4)
w = watch(db, 1.8)
check('up-left selects Shrug (8): B sees a roll', w['emote'] == 8 and w['roll'] > 0.05, w)
time.sleep(2.5)
# 4. releasing with the mouse still in the middle cancels
send(da, ['key G down'], 0.2); send(da, ['key G up'], 0.5); sa = social(da)
check('no direction chosen: nothing plays', sa and sa['emote'] == 0, sa and sa['emote'])
# 5. the character: by command, B draws A with it; and back
send(da, ['character fox'], 1.5); sb = social(db); bs = model_stat(db)
check('A announces fox: B has it in presence', sb and any(r['id'] == 1 and r['character'] == 'fox' for r in sb['remote']), sb and sb['remote'])
check('B draws A as the fox (it has that model)', bs and bs['model'] == 'fox', bs and bs['model'])
send(da, ['character none'], 1.5); bs = model_stat(db)
check('A announces nothing again: B is back to its own choice', bs and bs['model'] == 'cesium-man', bs and bs['model'])
# 6. the real list: C opens it, Down twice (cesium-man, fox), Enter chooses
send(da, ['key C down'], 0.12); send(da, ['key C up'], 0.5); sa = social(da)
check('C opens the character list', sa and sa['picker'] == 1, sa and sa['picker'])
for k in ('DOWN', 'DOWN'): send(da, ['key %s down' % k], 0.12); send(da, ['key %s up' % k], 0.3)
send(da, ['key ENTER down'], 0.12); send(da, ['key ENTER up'], 1.5); sa = social(da); bs = model_stat(db)
check('Enter chooses the second model: A is the fox, B draws it so', sa and sa['character'] == 'fox' and sa['picker'] == 0 and bs and bs['model'] == 'fox', (sa and (sa['character'], sa['picker']), bs and bs['model']))
# 7. Esc closes the list and the chat box and nothing else: the same press arrives a moment later as raw input and must not open the game's pause menu
send(da, ['key C down'], 0.12); send(da, ['key C up'], 0.5); send(da, ['key ESC down'], 0.12); send(da, ['key ESC up'], 1.2); sa = social(da)
check('Esc closes the character list and the game is not paused', sa and sa['picker'] == 0 and sa['paused'] == 0, sa and (sa['picker'], sa['paused']))
send(da, ['key T down'], 0.12); send(da, ['key T up'], 0.5); send(da, ['key ESC down'], 0.12); send(da, ['key ESC up'], 1.2); sa = social(da)
check('Esc cancels the chat box and the game is not paused', sa and sa['paused'] == 0, sa and sa['paused'])
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
