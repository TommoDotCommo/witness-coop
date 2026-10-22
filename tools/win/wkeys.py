# Focus the game window of <pid> and hold a key (scancode) for <seconds>, or move the mouse. Test-copy use only.
# usage: wkeys.py <pid> key <W|A|S|D|ENTER|ESC|SPACE> <seconds> | wkeys.py <pid> mouse <dx> <dy>
import ctypes, ctypes.wintypes as wt, sys, time
u = ctypes.WinDLL('user32', use_last_error=True); k = ctypes.WinDLL('kernel32')
pid = int(sys.argv[1]); found = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(h, lp):
    p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
    if p.value == pid and u.IsWindowVisible(h):
        r = wt.RECT(); u.GetClientRect(h, ctypes.byref(r))
        if r.right > 200: found.append(h)
    return True
u.EnumWindows(cb, 0)
hwnd = found[0]
fg = u.GetForegroundWindow(); t1 = u.GetWindowThreadProcessId(fg, None); t2 = k.GetCurrentThreadId()
u.AttachThreadInput(t2, t1, True); u.ShowWindow(hwnd, 9); u.BringWindowToTop(hwnd); u.SetForegroundWindow(hwnd); u.AttachThreadInput(t2, t1, False)
time.sleep(0.5)
print('foreground is game:', u.GetForegroundWindow() == hwnd)
class KI(ctypes.Structure): _fields_ = [('wVk', wt.WORD), ('wScan', wt.WORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('dwExtraInfo', ctypes.c_size_t)]
class MI(ctypes.Structure): _fields_ = [('dx', wt.LONG), ('dy', wt.LONG), ('mouseData', wt.DWORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD), ('dwExtraInfo', ctypes.c_size_t)]
class IU(ctypes.Union): _fields_ = [('ki', KI), ('mi', MI)]
class IN(ctypes.Structure): _fields_ = [('type', wt.DWORD), ('u', IU)]
SC = {'W': 0x11, 'A': 0x1E, 'S': 0x1F, 'D': 0x20, 'ENTER': 0x1C, 'ESC': 0x01, 'SPACE': 0x39}
def send(i): u.SendInput(1, ctypes.byref(i), ctypes.sizeof(IN))
if sys.argv[2] == 'key':
    sc = SC[sys.argv[3].upper()]
    send(IN(1, IU(ki=KI(0, sc, 0x8, 0, 0)))); time.sleep(float(sys.argv[4])); send(IN(1, IU(ki=KI(0, sc, 0x8 | 0x2, 0, 0))))
elif sys.argv[2] == 'click':
    down, up = {'L': (0x2, 0x4), 'R': (0x8, 0x10)}[sys.argv[3].upper()]
    send(IN(0, IU(mi=MI(0, 0, 0, down, 0, 0)))); time.sleep(float(sys.argv[4]) if len(sys.argv) > 4 else 0.08); send(IN(0, IU(mi=MI(0, 0, 0, up, 0, 0))))
elif sys.argv[2] == 'seq':
    # seq "mouse dx dy" / "down L" / "up L" / "wait s" steps separated by ';' to run with no gaps in focus
    for step in ' '.join(sys.argv[3:]).split(';'):
        a = step.split()
        if a[0] == 'mouse':
            dx, dy, n = int(a[1]), int(a[2]), int(a[3]) if len(a) > 3 else 10
            for _ in range(n): send(IN(0, IU(mi=MI(dx // n, dy // n, 0, 0x1, 0, 0)))); time.sleep(0.02)
        elif a[0] in ('down', 'up'):
            d, uu = {'L': (0x2, 0x4), 'R': (0x8, 0x10)}[a[1].upper()]; send(IN(0, IU(mi=MI(0, 0, 0, d if a[0] == 'down' else uu, 0, 0))))
        elif a[0] == 'wait': time.sleep(float(a[1]))
else:
    dx, dy = int(sys.argv[3]), int(sys.argv[4])
    for _ in range(20): send(IN(0, IU(mi=MI(dx // 20, dy // 20, 0, 0x1, 0, 0)))); time.sleep(0.02)
print('sent')
