# Screenshot of the game window using GDI + stdlib only. Usage: wshot.py <pid> <out.png>
import ctypes, ctypes.wintypes as wt, struct, sys, zlib
u = ctypes.WinDLL('user32', use_last_error=True); g = ctypes.WinDLL('gdi32')
u.SetProcessDPIAware()
pid = int(sys.argv[1]); out = sys.argv[2]
found = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(hwnd, lp):
    p = wt.DWORD(); u.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
    if p.value == pid and u.IsWindowVisible(hwnd):
        r = wt.RECT(); u.GetClientRect(hwnd, ctypes.byref(r))
        if r.right > 200 and r.bottom > 200: found.append(hwnd)
    return True
if pid == 0:
    w, h = u.GetSystemMetrics(0), u.GetSystemMetrics(1); pt = wt.POINT(0, 0); hwnd = 0
else:
    u.EnumWindows(cb, 0)
    if not found: print('no window'); sys.exit(1)
    hwnd = found[0]
    r = wt.RECT(); u.GetClientRect(hwnd, ctypes.byref(r)); w, h = r.right, r.bottom
    pt = wt.POINT(0, 0); u.ClientToScreen(hwnd, ctypes.byref(pt))
print('capture', w, h, 'at', pt.x, pt.y)
sdc = u.GetDC(0); mdc = g.CreateCompatibleDC(sdc); bmp = g.CreateCompatibleBitmap(sdc, w, h); g.SelectObject(mdc, bmp)
g.BitBlt(mdc, 0, 0, w, h, sdc, pt.x, pt.y, 0x00CC0020 | 0x40000000)
class BIH(ctypes.Structure):
    _fields_ = [('s', wt.DWORD), ('w', wt.LONG), ('h', wt.LONG), ('p', wt.WORD), ('b', wt.WORD), ('c', wt.DWORD), ('i', wt.DWORD), ('x', wt.LONG), ('y', wt.LONG), ('u', wt.DWORD), ('m', wt.DWORD)]
bi = BIH(ctypes.sizeof(BIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
buf = ctypes.create_string_buffer(w*h*4)
g.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
raw = buf.raw
# downscale 2x by sampling, BGRA -> RGB
step = 2 if w > 1400 else 1
rows = []
for y in range(0, h, step):
    row = bytearray(b'\x00')
    base = y*w*4
    for x in range(0, w, step):
        o = base + x*4
        row += bytes((raw[o+2], raw[o+1], raw[o]))
    rows.append(bytes(row))
W, H = (w + step - 1)//step, len(rows)
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
open(out, 'wb').write(png); print('wrote', out, W, H, len(png))
