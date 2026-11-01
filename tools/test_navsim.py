#!/usr/bin/env python3
"""Game-free test of the walker (tools/win/wnav.py): the real planner and the real walk loop against the simulated levels of tools/navsim.py,
on a simulated clock (the whole file takes about half a minute), with mutants.
usage: python3 tools/test_navsim.py [--levels-only] [level ...]      (--levels-only: no mutants; exit 0 when every check passes)

Per level the expected outcome is in navsim.py (arrives within N simulated seconds, or gives up with one of the reasons within N): the closed
room must give up, not wander; a view that does not turn must be said so within seconds. Also checked: the WalkResult has all its fields and
only a known `why`; every walk that did not arrive wrote a giveup line to the typed trace and every line of the trace is well formed; the
random clutter worlds (fixed seeds) give a structured result each time and never arrive where no way exists.
Mutants (a one-line change of wnav.py, run in a copy through NAVSIM_WIN) must each fail at least one level:
 a walker that ignores bumps, a planner without the wall rule, a watchdog that never fires, a progress clock that never resets,
 no check that the view turns, a stuck walk that never says why."""
import math, os, subprocess, sys, tempfile, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import navsim
import wnav, wtrace

CLUTTER_SEEDS = (1, 2, 3, 4, 5, 6, 7, 8)


def judge(lv, r):
    """(ok, message): does the walk's result match the level's expectation?"""
    e = lv.expect
    if e[0] == 'arrives':
        ok = r.ok and r.why == 'arrived' and r.seconds <= e[1]
        return ok, 'arrives within %ds: %s in %.0f s (%.1f m left)' % (e[1], r.why, r.seconds, r.distance_left)
    ok = (not r.ok) and r.why in e[1] and r.seconds <= e[2]
    return ok, 'gives up (%s) within %ds: %s in %.0f s (%.1f m left, against %s)' % ('/'.join(sorted(e[1])), e[2], r.why, r.seconds, r.distance_left, r.stuck_class or '-')


def structure(r, w):
    """Problems in the shape of a WalkResult and its trace."""
    bad = []
    for k in ('ok', 'why', 'detail', 'distance_left', 'last_way', 'stuck_class', 'tried', 'seconds', 'walked', 'stuck', 'replans'):
        if k not in r: bad.append('no %s' % k)
    if r.get('why') not in wnav.WalkResult.WHYS: bad.append('why %r' % (r.get('why'),))
    if bool(r.get('ok')) != (r.get('why') == 'arrived'): bad.append('ok and why disagree')
    kinds = [l['kind'] for l in w.tr.lines]
    if kinds[:1] != ['walk_start']: bad.append('trace does not start with walk_start')
    if (kinds.count('giveup') == 1) != (not r.ok) or kinds.count('giveup') > 1: bad.append('giveup lines: %d for ok=%s' % (kinds.count('giveup'), r.ok))
    if not r.ok and w.tr.lines[-1]['kind'] != 'giveup': bad.append('giveup is not the last line')
    if not r.ok and w.tr.lines[-1].get('why') != r.why: bad.append('giveup why differs')
    try:
        for l in w.tr.lines: wtrace.check(l)
    except ValueError as e: bad.append(str(e))
    return bad


def run_levels(names, quiet=False, fail_fast=False):
    fails = []
    def check(name, ok, why=''):
        if not quiet: print('%-14s %s %s' % (name, 'ok' if ok else 'FAILED', why), flush=True)
        if not ok: fails.append(name)
        return ok
    for f in navsim.LEVELS:
        lv = f()
        if names and lv.name not in names: continue
        r, sim, w = navsim.walk(lv)
        ok, msg = judge(lv, r); bad = structure(r, w)
        check(lv.name, ok and not bad, msg + (' | ' + '; '.join(bad) if bad else ''))
        if fail_fast and fails: return fails
    if names: return fails
    # the closed room: the stall watchdog fires and the trace says so
    r, sim, w = navsim.walk(navsim.closed_room()); stalls = [l for l in w.tr.lines if l['kind'] == 'stall']
    check('stall trace', bool(stalls) and any('wall rule' in s['tried'] for s in stalls) and any('replan' in s['tried'] for s in stalls), 'stall lines: %s' % [s['tried'] for s in stalls])
    # a walk is the same every time (the simulated clock and a seeded world)
    a, _, _ = navsim.walk(navsim.zigzag()); b, _, _ = navsim.walk(navsim.zigzag())
    check('repeat', (a.why, a.seconds, a.walked) == (b.why, b.seconds, b.walked), '%s %s' % ((a.why, a.seconds), (b.why, b.seconds)))
    # random clutter: a structured result each time; arrives only where a way exists
    arrived = 0; reach = 0
    for seed in CLUTTER_SEEDS:
        lv = navsim.clutter(seed); r, sim, w = navsim.walk(lv); bad = structure(r, w)
        reach += lv.reachable; arrived += bool(r.ok)
        check('clutter%d' % seed, not bad and (not r.ok or lv.reachable) and r.seconds <= lv.seconds + 1, '%s in %.0f s, reachable=%s %s' % (r.why, r.seconds, lv.reachable, '; '.join(bad)))
        if fail_fast and fails: return fails
    if not quiet: print('clutter: arrived in %d of %d worlds (%d have a way)' % (arrived, len(CLUTTER_SEEDS), reach))
    # the proxy stops taking commands in the middle of a walk: a structured result, not an exception out of the caller's run
    sim = navsim.Sim(navsim.open_field()); orig = sim.send; n = [0]
    def dying(lines, settle=0.0):
        n[0] += 1
        if n[0] > 200: raise TimeoutError('proxy did not take command file')
        orig(lines, settle)
    sim.send = dying
    with tempfile.TemporaryDirectory() as d:
        try: r = navsim.SimWalker(sim, d).go((30, 6, wnav.EYE), 1.0, 60, ztol=1.8); got = '%s: %s' % (r.why, r.detail)
        except Exception as e: r, got = None, 'raised %r' % (e,)
    check('proxy dies', r is not None and not r.ok and r.why == 'timed out' and 'proxy' in r.detail, got)
    # the simulator itself
    lv = navsim.Level('door', [navsim.Block(5, -5, 6, 5, 'door', name='d')], (0, 0), (10, 0), ('arrives', 60))
    s = navsim.Sim(lv); s.send(['key W down'], 4.0); shut = s.x
    s.open_door('d'); s.send([], 4.0)
    check('sim door', shut < 5 and s.x > 7, 'the body stopped at %.1f, went through at %.1f once it was open' % (shut, s.x))
    s = navsim.Sim(navsim.open_field()); s.send(['key W down'], 1.0)
    check('sim speed', abs(s.walked - navsim.SPEED * 1.011) < 0.1, 'walked %.2f m in 1 s' % s.walked)
    s = navsim.Sim(navsim.wall_gap()); pts = s.depth(); nearest = min(abs(x - 12) for x, y, z in pts if 1.0 < z < 2.5 and abs(y) < 5) if any(1.0 < z < 2.5 for _, _, z in pts) else 99
    check('sim depth', len(pts) > 500 and any(abs(z) < 0.01 for _, _, z in pts), '%d points, ground at z 0, wall points at the wall' % len(pts))
    return fails


# one-line changes of wnav.py, each of which must make a level fail
MUTANTS = [
    ("                if self.map.bump(*centre(c_)): n += 1\n                self.avoid.add(c_)\n", "                pass\n"),      # a walker that ignores bumps
    ("    if mode == '1': return True\n", "    return False\n"),      # a planner without the wall rule
    ("    return any(math.dist(s[:2], q[:2]) < r for s in stuck_at)\n", "    return False\n"),      # a planner without the wall rule (the default of the second stuck near here)
    ("            if now - prog_t > window or (bumps_near >= 4 and now - near_t > 3 * stall):", "            if False:"),      # a watchdog that never fires
    ("            prog_t, window = self.now(), stall\n", "            pass\n"),      # a progress clock that never resets
    ("                elif self.now() - turn_ref[1] > 2.0: return done(", "                elif False: return done("),      # no check that the view turns
    ("        except TimeoutError as e:\n            q = self.pos(); left", "        except KeyError as e:\n            q = self.pos(); left"),      # a proxy that dies raises out of the walk
    ("            if not ok: self.tr.emit('giveup', why=why", "            if False: self.tr.emit('giveup', why=why"),      # a stuck walk that never says why
    ("                if wall_rule(self.stuck_at[:-1], q, os.environ.get('WNAV_PRESSED')) and self.look(0):", "                if False:"),      # (the wall rule never applied at a stuck)
]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if '--levels-only' in sys.argv:
        fails = run_levels(set(args), fail_fast='--fail-fast' in sys.argv); print('FAILED: %s' % ', '.join(fails) if fails else 'levels passed'); return 1 if fails else 0
    fails = run_levels(set(args)); bad = bool(fails); print('clean:', 'FAILED (%s)' % ', '.join(fails) if bad else 'passed', flush=True)
    src = os.path.join(HERE, 'win'); text = open(os.path.join(src, 'wnav.py')).read()
    for old, new in MUTANTS:
        if old not in text: print('mutant text not found: %s' % old[:70]); bad = True; continue
        with tempfile.TemporaryDirectory(prefix='navsim-mut-') as d:
            shutil.copy(os.path.join(src, 'wtrace.py'), d); open(os.path.join(d, 'wnav.py'), 'w').write(text.replace(old, new, 1))
            r = subprocess.run([sys.executable, os.path.join(HERE, 'test_navsim.py'), '--levels-only', '--fail-fast'], capture_output=True, text=True, timeout=900, env={**os.environ, 'NAVSIM_WIN': d})
            last = (r.stdout.strip().splitlines() or ['?'])[-1]; killed = r.returncode != 0
            print('mutant %-70s %s (%s)' % (old.strip()[:70], 'rejected' if killed else 'SURVIVED', last[:120]), flush=True); bad |= not killed
    print('ALL OK' if not bad else 'FAILED'); return 1 if bad else 0


if __name__ == '__main__': sys.exit(main())
