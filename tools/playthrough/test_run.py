#!/usr/bin/env python3
"""Game-free (T0) test of the playthrough orchestrator's decisions (run.py), with fake bots on a simulated world.

The world (the vanilla logic) solves every panel a bot draws at once, with two traps taken from the game:
 - the tutorial gate's line gets stuck unless Back Left (0xa3b5) was drawn through its other exit (node 104), as its latch stays shut;
 - a panel the logic says is available is only powered (and so only started by a bot) once the world has derived it.
The run must reach the credits on its own: it has to notice the stuck gate, redraw Back Left the other way and come back. Mutants, each a one-line
change of run.py, must fail: no redraw of prerequisites, no power gating, redraws drawn as fresh solves.
usage: python3 test_run.py"""
import os, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))

CHECK = r'''
import os, sys, threading, time, types
sys.path.insert(0, TOOLS); sys.path.insert(0, D)      # D first: the (possibly mutated) playthrough modules, then the shared tools
import run
from logic import Logic, CREDITS
run.time.sleep = lambda s: None      # the loop's pauses are for real games
L = Logic()
GATE, BACK_LEFT, GOOD_END = 0x3629, 0xa3b5, 104

class World:
    def __init__(self): self.solved = set(); self.ends = {}; self.lock = threading.Lock(); self.drawn = []; self.reads = 0; self.solved_at = {}
    def powered(self):
        # Power takes a moment to arrive (a cable lights up): Bend (0x182) is powered only half a second after the first panel was solved
        # (the loop's pauses are off here, so a bot that does not wait for power gets to it well before that).
        out = set(L.available(self.solved)) | self.solved
        if 0x182 in out and 0x182 not in self.solved and time.time() - self.solved_at.get(0x64, time.time()) < 0.5: out.discard(0x182)
        return out
W = World()

class FakeBot:
    def __init__(self, i): self.id = i; self.pid = str(1000 + i); self.dir = None
    def state(self):
        with W.lock:
            W.reads += 1; done, _ = L.derive(W.solved)
            return set(W.solved), {h for h in done if L.entities[h].kind == 'Door'}, W.powered()
    def alive(self): return True

class P(run.Playthrough):
    def __init__(self):
        self.logic = L; self.n = 2; self.goal_name = 'credits'; self.deadline = time.time() + 120
        self.bots = [FakeBot(1), FakeBot(2)]; self.events = []
        self.rec = lambda kind, **d: self.events.append(dict(kind=kind, **d))
        self.manifest = {h: {'id': h, 'nodes': 1} for h in L.panels}; self.paths = {}; self.no_solver = set()
        self.planner = None; self.solved = set(); self.lock = threading.Lock(); self.prop = []
        self.powered = set(); self.variant = {}; self.stalled_since = {}; self.fixups = []; self.last_why = {}; self.restore = set()
        self.start = ''; self.resume = ''; self.avoid = set(); self.candidates_only = set(); self.attempts = {}; self.redrawn = set()
    def launch(self): pass
    def checkpoint(self): pass
    def load_manifest(self): pass
    def compare_worlds(self, settle=5): pass
    def summary(self): pass
    def path_for(self, h):
        if h not in self.paths:      # Back Left has two solutions: the default one ends at 103, the other at 104 (as in the game)
            v = [[0, 103], [0, GOOD_END]] if h == BACK_LEFT else [[0, 1]]
            self.variant[h] = (0, v); self.paths[h] = v[0]
        return self.paths[h]
    def solve(self, bot, h, redraw=False):
        path = self.paths[h]
        with W.lock:
            W.drawn.append((h, redraw, path[-1]))
            if h not in W.powered(): raise RuntimeError('%#x drawn while the game had it off' % h)      # the game: an unpowered panel cannot be started
            if h == GATE and W.ends.get(BACK_LEFT) != GOOD_END:
                self.last_why[h] = 'stuck before node 35 (path index 10)'; return False
            if h in W.solved and not redraw: return True      # already solved: nothing drawn
            W.solved.add(h); W.ends[h] = path[-1]; W.solved_at.setdefault(h, time.time()); return True

p = P(); p.play()
ok = CREDITS in W.solved
redrew = [d for d in W.drawn if d[0] == BACK_LEFT and d[1] and d[2] == GOOD_END]
print('credits reached' if ok else 'credits NOT reached', '| Back Left redrawn through 104:', bool(redrew), '| solves', len(W.solved))
sys.exit(0 if ok and redrew else 1)
'''

MUTANTS = [
    ("elif why.startswith('stuck') or 'did not accept' in why:", "elif False:"),                                    # a stuck line never redraws a prerequisite
    ("can_solve=lambda h: h in self.manifest and h in self.powered and self.path_for(h) is not None", "can_solve=lambda h: h in self.manifest and self.path_for(h) is not None"),   # no power gating
    ("busy[i] = (pool.submit(self.solve, self.bots[i - 1], h, True), h)", "busy[i] = (pool.submit(self.solve, self.bots[i - 1], h), h)"),    # a redraw sent as a plain solve
]


def run_with(folder):
    r = subprocess.run([sys.executable, '-c', 'D=%r\nTOOLS=%r\n' % (folder, os.path.dirname(HERE)) + CHECK], capture_output=True, text=True, timeout=600)
    return r.returncode == 0, ((r.stdout + r.stderr).strip().splitlines() or [''])[-1]


def main():
    ok, out = run_with(HERE); print('clean:', 'passed' if ok else 'FAILED', '-', out, flush=True); bad = not ok
    for old, new in MUTANTS:
        with tempfile.TemporaryDirectory(prefix='run-mut-') as d:
            for f in os.listdir(HERE):
                if f.endswith('.py'): shutil.copy(os.path.join(HERE, f), d)
            shutil.copytree(os.path.join(HERE, 'data'), os.path.join(d, 'data'))
            s = open(os.path.join(d, 'run.py')).read(); assert s.count(old) == 1, old; open(os.path.join(d, 'run.py'), 'w').write(s.replace(old, new))
            caught, out = run_with(d); caught = not caught
            print('mutant %s: %s (%s)' % (old[:60], 'rejected' if caught else 'SURVIVED', out), flush=True); bad |= not caught
    print('ALL OK' if not bad else 'FAILED'); return 1 if bad else 0


if __name__ == '__main__': sys.exit(main())
