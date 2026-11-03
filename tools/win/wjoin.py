# T2 check of finding each other (co-op standard B, C), run with Windows Python: two real launchers, no game. A hosts Direct (open to my networks); B chooses Join a friend:
# its Join list shows A from the LAN scan within 6 s, and B joins from the row (no code). B leaves and joins again by pasting A's WIT- invite code (every route at once).
# A's "Friends can join by" says Same network OK (the lobby answered its own LAN-address probe). B remembered A as a recent host. Then A hosts invite only:
# B's list no longer shows A, and the pasted code still joins (the code carries the secret). Every check reads the launchers' stat files (state, not pictures).
# usage: wjoin.py <launcher A> <dir A> <launcher B> <dir B> <lobby port> <name A> <rect A> <rect B> [mutant | pictures <folder>]
#   pictures: the windows at 1280x800, and window pictures (PrintWindow: nothing else on the desktop) of B's Join list and A's lobby into <folder> as BMP
#   mutant: B is a build that ignores every CPS2 answer: A must not be listed (the run must fail its list checks)
import os, re, subprocess, sys, time
exeA, dirA, exeB, dirB, lport, nameA, rectA, rectB = sys.argv[1:9]; lport = int(lport); mutant = len(sys.argv) > 9 and sys.argv[9] == 'mutant'; pics = sys.argv[10] if len(sys.argv) > 10 and sys.argv[9] == 'pictures' else ''
here = os.path.dirname(os.path.abspath(__file__))
def sp(d): return os.path.join(d, 'launcher_stat.txt')
def cp(d): return os.path.join(d, 'launcher_cmd.txt')
def cmd(d, *lines):
    p = cp(d); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.05)
    open(p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(p + '.tmp', p)
    t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.05)
def stat(d):
    try: txt = open(sp(d), encoding='utf-8', errors='ignore').read()
    except OSError: return None
    r = {'members': {}, 'rows': [], 'hostrow': {}, 'lobby': {}, 'joinlist': {}, 'raw': txt, 'invite_code': '-', 'status': '', 'ui': ''}
    for l in txt.splitlines():
        if l.startswith('lobby '): v = l.split(); r['lobby'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
        elif l.startswith('member '):
            m = re.match(r'member (\d+) name (.*) host (\d)', l)
            if m: r['members'][int(m[1])] = m[2]
        elif l.startswith('joinrow '):
            m = re.match(r'joinrow name (.*) ip (\S+) port (\d+) route (\d+) mismatch (\d) full (\d) players (\d+) limit (\d+) state (\d+) line (.*)', l)
            if m: r['rows'].append({'name': m[1], 'ip': m[2], 'port': int(m[3]), 'route': int(m[4]), 'mismatch': int(m[5]), 'line': m[10]})
        elif l.startswith('hostrow '):
            m = re.match(r'hostrow (\d) state (\d) (.*)', l)
            if m: r['hostrow'][int(m[1])] = (int(m[2]), m[3])
        elif l.startswith('joinlist '): v = l.split(); r['joinlist'] = {v[i]: v[i + 1] for i in range(1, len(v) - 1, 2)}
        elif l.startswith('hostinvite_code '): r['invite_code'] = l[16:].strip()
        elif l.startswith('lobbystatus '): r['status'] = l[12:]
        elif l.startswith('uistatus '): r['ui'] = l[9:]
        elif l.startswith('probes '): m = re.match(r'probes answered (\d+) invite_only (\d)', l); r['probes'] = int(m[1]); r['invite_only'] = int(m[2])
    return r
def wait(d, pred, secs):
    end = time.time() + secs; s = None
    while time.time() < end:
        s = stat(d)
        try:
            if s and pred(s): return s
        except (KeyError, IndexError, ValueError): pass
        time.sleep(0.15)
    return s
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-120s %s  (%s)' % (name, 'ok' if cond else 'FAILED', str(detail)[:260]), flush=True)
def picture(pid, name):
    import ctypes, ctypes.wintypes as wt, struct
    u = ctypes.WinDLL('user32'); g32 = ctypes.WinDLL('gdi32'); found = []
    u.GetWindowDC.restype = wt.HDC; u.GetWindowDC.argtypes = [wt.HWND]; u.ReleaseDC.argtypes = [wt.HWND, wt.HDC]; u.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]; u.RedrawWindow.argtypes = [wt.HWND, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
    for f, r, a in (('CreateCompatibleDC', ctypes.c_void_p, [wt.HDC]), ('CreateCompatibleBitmap', ctypes.c_void_p, [wt.HDC, ctypes.c_int, ctypes.c_int]), ('SelectObject', ctypes.c_void_p, [ctypes.c_void_p, ctypes.c_void_p]),
                    ('GetDIBits', ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]), ('DeleteObject', wt.BOOL, [ctypes.c_void_p]), ('DeleteDC', wt.BOOL, [ctypes.c_void_p])):
        getattr(g32, f).restype = r; getattr(g32, f).argtypes = a
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); c = ctypes.create_unicode_buffer(40); u.GetClassNameW(h, c, 40)
        if p.value == pid and c.value == 'WitnessLauncher' and u.IsWindowVisible(h): found.append(h)
        return True
    u.EnumWindows(cb, 0)
    if not found: print('  no window for the picture', name); return
    h = found[0]; u.RedrawWindow(h, None, None, 0x1 | 0x80 | 0x100); time.sleep(0.4)
    r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r)); w, hh = r.right - r.left, r.bottom - r.top
    dc = u.GetWindowDC(h); mdc = g32.CreateCompatibleDC(dc); bmp = g32.CreateCompatibleBitmap(dc, w, hh); g32.SelectObject(mdc, bmp); u.PrintWindow(h, mdc, 2)
    hdr = struct.pack('<IiiHHIIiiII', 40, w, -hh, 1, 32, 0, w * hh * 4, 0, 0, 0, 0); buf = ctypes.create_string_buffer(w * hh * 4); g32.GetDIBits(mdc, bmp, 0, hh, buf, hdr, 0)
    with open(os.path.join(pics, name + '.bmp'), 'wb') as f: f.write(struct.pack('<2sIHHI', b'BM', 54 + w * hh * 4, 0, 0, 54) + hdr + bytes(buf))
    g32.DeleteObject(bmp); g32.DeleteDC(mdc); u.ReleaseDC(h, dc); print('  picture', name, w, hh, flush=True)
def listed(s): return [r for r in s['rows'] if r['name'] == nameA]
for d in (dirA, dirB):
    for f in (sp(d), cp(d)):
        if os.path.exists(f): os.remove(f)
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4      # SW_SHOWNOACTIVATE: never takes the focus
procs = []
try:
    for exe, d, rect in ((exeA, dirA, rectA), (exeB, dirB, rectB)):
        p = subprocess.Popen([exe, '--dir', d, '--no-activate', '--stat-file', sp(d), '--cmd-file', cp(d)], startupinfo=si); procs.append(p)
        print('LAUNCHERPID %d' % p.pid, flush=True)
    for p, rect in zip(procs, (rectA, rectB)):
        time.sleep(1.0); x, y, w, h = rect.split(); w, h = ('1280', '800') if pics else (w, h); subprocess.run([sys.executable, os.path.join(here, 'wplace.py'), str(p.pid), x, y, w, h], capture_output=True, timeout=30)
    a = wait(dirA, lambda s: True, 20); b = wait(dirB, lambda s: True, 20)
    check('0. both launchers are up', a and b, '')
    cmd(dirA, 'tab 0', 'choice host', 'host %d' % lport)
    a = wait(dirA, lambda s: s['lobby']['active'] and s['lobby']['host'] and s['hostrow'].get(1, (0,))[0] == 1, 20)
    check('1. A hosts Direct (open to my networks); Friends can join by: Same network OK (its lobby answered on its own LAN address)', a and a['lobby'].get('active') and a['hostrow'].get(1, (0,))[0] == 1 and 'Same network' in a['hostrow'][1][1], a and a['hostrow'])
    check('   the other rows are there with a result: Steam (not used), Tailscale, Internet (router port off in this test), Windows Firewall', a and len(a['hostrow']) == 5 and a['hostrow'][0][0] == 3 and a['hostrow'][3][0] == 3 and a['hostrow'][4][0] in (1, 2), a and a['hostrow'])
    check('   the invite is a WIT- code', a and a['invite_code'].startswith('WIT-'), a and a['invite_code'])
    t0 = time.time(); cmd(dirB, 'tab 0', 'choice join')
    b = wait(dirB, lambda s: listed(s), 6.5); dt = time.time() - t0
    check('2. B chooses Join a friend: its Join list shows A from the LAN within 6 s', b and listed(b) and listed(b)[0]['route'] == 0 and dt <= 6.5, b and (round(dt, 1), b['rows'], b['joinlist']))
    row = b and listed(b) and listed(b)[0]
    check('   the row says in lobby, 1 of 16, LAN, a ping; same version (not greyed)', row and 'in lobby' in row['line'] and '1 of 16' in row['line'] and 'LAN' in row['line'] and not row['mismatch'], row)
    if pics: time.sleep(1.0); picture(procs[1].pid, 'launcher_join')
    a = stat(dirA); check('   A answered B\'s probes (CPQ2 "WIT ")', a and a.get('probes', 0) >= 1, a and a.get('probes'))
    if not mutant:
        cmd(dirB, 'joinrow ' + nameA)
        b = wait(dirB, lambda s: s['lobby']['joined'] and len(s['members']) == 2, 15); a = wait(dirA, lambda s: len(s['members']) == 2, 10)
        check('3. B joins from the row (no code: the lobby is open to my networks); both lists show two members', b and a and b['lobby'].get('joined') and len(a['members']) == 2, b and (b['lobby'], b['status']))
        if pics:
            cmd(dirA, 'chat Everybody here? The panel in the garden is a good start.'); cmd(dirB, 'chat Here. Ready when you are.', 'ready 1'); time.sleep(2.0); picture(procs[0].pid, 'launcher_lobby')
        b = wait(dirB, lambda s: s['joinlist'].get('recents') == '1', 5)
        check('   B remembered A as a recent host (route LAN)', b and b['joinlist'].get('recents') == '1' and b['joinlist'].get('route') == '0', b and b['joinlist'])
        code = a['invite_code']
        cmd(dirB, 'leave'); a = wait(dirA, lambda s: len(s['members']) == 1, 12)
        check('   B left: A is alone again', a and len(a['members']) == 1, a and a['members'])
        cmd(dirB, 'tab 0', 'choice join', 'join ' + code)
        b = wait(dirB, lambda s: s['lobby']['joined'] and len(s['members']) == 2, 15); a = wait(dirA, lambda s: len(s['members']) == 2, 10)
        check('4. B joins again from the pasted WIT- code (its routes tried at once)', b and a and b['lobby'].get('joined') and len(a['members']) == 2, b and (b['lobby'], b['status'], b['ui']))
        cmd(dirB, 'leave'); cmd(dirA, 'leave'); time.sleep(1.5)
        cmd(dirA, 'setting cfg:lobby_visibility invite', 'tab 0', 'choice host', 'host %d' % lport)
        a = wait(dirA, lambda s: s['lobby']['active'] and s.get('invite_only') == 1 and s['invite_code'].startswith('WIT-'), 20)
        check('5. A hosts again, invite only', a and a.get('invite_only') == 1, a and a['lobby'])
        cmd(dirB, 'tab 0', 'choice join'); b0 = stat(dirB); scans0 = int(b0['joinlist'].get('scans', 0)) if b0 else 0
        b = wait(dirB, lambda s: int(s['joinlist'].get('scans', 0)) >= scans0 + 2, 16)
        check('   B\'s Join list does not show an invite-only lobby (two scans later)', b and not listed(b), b and b['rows'])
        cmd(dirB, 'join ' + a['invite_code'])
        b = wait(dirB, lambda s: s['lobby']['joined'] and len(s['members']) == 2, 15)
        check('   the pasted code (with the secret) still joins it', b and b['lobby'].get('joined'), b and (b['lobby'], b['status'], b['ui']))
finally:
    for p in procs:
        try: p.terminate()
        except Exception: pass
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
