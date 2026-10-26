# T2 check of a game window that is not active, run with Windows Python: A's window gets the real focus (and the lab's input lock is held by the caller), B's window does not; B's frame rate
# (the game's global frame counter) is read for a few seconds, and A's too. The focus is given back afterwards.
# usage: wunfocus.py <pidA> <pidB> <seconds>      prints: FPS focusedA <x> unfocusedB <y>
import ctypes, ctypes.wintypes as wt, os, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
u = ctypes.WinDLL('user32'); k32 = ctypes.WinDLL('kernel32')
pa, pb, secs = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
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
focus(ha); time.sleep(2.5)
if u.GetForegroundWindow() != ha: print('FOCUS failed'); sys.exit(2)
a0, b0, t0 = A.u32(A.base + 0x63954C), B.u32(B.base + 0x63954C), time.time(); time.sleep(secs); a1, b1, dt = A.u32(A.base + 0x63954C), B.u32(B.base + 0x63954C), time.time() - t0
focus(prev)
print('FPS focusedA %.1f unfocusedB %.1f' % ((a1 - a0) / dt, (b1 - b0) / dt))
