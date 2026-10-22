# prints "free" if the system-wide cursor clip rectangle is the whole screen (nothing grabbed the mouse), else "grabbed <rect>"
import ctypes, ctypes.wintypes as wt
u = ctypes.WinDLL('user32'); c = wt.RECT(); u.GetClipCursor(ctypes.byref(c))
full = (c.right - c.left) >= u.GetSystemMetrics(78) and (c.bottom - c.top) >= u.GetSystemMetrics(79)
print('free' if full else 'grabbed (%d,%d,%d,%d)' % (c.left, c.top, c.right, c.bottom))
