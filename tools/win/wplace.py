import ctypes, ctypes.wintypes as wt, sys
u = ctypes.WinDLL('user32'); pid, x, y, w, h = map(int, sys.argv[1:6]); found = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(hw, lp):
    p = wt.DWORD(); u.GetWindowThreadProcessId(hw, ctypes.byref(p))
    if p.value == pid and u.IsWindowVisible(hw):
        r = wt.RECT(); u.GetClientRect(hw, ctypes.byref(r))
        if r.right > 200: found.append(hw)
    return True
u.EnumWindows(cb, 0)
print('placed', bool(found and u.SetWindowPos(found[0], 0, x, y, w, h, 0x0004)))
