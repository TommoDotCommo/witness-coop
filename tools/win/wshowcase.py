# The showcase scenes, run with Windows Python by tools/showcase.py once N real games are up in one group. Every picture is the game's own back-buffer capture (`shot`),
# converted to PNG; showcase.json lists the pictures with their captions (tools/showcase.md is written from the same words).
# usage: wshowcase.py <out dir> <scenes: all or a comma list> <spot panel (hex)> <drawing plan 'panel:node,node,...' or -> <pid|game dir> ...      (game 1 is the camera of most scenes)
import os, sys, time, math, json
HERE = os.path.dirname(os.path.abspath(__file__))
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
exec(compile(open(os.path.join(HERE, 'wimg.py')).read(), 'wimg.py', 'exec'))
out, want, SPOT, PLAN = sys.argv[1], sys.argv[2].split(','), sys.argv[3], ('' if sys.argv[4] == '-' else sys.argv[4])
os.makedirs(out, exist_ok=True)
def wrap(a): return (a + math.pi) % (2 * math.pi) - math.pi

class Game:
    def __init__(self, idx, spec):
        pid, d = spec.split('|', 1); self.idx, self.pid, self.dir = idx, int(pid), d; self.proc = attach(self.pid)
    def send(self, lines, settle=0.0):
        cmd = os.path.join(self.dir, 'witness_cmd.txt'); t = time.time()
        while os.path.exists(cmd) and time.time() - t < 10: time.sleep(0.01)
        tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
        t = time.time()
        while os.path.exists(cmd) and time.time() - t < 10: time.sleep(0.01)
        time.sleep(settle)
    def cam(self): return self.proc.camera()
    def key(self, k, hold=0.12, settle=0.4): self.send(['key %s down' % k], hold); self.send(['key %s up' % k], settle)
    def grab(self, name):        # `shot <name>`: wait for the file, convert it to PNG in the output folder
        path = os.path.join(self.dir, 'shots', name + '.bmp')
        try: os.remove(path)
        except OSError: pass
        self.send(['shot ' + name]); t = time.time(); last = -1
        while time.time() - t < 8:
            try: sz = os.path.getsize(path)
            except OSError: sz = -1
            if sz > 0 and sz == last: break
            last = sz; time.sleep(0.15)
        else: raise TimeoutError('no shot ' + name)
        return path
games = [Game(i, a) for i, a in enumerate(sys.argv[5:])]
N = len(games); manifest = []
def snap(g, name, caption, hline=None):
    assert len(name) <= 15, 'the proxy keeps 15 characters of a shot name: ' + name
    bmp_to_png(g.grab(name), os.path.join(out, name + '.png'), 1, hline); manifest.append(dict(file=name + '.png', caption=caption, player=g.idx + 1)); print('  picture', name, flush=True)
def turn_to(g, target, tol=0.02):
    """Turn a game's player with real mouse input until it looks along `target` (heading, radians); +dx turns right (the heading falls)."""
    for _ in range(30):
        th = g.cam()[1][0]; err = wrap(target - th)
        if abs(err) < tol: return True
        counts = max(1, int(abs(err) / 0.0025)); lines = []; sign = -1 if err > 0 else 1
        while counts > 0 and len(lines) < 14: c = min(14, counts); lines.append('mouse %d 0' % (sign * c)); counts -= c
        g.send(lines, len(lines) * 0.045 + 0.35)
    return False
H0, P0 = 0.0, (0.0, 0.0, 0.0)      # the heading and the eye position every game has after the teleport (they all stand at the same spot, facing the same panel)
def teleport_all(spot):
    global H0, P0
    for g in games: g.send(['menusel %s' % spot, 'menudo teleport'])
    time.sleep(5.0); pose = games[-1].cam(); H0, P0 = pose[1][0], pose[0]

LAWN = 6.0        # metres from the teleport spot onto the open lawn where the photo is taken (in the direction away from the pillar)
def stage(vi):
    """Game `vi` becomes the camera: everyone is turned back to the common heading, then it turns away from it (so it faces the others) and steps out onto the lawn."""
    V = games[vi]; others = [g for k, g in enumerate(games) if k != vi]; h_o = H0
    for g in games: turn_to(g, h_o, 0.04)
    th = wrap(h_o + math.pi); turn_to(V, th); (vx, vy, vz) = P0; fx, fy = math.cos(th), math.sin(th); ground = vz - 1.69      # always from the teleport spot, wherever the game stands now
    V.send(['tp %.3f %.3f %.3f %.4f %.4f' % (vx + fx * LAWN, vy + fy * LAWN, ground, th, 0.0)], 1.0)
    return (vx + fx * LAWN, vy + fy * LAWN, ground), th, h_o, others

def lineup(vi, tag, caption):
    """Game `vi` looks at all the others, who stand on an arc around it facing its way (every player at its own angle, alternating between two distances for some depth),
    each drawn with its own character model. Two straight rows hide players behind each other whatever the spacing; an arc does not."""
    V = games[vi]; (bx, by, ground), th, h_o, others = stage(vi); n = len(others)
    step = 0.2 if n > 4 else 0.26
    for k, g in enumerate(others):
        a = (k - (n - 1) / 2.0) * step; d = 6.2 + 1.0 * (k % 2)
        x, y = bx + d * math.cos(th + a), by + d * math.sin(th + a)      # a > 0: to the left of the camera's line of sight
        g.send(['tp %.3f %.3f %.3f %.4f %.4f' % (x, y, ground, h_o, 0.0)])
    time.sleep(4.0)
    if tag: snap(V, tag, caption)

def walkers(vi, tag, caption):
    """The others walk toward the camera for a moment: every model plays its own walk (or the clip it has), the nearest ones close up."""
    V = games[vi]; (bx, by, ground), th, h_o, others = stage(vi); fx, fy, lx, ly = math.cos(th), math.sin(th), -math.sin(th), math.cos(th)
    for k, g in enumerate(others):      # a row 9 m away, facing the camera, then they all walk toward it
        off = (k - (len(others) - 1) / 2.0) * 1.75; g.send(['tp %.3f %.3f %.3f %.4f %.4f' % (bx + fx * 10 + lx * off, by + fy * 10 + ly * off, ground, h_o, 0.0)])
    time.sleep(3.0)
    for g in others: g.send(['key W down'])
    time.sleep(1.3); snap(V, tag, caption)
    for g in others: g.send(['key W up'])
    time.sleep(1.0)

def pause_pair(vi):
    V = games[vi]; snap(V, 'pause_0_playing', 'Playing: the other players are drawn.')
    V.key('ESC', 0.15, 3.0); snap(V, 'pause_1_menu', 'The pause menu is open: no player, name tag or line is drawn over it.')
    V.key('ESC', 0.15, 2.0)


def pov_scene(vi):
    """The camera player watches another player's own screen (V on the player you look at, ] and [ to cycle, F5 for the list)."""
    V = games[vi]; target = games[1 if vi == 0 else 0]
    lineup(vi, None, None)      # everybody back in rows on the lawn, so the watched player's own screen shows the garden and not the inside of somebody's model
    V.key('F5', 0.12, 1.5); snap(V, 'pov_roster', 'F5 opens the player list: pick whose screen to watch.'); V.key('F5', 0.12, 1.0)
    V.send(['pov %d' % (target.idx + 1), 'partner large'], 6.0); snap(V, 'pov_watch', "Watching player %d's own screen as a backup view (pictures stream only to whoever asks for them)." % (target.idx + 1))
    V.send(['pov off', 'partner off'], 1.0)

def devmenu_scene(vi):
    """The dev menu (F8): the map with every player, the nearby puzzles with their links, the save points; and the F3 panel mouse boost message."""
    V = games[vi]
    V.send(['menu map'], 2.5); snap(V, 'devmenu_map', 'Dev menu, map page: every panel (green solved, amber powered, grey off), the doors, and all the players.')
    V.send(['menu nearby'], 2.0); snap(V, 'devmenu_nearby', 'Dev menu, nearby page: the puzzles around you with their links and actions.')
    V.send(['menu saves'], 2.0); snap(V, 'devmenu_saves', 'Dev menu, saves page: save points and presets, restored through the game\'s own pause menu.')
    V.send(['menu toggle'], 1.0)
    V.key('F3', 0.12, 0.6); snap(V, 'boost_toast', 'F3 toggles the panel mouse boost (the circle follows the real mouse on a puzzle).'); V.key('F3', 0.12, 0.5)

def panel_scene(vi):
    """Shared panels and lines: one player draws the maze solution with real mouse input while another stands in front of it and watches the line appear."""
    V = games[vi]; drawer = games[1 if vi == 0 else 0]
    plan = PLAN
    V.send(['menusel 293', 'menudo teleport'], 4.0); snap(V, 'panel_0_before', 'The cable-driving maze, unsolved, seen by player %d.' % (vi + 1))
    if not plan: print('  no drawing plan (no panel manifest): skipping the drawing'); return
    pid_, nodes = plan.split(':', 1); import subprocess
    pr = subprocess.Popen([sys.executable, '-I', os.path.join(HERE, 'wdraw.py'), str(drawer.pid), drawer.dir, pid_, nodes], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    t = time.time(); k = 0
    while pr.poll() is None and time.time() - t < 150:
        time.sleep(1.0)
        if time.time() - t > 12 + 9 * k and k < 2: snap(V, 'panel_draw_%d' % (k + 1), 'Player %d draws the line with the mouse: player %d sees it appear on the panel, live.' % (drawer.idx + 1, vi + 1)); k += 1
    res = (pr.stdout.read() or '').strip().splitlines()[-1:] ; print('  drawing result:', res, flush=True)
    time.sleep(4.0); snap(V, 'panel_2_solved', "Solved by the other player: the panel is solved in this game too, and the line stays.")

SCENES = [('lineup', lambda: (lineup(0, 'lineup_a', 'Eight players in one group, each drawn with a different character model (seen by player 1: players 2 to %d).' % N),
                              lineup(1, 'lineup_b', 'The same group seen by player 2: this time player 1 is in the picture.') if N > 2 else None)),
          ('walk', lambda: walkers(0, 'walk', 'The others walking toward the camera: the walk clip of each model, at the speed the other game measured.')),
          ('pov', lambda: pov_scene(0)), ('devmenu', lambda: devmenu_scene(0)), ('pause', lambda: pause_pair(0)), ('panel', lambda: panel_scene(0))]
t0 = time.time(); print('showcase: %d games, scenes %s, spot panel 0x%s' % (N, ','.join(want), SPOT), flush=True)
teleport_all(SPOT)
for name, fn in SCENES:
    if 'all' in want or name in want: print('scene', name, flush=True); fn()
json.dump(manifest, open(os.path.join(out, 'showcase.json'), 'w'), indent=1); print('done in %.0f s, %d pictures' % (time.time() - t0, len(manifest)))
