# A picture of ONE window, asked for by the window itself (PrintWindow), not copied from the screen: nothing in front of it or around it can end up in the picture (the desktop copy of wshot.py
# once caught a private chat window). Used for the launcher. usage: wwinshot.py <pid> <window class> <out.png> [page]     (page: select that tab first)
import ctypes, ctypes.wintypes as wt, struct, sys, time, zlib
u = ctypes.WinDLL('user32', use_last_error=True); g = ctypes.WinDLL('gdi32')
u.SetProcessDPIAware()
pid, cls, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
found = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(hwnd, lp):
    p = wt.DWORD(); u.GetWindowThreadProcessId(hwnd, ctypes.byref(p)); c = ctypes.create_unicode_buffer(60); u.GetClassNameW(hwnd, c, 60)
    if p.value == pid and c.value == cls: found.append(hwnd)
    return True
t0 = time.time()
while not found and time.time() - t0 < 15: u.EnumWindows(cb, 0); time.sleep(0.2)
if not found: print('no window'); sys.exit(1)
hwnd = found[0]; r = wt.RECT(); u.GetWindowRect(hwnd, ctypes.byref(r)); w, h = r.right - r.left, r.bottom - r.top
sdc = u.GetDC(0); mdc = g.CreateCompatibleDC(sdc); bmp = g.CreateCompatibleBitmap(sdc, w, h); g.SelectObject(mdc, bmp)
ok = u.PrintWindow(hwnd, mdc, 2)      # PW_RENDERFULLCONTENT
class BIH(ctypes.Structure):
    _fields_ = [('s', wt.DWORD), ('w', wt.LONG), ('h', wt.LONG), ('p', wt.WORD), ('b', wt.WORD), ('c', wt.DWORD), ('i', wt.DWORD), ('x', wt.LONG), ('y', wt.LONG), ('u', wt.DWORD), ('m', wt.DWORD)]
bi = BIH(ctypes.sizeof(BIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0); buf = ctypes.create_string_buffer(w * h * 4); g.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0); raw = buf.raw
rows = []
for y in range(h):
    row = bytearray(b'\x00'); base = y * w * 4
    for x in range(w): o = base + x * 4; row += bytes((raw[o + 2], raw[o + 1], raw[o]))
    rows.append(bytes(row))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
open(out, 'wb').write(png); print('wrote', out, w, h, 'printwindow', ok)
