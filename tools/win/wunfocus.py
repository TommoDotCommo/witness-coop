# T2 check of a game window that is not active, run with Windows Python: A's window gets the real focus (and the lab's input lock is held by the caller), B's window does not; B's frame rate
# (the game's global frame counter) is read for a few seconds, and A's too. The focus is given back afterwards.
# Also: B (not active) must not try to cage or move the real mouse (the game's own attempts are counted by the proxy: `cursorstat`), and once the focus is given back no window holds the mouse in a cage
# (the real clip rectangle is the whole screen).
# usage: wunfocus.py <pidA> <pidB> <seconds> <game dir B>      prints: FPS focusedA <x> unfocusedB <y>; CURSOR unfocused_clip <n> unfocused_setpos <n> caged <0|1>
import ctypes, ctypes.wintypes as wt, os, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
u = ctypes.WinDLL('user32'); k32 = ctypes.WinDLL('kernel32')
pa, pb, secs, dirb = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
import re
def hwnd_of(pid):
    f = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h) and u.GetWindowTextLengthW(h) > 0: f.append(h)
        return True
    u.EnumWindows(cb, 0); return f[0]
def focus(h):
    fg = u.GetForegroundWindow(); t1 = u.GetWindowThreadProcessId(fg, None); t2 = k32.GetCurrentThreadId()
    u.AttachThreadInput(t2, t1, True); u.SetForegroundWindow(h); u.BringWindowToTop(h); u.AttachThreadInput(t2, t1, False)
A, B = attach(pa), attach(pb); ha, hb = hwnd_of(pa), hwnd_of(pb); prev = u.GetForegroundWindow()
def send_b(line):
    p = os.path.join(dirb, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.01)
    open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.01)
def cursorstat():
    log = os.path.join(dirb, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('cursorstat:')); send_b('cursorstat'); t = time.time()
    while time.time() - t < 8:
        ls = [l for l in open(log, errors='ignore') if l.startswith('cursorstat:')]
        if len(ls) > n0: m = re.search(r'unfocused_clip (\d+) unfocused_setpos (\d+)', ls[-1]); return int(m[1]), int(m[2])
        time.sleep(0.05)
    return None
focus(ha); time.sleep(2.5)
if u.GetForegroundWindow() != ha: print('FOCUS failed'); sys.exit(2)
c0 = cursorstat(); a0, b0, t0 = A.u32(A.base + 0x63954C), B.u32(B.base + 0x63954C), time.time(); time.sleep(secs); a1, b1, dt = A.u32(A.base + 0x63954C), B.u32(B.base + 0x63954C), time.time() - t0; c1 = cursorstat()
focus(prev); time.sleep(1.5)
clip = wt.RECT(); u.GetClipCursor(ctypes.byref(clip)); vx, vy, vw, vh = u.GetSystemMetrics(76), u.GetSystemMetrics(77), u.GetSystemMetrics(78), u.GetSystemMetrics(79)
caged = int(not (clip.left <= vx and clip.top <= vy and clip.right >= vx + vw and clip.bottom >= vy + vh))
print('FPS focusedA %.1f unfocusedB %.1f' % ((a1 - a0) / dt, (b1 - b0) / dt))
print('CURSOR unfocused_clip %d unfocused_setpos %d caged %d' % ((c1[0] - c0[0]) if c0 and c1 else -1, (c1[1] - c0[1]) if c0 and c1 else -1, caged))
