# Pictures of the puzzle editor (launcher/editor.cpp) and of the Randomizers tab with "Your puzzles", asked of the windows themselves (PrintWindow), with
# no game: the launcher runs on a fresh folder in %TEMP% holding a sample set. Windows Python.
#   weditor_shot.py <launcher exe> <out dir>
import os, re, struct, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
exec(compile(open(os.path.join(HERE, 'wimg.py')).read(), 'wimg.py', 'exec'))
exe, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
gdir = tempfile.mkdtemp(prefix='witness-editor-shot-')
open(os.path.join(gdir, 'witness_coop.cfg'), 'w').write('name=Fox\nsave_dir=instance\n')
sets = os.path.join(gdir, 'puzzles', 'sets'); os.makedirs(sets)
open(os.path.join(sets, 'Sample.txt'), 'w', newline='\n').write('''witness-coop puzzles 1
set Sample
puzzle 4 4 none 0x00000 Squares and stars
at 0 8 start
at 8 0 end right
at 1 1 square black
at 3 3 square white
at 5 1 star orange
at 7 5 star orange
at 1 7 triangle orange 2
at 5 5 eliminator white
at 4 2 dot black
at 3 6 gap break
puzzle 4 4 none 0x00000 Shapes
at 0 8 start
at 8 0 end top
at 1 1 poly yellow 0x0013 rotated
at 5 5 poly yellow 0x0017
at 7 7 ylop blue 0x0001
puzzle 5 5 x 0x00022 Mirror
at 0 10 start
at 10 10 start
at 0 0 end left
at 10 0 end right
at 2 4 dot blue
at 8 6 dot yellow
at 5 5 square white
''')
p = subprocess.Popen([exe, '--dir', gdir, '--no-activate', '--size', '1280', '800', '--stat-file', os.path.join(gdir, 'stat.txt'), '--cmd-file', os.path.join(gdir, 'cmd.txt')])
import ctypes, ctypes.wintypes as wt
u = ctypes.WinDLL('user32', use_last_error=True); g32 = ctypes.WinDLL('gdi32', use_last_error=True); u.SetProcessDPIAware()
def cmd(*lines):
    f = os.path.join(gdir, 'cmd.txt'); t = time.time()
    while os.path.exists(f) and time.time() - t < 8: time.sleep(0.05)
    open(f + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(f + '.tmp', f)
    t = time.time()
    while os.path.exists(f) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(0.8)
def find(cls):
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        q = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(q)); c = ctypes.create_unicode_buffer(64); u.GetClassNameW(h, c, 64)
        if q.value == p.pid and c.value == cls and u.IsWindowVisible(h): found.append(h)
        return True
    u.EnumWindows(cb, 0); return found[0] if found else None
def picture(h, name):
    u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]; u.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]; u.GetWindowDC.restype = wt.HDC; u.GetWindowDC.argtypes = [wt.HWND]; u.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
    g32.CreateCompatibleDC.restype = ctypes.c_void_p; g32.CreateCompatibleBitmap.restype = ctypes.c_void_p; g32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; g32.GetDIBits.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
    g32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]; g32.CreateCompatibleDC.argtypes = [wt.HDC]; g32.DeleteObject.argtypes = [ctypes.c_void_p]; g32.DeleteDC.argtypes = [ctypes.c_void_p]
    u.RedrawWindow.argtypes = [wt.HWND, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]; u.RedrawWindow(h, None, None, 0x1 | 0x80 | 0x100); time.sleep(0.4)
    r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r)); w, hh = r.right - r.left, r.bottom - r.top
    dc = u.GetWindowDC(h); mdc = g32.CreateCompatibleDC(dc); bmp = g32.CreateCompatibleBitmap(dc, w, hh); g32.SelectObject(mdc, bmp); u.PrintWindow(h, mdc, 2)
    class BIH(ctypes.Structure): _fields_ = [('size', wt.DWORD), ('w', ctypes.c_long), ('h', ctypes.c_long), ('planes', wt.WORD), ('bits', wt.WORD), ('comp', wt.DWORD), ('img', wt.DWORD), ('x', ctypes.c_long), ('y', ctypes.c_long), ('u', wt.DWORD), ('i', wt.DWORD)]
    bi = BIH(40, w, -hh, 1, 32, 0, 0, 0, 0, 0, 0); buf = ctypes.create_string_buffer(w * hh * 4); g32.GetDIBits(mdc, bmp, 0, hh, buf, ctypes.byref(bi), 0)
    path = os.path.join(out, name + '.bmp'); f = open(path, 'wb'); f.write(struct.pack('<2sIHHI', b'BM', 54 + w * hh * 4, 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, -hh, 1, 32, 0, w * hh * 4, 0, 0, 0, 0))
    raw = bytearray(buf.raw)
    for i in range(3, len(raw), 4): raw[i] = 255
    f.write(bytes(raw)); f.close(); g32.DeleteObject(bmp); g32.DeleteDC(mdc); u.ReleaseDC(h, dc)
    bmp_to_png(path, os.path.join(out, name + '.png'), 1, None); os.remove(path); print('picture', name, flush=True)
try:
    time.sleep(2.5)
    cmd('tab 2', 'randomizer custom;set:0000000000000000'); time.sleep(0.5)
    h = find('WitnessLauncher'); picture(h, 'randomizers_your_puzzles') if h else print('no launcher window')
    cmd('editor Sample'); time.sleep(1.0)
    e = find('WitnessPuzzleEditor'); picture(e, 'editor') if e else print('no editor window')
    cmd('editortest select 1', 'editortest solve'); time.sleep(0.5); picture(e, 'editor_shapes_solution')
    cmd('editortest select 2', 'editortest count'); time.sleep(0.5); picture(e, 'editor_symmetry_count')
    st = open(os.path.join(gdir, 'stat.txt'), errors='ignore').read() if os.path.exists(os.path.join(gdir, 'stat.txt')) else ''
    print('\n'.join(l for l in st.splitlines() if l.startswith('randomizers') or l.startswith('editor')))
finally:
    p.kill()
