#!/usr/bin/env python3
"""Two-client audience TV seat scenarios (run with tools/qa.sh probe tools/qa_tv_seats.py [race|wearer|deployer|terrain]...).
A is the wearer (source of the picture), B deploys A's screen. Everything is asserted on state read from the two games.
  race:     (race_mutant: the same with the tiebreak switched off, which must show the double seat) both players take the same stool in the same instant; afterwards exactly one may be seated there.
  wearer:   the wearer's game dies while the deployer sits; the deployer must stand at the entry pose, screen and streams gone.
  deployer: the deployer's game dies while the wearer sits; same expectations for the wearer.
  look:     takes in-game captures of the screen and stools (flat ground and the spot below) into the run folder.
  terrain:  deploy at several places on the island (tour spots, spot 17 is steep); every stool must stand on the ground the game really has there (probed from a higher drop than the mod's own),
            and the screen's bottom edge must clear the ground under both ends. terrain_mutant: the same with no_terrain_probe=1 (flat-ground placement), which must fail at spot 17.
TV_SEATS_TRIALS=n sets the number of simultaneous-press trials (default 6). TV_SEATS_SPOTS=1,4,.. sets the tour spots (default a spread).
"""
import json, math, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import DIR1, DIR2, RUN, PYW, TOOLS, pyw, win, pid, real_saves
import qa_sweep

EYE = 1.71
CFG = 'pov_tv=1\npov_tv_consent=%d'


class Game:
    def __init__(self, which, directory): self.which = which; self.dir = directory; self.alive = True

    def send(self, text):
        dest = os.path.join(self.dir, 'witness_cmd.txt'); deadline = time.monotonic() + 15
        while os.path.exists(dest):
            if time.monotonic() > deadline: raise RuntimeError('command not consumed by ' + self.which)
            time.sleep(.05)
        tmp = dest + '.tmp'; open(tmp, 'w').write(text + '\n'); os.replace(tmp, dest)

    def dump(self, command, name):
        dest = os.path.join(self.dir, name)
        if os.path.exists(dest): os.remove(dest)
        self.send(command); deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try: return json.load(open(dest))
            except (OSError, ValueError): time.sleep(.05)
        raise RuntimeError('no %s from %s' % (name, self.which))

    def tv(self): return self.dump('tvstate', 'witness_tv.json')
    def net(self): return self.dump('netstate', 'witness_net.json')

    def camera(self):
        for line in pyw('wcam.py', pid(self.which)).splitlines():
            n = line.split()
            if len(n) == 5:
                try: return [float(x) for x in n]
                except ValueError: pass
        raise RuntimeError('camera read failed')

    def kill(self):
        subprocess.run(['taskkill.exe', '/PID', pid(self.which), '/F'], capture_output=True); self.alive = False


def wait(predicate, what, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if predicate(): print('PASS', what, flush=True); return
        except Exception: pass
        time.sleep(.5)
    raise AssertionError('FAIL ' + what)


def start(consent_a=1, extra=''):
    qa_sweep.restart('name=Alpha\n' + CFG % consent_a + extra, 'name=Beta\n' + CFG % 0 + extra)
    a, b = Game('A', DIR1), Game('B', DIR2)
    wait(lambda: a.net()['ready'] and b.net()['ready'], 'both games in the stable world', 120); time.sleep(4)
    return a, b


def deploy(a, b, command='menusel c335\nmenudo teleport'):
    a.send(command); time.sleep(3)
    x, y, z, yaw, _ = a.camera()
    b.send('hover %.5f %.5f %.5f %.6f -0.1' % (x - math.cos(yaw) * 3.5, y - math.sin(yaw) * 3.5, z, yaw))
    wait(lambda: abs(b.camera()[0] - (x - math.cos(yaw) * 3.5)) < .4, 'deployer next to the wearer', 30); time.sleep(2)
    b.send('tvdeploy 1')
    wait(lambda: len(a.tv()['audience']) == 1 and len(b.tv()['audience']) == 1 and not b.tv()['probing'], 'screen deployed (ground measured) and known to both', 60)
    s = b.tv()['audience'][0]
    return dict(yaw=yaw, center=s['center'], ground=s['ground'], stools=s['stools'], wearer=(x, y, z))


def stool(g, i):
    f = (math.cos(g['yaw']), math.sin(g['yaw'])); r = (f[1], -f[0]); side = (i - 1) * 1.35
    return [g['center'][0] + f[0] * 4 + r[0] * side, g['center'][1] + f[1] * 4 + r[1] * side, g['stools'][i]]


def entry(g, i):
    s = stool(g, i); f = (math.cos(g['yaw']), math.sin(g['yaw'])); return [s[0] + f[0], s[1] + f[1], s[2] + EYE]


def stand_at_entry(game, g, i):
    e = entry(g, i); face = g['yaw'] + math.pi; pitch = math.atan2(.5 - EYE, 1)
    game.send('hover %f %f %f %f %f' % (*e, face, pitch))
    wait(lambda: abs(game.camera()[0] - e[0]) < .1 and abs(game.camera()[1] - e[1]) < .1, game.which + ' at the stool entry', 20)
    game.send('hover off')


def press(game): game.send('key K down\nkey K up')


def race(trials, mutant=False):
    a, b = start(extra='\nno_seat_tiebreak=1' if mutant else ''); g = deploy(a, b); b.send('hover off')
    both = 0
    if not mutant:      # the ordinary case first: a stool someone has been sitting on for a while is taken
        stand_at_entry(a, g, 1); press(a); wait(lambda: a.tv()['seated'], 'A seated first', 15); time.sleep(2)
        stand_at_entry(b, g, 1); press(b); time.sleep(3)
        assert not b.tv()['seated'], 'B sat on the stool A already occupies'
        print('PASS a stool occupied for 2 s refuses a second player', flush=True)
        press(a); wait(lambda: not a.tv()['seated'], 'A stands', 15)
    for trial in range(trials):
        for game in (a, b): stand_at_entry(game, g, 1)
        press(a); press(b)               # two files written back to back: both games take them within a frame or two
        time.sleep(4)
        sa, sb = a.tv()['seated'], b.tv()['seated']
        pa, pb = a.camera(), b.camera()
        close = math.hypot(pa[0] - pb[0], pa[1] - pb[1]) < .5
        print('trial %d: seated A=%s B=%s eyes %.2f apart' % (trial, sa, sb, math.hypot(pa[0] - pb[0], pa[1] - pb[1])), flush=True)
        if sa and sb and close: both += 1
        for game in (a, b):
            if game.tv()['seated']: press(game)
        wait(lambda: not a.tv()['seated'] and not b.tv()['seated'], 'both stand again', 15)
    if mutant:
        assert both > 0, 'the mutant never produced a double seating: the trial cannot fail'
        print('PASS (mutant) double seating observed in %d of %d trials' % (both, trials), flush=True); return
    assert both == 0, 'both players sat on the same stool in %d of %d trials' % (both, trials)
    print('PASS no double seating in %d simultaneous trials' % trials, flush=True)


def after_loss(survivor, g, what):
    e = entry(g, 1)
    wait(lambda: not survivor.tv()['seated'], what + ': survivor stands', 25)
    wait(lambda: len(survivor.tv()['audience']) == 0, what + ': screen removed', 15)
    wait(lambda: (lambda t: t['audience_streams'] == 0 and t['audience_resources'] == 0 and t['audience_stats'] == 0)(survivor.tv()), what + ': streams, textures and stats freed', 15)
    c = survivor.camera(); assert abs(c[2] - e[2]) < .3, (what, 'camera not back at standing height', c[2], e[2])
    print('PASS %s: standing again at the entry, camera height %.2f' % (what, c[2]), flush=True)


def seated_loss(who_sits, who_dies):
    a, b = start(); g = deploy(a, b); b.send('hover off')
    sitter, dying = (a, b) if who_sits == 'A' else (b, a)
    stand_at_entry(sitter, g, 1); press(sitter)
    wait(lambda: sitter.tv()['seated'], who_sits + ' seated', 15); time.sleep(1)
    dying.kill(); print('killed', who_dies, flush=True)
    after_loss(sitter, g, 'wearer lost' if who_dies == 'A' else 'deployer lost')
    sitter.send('hover off')


def deploy_near(a, b, what):
    """The deployer stands next to the wearer, as a person would (the world around it has collision there); the mod measures the ground by itself."""
    x, y, z, yaw, _ = a.camera(); bx, by = x - math.cos(yaw) * 3.5, y - math.sin(yaw) * 3.5
    b.send('hover %.5f %.5f %.5f %.6f -0.1' % (bx, by, z, yaw))
    wait(lambda: abs(b.camera()[0] - bx) < .4 and abs(b.camera()[1] - by) < .4, what + ': deployer next to the wearer', 30); time.sleep(2)
    b.send('tvdeploy 1')
    wait(lambda: len(b.tv()['audience']) == 1 and not b.tv()['probing'], what + ': deployed', 60); time.sleep(1)
    s = b.tv()['audience'][0]; return s


def terrain(mutant=False):
    a, b = start(extra='\nno_terrain_probe=1' if mutant else ''); b.send('hover off')
    spots = [int(s) for s in os.environ.get('TV_SEATS_SPOTS', '2,5,8,11,14,17,20,23,26,29').split(',')]
    state = os.path.join(RUN, 'tourA.txt'); report = []
    for n in spots:
        out = pyw('wtour.py', pid('A'), win(DIR1), win(state), 'go', str(n), timeout=120).strip().splitlines()
        print(out[-1] if out else 'no tour output', flush=True)
        if not out or 'refused' in out[-1] or 'cannot be entered' in out[-1]: continue
        time.sleep(3); x, y, z, yaw, _ = a.camera()
        if b.tv()['deploy_target']: b.send('tvretract'); time.sleep(1.5)
        try: s = deploy_near(a, b, 'spot %d' % n)
        except AssertionError: continue
        g = dict(yaw=yaw, center=s['center'], ground=s['ground'], stools=s['stools'], bottom=s['bottom'])
        row = dict(spot=n, measured=s['measured'], wearer_ground=round(z - EYE, 2), screen_bottom=round(s['bottom'], 2), stools=[])
        for i in range(3):
            t = stool(g, i)
            b.send('tp %f %f %f %f 0' % (t[0], t[1], t[2] + 6, yaw)); time.sleep(1.5)     # the check drops from 6 m, the mod's own probe from 2.2 m
            last = None
            for _ in range(30):
                c = b.camera(); z2 = c[2]
                if last is not None and abs(z2 - last) < .01: break
                last = z2; time.sleep(.5)
            if math.hypot(c[0] - t[0], c[1] - t[1]) > 1:      # the game put the probe back at its spawn point: no ground known here
                print('spot %d stool %d: probe was returned to %.0f %.0f, not measured' % (n, i, c[0], c[1]), flush=True); continue
            row['stools'].append(dict(stool=i, model_ground=round(t[2], 2), real_ground=round(z2 - EYE, 2), error=round(t[2] - (z2 - EYE), 2)))
        report.append(row); print(json.dumps(row), flush=True)
        b.send('tvretract'); time.sleep(1)
    json.dump(report, open(os.path.join(RUN, 'tv_terrain_mutant.json' if mutant else 'tv_terrain.json'), 'w'), indent=2)
    errors = [abs(s['error']) for r in report for s in r['stools']]
    assert errors, 'no spot could be measured'
    uneven = [(r['spot'], s['stool'], s['error']) for r in report for s in r['stools'] if abs(s['error']) > .35]
    print('terrain: %d stools measured, worst error %.2f m' % (len(errors), max(errors)), flush=True)
    if mutant:
        assert any(r['spot'] == 17 for r in report), 'spot 17 could not be measured: the mutant check proves nothing'
        assert any(sp == 17 for sp, _, _ in uneven), 'the flat-ground mutant did not fail at spot 17: the check cannot fail'
        print('PASS (mutant) flat-ground placement floats or sinks stools at spot 17: %s' % uneven, flush=True); return
    assert any(r['spot'] == 17 for r in report), 'spot 17 (the steep one) could not be measured'
    assert not uneven, 'stools hover or sink on uneven ground (spot, stool, error m): %s' % uneven
    sunk = [(r['spot'], r['screen_bottom'], r['wearer_ground']) for r in report if r['screen_bottom'] < r['wearer_ground'] + 2.0 - .01]
    assert not sunk, 'screen bottom edge below the wearer ground + 2 m: %s' % sunk
    print('PASS stools stand on the real ground at every measured spot', flush=True)


def shoot(game, name):
    bmp = os.path.join(game.dir, 'shots', name + '.bmp')
    if os.path.exists(bmp): os.remove(bmp)
    game.send('shot ' + name); deadline = time.monotonic() + 10
    while time.monotonic() < deadline and not (os.path.exists(bmp) and os.path.getsize(bmp) > 1000): time.sleep(.2)
    time.sleep(.5); subprocess.run(['python3', os.path.join(TOOLS, 'bmp2png.py'), bmp, os.path.join(RUN, name + '.png'), '1'], check=True)


def look():
    """In-game captures to look at (RUN folder): the screen and stools on flat ground and on the uneven spot, from the audience side."""
    a, b = start(); b.send('hover off')
    for tag, command in (('flat', 'menusel c335\nmenudo teleport'), ('slope', None)):
        if command is None:
            pyw('wtour.py', pid('A'), win(DIR1), win(os.path.join(RUN, 'tourA.txt')), 'go', '17', timeout=120); time.sleep(3)
            x, y, z, yaw, _ = a.camera(); b.send('tvretract'); time.sleep(1.5)
            s = deploy_near(a, b, 'slope'); b.send('hover off')
            g = dict(yaw=yaw, center=s['center'], ground=s['ground'], stools=s['stools'], wearer=(x, y, z))
        else:
            g = deploy(a, b, command)
        for view, dist, aim in (('view', 7, 'screen'), ('stools', 7.5, 'stools'), ('side', 6, 'stools')):
            side = 4.0 if view == 'side' else 0
            f = (math.cos(g['yaw']), math.sin(g['yaw'])); r = (f[1], -f[0])
            x = g['center'][0] + f[0] * dist + r[0] * side; y = g['center'][1] + f[1] * dist + r[1] * side
            eye = g['stools'][1] + EYE if view != 'view' else g['ground'] + EYE
            tx, ty = g['center'][0] + f[0] * 4, g['center'][1] + f[1] * 4
            tz = g['stools'][1] if aim == 'stools' else g['center'][2]
            look = math.atan2((g['center'][1] if aim == 'screen' else ty) - y, (g['center'][0] if aim == 'screen' else tx) - x)
            pitch = math.atan2(tz - eye, math.hypot((g['center'][0] if aim == 'screen' else tx) - x, (g['center'][1] if aim == 'screen' else ty) - y))
            b.send('hover %f %f %f %f %f' % (x, y, eye, look, pitch))
            wait(lambda: abs(b.camera()[0] - x) < .3 and abs(b.camera()[1] - y) < .3, 'viewer for the %s shot' % view, 30); time.sleep(3)
            shoot(b, 'tvs_%s_%s' % (tag, view))      # the command takes at most 15 characters
            if view == 'view': shoot(a, 'tvs_%s_wear' % tag)
        print('captures', tag, flush=True); b.send('hover off')


def main():
    # 'terrain', 'terrain_mutant' and 'look' are not in the default set: name them
    which = sys.argv[1:] or ['race', 'race_mutant', 'wearer', 'deployer']; before = real_saves(); failures = []
    try:
        for name in which:
            print('=== tv seats:', name, flush=True)
            try:
                if name in ('race', 'race_mutant'): race(int(os.environ.get('TV_SEATS_TRIALS', '6')), name == 'race_mutant')
                elif name == 'wearer': seated_loss('B', 'A')
                elif name == 'deployer': seated_loss('A', 'B')
                elif name == 'terrain': terrain()
                elif name == 'terrain_mutant': terrain(True)
                elif name == 'look': look()
                else: raise SystemExit('unknown scenario ' + name)
            except AssertionError as e: print(e, flush=True); failures.append(name)
    finally:
        assert real_saves() == before, 'real saves changed'
    print('FAILED: %s' % failures if failures else 'ALL OK: tv seat scenarios'); return 1 if failures else 0


if __name__ == '__main__': sys.exit(main())
