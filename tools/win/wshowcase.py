# The showcase scenes, run with Windows Python by tools/showcase.py once N real games are up in one group. Every picture is the game's own back-buffer capture (`shot`),
# converted to PNG; showcase.json lists the pictures with their captions (tools/showcase.md is written from the same words).
# usage: wshowcase.py <out dir> <scenes: all or a comma list> <spot panel (hex)> <drawing plan 'panel:node,node,...' or -> <pid|game dir> ...      (game 1 is the camera of most scenes)
import os, sys, time, math, json, struct
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

MESSAGES = ['Over here!', 'Found the second door', 'The tree panel is a trick', 'Going left', 'Nice view from up here', 'Check the shed', 'On my way', 'Anyone have the garden maze?']
def ground_point(V, dist, side):
    """A point on the ground `dist` metres from the camera player, `side` radians to the left of its line of sight"""
    (cx, cy, cz), (th, ph) = V.cam(); return (cx + dist * math.cos(th + side), cy + dist * math.sin(th + side), cz - 1.69)

def social_scene(vi):
    """Chat, typing dots and pings: the others speak, one is typing, pings are placed in front of and behind the camera player."""
    V = games[vi]; lineup(vi, None, None); others = [g for k, g in enumerate(games) if k != vi]
    for k, g in enumerate(others): g.send(['chat ' + MESSAGES[k % len(MESSAGES)]])
    if len(others) > 2: others[1].send(['typing on'])
    for k, (dist, side) in enumerate(((14, 0.45), (22, -0.4), (18, 3.2))): x, y, z = ground_point(V, dist, side); others[(k + 2) % len(others)].send(['pingat %.3f %.3f %.3f' % (x, y, z)])
    x, y, z = ground_point(V, 10, 0.0); V.send(['pingat %.3f %.3f %.3f' % (x, y, z)])
    time.sleep(1.2); snap(V, 'social', 'Chat lines over the players who spoke (and in the log at the bottom left), three dots over the player who is typing, and pings: a pole with the name and the distance, an arrow at the edge of the screen for the one behind the camera.')
    if len(others) > 2: others[1].send(['typing off'])
    V.send(['chat Looking good from here'], 0.4)

def emote_scene(vi):
    """Emotes: the others play different ones at the same moment; the wheel; the character list."""
    V = games[vi]; lineup(vi, None, None); others = [g for k, g in enumerate(games) if k != vi]
    for k, g in enumerate(others): g.send(['emote %d' % (k % 8 + 1)])
    time.sleep(0.9); snap(V, 'emotes', 'Emotes at the same moment: a hop, a bow, a spin, a nod, a wave, a shake, a shrug, a dance, each with its name over the head. A model plays its own clip when it has one, otherwise a procedural motion.')
    time.sleep(2.5); V.send(['wheel open'], 0.4); V.send(['mouse 40 -40'], 0.5); snap(V, 'emote_wheel', 'The emote wheel (hold G): eight slots, the one the mouse points to lit. The camera does not turn while it is open.'); V.send(['wheel close'], 0.4)
    V.key('C', 0.12, 0.8); snap(V, 'character_list', 'The character list (C): the model the others see you as. Up / Down, Enter.'); V.key('ESC', 0.12, 0.6)

def draw_polyline(g, pts, inks=0):
    g.send(['canvas on', 'reticle %.4f %.4f' % pts[0]], 0.2)
    for _ in range(inks): g.key('B', 0.1, 0.1)
    g.send(['key X down'], 0.25)
    for p in pts[1:]: g.send(['reticle %.4f %.4f' % p], 0.16)
    g.send(['key X up', 'canvas off'], 0.3)

def draw_scene(vi):
    """Free drawing: three players draw on the ground and the scene in front of them with the pen (X held, Z for the canvas); everybody sees the ink."""
    V = games[vi]; lineup(vi, None, None); others = [g for k, g in enumerate(games) if k != vi]
    circle = [(0.5 + 0.2 * math.cos(a * 0.4), 0.74 + 0.1 * math.sin(a * 0.4)) for a in range(17)]
    wave = [(0.12 + 0.76 * t / 24.0, 0.86 + 0.05 * math.sin(t * 0.9)) for t in range(25)]
    star = [(0.5 + r * math.cos(a), 0.62 + r * 1.7 * math.sin(a)) for a, r in ((k * 4 * math.pi / 5 + math.pi / 2, 0.07) for k in range(6))]
    draw_polyline(V, circle, 2); draw_polyline(V, wave, 5)
    if others: draw_polyline(others[0], [(x * 0.8 + 0.1, y - 0.15) for x, y in star], 1)
    time.sleep(0.8); snap(V, 'drawing', 'Free drawing: ink goes onto the surface under the pen in the player\'s own colour (B changes it), and everyone sees it as it is drawn. X draws under the cross-hair, Z turns the mouse into a pen.')
    V.send(['key SHIFT down'], 0.12); V.key('X', 0.12, 0.2); V.send(['key SHIFT up'], 0.5)      # Shift+X: clear mine
    for g in others[:1]: g.send(['drawclear'])

def voice_scene(vi):
    """Voice: the even players talk (a test voice instead of a microphone); their speaking marks show beside their heads, and the microphone line at the bottom left."""
    V = games[vi]; lineup(vi, None, None); time.sleep(1.5)
    snap(V, 'voice', 'Voice chat: moving bars beside the head of every player who is talking, the microphone line at the bottom left (hold N to talk). Near players are heard in the world, far ones over the radio.')

def map_scene(vi):
    """The island picture: the player flies over the island and stitches the game's own frames, then the dev menu map shows it."""
    V = games[vi]; V.send(['mapgen start'], 1.0); t0 = time.time(); log = os.path.join(V.dir, 'witness_proxy.log')
    while time.time() - t0 < 1500 and not any(l.startswith('mapgen: done') for l in open(log, errors='ignore')): time.sleep(3)
    time.sleep(5.0); V.send(['menu map'], 2.5); snap(V, 'map_picture', 'The dev menu map (F8) with the picture of the island under it: made by flying the player over the island looking down and stitching the game\'s own frames; the panels, doors and players are drawn over it.')
    V.send(['menu toggle'], 1.0)

def launcher_scene(vi):
    """The launcher window: the pages and the new-game window, captured from the window itself."""
    import subprocess, ctypes, ctypes.wintypes as wt
    exe = os.path.join(games[vi].dir, 'witness_launcher.exe')
    if not os.path.exists(exe): print('  no launcher exe in the game folder: skipping the launcher scene'); return
    u = ctypes.WinDLL('user32', use_last_error=True); g32 = ctypes.WinDLL('gdi32', use_last_error=True)
    u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]; u.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]; u.GetWindowDC.restype = wt.HDC; u.GetWindowDC.argtypes = [wt.HWND]; u.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
    g32.CreateCompatibleDC.restype = ctypes.c_void_p; g32.CreateCompatibleBitmap.restype = ctypes.c_void_p; g32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; g32.GetDIBits.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
    g32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]; g32.CreateCompatibleDC.argtypes = [wt.HDC]; g32.DeleteObject.argtypes = [ctypes.c_void_p]; g32.DeleteDC.argtypes = [ctypes.c_void_p]
    WinEnum = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def find(pid, cls):
        found = []
        @WinEnum
        def cb(h, l):
            p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); c = ctypes.create_unicode_buffer(40); u.GetClassNameW(h, c, 40)
            if p.value == pid and c.value == cls and u.IsWindowVisible(h): found.append(h)
            return True
        u.EnumWindows(cb, 0); return found[0] if found else None
    def grab_window(h, name, caption):
        r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r)); w, hh = r.right - r.left, r.bottom - r.top
        dc = u.GetWindowDC(h); mdc = g32.CreateCompatibleDC(dc); bmp = g32.CreateCompatibleBitmap(dc, w, hh); g32.SelectObject(mdc, bmp); u.PrintWindow(h, mdc, 2)
        class BIH(ctypes.Structure): _fields_ = [('size', wt.DWORD), ('w', ctypes.c_long), ('h', ctypes.c_long), ('planes', wt.WORD), ('bits', wt.WORD), ('comp', wt.DWORD), ('img', wt.DWORD), ('x', ctypes.c_long), ('y', ctypes.c_long), ('u', wt.DWORD), ('i', wt.DWORD)]
        bi = BIH(40, w, -hh, 1, 32, 0, 0, 0, 0, 0, 0); buf = ctypes.create_string_buffer(w * hh * 4); g32.GetDIBits(mdc, bmp, 0, hh, buf, ctypes.byref(bi), 0)
        path = os.path.join(out, name + '.bmp'); f = open(path, 'wb'); f.write(struct.pack('<2sIHHI', b'BM', 54 + w * hh * 4, 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, -hh, 1, 32, 0, w * hh * 4, 0, 0, 0, 0))
        raw = bytearray(buf.raw)
        for i in range(3, len(raw), 4): raw[i] = 255
        f.write(bytes(raw)); f.close(); g32.DeleteObject(bmp); g32.DeleteDC(mdc); u.ReleaseDC(h, dc)
        bmp_to_png(path, os.path.join(out, name + '.png'), 1, None); os.remove(path); manifest.append(dict(file=name + '.png', caption=caption, player=0)); print('  picture', name, flush=True)
    si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
    pages = [(0, 'launcher_play', 'The launcher (witness_launcher.exe): Play. Continue starts the game on the latest save with the settings of every page; New game asks what is already completed.'),
             (1, 'launcher_video', 'Video: the game\'s own Launcher.variables, edited outside the game and written in its format (render profile, full screen, vsync, resolution or a custom size, anti-aliasing, the quality settings).'),
             (2, 'launcher_sound', 'Sound and voice: the game\'s volume (set on its Windows audio session), voice mode, push-to-talk key, voice and microphone volume, proximity and radio ranges.'),
             (4, 'launcher_options', 'Game options: the options that live inside each save (mouse speed, invert, brightness, field of view, ...), patched in place with a copy of the save kept.'),
             (6, 'launcher_saves', 'Saves: each save with the players who were in the game and how far it got (panels, lasers, time played); the details on the right of the list show the progress by area.')]
    for page, name, caption in pages:
        p = subprocess.Popen([exe, '--dir', games[vi].dir, '--page', str(page), '--no-activate'], startupinfo=si); h = None; t0 = time.time()
        while time.time() - t0 < 10 and not h: h = find(p.pid, 'WitnessLauncher'); time.sleep(0.2)
        time.sleep(0.8)
        if h:
            if page == 6:      # select the newest save so the details are shown
                pass
            grab_window(h, name, caption)
        p.kill()
    p = subprocess.Popen([exe, '--dir', games[vi].dir, '--page', '0', '--new-game-window', '--no-activate'], startupinfo=si); h = None; t0 = time.time()
    while time.time() - t0 < 10 and not h: h = find(p.pid, 'WitnessNewGame'); time.sleep(0.2)
    time.sleep(0.6)
    if h:      # tick a few areas and ask for three lasers, then say what that comes to
        kids = []
        @WinEnum
        def cb(c, l): kids.append((u.GetDlgCtrlID(c), c)); return True
        u.EnumChildWindows(h, cb, 0); ids = dict(kids)
        for k in (4, 7, 10): 
            if 400 + k in ids: u.SendMessageW(ids[400 + k], 0xF1, 1, 0); u.SendMessageW(h, 0x111, 400 + k, ids[400 + k])
        time.sleep(0.5); grab_window(h, 'launcher_newgame', 'New game: tick the parts of the map that are done (or light some lasers, or take a ready-made start); the window says how many panels that is and what else the game needs first. Whatever the game needs before a part is solved with it, in order.')
    p.kill()

SCENES = [('lineup', lambda: (lineup(0, 'lineup_a', 'Eight players in one group, each drawn with a different character model (seen by player 1: players 2 to %d).' % N),
                              lineup(1, 'lineup_b', 'The same group seen by player 2: this time player 1 is in the picture.') if N > 2 else None)),
          ('walk', lambda: walkers(0, 'walk', 'The others walking toward the camera: the walk clip of each model, at the speed the other game measured.')),
          ('pov', lambda: pov_scene(0)), ('devmenu', lambda: devmenu_scene(0)), ('pause', lambda: pause_pair(0)), ('social', lambda: social_scene(0)), ('emote', lambda: emote_scene(0)), ('draw', lambda: draw_scene(0)), ('voice', lambda: voice_scene(0)), ('panel', lambda: panel_scene(0)), ('map', lambda: map_scene(0)), ('launcher', lambda: launcher_scene(0))]
t0 = time.time(); print('showcase: %d games, scenes %s, spot panel 0x%s' % (N, ','.join(want), SPOT), flush=True)
teleport_all(SPOT)
for name, fn in SCENES:
    if 'all' in want or name in want: print('scene', name, flush=True); fn()
json.dump(manifest, open(os.path.join(out, 'showcase.json'), 'w'), indent=1); print('done in %.0f s, %d pictures' % (time.time() - t0, len(manifest)))
