# T2 check of the launcher window, run with Windows Python: starts witness_launcher.exe on a game folder, sets controls with window messages (what a click and a typed value do),
# presses Save settings, and prints what the files on disk hold; with `launch` also moves the launcher to a monitor other than the primary one, presses Continue, prints the pid of
# the game it started and checks that the game's window opens on the launcher's monitor (with one monitor that check is printed as SKIP).
# usage: wlauncher.py <launcher exe> <game dir> <dump file> <files|launch>      (prints lines: CHECK <ok|FAILED> <name> | <detail>, GAMEPID <n>)
import ctypes, ctypes.wintypes as wt, os, re, struct, subprocess, sys, time, json
exe, gdir, dump, mode = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
u = ctypes.WinDLL('user32', use_last_error=True)
u.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]; u.SendMessageW.restype = ctypes.c_ssize_t
u.GetDlgCtrlID.argtypes = [wt.HWND]; u.FindWindowW.argtypes = [wt.LPCWSTR, wt.LPCWSTR]; u.FindWindowW.restype = wt.HWND
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
u.EnumChildWindows.argtypes = [wt.HWND, EnumProc, wt.LPARAM]
WM_SETTEXT, WM_GETTEXT, WM_GETTEXTLENGTH, WM_COMMAND, BM_SETCHECK, BM_GETCHECK, CB_SETCURSEL, CB_GETCURSEL, CB_GETCOUNT = 0xC, 0xD, 0xE, 0x111, 0xF1, 0xF0, 0x14E, 0x147, 0x146
def children(h):
    out = {}
    @EnumProc
    def cb(c, l): out[u.GetDlgCtrlID(c)] = c; return True
    u.EnumChildWindows(h, cb, 0); return out
def text(h):
    n = u.SendMessageW(h, WM_GETTEXTLENGTH, 0, 0); b = ctypes.create_unicode_buffer(n + 2); u.SendMessageW(h, WM_GETTEXT, n + 1, ctypes.addressof(b)); return b.value
def settext(h, s):
    buf = ctypes.create_unicode_buffer(s); u.SendMessageW(h, WM_SETTEXT, 0, ctypes.addressof(buf))
def check(ok, name, detail=''): print('CHECK %s %s | %s' % ('ok' if ok else 'FAILED', name, detail), flush=True)

si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
if os.path.exists(dump): os.remove(dump)
proc = subprocess.Popen([exe, '--dir', gdir, '--dump-controls', dump, '--no-activate'], startupinfo=si)
main = None; t0 = time.time()
WinEnum = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def find_window(pid):      # the launcher's own window (not one left over from an earlier run)
    found = []
    @WinEnum
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); c = ctypes.create_unicode_buffer(40); u.GetClassNameW(h, c, 40)
        if p.value == pid and c.value == 'WitnessLauncher': found.append(h)
        return True
    u.EnumWindows(cb, 0); return found[0] if found else None
while time.time() - t0 < 15 and not (main and os.path.exists(dump)):
    main = find_window(proc.pid); time.sleep(0.2)
time.sleep(0.5)
ids = {}; keys = {}
for l in open(dump):
    i, k = l.split(None, 1); keys[k.strip()] = int(i)
kids = children(main)
def ctl(k): return kids[keys[k]]
def setv(k, v):
    h = ctl(k); cls = ctypes.create_unicode_buffer(40); u.GetClassNameW(h, cls, 40)
    if cls.value == 'Button': u.SendMessageW(h, BM_SETCHECK, 1 if v else 0, 0)
    elif cls.value == 'ComboBox': u.SendMessageW(h, CB_SETCURSEL, int(v), 0)
    else: settext(h, str(v))
def getv(k):
    h = ctl(k); cls = ctypes.create_unicode_buffer(40); u.GetClassNameW(h, cls, 40)
    if cls.value == 'Button': return u.SendMessageW(h, BM_GETCHECK, 0, 0)
    if cls.value == 'ComboBox': return u.SendMessageW(h, CB_GETCURSEL, 0, 0)
    return text(h)
def press(k): u.SendMessageW(main, WM_COMMAND, keys[k], ctl(k))      # BN_CLICKED = 0 in the high word

cfg_path = os.path.join(gdir, 'witness_coop.cfg'); saves = os.path.join(gdir, 'appdata', 'The Witness')
def newest_save():
    fs = sorted([f for f in os.listdir(saves) if f.endswith('.witness_campaign')], key=lambda f: os.path.getmtime(os.path.join(saves, f)))
    return os.path.join(saves, fs[-1])
def opt(b, name):
    p = b'\0' + name.encode() + b'\0'; i = b.find(p); return b[i + len(p):i + len(p) + 4]
cfg0 = open(cfg_path).read(); save0 = open(newest_save(), 'rb').read()

# the window shows what is on disk
check(getv('cfg:name') == dict(l.split('=', 1) for l in cfg0.splitlines() if '=' in l and not l.startswith('#')).get('name', ''), 'the window starts from the cfg on disk', repr(getv('cfg:name')))
check(float(getv('opt:mouse_speed_3d')) == struct.unpack('<f', opt(save0, 'mouse_speed_3d'))[0], "the game's own option is read from the newest save", getv('opt:mouse_speed_3d'))

# a wrong value is refused and nothing is written
vpath = os.path.join(saves, 'Launcher.variables'); vbefore = open(vpath).read() if os.path.exists(vpath) else None
setv('vid:resolution', 4); setv('vid:customW', 10); press('apply'); time.sleep(0.3)
check((open(vpath).read() if os.path.exists(vpath) else None) == vbefore, 'a custom width of 10 is refused and Launcher.variables is not touched', text(ctl('status')))
check(open(cfg_path).read() == cfg0, 'a refused save leaves the cfg as it was')

# the real edit
vals = {'vid:renderProfile': 2, 'vid:fullscreen': 0, 'vid:vsync': 1, 'vid:highdpi': 0, 'vid:resolution': 4, 'vid:customW': 1366, 'vid:customH': 768, 'vid:multisampling': 3, 'vid:shading': 0, 'vid:reflection': 1, 'vid:texture': 2, 'vid:textureDetail': 1, 'vid:geometry': 0,
        'cfg:cap_fps': 45, 'cfg:name': 'Launcher Test', 'cfg:key_chat': 'y', 'cfg:launcher_no_sound': 0,
        'opt:mouse_speed_3d': '12.5', 'opt:user_fov_vertical': '70', 'opt:invert_y_axis': 1}
for k, v in vals.items(): setv(k, v)
settext(ctl('cfgmulti:peer'), '7,127.0.0.1,47100\r\n8,10.0.0.2,47101')
wrong = {k: ascii(getv(k)) for k, v in vals.items() if str(getv(k)) != str(v)}
check(not wrong, 'the controls hold what was typed', wrong)
press('apply'); time.sleep(0.5)
status = text(ctl('status'))
want_video = ':/launcher\nrender_profile 2\nfullscreen false\nvsync true\nhighdpi false\nresolution 4\nmultisampling 3\ncustom_width 1366\ncustom_height 768\nshading_quality 0\nreflection_quality 1\ntexture_quality 2\ntexture_detail 1\ngeometry_detail 0\n'
got = open(vpath).read() if os.path.exists(vpath) else ''
check(got == want_video, 'Launcher.variables holds exactly what was chosen, in the game\'s own format', status if got != want_video else '')
cfg1 = open(cfg_path).read(); d1 = dict(l.split('=', 1) for l in cfg1.splitlines() if '=' in l and not l.startswith('#')); d0 = dict(l.split('=', 1) for l in cfg0.splitlines() if '=' in l and not l.startswith('#'))
check(d1.get('name') == 'Launcher Test' and d1.get('cap_fps') == '45' and d1.get('key_chat') == 'Y', 'the cfg has the new name, frame limit and key (the key upper-cased)', {k: d1.get(k) for k in ('name', 'cap_fps', 'key_chat')})
check(re.findall(r'^peer=(.*)$', cfg1, re.M) == ['7,127.0.0.1,47100', '8,10.0.0.2,47101'], 'the other players are the lines typed', re.findall(r'^peer=(.*)$', cfg1, re.M))
lost = [k for k in d0 if k not in d1 and k not in ('peer', 'cap_fps', 'launcher_no_sound')]
check(not lost, 'every other line of the cfg is still there (id, listen, save_dir, dev_menu, ...)', lost)
newsave = open(newest_save(), 'rb').read()
check(struct.unpack('<f', opt(newsave, 'mouse_speed_3d'))[0] == 12.5 and struct.unpack('<f', opt(newsave, 'user_fov_vertical'))[0] == 70.0 and struct.unpack('<i', opt(newsave, 'invert_y_axis'))[0] == 1, "the game's options are changed inside the save (floats as floats, yes/no as the integer 1)")
diff = [i for i in range(len(save0)) if save0[i] != newsave[i]]
check(len(newsave) == len(save0) and 0 < len(diff) <= 12, 'nothing else in the save changed (same size, only the edited values)', len(diff))
check(os.path.exists(newest_save() + '.before-launcher') and open(newest_save() + '.before-launcher', 'rb').read() == save0, 'a copy of the save as it was is kept')
class MI(ctypes.Structure): _fields_ = [('cb', wt.DWORD), ('mon', wt.RECT), ('work', wt.RECT), ('flags', wt.DWORD)]
u.MonitorFromWindow.restype = ctypes.c_void_p; u.MonitorFromWindow.argtypes = [wt.HWND, wt.DWORD]; u.GetMonitorInfoW.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
def monitor_of(h):
    mi = MI(); mi.cb = ctypes.sizeof(MI); u.GetMonitorInfoW(u.MonitorFromWindow(h, 2), ctypes.byref(mi)); r = mi.mon; return (r.left, r.top, r.right, r.bottom), mi.flags & 1
def monitors():
    out = []
    MonProc = ctypes.WINFUNCTYPE(wt.BOOL, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(wt.RECT), wt.LPARAM)
    @MonProc
    def cb(m, dc, r, l): mi = MI(); mi.cb = ctypes.sizeof(MI); u.GetMonitorInfoW(m, ctypes.byref(mi)); out.append(((mi.mon.left, mi.mon.top, mi.mon.right, mi.mon.bottom), (mi.work.left, mi.work.top), mi.flags & 1)); return True
    u.EnumDisplayMonitors(None, None, cb, 0); return out
def game_window(pid):
    found = []
    @WinEnum
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r))
        if p.value == pid and u.IsWindowVisible(h) and r.right - r.left > 200: found.append(h)
        return True
    u.EnumWindows(cb, 0); return found[0] if found else None
if mode == 'launch':
    other = [m for m in monitors() if not m[2]]
    if other: u.SetWindowPos(main, None, other[0][1][0] + 40, other[0][1][1] + 40, 0, 0, 0x0001 | 0x0004 | 0x0010)      # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    time.sleep(0.3); lmon = monitor_of(main)[0]
    press('launch'); t0 = time.time(); gp = None
    while time.time() - t0 < 30 and not gp:
        k32 = ctypes.WinDLL('kernel32', use_last_error=True)
        class PE(ctypes.Structure): _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('pid', wt.DWORD), ('heap', ctypes.c_void_p), ('mod', wt.DWORD), ('thr', wt.DWORD), ('ppid', wt.DWORD), ('pri', ctypes.c_long), ('flags', wt.DWORD), ('exe', ctypes.c_wchar * 260)]
        k32.CreateToolhelp32Snapshot.restype = ctypes.c_void_p; k32.Process32FirstW.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; k32.Process32NextW.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; k32.OpenProcess.restype = ctypes.c_void_p; k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]; k32.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, wt.DWORD, ctypes.c_wchar_p, ctypes.POINTER(wt.DWORD)]; k32.CloseHandle.argtypes = [ctypes.c_void_p]
        snap = k32.CreateToolhelp32Snapshot(2, 0); pe = PE(); pe.dwSize = ctypes.sizeof(PE); ok = k32.Process32FirstW(snap, ctypes.byref(pe))
        while ok:
            if pe.exe.lower() == 'witness64_d3d11.exe':
                h = k32.OpenProcess(0x1000, False, pe.pid); b = ctypes.create_unicode_buffer(520); n = wt.DWORD(520)
                if h and k32.QueryFullProcessImageNameW(h, 0, b, ctypes.byref(n)) and os.path.dirname(b.value).lower() == gdir.lower().rstrip('\\'): gp = pe.pid
                if h: k32.CloseHandle(h)
            ok = k32.Process32NextW(snap, ctypes.byref(pe))
        k32.CloseHandle(snap); time.sleep(0.5)
    print('GAMEPID %s' % gp, flush=True)
    gw = None; t0 = time.time()
    while gp and time.time() - t0 < 120 and not gw: gw = game_window(gp); time.sleep(0.5)
    if not other: print('SKIP the game window opens on the launcher\'s monitor | only one monitor', flush=True)
    else:
        time.sleep(3); gmon = monitor_of(gw)[0] if gw else None
        check(gmon == lmon, "the game window opens on the launcher's monitor (not the primary one)", 'launcher %s game %s' % (lmon, gmon))
time.sleep(0.5); u.SendMessageW(main, 0x10, 0, 0)      # WM_CLOSE
try: proc.wait(timeout=5)
except Exception: proc.kill()
print('DONE', flush=True)
