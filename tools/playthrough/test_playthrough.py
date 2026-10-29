#!/usr/bin/env python3
"""Game-free (T0) tests of the progression logic and the N-bot planner, with a mutant for every rule they rely on.
Each mutant is the module source with one line changed; it must fail at least one check. usage: python3 test_playthrough.py"""
import os, subprocess, sys, tempfile, shutil
HERE = os.path.dirname(os.path.abspath(__file__))

CHECKS = r'''
import sys; sys.path.insert(0, HERE)
from logic import Logic, CREDITS, REDIRECT
from planner import Planner, simulate, SimGame, finishes, route
L = Logic()
def check(ok, what):
    if not ok: print('FAIL', what); sys.exit(1)
check(len(L.panels) == 584 and len(L.lasers) == 11 and len(L.regions) == 157, 'the file parses into 584 panels, 11 lasers, 157 regions')
# the very start: one panel, then the next, as in the game
check(L.available(set()) == [0x00064], 'only the first hallway panel at the start')
check(L.available({0x00064}) == [0x00182], 'the second panel after the first')
# the tutorial gate: Outside Tutorial is behind 0x03629, which needs three panels
s = {0x00064, 0x00182, 0x00293, 0x00295, 0x002C2, 0x0A3B5}
check(0x03629 not in L.available(s), 'the gate panel stays off with one of its three panels missing')
check(0x03629 in L.available(s | {0x0A3B2}), 'the gate panel turns on with all three')
check('Outside Tutorial' not in L.derive(s | {0x0A3B2})[1] and 'Outside Tutorial' in L.derive(s | {0x0A3B2, 0x03629})[1], 'Outside Tutorial opens with the gate panel')
# one-way connections: the mountain floor 1 bridge only goes down, so the bridge region does not lead back to Mountaintop
done, reach = L.derive(set(L.panels))
one_way = [(src, dst) for src, conns in L.connections.items() for dst, rule in conns if src == 'Mountain Floor 1 Trash Pillar' and dst == 'Mountain Floor 1 Bridge']
check(not one_way, 'a TrueOneWay connection is not reversed')
# lasers: Box Short needs 7 lasers and the Desert redirect
route_ = route(L)
check(len(route_) < 300 and CREDITS in route_, 'the credits route is a pruned subset that contains the ending')
check(finishes(L, route_, {CREDITS}), 'the credits route reaches the ending')
check(not finishes(L, route_ - {0x00064}, {CREDITS}), 'without the very first panel the ending cannot be reached')
# a door: the Tutorial vault box sits behind the vault door, which the vault panel opens
outside = s | {0x0A3B2, 0x03629}
check(0x03481 not in L.available(outside) and 0x033D4 in L.available(outside), 'the vault box waits behind its door; the vault panel is available')
check(0x03481 in L.available(outside | {0x033D4}), 'solving the vault panel opens the door to the vault box')
everything = set(L.panels)
check(0x09F7F not in L.available(everything - {REDIRECT, 0x09F7F}), 'Box Short is refused without the Desert redirect')
check(0x09F7F in L.available(everything - {0x09F7F}), 'Box Short is available with eleven lasers and the redirect')
# N bots: the planner only ever hands out panels the game would accept (SimGame raises otherwise), and more bots finish sooner
ok1, t1, _, _ = simulate(L, 1)
ok4, t4, per4, _ = simulate(L, 4)
check(ok1 and ok4, 'one bot and four bots reach the credits')
check(t4 < 0.6 * t1, 'four bots finish in well under 60 percent of one bot\'s time (%.0f vs %.0f s)' % (t4, t1))
check(min(per4.values()) > 0, 'every bot gets work')
# a panel nobody can solve is reported as blocking, not skipped silently
p = Planner(L, can_solve=lambda h: h != 0x00182)
check(p.blocked({0x00064}) == [0x00182], 'an unsolvable panel on the route is reported as the blocker')
print('ALL OK')
'''

MUTANTS = {
    'logic.py': [
        ('back = [alt for alt in rule if \'TrueOneWay\' not in alt]', 'back = list(rule)'),                                 # one-way reversed
        ("and (not m.group(2) or REDIRECT in done)", ''),                                                                    # redirect ignored
        ("if h not in done and e.kind in ('Door', 'Laser') and self.holds(e.rule, done, reach)", "if h not in done and e.kind in ('Door', 'Laser')"),   # doors always open
        ("if h not in done and self.entities[h].kind in kinds and self.holds(self.entities[h].rule, done, reach)]", "if h not in done and self.entities[h].kind in kinds]"),   # panel rules ignored
    ],
    'planner.py': [
        ("return [h for h in self.logic.available(solved) if h in self.targets and h not in busy", "return [h for h in self.targets if h not in solved and h not in busy"),   # hands out unavailable panels
        ("if not new: return goal <= solved", "if not new: return True"),                                                    # route not verified
        ("return dict(zip(free_bots, cands))", "return dict(zip(free_bots[:1], cands))"),                                     # only one bot ever works
        ("return [h for h in self.logic.available(solved) if h in self.targets]", "return []"),                                # blockers never reported
    ],
}


def run(folder):
    r = subprocess.run([sys.executable, '-c', 'HERE=%r\n' % folder + CHECKS], capture_output=True, text=True, timeout=1800)
    return r.returncode == 0, (r.stdout + r.stderr).strip().splitlines()[-1:] or ['']


def main():
    ok, out = run(HERE); print('clean:', 'passed' if ok else 'FAILED %s' % out, flush=True)
    bad = not ok
    for name, edits in MUTANTS.items():
        for old, new in edits:
            with tempfile.TemporaryDirectory(prefix='playthrough-mut-') as d:
                shutil.copytree(os.path.join(HERE, 'data'), os.path.join(d, 'data'))
                for f in ('logic.py', 'planner.py'):
                    text = open(os.path.join(HERE, f)).read()
                    if f == name:
                        assert text.count(old) == 1, (f, old); text = text.replace(old, new)
                    open(os.path.join(d, f), 'w').write(text)
                caught, out = run(d)
                caught = not caught
                print('mutant %s: %s %s' % (name, 'rejected' if caught else 'SURVIVED', '' if caught else old), flush=True)
                bad |= not caught
    print('ALL OK' if not bad else 'FAILED'); return 1 if bad else 0


if __name__ == '__main__': sys.exit(main())
