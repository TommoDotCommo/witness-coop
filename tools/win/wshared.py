# T2 check of the shared co-op keys and the hold-Tab player panel, run with Windows Python. A has the OLD default keys in its cfg (they must migrate), B holds a custom chat key on V (it must keep it).
# Every assertion reads state from the games: keystat (the resolved bindings), panelstat (panel state and control positions), povstat (who is watched), socialstat (marks B holds),
# voicestat (the transmit and receive counters), and the process itself (a confirmed Leave closes A only).
# usage: wshared.py <pidA> <dirA> <pidB> <dirB>
import os, sys, time, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.02)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.02)
    time.sleep(settle)
def last(d, cmd, head, wait=6.0):
    """Ask the game to log one `head:` line (command `cmd`) and return that new line."""
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith(head))
    send(d, [cmd]); t = time.time()
    while time.time() - t < wait:
        ls = [l for l in open(log, errors='ignore') if l.startswith(head)]
        if len(ls) > n0: return ls[-1]
        time.sleep(0.1)
    return None
def nums(line): return {k: int(float(v)) for k, v in re.findall(r'(\w+) (-?\d+(?:\.\d+)?)', line or '')}
def pstat(d): return nums(last(d, 'panelstat', 'panelstat:'))
def kstat(d): return nums(last(d, 'keystat', 'keystat:'))
def pov(d): return nums(last(d, 'povstat', 'povstat:'))
def marks(d):
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('socialstat: open'))
    send(d, ['socialstat']); time.sleep(1.0)
    ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('socialstat: open')]
    return sum(1 for l in ls[idx[-1]:idx[-1] + 40] if l.startswith('socialstat: mark from 1')) if idx else None
def voice(d):
    m = re.match(r'voicestat: mode (\d) ptt (\d+) transmitting (\d+) sent (\d+) captured (\d+) received (\d+)', last(d, 'voicestat', 'voicestat: mode') or '')
    return dict(ptt=int(m[2]), transmitting=int(m[3]), sent=int(m[4]), received=int(m[6])) if m else None
def key(d, name, hold=0.15, settle=0.4): send(d, ['key %s down' % name], hold); send(d, ['key %s up' % name], settle)
def alive(pid):
    h = k.OpenProcess(0x100000 | 0x1000, False, pid)
    if not h: return False
    r = k.WaitForSingleObject(h, 0); k.CloseHandle(h); return r == 0x102
def click_at(d, x, y, settle=0.5):
    p = pstat(d); send(d, ['mouse %d %d' % (x - p['cursorx'], y - p['cursory'])], 0.3); send(d, ['button L down'], 0.1); send(d, ['button L up'], settle)
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-84s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
B_ID = 2
# 0. the keys the games resolved
ka, kb = kstat(da), kstat(db)
check('A (cfg: the old defaults) got the new defaults', ka and ka['key_ping'] == ord('V') and ka['key_ping_alt'] == 4 and ka['key_voice'] == ord('B') and ka['key_voice_alt'] == 5 and ka['key_emote'] == ord('H') and ka['key_voice_channel'] == ord('Y') and ka['key_color'] == 7, ka)
check('B (cfg: chat on V) kept the player\'s key; its ping is on middle mouse only', kb and kb['key_chat'] == ord('V') and kb['key_ping'] == 0 and kb['key_ping_alt'] == 4, kb)
for d, who in ((da, 'A'), (db, 'B')):
    f = os.path.join(d, 'moved_keys.txt'); txt = open(f).read() if os.path.exists(f) else ''
    want = 'Ping a place: P is now V or MMB' if who == 'A' else 'Push to talk: N is now B or XBUTTON1'      # (B's own V is not a moved default: Ping is not listed for it)
    check('%s: the moved-keys list was written once, beside the cfg' % who, want in txt and 'Player list: F5 is now hold Tab' in txt and (who == 'B') == ('Ping a place' not in txt) and os.path.exists(os.path.join(d, 'keys_scheme')), txt.splitlines()[:2])
# 1. the world: A and B near each other so a ping has a surface; both loaded
send(da, ['menusel c335', 'menudo teleport'], 4.0)
(cx, cy, cz), (th, ph) = A.camera()
import math
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx - math.cos(th) * 5.0, cy - math.sin(th) * 5.0, cz - 1.69, th, 0.0)], 3.0)
# 2. ping by V (and middle mouse) reaches B; V does not watch
m0 = marks(db); key(da, 'V', 0.12, 1.5); m1 = marks(db); p1 = pov(da)
check('A pings with V: B holds one more mark from A', m0 is not None and m1 == m0 + 1, (m0, m1))
check('V did not start watching anybody', p1 and p1['watching'] == 0, p1)
key(da, 'MMB', 0.12, 1.5); m2 = marks(db)
check('A pings with the middle mouse button too', m2 == m1 + 1, (m1, m2))
# 3. push to talk by B and by the mouse side button: A's transmit counters, B's receive counter
v0, w0 = voice(da), voice(db)
check('before: not transmitting', v0 and v0['ptt'] == 0 and v0['sent'] == 0, v0)
send(da, ['key B down'], 1.2); v1, w1 = voice(da), voice(db)
check('key B held: A transmits (counter rises) and B receives', v1 and v1['ptt'] == 1 and v1['transmitting'] == 1 and v1['sent'] > v0['sent'] + 15 and w1['received'] > w0['received'] + 15, (v1, w1))
send(da, ['key B up'], 1.0); v2 = voice(da); time.sleep(0.5); v3 = voice(da)
check('key B released: the transmission ends', v2 and v3 and v2['ptt'] == 0 and v3['sent'] <= v2['sent'] + 3, (v2, v3))
send(da, ['key XBUTTON1 down'], 1.2); v4, w4 = voice(da), voice(db)
check('the mouse side button talks as well', v4 and v4['ptt'] == 1 and v4['sent'] > v3['sent'] + 15 and w4['received'] > w1['received'] + 15, (v4, w4))
send(da, ['key XBUTTON1 up'], 1.0)
# 4. hold Tab: the panel lists B; Release closes it
send(da, ['key TAB down'], 0.6); p0 = pstat(da)
check('Tab held: the panel is open and lists B (one row)', p0 and p0['open'] == 1 and p0['rows'] == 1 and p0['first_id'] == B_ID, p0)
pb0 = pstat(db)
check('the invite the launcher shared is offered (Copy invite shown) on A, hidden on B', p0['invite'] == 1 and p0['copyx'] > 0 and pb0['invite'] == 0 and pb0['copyx'] == 0, (p0['invite'], p0['copyx'], pb0['invite'], pb0['copyx']))
click_at(da, p0['mutex'], p0['mutey']); p1 = pstat(da)
check('clicking Mute on B\'s row mutes B (voice volume 0)', p1['first_muted'] == 1 and p1['actions'] == p0['actions'] + 1, p1)
click_at(da, p0['mutex'], p0['mutey']); p2 = pstat(da)
check('clicking again unmutes', p2['first_muted'] == 0, p2)
click_at(da, p0['voiceupx'], p0['voiceupy']); p3 = pstat(da)
check('the voice volume toggle goes up by 10', p3['voice_volume'] == p0['voice_volume'] + 10 or p0['voice_volume'] == 100, (p0['voice_volume'], p3['voice_volume']))
click_at(da, p0['tagsx'], p0['tagsy']); p4 = pstat(da)
check('the name tags toggle switches them off', p4['tags_off'] == 1, p4)
click_at(da, p0['tagsx'], p0['tagsy']); click_at(da, p0['watchx'], p0['watchy']); w = pov(da)
check('clicking Watch on B\'s row starts watching B', w and w['watching'] == B_ID, w)
send(da, ['key TAB up'], 0.6); pc = pstat(da)
check('releasing Tab closes the panel', pc and pc['open'] == 0, pc)
# 5. [ and ] cycle
key(da, 'RB', 0.1, 0.6); c1 = pov(da); key(da, 'LB', 0.1, 0.6); c2 = pov(da)
check('] after the last (only) player goes off; [ goes back to B', c1 and c2 and c1['watching'] == 0 and c2['watching'] == B_ID, (c1 and c1['watching'], c2 and c2['watching']))
# 6. the panel by keyboard, and Leave: no confirmation => nothing happens; confirmed => A's game closes, B's keeps running
send(da, ['key TAB down'], 0.5); key(da, 'L', 0.1, 0.4); l1 = pstat(da)
check('L (Leave) asks first: the confirmation is open and the game runs', l1 and l1['leave_ask'] == 1 and alive(pa), l1)
key(da, 'ESC', 0.1, 0.4); l2 = pstat(da)
check('Esc answers "stay": the question closes, the panel stays', l2 and l2['leave_ask'] == 0 and l2['open'] == 1 and alive(pa), l2)
click_at(da, p0['leavex'], p0['leavey']); l3 = pstat(da)
check('clicking Leave opens the same confirmation', l3 and l3['leave_ask'] == 1, l3)
key(da, 'ENTER', 0.1, 0.1)
t = time.time()
while alive(pa) and time.time() - t < 20: time.sleep(0.5)
check('confirming closes A\'s game; B keeps running', not alive(pa) and alive(pb), (alive(pa), alive(pb)))
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
