# T2 check, Windows Python: the page's main button keeps its look when the focus moves. Host by the mouse as a player does (Play, Host, Direct, a click on
# Open lobby, posted to the buttons: the real pointer is not used), then, without hovering anything, Start game must be drawn in the main buttons' yellow.
# The click gives Open lobby the focus and the dialog keys move the default-button style to it; a new label (WM_SETTEXT) and the keyboard cues (WM_UPDATEUISTATE)
# make Windows paint its own white button over ours (owner, 2026-11-08: Start game white until hovered). Read from the screen: the window is put on top for a moment.
# usage: wprimary.py <launcher exe> <folder>      (the folder gets a witness_coop.cfg for a Direct lobby on a free port)
import ctypes, ctypes.wintypes as wt, os, struct, subprocess, sys, time
exe, d = sys.argv[1], sys.argv[2]
os.makedirs(d, exist_ok=True); sp, cp = os.path.join(d, 'stat.txt'), os.path.join(d, 'cmd.txt')
open(os.path.join(d, 'witness64_d3d11.exe'), 'wb').close()
open(os.path.join(d, 'witness_coop.cfg'), 'w').write('id=1\nlisten=47961\nname=Probe\nforce_active=1\nlobby_port=47960\nupnp=0\nfind_tailscale=0\nvoice_mode=off\nvoice_mute_output=1\n')
open(os.path.join(d, 'witness_launcher_update.cfg'), 'w').write('update_check=0\n')
u = ctypes.WinDLL('user32'); g32 = ctypes.WinDLL('gdi32')
u.GetWindowDC.restype = wt.HDC; u.GetWindowDC.argtypes = [wt.HWND]; u.ReleaseDC.argtypes = [wt.HWND, wt.HDC]; u.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
u.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
for f, r, a in (('CreateCompatibleDC', ctypes.c_void_p, [wt.HDC]), ('CreateCompatibleBitmap', ctypes.c_void_p, [wt.HDC, ctypes.c_int, ctypes.c_int]), ('SelectObject', ctypes.c_void_p, [ctypes.c_void_p, ctypes.c_void_p]),
                ('GetDIBits', ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]), ('DeleteObject', wt.BOOL, [ctypes.c_void_p]), ('DeleteDC', wt.BOOL, [ctypes.c_void_p])):
    getattr(g32, f).restype = r; getattr(g32, f).argtypes = a
Enum = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def top_of(pid):
    out = []
    @Enum
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); c = ctypes.create_unicode_buffer(40); u.GetClassNameW(h, c, 40)
        if p.value == pid and c.value == 'WitnessLauncher' and u.IsWindowVisible(h): out.append(h)
        return True
    u.EnumWindows(cb, 0); return out[0] if out else None
def child(top, text):
    out = []
    @Enum
    def cb(h, l):
        b = ctypes.create_unicode_buffer(100); u.GetWindowTextW(h, b, 100)
        if b.value == text and u.IsWindowVisible(h): out.append(h)
        return True
    u.EnumChildWindows(top, cb, 0); return out[0] if out else None
def click(h): u.PostMessageW(h, 0x0201, 1, 0x000a000a); time.sleep(0.05); u.PostMessageW(h, 0x0202, 0, 0x000a000a)
u.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wt.UINT]; g32.GetPixel.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]; g32.GetPixel.restype = wt.DWORD
u.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, ctypes.c_void_p]
def pixel(top, ctl, dx, dy):      # the pixel on the screen itself, the window on top for that moment (PrintWindow asks the window to paint again and hides what Windows drew over it)
    u.SetWindowPos(top, wt.HWND(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010); time.sleep(0.4)
    r = wt.RECT(); u.GetWindowRect(ctl, ctypes.byref(r)); dc = u.GetDC(None); c = g32.GetPixel(dc, r.left + dx, r.top + dy); u.ReleaseDC(None, dc)
    u.SetWindowPos(top, wt.HWND(-2), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)
    return c & 255, (c >> 8) & 255, (c >> 16) & 255
def yellow(c): return c and c[0] > 200 and c[1] > 150 and c[2] < 130
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('CHECK %s %s | %s' % ('ok' if cond else 'FAILED', name, detail), flush=True)
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
p = subprocess.Popen([exe, '--dir', d, '--no-activate', '--stat-file', sp, '--cmd-file', cp], startupinfo=si)
try:
    t0 = time.time(); top = None
    while time.time() - t0 < 15 and not top: top = top_of(p.pid); time.sleep(0.2)
    time.sleep(1.5); open(cp + '.tmp', 'w').write('tab 0\nchoice host\n'); os.replace(cp + '.tmp', cp); time.sleep(1.5)
    direct = child(top, 'Direct (same network, VPN or a forwarded port)'); click(direct); time.sleep(0.8)
    ob = child(top, 'Open lobby'); click(ob); time.sleep(2.5)
    st = child(top, 'Start game'); lobby = open(sp, errors='ignore').read() if os.path.exists(sp) else ''
    check('hosting after clicking Direct and Open lobby', 'lobby active 1 host 1' in lobby, [l for l in lobby.splitlines() if l.startswith('lobby ')])
    c = st and pixel(top, st, 10, 6)
    check('Start game is drawn yellow (the main button) without being hovered', yellow(c), c)
    if st: u.SendMessageW(st, 0x000C, 0, ctypes.c_wchar_p('Start game')); time.sleep(0.3)      # WM_SETTEXT: the lobby relabels it (Waiting for ..., Start game)
    c = st and pixel(top, st, 10, 6)
    check('   still yellow after its label is set again (Windows paints its own white button for that)', yellow(c), c)
    if st: u.SendMessageW(st, 0x0128, (0x2 << 16) | 1, None); time.sleep(0.3)      # WM_UPDATEUISTATE: the keyboard cues, sent to every control when a key is used
    c = st and pixel(top, st, 10, 6)
    check('   still yellow after the keyboard cues change (a key press)', yellow(c), c)
finally:
    p.kill()
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
