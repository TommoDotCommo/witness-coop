# T2 check of voice chat, run with Windows Python. A talks (a test voice of three tones: 120 Hz, 1 kHz and 5.5 kHz, in place of a microphone), B listens (its speaker output is muted but the
# mixer runs as usual and reports what it mixed). The test moves A to different distances and sides of B (positions read from both games' memory, so the expected values come from the
# real geometry) and compares B's mixer with the formulas: loudness falls with distance, pans to the side the speaker is on, turns into a band-limited radio far away, fades between.
# usage: wvoice.py <pidA> <dirA> <pidB> <dirB> <ptt|open>      (B's cfg: voice_volume=<vol>%, voice_radio_range=<r>, game_volume=<g>; see qa_voice.py)
import os, sys, time, math, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, mode = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
VOL, RADIO_RANGE, RANGE = float(sys.argv[6]) if len(sys.argv) > 6 else 1.0, float(sys.argv[7]) if len(sys.argv) > 7 else 0.0, 25.0
GV = float(sys.argv[8]) if len(sys.argv) > 8 else 1.0
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    time.sleep(settle)
def count(d, prefix): return sum(1 for l in open(os.path.join(d, 'witness_proxy.log'), errors='ignore') if l.startswith(prefix))
def lines_after(d, cmd, head, tailre, wait=6.0):
    log = os.path.join(d, 'witness_proxy.log'); n0 = count(d, head); send(d, [cmd]); t = time.time()
    while time.time() - t < wait:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith(head)]
        if len(idx) > n0:
            time.sleep(0.1); ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith(head)]; blk = [ls[idx[-1]]]
            for l in ls[idx[-1] + 1:]:
                if not re.match(tailre, l): break
                blk.append(l)
            return blk
        time.sleep(0.05)
    return []
F = r'(-?[0-9.]+)'
def vstat(d):
    b = lines_after(d, 'voicestat', 'voicestat: mode', r'voicestat: speaker ')
    if not b: return None
    m = re.match(r'voicestat: mode (\d) ptt (\d+) transmitting (\d+) sent (\d+) captured (\d+) received (\d+) played (\d+) game_volume ' + F + r' hud_speakers (\d+) hud_mic (\d)', b[0])
    r = dict(mode=int(m[1]), ptt=int(m[2]), transmitting=int(m[3]), sent=int(m[4]), received=int(m[6]), game_volume=float(m[8]), hud_speakers=int(m[9]), hud_mic=int(m[10]), sp={})
    for l in b[1:]:
        m = re.match(r'voicestat: speaker (\d+) talking (\d) prox ' + F + ' radio ' + F + ' pan ' + F + ' rms ' + F + ' tone ' + F + ' ' + F + ' ' + F + r' packets (\d+) concealed (\d+) dropped (\d+)', l)
        r['sp'][int(m[1])] = dict(talking=int(m[2]), prox=float(m[3]), radio=float(m[4]), pan=float(m[5]), rms=float(m[6]), tone=[float(m[7]), float(m[8]), float(m[9])], packets=int(m[10]), concealed=int(m[11]))
    return r
def speaking_seen_by_b():
    b = lines_after(db, 'socialstat', 'socialstat: open', r'socialstat: (chat|mark|me|remote) ')
    for l in b:
        m = re.match(r'socialstat: remote (\d+) typing (\d+) speaking (\d+)', l)
        if m and int(m[1]) == 1: return int(m[3])
    return None
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-92s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
def smooth(x): x = max(0.0, min(1.0, x)); return x * x * (3 - 2 * x)
def expect(d, beta):
    near = 2.5; R = RANGE; fade = max(6.0, 0.3 * R); inv = 1.0 if d <= near else near / d
    taper = 1 - smooth((d - 0.75 * R) / (R + fade - 0.75 * R)); r = smooth((d - R) / fade)
    radio = r * 0.85 * VOL
    if RADIO_RANGE > 0 and d > RADIO_RANGE: radio *= 1 - smooth((d - RADIO_RANGE) / (0.1 * RADIO_RANGE + 1))
    return inv * taper * VOL, radio, math.sin(beta) * 0.9
def place(d, beta_deg):
    """B stays; A is put d metres from B at beta degrees to B's right; the real positions and heading are read back from both games"""
    (bx, by, bz), (bth, bph) = B.camera(); beta = math.radians(beta_deg); ang = bth - beta
    send(da, ['hover %.3f %.3f %.3f %.4f 0' % (bx + d * math.cos(ang), by + d * math.sin(ang), bz, bth)], 3.0)      # held in the air where it is put: no ground is needed that far away
    (ax, ay, az), _ = A.camera(); (bx, by, bz), (bth, bph) = B.camera()
    dx, dy, dz = ax - bx, ay - by, az - bz; dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    bearing = math.atan2(dx * math.sin(bth) - dy * math.cos(bth), dx * math.cos(bth) + dy * math.sin(bth))
    return dist, bearing
send(da, ['menusel c335', 'menudo teleport'], 4.0)
(ax, ay, az), (ath, aph) = A.camera()
send(db, ['tp %.4f %.4f %.4f %.5f 0' % (ax - math.cos(ath) * 2.0, ay - math.sin(ath) * 2.0, az - 1.69, ath)], 3.0)
if mode == 'open':
    d, beta = place(5.0, 90); time.sleep(1.5); s = vstat(db); e = expect(d, beta); sp = s['sp'].get(1) if s else None
    check('1. 5 m to the right: B hears A (talking, packets arriving)', sp and sp['talking'] == 1 and sp['packets'] > 20, sp and (sp['talking'], sp['packets']))
    check('   proximity gain follows the distance (2.5 / d times the volume)', sp and abs(sp['prox'] - e[0]) < 0.03, (sp and round(sp['prox'], 3), round(e[0], 3), 'd=%.2f' % d))
    check('   no radio yet, and the voice is to the right', sp and sp['radio'] == 0 and abs(sp['pan'] - e[2]) < 0.08 and sp['pan'] > 0.5, sp and (sp['radio'], round(sp['pan'], 2), round(e[2], 2)))
    check('   the whole voice is there (120 Hz, 1 kHz, 5.5 kHz) and loud enough', sp and sp['tone'][0] > 0.01 and sp['tone'][1] > 0.01 and sp['tone'][2] > 0.4 * sp['tone'][1] and sp['rms'] > 0.01, sp and ([round(t, 3) for t in sp['tone']], round(sp['rms'], 3)))
    check('   B sees A as speaking (the flag in A\'s presence)', speaking_seen_by_b() == 1)
    d, beta = place(12.0, -90); time.sleep(1.5); s = vstat(db); e = expect(d, beta); sp = s['sp'].get(1) if s else None
    check('2. 12 m to the left: quieter and in the left ear', sp and abs(sp['prox'] - e[0]) < 0.03 and sp['pan'] < -0.5 and sp['radio'] == 0, sp and (round(sp['prox'], 3), round(e[0], 3), round(sp['pan'], 2), 'd=%.2f' % d))
    d, beta = place(28.0, 0); time.sleep(1.5); s = vstat(db); e = expect(d, beta); sp = s['sp'].get(1) if s else None
    check('3. 28 m, just outside the range: the proximity voice and the radio are both there (the fade)', sp and sp['prox'] > 0.005 and sp['radio'] > 0.1 and abs(sp['radio'] - e[1]) < 0.06, sp and (round(sp['prox'], 3), round(sp['radio'], 3), 'want radio %.3f' % e[1], 'd=%.2f' % d))
    sa = vstat(da)
    check('   B draws the speaking marks over A (A is ahead of B) and A draws its microphone indicator', s and s['hud_speakers'] >= 1 and sa and sa['hud_mic'] == 1 and s['hud_mic'] == 0, s and sa and (s['hud_speakers'], sa['hud_mic'], s['hud_mic']))
    d, beta = place(80.0, 90); time.sleep(1.5); s = vstat(db); e = expect(d, beta); sp = s['sp'].get(1) if s else None
    check('4. 80 m away: the radio, no proximity voice, from the middle', sp and sp['talking'] == 1 and sp['prox'] == 0 and abs(sp['radio'] - e[1]) < 0.04 and abs(sp['pan'] - e[2]) < 0.08, sp and (round(sp['prox'], 3), round(sp['radio'], 3), round(e[1], 3), 'd=%.2f' % d))
    check('   it sounds like a walkie-talkie: 120 Hz and 5.5 kHz at least 12 dB below 1 kHz, 1 kHz present', sp and sp['tone'][1] > 0.02 * VOL and sp['tone'][0] < 0.25 * sp['tone'][1] and sp['tone'][2] < 0.25 * sp['tone'][1], sp and [round(t, 3) for t in sp['tone']])
    if RADIO_RANGE > 0:
        d, beta = place(RADIO_RANGE * 1.6, 0); time.sleep(1.5); s = vstat(db); sp = s['sp'].get(1) if s else None; e = expect(d, beta)
        check('5. beyond the radio range (%d m) nothing is heard' % RADIO_RANGE, sp and sp['radio'] < 0.02 and sp['prox'] == 0, sp and (round(sp['radio'], 3), 'd=%.1f' % d))
else:
    d, beta = place(5.0, 90); time.sleep(1.0); s0 = vstat(db); sp = s0['sp'].get(1) if s0 else None
    check('1. push to talk, key not held: nothing is sent', not sp or (sp['talking'] == 0 and sp['packets'] == 0), sp and (sp['talking'], sp['packets']))
    check('   and B does not see A as speaking', speaking_seen_by_b() in (0, None))
    send(da, ['key N down'], 1.2); s1 = vstat(db); sp = s1['sp'].get(1) if s1 else None
    check('2. key N held: B hears A', sp and sp['talking'] == 1 and sp['packets'] > 20, sp and (sp['talking'], sp['packets']))
    check('   B sees A as speaking', speaking_seen_by_b() == 1)
    send(da, ['key N up'], 1.5); s2 = vstat(db); sp2 = s2['sp'].get(1) if s2 else None
    check('3. key released: the transmission ends, B stops hearing A and the speaking flag drops', sp2 and sp2['talking'] == 0 and speaking_seen_by_b() == 0, sp2 and sp2['talking'])
    p1 = sp2['packets'] if sp2 else 0; time.sleep(1.0); s3 = vstat(db)
    check('   and nothing more arrives', s3 and s3['sp'][1]['packets'] == p1, s3 and (s3['sp'][1]['packets'], p1))
if mode == 'open':
    s = vstat(db)
    if s and s['game_volume'] >= 0: check('6. game_volume is set on the game\'s Windows audio session (read back from the system)', abs(s['game_volume'] - GV) < 0.02, s['game_volume'])
    else: print('  6. game volume: unknown (the game has no audio session on this machine)', flush=True)
send(da, ['hover off'])
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
