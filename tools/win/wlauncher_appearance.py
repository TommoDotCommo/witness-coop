# Launcher Appearance page: window-message control checks, persistence, invalid/missing values and PrintWindow capture.
import ctypes, ctypes.wintypes as wt, os, subprocess, sys, time
exe, gdir, shot = sys.argv[1], sys.argv[2], sys.argv[3]
u = ctypes.WinDLL('user32', use_last_error=True)
u.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]; u.SendMessageW.restype = ctypes.c_ssize_t
u.GetDlgCtrlID.argtypes = [wt.HWND]; u.GetDlgCtrlID.restype = ctypes.c_int
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
WM_COMMAND, WM_CLOSE, CB_SETCURSEL, CB_GETCURSEL, CB_GETCOUNT = 0x111, 0x10, 0x14E, 0x147, 0x146
NAMES = ('skin', 'hair', 'eyes', 'coat', 'scarf', 'trousers', 'boots', 'gear')
KEYS = ['cfg:appearance_' + n for n in NAMES]
cfg_path = os.path.join(gdir, 'witness_coop.cfg'); original = open(cfg_path, 'rb').read()
proc = None; ok = True

def check(name, condition, detail=''):
    global ok
    ok = ok and bool(condition)
    print('CHECK %s %s | %s' % ('ok' if condition else 'FAILED', name, detail), flush=True)

def close():
    global proc
    if proc is not None and proc.poll() is None:
        for h in windows(proc.pid): u.SendMessageW(h, WM_CLOSE, 0, 0)
        try: proc.wait(timeout=5)
        except Exception: proc.kill()
    proc = None

def windows(pid):
    found = []
    @EnumProc
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); c = ctypes.create_unicode_buffer(40); u.GetClassNameW(h, c, 40)
        if p.value == pid and c.value == 'WitnessLauncher': found.append(h)
        return True
    u.EnumWindows(cb, 0); return found

def start():
    global proc
    dump = os.path.join(gdir, 'launcher_appearance_controls.txt')
    try: os.remove(dump)
    except OSError: pass
    si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
    proc = subprocess.Popen([exe, '--dir', gdir, '--dump-controls', dump, '--no-activate', '--page', '8'], startupinfo=si)
    deadline = time.time() + 15; main = None
    while time.time() < deadline and not (main and os.path.exists(dump)):
        ws = windows(proc.pid); main = ws[0] if ws else None; time.sleep(0.2)
    if not main: raise RuntimeError('launcher window did not appear')
    ids = {k.strip(): int(i) for i, k in (line.split(None, 1) for line in open(dump))}
    kids = {}
    @EnumProc
    def cb(h, l): kids[u.GetDlgCtrlID(h)] = h; return True
    u.EnumChildWindows(main, cb, 0)
    return main, {k: kids[ids[k]] for k in KEYS}, kids[ids['apply']]

def selections(controls): return [u.SendMessageW(h, CB_GETCURSEL, 0, 0) for h in controls.values()]

try:
    main, controls, apply = start()
    counts = [u.SendMessageW(h, CB_GETCOUNT, 0, 0) for h in controls.values()]
    check('all eight appearance controls have four bounded choices', counts == [4] * 8, counts)
    check('launcher loads the configured palette', selections(controls) == [1] * 8, selections(controls))
    wanted = [2, 3, 1, 2, 3, 1, 2, 3]
    for h, value in zip(controls.values(), wanted): u.SendMessageW(h, CB_SETCURSEL, value, 0)
    check('all appearance dropdowns accept independent choices', selections(controls) == wanted, selections(controls))
    u.SendMessageW(main, WM_COMMAND, u.GetDlgCtrlID(apply), apply); time.sleep(0.7)
    cfg = open(cfg_path).read()
    check('Apply writes every appearance choice', all(('appearance_%s=%d' % kv) in cfg for kv in zip(NAMES, wanted)))
    r = subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wwinshot.py'),
                        str(proc.pid), 'WitnessLauncher', shot], capture_output=True, text=True, timeout=20)
    print(r.stdout, end='')
    check('Appearance page captured from its own window', r.returncode == 0 and os.path.getsize(shot) > 10000, r.stderr.strip())
    close()

    main, controls, apply = start()
    check('reopening preserves every appearance choice', selections(controls) == wanted, selections(controls))
    close()

    lines = [line for line in open(cfg_path).read().splitlines() if line != 'appearance_eyes=1']
    lines = ['appearance_skin=9' if line == 'appearance_skin=2' else 'appearance_hair=2junk' if line == 'appearance_hair=3' else line for line in lines]
    open(cfg_path, 'w').write('\n'.join(lines) + '\n')
    main, controls, apply = start()
    got = selections(controls)
    check('invalid and missing entries fall back independently to Original', got[:3] == [0, 0, 0] and got[3:] == wanted[3:], got)
    close()
finally:
    close(); open(cfg_path, 'wb').write(original)
print('ALL OK' if ok else 'FAILED', flush=True)
sys.exit(0 if ok else 1)
