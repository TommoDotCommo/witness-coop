"""Game-free checks of the walker's loop (tools/win/wnav.py Walker.go) in a simulated world, with a mutant for each protection.
Usage: python3 tools/test_wnav_walk.py      (exit 0 when all checks pass and every mutant is rejected)

The world: the player's eye position and view, keys W/A/S/D at 1.8 m/s, the mouse at 0.0025 rad a count, and walls the depth camera
does not see (the game's invisible walls) that stop the player. The camera sees the open ground everywhere. Time is simulated: the
walker's sleeps move the world on, so a minute of walking takes well under a second.
 - sidestep: an unseen post straight ahead: the walker gets stuck, steps aside and gets there;
 - pocket:   an unseen U of walls round the straight way, open behind: stuck at its end again and again, the walker backs out along its
             trail and goes round it;
 - teleport: a jump of the player (a teleport) starts a new trail: backing out never walks back to before the jump."""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = r'''
import math, os, sys, tempfile
sys.path.insert(0, D); import wnav
fails = []
def check(name, ok, why=''):
    print('%-9s %s %s' % (name, 'ok' if ok else 'FAILED', why)); ok or fails.append(name)

class World:
    def __init__(self, walls, start):
        self.t = 0.0; self.x, self.y = start; self.z = wnav.EYE; self.yaw, self.pitch = 0.0, 0.0; self.keys = set(); self.walls = walls
    def crosses(self, a, b):
        for (p, q) in self.walls:
            d = (b[0] - a[0]) * (q[1] - p[1]) - (b[1] - a[1]) * (q[0] - p[0])
            if abs(d) < 1e-12: continue
            u = ((p[0] - a[0]) * (q[1] - p[1]) - (p[1] - a[1]) * (q[0] - p[0])) / d; v = ((p[0] - a[0]) * (b[1] - a[1]) - (p[1] - a[1]) * (b[0] - a[0])) / d
            if -0.02 <= u <= 1.0 + 0.3 / max(1e-6, math.dist(a, b)) and 0 <= v <= 1: return True      # (0.3 m: the player's body)
        return False
    def advance(self, dt):
        while dt > 1e-9:
            h = min(dt, 0.02); dt -= h; self.t += h
            f = ('W' in self.keys) - ('S' in self.keys); s = ('A' in self.keys) - ('D' in self.keys)
            if not f and not s: continue
            vx = 1.8 * (f * math.cos(self.yaw) - s * math.sin(self.yaw)); vy = 1.8 * (f * math.sin(self.yaw) + s * math.cos(self.yaw))
            nx, ny = self.x + vx * h, self.y + vy * h
            if not self.crosses((self.x, self.y), (nx, ny)): self.x, self.y = nx, ny

class Clock:
    def __init__(self, w): self.w = w
    def monotonic(self): return self.w.t
    def time(self): return self.w.t
    def sleep(self, s): self.w.advance(max(s, 0.001))

class Proc:
    def __init__(self, w): self.w = w
    def camera(self): self.w.advance(0.002); return (self.w.x, self.w.y, self.w.z), (self.w.yaw, self.w.pitch)

class Control:
    frame = 1 / 60
    def __init__(self, w): self.w = w
    def send(self, lines, settle=0.0):
        for l in lines:
            a = l.split()
            if a[0] == 'key': (self.w.keys.add if a[2] == 'down' else self.w.keys.discard)(a[1])
            elif a[0] == 'mouse': self.w.yaw = wnav.wrap(self.w.yaw - int(a[1]) * 0.0025); self.w.pitch = max(-0.93, min(0.93, self.w.pitch - int(a[2]) * 0.0025))
            elif a[0] == 'jump': self.w.x, self.w.y = float(a[1]), float(a[2])
        self.w.advance(0.011 + settle)

class SimWalker(wnav.Walker):
    def look(self, again=1):      # the camera sees the open ground round the player (the walls are invisible)
        q = self.pos(); pts = [(q[0] + i * 0.25, q[1] + j * 0.25, 0.0) for i in range(-60, 61) for j in range(-60, 61) if i * i + j * j <= 3600]
        self.map.integrate(pts); self.looks += 1; self.map.stand(q[0], q[1], q[2] - wnav.EYE); return True
    def open_doors(self): return []

def walk(walls, start, target, seconds=240):
    w = World(walls, start); wnav.time = Clock(w); d = tempfile.mkdtemp()
    wk = SimWalker(Proc(w), Control(w), d); r = wk.go((target[0], target[1], wnav.EYE), 0.8, seconds); return r, w, wk

r, w, wk = walk([((6.0, -0.4), (6.0, 0.4))], (0.0, 0.0), (12.0, 0.0))
check('sidestep', r['ok'] and r['sidesteps'] >= 1, '%s in %.0f s, %d stucks, %d sidesteps' % (r['why'] or 'there', r['seconds'], r['stuck'], r['sidesteps']))
U = [((4.0, 3.0), (10.0, 3.0)), ((10.0, 3.0), (10.0, -3.0)), ((10.0, -3.0), (4.0, -3.0))]
r, w, wk = walk(U, (0.0, 0.0), (16.0, 0.0))
check('pocket', r['ok'] and r['backouts'] >= 1, '%s in %.0f s, %d stucks, %d backouts' % (r['why'] or 'there', r['seconds'], r['stuck'], r['backouts']))
# teleport: a trail from before a jump is not walked back to
w = World([], (0.0, 0.0)); wnav.time = Clock(w); wk = SimWalker(Proc(w), Control(w), tempfile.mkdtemp())
wk.go((10.0, 0.0, wnav.EYE), 0.8, 30); Control(w).send(['jump 50 50']); wk.go((52.0, 50.0, wnav.EYE), 0.8, 30)
far = [q for q in wk.trail if math.dist(q[:2], (50, 50)) > 5]
check('teleport', not far and len(wk.trail) >= 2, '%d trail points from before the jump' % len(far))
sys.exit(1 if fails else 0)
'''

MUTANTS = [      # (file text, replacement): each removes one protection
    ("            if math.dist(self.pos()[:2], q[:2]) > 0.3: self.sidesteps += 1; return True\n", ""),      # no sidestep
    ("if pocketed and self.back_out('stuck %d times here' % (again + 1)): way = None; continue", "pass"),      # no backing out of a pocket
    ("if self.trail and math.dist(q[:2], self.trail[-1][:2]) > 3.0: self.trail = []", "pass"),      # a teleport kept in the trail
    ("                        if v in dear: cost = max(cost, 40.0)\n", ""),      # the pocket round a back-out is not dear: the next try goes in beside it
]


def run_with(folder):
    r = subprocess.run([sys.executable, '-c', 'D=%r\n' % folder + CHECK], capture_output=True, text=True, timeout=300)
    return r.returncode == 0, r.stdout + r.stderr[-800:]


def main():
    src = os.path.join(HERE, 'win', 'wnav.py')
    ok, out = run_with(os.path.dirname(src)); print(out.strip()); bad = not ok; print('clean:', 'passed' if ok else 'FAILED')
    text = open(src).read()
    for old, new in MUTANTS:
        if old not in text: print('mutant text not found: %s' % old[:70]); bad = True; continue
        with tempfile.TemporaryDirectory(prefix='wnav-walk-mut-') as d:
            open(os.path.join(d, 'wnav.py'), 'w').write(text.replace(old, new, 1))
            ok, out_ = run_with(d)
            print('mutant %-70s %s (%s)' % (old.strip()[:70], 'rejected' if not ok else 'SURVIVED', ' | '.join(l for l in out_.splitlines() if 'FAILED' in l or 'Error' in l)[:200]), flush=True); bad |= ok
    print('ALL OK' if not bad else 'FAILED'); return 1 if bad else 0


if __name__ == '__main__': sys.exit(main())
