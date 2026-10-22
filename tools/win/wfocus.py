# usage: wfocus.py save                         prints the handle of the window that has the focus
#        wfocus.py guard <hwnd> <secs> <stopfile> <pid>..  for <secs> (or until <stopfile> exists), whenever one of the games has the
#                                              focus, give it back to <hwnd>
# The game takes the foreground by itself while it starts, which would interrupt whoever is typing in a terminal.
import ctypes, ctypes.wintypes as wt, sys, time
u = ctypes.WinDLL('user32'); k = ctypes.WinDLL('kernel32')
def owner(h):
    p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); return p.value
def give_back(h):
    fg = u.GetForegroundWindow(); t1 = u.GetWindowThreadProcessId(fg, None); t2 = k.GetCurrentThreadId()
    u.AttachThreadInput(t2, t1, True); u.SetForegroundWindow(h); u.AttachThreadInput(t2, t1, False)
if sys.argv[1] == 'save':
    print(u.GetForegroundWindow())
else:
    import os
    prev = int(sys.argv[2]); secs = float(sys.argv[3]); stop = sys.argv[4]; pids = {int(x) for x in sys.argv[5:]}; end = time.time() + secs; n = 0
    while time.time() < end and not os.path.exists(stop):
        h = u.GetForegroundWindow()
        if prev and h != prev and owner(h) in pids: give_back(prev); n += 1
        time.sleep(0.25)
    print('gave the focus back', n, 'times')
