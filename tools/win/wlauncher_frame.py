# Game-free check of the launcher's own window frame and two lobby details, on its real window (Windows Python; tools/qa_launcher_frame.py runs it):
# the Windows title bar is gone (the client starts at the window's top) and the banner answers as the caption, with minimize, maximize and close on its
# top right and the top edge sizing; the race goal shows only for a race; "You left the lobby." clears itself.
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
finally:
    try: cmd('quit'); p.wait(5)
    except Exception: pass
    if p.poll() is None: p.terminate()
    shutil.rmtree(gdir, ignore_errors=True)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
