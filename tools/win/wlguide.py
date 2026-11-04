# T2 check of the launcher's quick guide under a held F1 (Windows Python, window messages only: no focus, no real keyboard, no input lock).
# A real hold is one WM_KEYDOWN and then autorepeat WM_KEYDOWN (bit 30 set) about 30 times a second: 3 s of it must show the guide window ONCE and
# paint it at most 3 times (the launcher counts both and writes them to its state file), keep it open all the time, and hide it on the key-up.
# usage: wlguide.py <launcher exe> <game dir> <stat file>
import ctypes, ctypes.wintypes as wt, os, re, subprocess, sys, time
exe, gdir, statf = sys.argv[1], sys.argv[2], sys.argv[3]
u = ctypes.WinDLL('user32', use_last_error=True); u.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
if os.path.exists(statf): os.remove(statf)
proc = subprocess.Popen([exe, '--dir', gdir, '--no-activate', '--stat-file', statf], startupinfo=si)
import atexit; atexit.register(lambda: proc.poll() is None and proc.terminate())
WinEnum = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cls(h): c = ctypes.create_unicode_buffer(60); u.GetClassNameW(h, c, 60); return c.value
main = None; t0 = time.time()
while time.time() - t0 < 15 and not main:
    found = []
    @WinEnum
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == proc.pid and cls(h) == 'WitnessLauncher': found.append(h)
        return True
    u.EnumWindows(cb, 0); main = found[0] if found else None; time.sleep(0.2)
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('CHECK %s %s | %s' % ('ok' if cond else 'FAILED', name, detail), flush=True)
def stat(key):
    t = time.time()
    while time.time() - t < 5:
        try:
            m = re.search(r'^%s (\d+)' % key, open(statf, errors='ignore').read(), re.M)
            if m: return int(m[1])
        except OSError: pass
        time.sleep(0.1)
check('launcher window found', main)
time.sleep(1.0); s0, p0 = stat('guideshows'), stat('guidepaints')
u.PostMessageW(main, 0x100, 0x70, 0x3B0001); time.sleep(0.6)      # the first press
opened = stat('guide')
for i in range(90): u.PostMessageW(main, 0x100, 0x70, 0x403B0001); time.sleep(0.033)      # autorepeat for 3 s
time.sleep(0.6); s1, p1, held = stat('guideshows'), stat('guidepaints'), stat('guide')
check('the first press opens the guide', opened == 1, 'guide %s' % opened)
check('3 s of autorepeat leave the guide open', held == 1, 'guide %s' % held)
check('the guide window is shown once for the whole hold', s1 - s0 == 1, 'shows %s -> %s' % (s0, s1))
check('the guide window is painted at most 3 times during a 3 s hold', p1 - p0 <= 3, 'paints %s -> %s' % (p0, p1))
u.PostMessageW(main, 0x101, 0x70, 0xC03B0001); time.sleep(0.8)
check('the key-up closes the guide', stat('guide') == 0, 'guide %s' % stat('guide'))
check('closing does not show it again', stat('guideshows') == s1, 'shows %s' % stat('guideshows'))
u.PostMessageW(main, 0x10, 0, 0); time.sleep(0.5)
print('ALL OK' if ok else 'FAILED', flush=True); sys.exit(0 if ok else 1)
