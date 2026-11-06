# Game-free check of the launcher's shape on its real window (Windows Python; tools/qa_launcher_shell.py runs it): four tabs, Play turning into Lobby, the
# settings saved on change, press-a-key capture refusing F1 and keys in use, Reset section, the remembered Play choice, and the pad (states injected through
# the launcher's command file, the same path as XInput: D-pad on a slider, RB/LB tabs, B back). Evidence: the launcher's state file and witness_coop.cfg on disk.
# usage: wlauncher_shell.py <launcher exe>      (a fresh folder in %TEMP% stands in for the game folder; no game is started)
import os, re, shutil, subprocess, sys, tempfile, time
exe = sys.argv[1]
gdir = tempfile.mkdtemp(prefix='witness-launcher-shell-')
cfgp, statf, cmdf = os.path.join(gdir, 'witness_coop.cfg'), os.path.join(gdir, 'stat.txt'), os.path.join(gdir, 'cmd.txt')
open(cfgp, 'w').write('name=Fox\nsave_dir=instance\nlisten=47001\nsome_future_key=kept\n')
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('CHECK %s %s | %s' % ('ok' if cond else 'FAILED', name, str(detail)[:200]), flush=True)
def stat():
    for _ in range(50):
        try:
            t = open(statf, errors='ignore').read()
            if t: return t
        except OSError: pass
        time.sleep(0.1)
    return ''
def ui():
    s = stat(); m = re.search(r'^ui (.*)$', s, re.M); st = re.search(r'^uistatus (.*)$', s, re.M); h = re.search(r'^hints (.*)$', s, re.M)
    return (m[1] if m else ''), (st[1] if st else ''), (h[1] if h else '')
def cfg():
    return dict(l.split('=', 1) for l in open(cfgp).read().splitlines() if '=' in l)
def cmd(*lines):
    t = time.time()
    while os.path.exists(cmdf) and time.time() - t < 8: time.sleep(0.05)
    open(cmdf + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmdf + '.tmp', cmdf)
    t = time.time()
    while os.path.exists(cmdf) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(0.5)
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4      # SW_SHOWNOACTIVATE
p = subprocess.Popen([exe, '--dir', gdir, '--no-activate', '--size', '1280', '800', '--stat-file', statf, '--cmd-file', cmdf], startupinfo=si)
def clear_of_pointer(pid):      # a pointer resting over the window (someone at the PC) sends it mouse moves, which switch the footer back to keyboard hints: move the window aside (never the pointer)
    import ctypes, ctypes.wintypes as wt
    us = ctypes.WinDLL('user32'); us.SetProcessDPIAware(); found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hw, lp):
        q = wt.DWORD(); us.GetWindowThreadProcessId(hw, ctypes.byref(q)); c = ctypes.create_unicode_buffer(64); us.GetClassNameW(hw, c, 64)
        if q.value == pid and c.value == 'WitnessLauncher': found.append(hw)
        return True
    us.EnumWindows(cb, 0); cur = wt.POINT(); us.GetCursorPos(ctypes.byref(cur))
    for hw in found:
        r = wt.RECT(); us.GetWindowRect(hw, ctypes.byref(r))
        if r.left <= cur.x < r.right and r.top <= cur.y < r.bottom: us.SetWindowPos(hw, None, cur.x + 40, r.top, 0, 0, 0x0001 | 0x0004 | 0x0010)      # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
try:
    time.sleep(2.5); clear_of_pointer(p.pid)
    u, s, h = ui()
    check('1. the launcher opens on Play at 1280x800', 'tab 0 label Play' in u and 'client 1280 800' in u, u)
    cmd('tab 2', 'section 2', 'focus cfg:voice_volume', 'pad 0 0 0', 'pad 0004 0 0', 'pad 0 0 0'); u, s, h = ui()
    check('2. pad D-pad left on Voice volume lowers it and it is saved at once (no Save button)', cfg().get('voice_volume') == '95' and s.startswith('Saved'), (cfg().get('voice_volume'), s))
    check('   the footer switched to pad buttons', h.startswith('A Select   B Back   LB/RB Tabs'), h)
    cmd('pad 0200 0 0', 'pad 0 0 0'); u1 = ui()[0]; cmd('pad 0100 0 0', 'pad 0 0 0'); u2 = ui()[0]
    check('3. RB and LB change tabs', 'tab 3 ' in u1 and 'tab 2 ' in u2, (u1, u2))
    cmd('capture cfg:key_chat 0x70'); s1 = ui()[1]; cmd('capture cfg:key_chat 0x52'); s2 = ui()[1]
    check('4. press-a-key refuses F1 and a key in use, and says why; the cfg is unchanged', s1.startswith('F1 is the quick guide') and 'already used for Radio' in s2 and 'key_chat' not in cfg(), (s1, s2))
    cmd('capture cfg:key_chat 0x55')
    check('   a free key is taken and saved', cfg().get('key_chat') == 'U', cfg().get('key_chat'))
    cmd('section 2', 'pad 4000 0 0', 'pad 0 0 0')
    check('5. Reset section (X) puts Voice and sound back to its defaults, other sections untouched', cfg().get('voice_volume') == '100' and cfg().get('key_chat') == 'U', (cfg().get('voice_volume'), cfg().get('key_chat')))
    cmd('pad 2000 0 0', 'pad 0 0 0', 'padoff'); u = ui()[0]
    check('6. B goes back to Play', 'tab 0 ' in u, u)
    cmd('choice solo')
    check('7. the Play choice is remembered', cfg().get('launcher_choice') == 'solo', cfg().get('launcher_choice'))
    cmd('choice host', 'host 47391'); time.sleep(1.0); u = ui()[0]
    check('8. hosting turns Play into Lobby', 'label Lobby lobbyview 1' in u, u)
    cmd('leave'); t0 = time.time(); u = ui()[0]      # (leaving closes the router mapping first, which can take a few seconds)
    while 'label Play lobbyview 0' not in u and time.time() - t0 < 10: time.sleep(0.2); u = ui()[0]
    check('   leaving returns to the tiles', 'label Play lobbyview 0' in u, u)
    check('9. lines the launcher does not know are kept', cfg().get('some_future_key') == 'kept')
    cmd('quit'); p.wait(5)
    # an invite link (Setup's witness-coop:// handler starts the launcher with it as one argument): Play, the Join tile, the invite in the box; not joined, the choice not remembered
    for f in (statf, cmdf):
        try: os.remove(f)
        except OSError: pass
    link, invite = 'witness-coop://192.168.1.20:47100/ABC123/', '192.168.1.20:47100/ABC123'
    p = subprocess.Popen([exe, link, '--dir', gdir, '--no-activate', '--size', '1280', '800', '--stat-file', statf, '--cmd-file', cmdf], startupinfo=si)
    time.sleep(2.5); t = stat(); u, s, h = ui(); jb = re.search(r'^joinbox (.*)$', t, re.M); la = re.search(r'^lobby active (\d)', t, re.M)
    check('10. an invite link opens Play on the Join tile with the invite in the join box, and says to check it', 'tab 0 label Play' in u and 'choice join' in u and jb and jb[1] == invite and 'Check it, then press Join' in s, (u, jb and jb[1], s))
    check('   it is not joined by itself, and the remembered choice is not changed', la and la[1] == '0' and cfg().get('launcher_choice') == 'host', (la and la[1], cfg().get('launcher_choice')))
finally:
    try: cmd('quit'); p.wait(5)
    except Exception: pass
    if p.poll() is None: p.terminate()
    shutil.rmtree(gdir, ignore_errors=True)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
