# T2 check of the co-op pad layer (common/padlayer.h, proxy/pad.cpp), run with Windows Python. A stands in for a pad with the proxy's `pad` command,
# which answers the game's own XInputGetState as a real pad would; B is the other player. Every assertion reads state from the games: witness_pad.json
# (`padstat`: what the layer did and the pad the game last got), the camera in A's memory (the player did not move), panelstat (the panel),
# socialstat (the chat and marks B holds).
# usage: wpad.py <pidA> <dirA> <pidB> <dirB>
import os, sys, time, re, json, math
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
def padstat(d):
    f = os.path.join(d, 'witness_pad.json')
    try: os.remove(f)
    except OSError: pass
    send(d, ['padstat']); t = time.time()
    while time.time() - t < 5:
        try: return json.load(open(f))
        except (OSError, ValueError): time.sleep(0.05)
    return None
def last(d, cmd, head, wait=6.0):
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith(head))
    send(d, [cmd]); t = time.time()
    while time.time() - t < wait:
        ls = [l for l in open(log, errors='ignore') if l.startswith(head)]
        if len(ls) > n0: return ls[-1]
        time.sleep(0.1)
    return None
def nums(line): return {k: int(float(v)) for k, v in re.findall(r'(\w+) (-?\d+(?:\.\d+)?)', line or '')}
def pstat(d): return nums(last(d, 'panelstat', 'panelstat:'))
def social(d):
    """B's chat lines and marks from A (id 1), from one socialstat readout."""
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('socialstat: open'))
    send(d, ['socialstat']); t = time.time()
    while time.time() - t < 5:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('socialstat: open')]
        if len(idx) > n0: block = ls[idx[-1]:idx[-1] + 120]; return [l.split(' text ', 1)[1] for l in block if l.startswith('socialstat: chat from 1 ') and ' text ' in l], sum(1 for l in block if l.startswith('socialstat: mark from 1'))
        time.sleep(0.1)
    return None, None
def pad(buttons, lx=0, ly=0, rx=0, ry=0, lt=0, rt=0, settle=0.0): send(da, ['pad %x %d %d %d %d %d %d' % (buttons, lx, ly, rx, ry, lt, rt)], settle)
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-84s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
VIEW, UP, DOWN, START, RB, A_BTN = 0x20, 0x01, 0x02, 0x10, 0x200, 0x1000
# 0. A and B close together on open ground (a ping needs a surface in front)
send(da, ['menusel c335', 'menudo teleport'], 4.0)
(cx, cy, cz), (th, ph) = A.camera()
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx + math.cos(th) * 4.0, cy + math.sin(th) * 4.0, cz - 1.69, th + math.pi, 0.0)], 3.0)
pad(0, settle=1.0)
s0 = padstat(da)
check('the stand-in pad is on slot 0 and the game reads its pad through the layer', s0 and s0['fake'] and s0['slot'] == 0 and s0['game_calls'] > 0, s0 and (s0['slot'], s0['game_calls']))
calls0 = s0['game_calls'] if s0 else 0
# 1. hold View: the player panel
pad(VIEW, settle=0.9); s1 = padstat(da); p1 = pstat(da)
check('holding View opens the player panel', s1 and s1['list'] and s1['panel_open'] and p1.get('open') == 1, s1 and (s1['list'], s1['panel_open'], p1.get('open')))
check('the glyphs switched to the pad; voice is open mic (automatic)', s1 and s1['glyphs'] == 'pad' and s1['voice_mode'] == 1, s1 and (s1['glyphs'], s1['voice_mode']))
# 2. View + D-pad up: the emote wheel; with the left stick pushed forward and the right stick aside, the player stays where they are
(x0, y0, z0), (th0, ph0) = A.camera()
pad(VIEW | UP, 0, 32000, settle=0.4); pad(VIEW, 0, 32000, 0, 30000, settle=1.5)
s2 = padstat(da); (x1, y1, z1), (th1, ph1) = A.camera(); moved = math.dist((x0, y0, z0), (x1, y1, z1)); turned = abs(th1 - th0) + abs(ph1 - ph0)
check('View + D-pad up opens the emote wheel and closes the panel', s2 and s2['wheel'] == 1 and not s2['panel_open'] and s2['social_busy'], s2 and (s2['wheel'], s2['panel_open']))
check('the game got no D-pad, no stick while View was held (its last pad read)', s2 and s2['game_buttons'] == 0 and s2['game_ly'] == 0 and s2['game_rx'] == 0 and s2['game_calls'] > calls0, s2 and (s2['game_buttons'], s2['game_ly'], s2['game_rx'], s2['game_calls'] - calls0))
check('the player did not move or turn (left stick forward 1.5 s while View was held)', moved < 0.05 and turned < 0.01, 'moved %.3f m, turned %.4f rad' % (moved, turned))
pad(0, settle=0.6); s3 = padstat(da)
check('letting go of View closes the wheel (the stick picked an emote), no ping', s3 and s3['wheel'] == 0 and not s3['social_busy'] and s3['pings'] == 0, s3 and (s3['wheel'], s3['social_busy'], s3['pings']))
# 3. View + RB: the quick chat wheel; the right stick up picks "Ready", letting go sends it; B gets it
pad(VIEW | RB, settle=0.3); pad(VIEW, 0, 0, 0, 30000, settle=0.4); s4 = padstat(da); pad(0, settle=2.5)
chat_b, _ = social(db)
check('View + RB opened the quick chat wheel', s4 and s4['wheel'] == 2, s4 and s4['wheel'])
check('the quick chat line "Ready" reached B', chat_b is not None and 'Ready' in chat_b, chat_b)
# 4. a key: the hints and the voice mode follow the keyboard again; a pad press back to the pad
send(da, ['key SHIFT down', 'key SHIFT up'], 0.6); s5 = padstat(da)
check('after a key the hints show keys and voice is push to talk (automatic)', s5 and s5['glyphs'] == 'keys' and s5['voice_mode'] == 2, s5 and (s5['glyphs'], s5['voice_mode']))
# 5. a tap of View pings; B sees the mark
_, marks0 = social(db)
pad(VIEW, settle=0.08); pad(0, settle=2.5); s6 = padstat(da); _, marks1 = social(db)
check('a tap of View is one ping and no panel', s6 and s6['pings'] == 1 and not s6['panel_open'] and s6['glyphs'] == 'pad', s6 and (s6['pings'], s6['panel_open']))
check('B sees the ping', marks0 is not None and marks1 is not None and marks1 > marks0, (marks0, marks1))
# 6. View + D-pad down: chat with our on-screen keyboard (no Steam keyboard in the lab), A types "q", Menu sends; B gets it
pad(VIEW | DOWN, settle=0.3); pad(0, settle=0.4); s7 = padstat(da)
check('View + D-pad down opens chat with the on-screen keyboard', s7 and s7['keyboard'] == 'own' and s7['chats'] == 1, s7 and (s7['keyboard'], s7['chats'], s7['steam_keyboard']))
pad(A_BTN, settle=0.2); s8 = padstat(da); pad(0, settle=0.2); pad(START, settle=0.2); pad(0, settle=2.5)
chat_b, _ = social(db); s9 = padstat(da)
check('the game got no pad while the keyboard was up', s8 and s8['game_buttons'] == 0, s8 and s8['game_buttons'])
check('A on the keyboard typed q and Menu sent it to B; the keyboard closed', chat_b is not None and 'q' in chat_b and s9 and s9['keyboard'] == 'none', (chat_b, s9 and s9['keyboard']))
send(da, ['pad off'], 0.3)
print('ALL OK' if ok else 'FAILED', flush=True)
sys.exit(0 if ok else 1)
