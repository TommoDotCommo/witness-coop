# Game-free check of the launcher's own window frame and two lobby details, on its real window (Windows Python; tools/qa_launcher_frame.py runs it):
# the Windows title bar is gone (the client starts at the window's top) and the banner answers as the caption, with minimize, maximize and close on its
# top right and the top edge sizing; the race goal shows only for a race; "You left the lobby." clears itself. Also (owner, 2026-11-06): a tab opened with the mouse
# shows no focus line round its main button, the keyboard brings it back; the banner is a sunny sea; faint panels sit behind the pages; About's choice of full
# releases or pre-releases is saved.
# usage: wlauncher_frame.py <launcher exe>      (a fresh folder in %TEMP% stands in for the game folder; no game is started)
import ctypes, ctypes.wintypes as wt, os, re, shutil, subprocess, sys, tempfile, time
exe = sys.argv[1]
u = ctypes.WinDLL('user32', use_last_error=True); u.SetProcessDPIAware()
u.SendMessageW.restype = ctypes.c_ssize_t; u.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
gdir = tempfile.mkdtemp(prefix='witness-launcher-frame-')
cfgp, statf, cmdf = os.path.join(gdir, 'witness_coop.cfg'), os.path.join(gdir, 'stat.txt'), os.path.join(gdir, 'cmd.txt')
open(cfgp, 'w').write('name=Fox\nsave_dir=instance\nlisten=47001\n')
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('CHECK %s %s | %s' % ('ok' if cond else 'FAILED', name, str(detail)[:200]), flush=True)
def stat():
    for _ in range(50):
        try:
            t = open(statf, errors='ignore').read()
            if t: return t
        except OSError: pass
        time.sleep(0.1)
    return ''
def line(key): m = re.search(r'^%s ?(.*)$' % key, stat(), re.M); return m[1] if m else None
def cmd(*lines):
    t = time.time()
    while os.path.exists(cmdf) and time.time() - t < 8: time.sleep(0.05)
    open(cmdf + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmdf + '.tmp', cmdf)
    t = time.time()
    while os.path.exists(cmdf) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(0.5)
def windows(pid, cls):
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, lp):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); c = ctypes.create_unicode_buffer(64); u.GetClassNameW(h, c, 64)
        if p.value == pid and c.value == cls: found.append(h)
        return True
    u.EnumWindows(cb, 0); return found
def children(h):
    out = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(k, lp): out.append(k); return True
    u.EnumChildWindows(h, cb, 0); return out
def text(h): b = ctypes.create_unicode_buffer(200); u.GetWindowTextW(h, b, 200); return b.value
g32 = ctypes.WinDLL('gdi32')
def client_pixels(hw):      # the window's own picture of its client area (PrintWindow, nothing in front of it counts): (w, h, BGRA bytes)
    r = wt.RECT(); u.GetClientRect(hw, ctypes.byref(r)); w, h = r.right, r.bottom
    sdc = u.GetDC(0); mdc = g32.CreateCompatibleDC(sdc); bmp = g32.CreateCompatibleBitmap(sdc, w, h); g32.SelectObject(mdc, bmp); u.PrintWindow(hw, mdc, 3)      # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    class BIH(ctypes.Structure): _fields_ = [('s', wt.DWORD), ('w', wt.LONG), ('h', wt.LONG), ('p', wt.WORD), ('b', wt.WORD), ('c', wt.DWORD), ('i', wt.DWORD), ('x', wt.LONG), ('y', wt.LONG), ('u', wt.DWORD), ('m', wt.DWORD)]
    bi = BIH(ctypes.sizeof(BIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0); buf = ctypes.create_string_buffer(w * h * 4); g32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    g32.DeleteObject(bmp); g32.DeleteDC(mdc); u.ReleaseDC(0, sdc); return w, h, buf.raw
def region(px, x0, y0, x1, y1):
    w, h, raw = px; out = []
    for y in range(y0, y1):
        for x in range(x0, x1): o = (y * w + x) * 4; out.append((raw[o + 2], raw[o + 1], raw[o]))
    return out
class GTI(ctypes.Structure): _fields_ = [('cb', wt.DWORD), ('flags', wt.DWORD), ('active', wt.HWND), ('focus', wt.HWND), ('capture', wt.HWND), ('menuOwner', wt.HWND), ('moveSize', wt.HWND), ('caret', wt.HWND), ('rc', wt.RECT)]
def focused(hw):
    g = GTI(); g.cb = ctypes.sizeof(GTI); u.GetGUIThreadInfo(u.GetWindowThreadProcessId(hw, None), ctypes.byref(g)); return g.focus
def cues(): m = re.search(r'cues (\d) focustrace (\d) focusclass (\S*)', stat()); return (int(m[1]), int(m[2]), m[3]) if m else None
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4      # SW_SHOWNOACTIVATE
p = subprocess.Popen([exe, '--dir', gdir, '--no-activate', '--size', '1280', '800', '--stat-file', statf, '--cmd-file', cmdf], startupinfo=si)
try:
    time.sleep(2.5); hw = None
    for _ in range(30):
        w = windows(p.pid, 'WitnessLauncher')
        if w: hw = w[0]; break
        time.sleep(0.2)
    check('0. the launcher window is there', hw)
    wr = wt.RECT(); u.GetWindowRect(hw, ctypes.byref(wr)); cr = wt.RECT(); u.GetClientRect(hw, ctypes.byref(cr)); o = wt.POINT(0, 0); u.ClientToScreen(hw, ctypes.byref(o))
    check('1. no Windows title bar: the client starts at the top of the window, 1280 x 800', o.y == wr.top and cr.right == 1280 and cr.bottom == 800, (o.y, wr.top, cr.right, cr.bottom))
    def hit(x, y): return u.SendMessageW(hw, 0x84, 0, ((o.y + y) & 0xFFFF) << 16 | ((o.x + x) & 0xFFFF))      # WM_NCHITTEST at a client point
    got = {k: hit(*xy) for k, xy in {'banner': (600, 60), 'close': (1280 - 20, 12), 'max': (1280 - 69, 12), 'min': (1280 - 115, 12), 'top': (600, 2), 'below': (640, 600)}.items()}
    check('2. the banner is the caption; close, maximize and minimize on its top right; the top edge sizes; the page is not the caption',
          got['banner'] == 2 and got['close'] == 20 and got['max'] == 9 and got['min'] == 8 and got['top'] == 12 and got['below'] != 2, got)
    cmd('choice host', 'host 47393'); time.sleep(1.0)
    goal = [k for k in children(hw) if text(k) == 'panel:9F7F']
    vis = lambda: bool(goal) and bool(u.IsWindowVisible(goal[0]))
    coop = vis(); cmd('mode race'); race = vis(); cmd('mode coop'); back = vis()
    check('3. the race goal shows only for a race (co-op, race, co-op)', goal and not coop and race and not back, (len(goal), coop, race, back))
    cmd('leave'); t0 = time.time(); s0 = line('uistatus')      # (leaving closes the router mapping first, which can take a few seconds)
    while s0 != 'You left the lobby.' and time.time() - t0 < 10: time.sleep(0.2); s0 = line('uistatus')
    time.sleep(6.0); s1 = line('uistatus')
    check('4. "You left the lobby." is said, then clears itself within seconds', s0 == 'You left the lobby.' and s1 == '', (s0, s1))
    # 5. the focus line: a tab opened by the mouse (or a command) puts the focus on its main button without the line; Tab on the keyboard turns the line on,
    # a mouse click turns it off again
    cmd('tab 1'); a1 = cues(); cmd('tab 4'); a3 = cues()
    u.PostMessageW(focused(hw), 0x100, 0x09, 0x000F0001); time.sleep(0.4); cmd('tab 4'); kb = cues()      # WM_KEYDOWN Tab, then About again: its button has the focus with the cues on
    u.PostMessageW(focused(hw), 0x204, 0, 0); time.sleep(0.6); ms = cues()                                # WM_RBUTTONDOWN on the focused button (no action): the mouse in use
    check('5. a tab opened without the keyboard shows no focus line on its main button; Tab shows it; the mouse hides it again',
          a1 and a3 and a1[:2] == (0, 0) and a3 == (0, 0, 'Button') and kb == (1, 1, 'Button') and ms and ms[:2] == (0, 0), (a1, a3, kb, ms))
    # 6. the banner is a sunny sea: a bright blue sky right of the title, turquoise water below it (the stat file says "sea")
    px = client_pixels(hw); sky = region(px, 900, 6, 1100, 40); sea = region(px, 900, 86, 1100, 104)
    avg = lambda pts, k: sum(p[k] for p in pts) / len(pts)
    sr, sg, sb = (avg(sky, k) for k in range(3)); wr, wg, wb = (avg(sea, k) for k in range(3))
    check('6. the banner is a sunny sea: bright blue sky, turquoise water', line('art') == 'sea' and sb > 170 and sb > sr + 50 and (sr + sg + sb) / 3 > 120 and wg > wr + 50 and wb > wr + 50,
          (line('art'), [round(v) for v in (sr, sg, sb, wr, wg, wb)]))
    # 7. faint panels behind the pages: below the credits on About and above the status line, where only the background is, some pixels differ from the background colour and none by much
    bg = (13, 19, 22); band = region(px, 0, 706, 1280, 740); dev = [max(abs(p[k] - bg[k]) for k in range(3)) for p in band]
    marked, top = sum(1 for d in dev if d >= 3), max(dev)
    check('7. faint panels behind the pages: there, and at most 18 levels from the background', marked > 400 and top <= 18 and (line('backdrop') or '').startswith('panels'), (marked, top, line('backdrop')))
    # 8. About: full releases or pre-releases too, saved for the updater (update_prereleases=0|1 in the data folder's witness_launcher_update.cfg)
    radios = {text(k): k for k in children(hw) if text(k) in ('Full releases', 'Pre-releases too (newer, less tested)')}
    ucfg = os.path.join(gdir, 'witness_launcher_update.cfg'); rd = lambda: (re.search(r'^update_prereleases=(\S+)', open(ucfg).read(), re.M) or [None, None])[1] if os.path.exists(ucfg) else None
    BM_CLICK, BM_GETCHECK = 0xF5, 0xF0; got = []
    for name, want in (('Pre-releases too (newer, less tested)', '1'), ('Full releases', '0'), ('Pre-releases too (newer, less tested)', '1')):
        if name in radios: u.SendMessageW(radios[name], BM_CLICK, 0, 0); time.sleep(0.5); got.append((rd(), u.SendMessageW(radios[name], BM_GETCHECK, 0, 0)))
    check('8. About: full releases or pre-releases too, saved each time', len(radios) == 2 and got == [('1', 1), ('0', 1), ('1', 1)], (len(radios), got))
finally:
    try: cmd('quit'); p.wait(5)
    except Exception: pass
    if p.poll() is None: p.terminate()
    shutil.rmtree(gdir, ignore_errors=True)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
