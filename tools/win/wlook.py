# T2 check of how the launcher looks and behaves under the mouse, run with Windows Python: (1) hovering along one tab must not repaint the row of tabs again and again (flicker): 70 mouse moves along one
# tab may repaint it at most 3 times (the paints are counted by the launcher and written to its state file); (2) on every page no dropdown box may cover another control (a dropdown and a text
# of the same page must not overlap).     usage: wlook.py <launcher exe> <game dir> <stat file>
import ctypes, ctypes.wintypes as wt, os, re, subprocess, sys, time
exe, gdir, statf = sys.argv[1], sys.argv[2], sys.argv[3]
u = ctypes.WinDLL('user32', use_last_error=True); u.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]; u.SendMessageW.restype = ctypes.c_ssize_t
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
if os.path.exists(statf): os.remove(statf)
proc = subprocess.Popen([exe, '--dir', gdir, '--no-activate', '--stat-file', statf], startupinfo=si)
import atexit; atexit.register(lambda: proc.poll() is None and proc.terminate())      # (whatever happens below, the launcher is closed)
WinEnum = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cls(h): c = ctypes.create_unicode_buffer(60); u.GetClassNameW(h, c, 60); return c.value
main = None; t0 = time.time()
while time.time() - t0 < 15 and not main:
    found = []
    @WinEnum
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == proc.pid and cls(h) == 'WitnessLauncher': found.append(h)
        return True
    u.EnumWindows(cb, 0); main = found[0] if found else None; time.sleep(0.2)
time.sleep(1.0)
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('CHECK %s %s | %s' % ('ok' if cond else 'FAILED', name, detail), flush=True)
def paints():
    t = time.time()
    while time.time() - t < 5:
        try:
            m = re.search(r'tabpaints (\d+)', open(statf, errors='ignore').read())
            if m: return int(m[1])
        except OSError: pass
        time.sleep(0.1)
kids = []
@WinEnum
def collect(h, l): kids.append(h); return True
u.EnumChildWindows(main, collect, 0)
tab = [h for h in kids if cls(h) == 'SysTabControl32'][0]
time.sleep(0.8); p0 = paints()
r = wt.RECT(); u.GetWindowRect(tab, ctypes.byref(r)); oldpos = wt.POINT(); u.GetCursorPos(ctypes.byref(oldpos)); covered = False
u.SetCursorPos(r.left + 130, r.top + 17); time.sleep(0.4); pt = wt.POINT(r.left + 160, r.top + 17); covered = u.WindowFromPoint(pt) != tab
for x in range(130, 200): u.SetCursorPos(r.left + x, r.top + 17); time.sleep(0.015)      # the real pointer along the second tab (the resource provider's input lock is held by the caller)
u.SetCursorPos(oldpos.x, oldpos.y)
time.sleep(0.8); p1 = paints()
check('hovering along one tab repaints the row of tabs at most 3 times (70 mouse moves)', not covered and p0 is not None and p1 is not None and p1 - p0 <= 3, 'paints %s -> %s%s' % (p0, p1, ' (another window covers the tab)' if covered else ''))
# no dropdown covers another control of its page
by_parent = {}
for h in kids:
    pr = u.GetParent(h); by_parent.setdefault(pr, []).append(h)
bad = []
for pr, hs in by_parent.items():
    boxes = []
    for h in hs:
        r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r)); t = ctypes.create_unicode_buffer(80); u.GetWindowTextW(h, t, 80); boxes.append((cls(h), r, t.value))
    for c1, r1, t1 in boxes:
        if c1 != 'ComboBox': continue
        for c2, r2, t2 in boxes:
            if c2 in ('Static',) and t2 and not (r2.right <= r1.left or r2.left >= r1.right or r2.bottom <= r1.top or r2.top >= r1.bottom): bad.append((t2, r1.left, r1.right, r2.left, r2.right))
check('no dropdown box covers a text on any page', not bad, bad[:3])
# every label and hint fits its box (a text cut off at the edge, or run out of the window, is a defect): measured with the control's own font; a text that is allowed to wrap (tall boxes) is not looked at
g = ctypes.WinDLL('gdi32'); u.GetDC.restype = wt.HDC; u.GetDC.argtypes = [wt.HWND]; g.SelectObject.argtypes = [wt.HDC, ctypes.c_void_p]; g.SelectObject.restype = ctypes.c_void_p; u.DrawTextW.argtypes = [wt.HDC, wt.LPCWSTR, ctypes.c_int, ctypes.POINTER(wt.RECT), wt.UINT]
cut = []
for h in kids:
    if cls(h) != 'Static': continue
    t = ctypes.create_unicode_buffer(300); u.GetWindowTextW(h, t, 300)
    if not t.value: continue
    r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r)); w, hh = r.right - r.left, r.bottom - r.top
    if hh >= 36: continue
    font = u.SendMessageW(h, 0x31, 0, 0); dc = u.GetDC(h); old = g.SelectObject(dc, font); m = wt.RECT(0, 0, 0, 0); u.DrawTextW(dc, t.value, -1, ctypes.byref(m), 0x400 | 0x20)      # DT_CALCRECT | DT_SINGLELINE
    g.SelectObject(dc, old); u.ReleaseDC(h, dc)
    top = u.GetParent(h); pr = wt.RECT(); u.GetWindowRect(top, ctypes.byref(pr))
    if m.right > w + 1 or r.left + m.right > pr.right + 1: cut.append((t.value[:40], m.right, w))
check('no label or hint is cut off by its box or by the window edge', not cut, cut[:3])
proc.terminate()
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
