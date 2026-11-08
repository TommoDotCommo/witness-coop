# T2 check, Windows Python: the page's main button keeps its look when the focus moves. Host by the mouse as a player does (Play, Host, Direct, a click on
# Open lobby, posted to the buttons: the real pointer is not used), then, without hovering anything, Start game must be drawn in the main buttons' yellow.
# The click gives Open lobby the focus and the dialog keys move the default-button style to it (owner, 2026-11-08: Start game white until hovered).
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
def pixel(top, ctl, dx, dy):      # the window as it is on screen (PrintWindow, full content), at a point of the control
    wr = wt.RECT(); u.GetWindowRect(top, ctypes.byref(wr)); w, hh = wr.right - wr.left, wr.bottom - wr.top
    dc = u.GetWindowDC(top); mdc = g32.CreateCompatibleDC(dc); bmp = g32.CreateCompatibleBitmap(dc, w, hh); g32.SelectObject(mdc, bmp); u.PrintWindow(top, mdc, 2)
    hdr = struct.pack('<IiiHHIIiiII', 40, w, -hh, 1, 32, 0, w * hh * 4, 0, 0, 0, 0); buf = ctypes.create_string_buffer(w * hh * 4); g32.GetDIBits(mdc, bmp, 0, hh, buf, hdr, 0)
    g32.DeleteObject(bmp); g32.DeleteDC(mdc); u.ReleaseDC(top, dc)
    r = wt.RECT(); u.GetWindowRect(ctl, ctypes.byref(r)); i = ((r.top - wr.top + dy) * w + r.left - wr.left + dx) * 4; return buf.raw[i + 2], buf.raw[i + 1], buf.raw[i]
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
    check('Start game is drawn yellow (the main button) without being hovered', c and c[0] > 200 and c[1] > 150 and c[2] < 130, c)
finally:
    p.kill()
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
