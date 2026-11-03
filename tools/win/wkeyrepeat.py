"""Held mod keys in the game (Windows Python; window messages to the game's own window, nothing needs the focus): a real keyboard hold is WM_KEYDOWN, then
the autorepeat WM_KEYDOWN (bit 30) about 30 times a second, then WM_KEYUP. The proxy's counters (command keystat: developer menu toggles, player list
flips, canvas flips, ping key presses) must show that a held press key acted once and a hold key opened once and closed on the release.
  F8 held 1.5 s: the developer menu opens once; F8 tapped: it closes
  Tab held 1.5 s (the hold-Tab player panel): it opens once and closes on the release (two flips)
  Z held 1.5 s: the drawing canvas flips once; Z tapped: it flips back
  V held 1.5 s: one ping
usage: wkeyrepeat.py <instance folder> <pid>"""
import os, sys, time, re, ctypes, ctypes.wintypes as wt
directory, pid = sys.argv[1], int(sys.argv[2])
log = os.path.join(directory, 'witness_proxy.log'); ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('CHECK %s %s | %s' % ('ok' if cond else 'FAILED', name, detail), flush=True)
def send(lines):
    path = os.path.join(directory, 'witness_cmd.txt'); end = time.monotonic() + 10
    while os.path.exists(path):
        assert time.monotonic() < end, 'previous command not consumed'; time.sleep(.02)
    with open(path + '.tmp', 'w') as f: f.write('\n'.join(lines) + '\n')
    os.replace(path + '.tmp', path)
    while os.path.exists(path):
        assert time.monotonic() < end, 'command not consumed'; time.sleep(.02)
def keystat():
    n = len(re.findall('keystat:', open(log, errors='ignore').read())); send(['keystat']); end = time.monotonic() + 5
    while time.monotonic() < end:
        m = re.findall(r'keystat: devmenu_toggles (\d+) roster_flips (\d+) canvas_flips (\d+) ping_keys (\d+)', open(log, errors='ignore').read())
        if len(m) > n: return tuple(int(x) for x in m[-1])
        time.sleep(.05)
    raise AssertionError('no keystat')
u = ctypes.WinDLL('user32'); wins = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def _cb(h, _):
    p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p)); r = wt.RECT(); u.GetClientRect(h, ctypes.byref(r))
    if p.value == pid and u.IsWindowVisible(h) and r.right > 200: wins.append(h)
    return True
u.EnumWindows(_cb, 0); assert len(wins) == 1, wins; hwnd = wins[0]
def hold(vk, scan, seconds=1.5):      # WM_KEYDOWN, the autorepeat 30 a second, WM_KEYUP
    u.PostMessageW(hwnd, 0x100, vk, 1 | scan << 16)
    for _ in range(int(seconds * 30)): time.sleep(.033); u.PostMessageW(hwnd, 0x100, vk, 1 | scan << 16 | 0x40000000)
    u.PostMessageW(hwnd, 0x101, vk, 1 | scan << 16 | 0xC0000000); time.sleep(.5)
def tap(vk, scan): u.PostMessageW(hwnd, 0x100, vk, 1 | scan << 16); time.sleep(.2); u.PostMessageW(hwnd, 0x101, vk, 1 | scan << 16 | 0xC0000000); time.sleep(.5)
d0, r0, c0, p0 = keystat(); hold(0x77, 0x42); d1, r1, c1, p1 = keystat()
check('F8 held 1.5 s opens the developer menu once (no toggling on the autorepeat)', d1 - d0 == 1, 'toggles %d' % (d1 - d0))
tap(0x77, 0x42); d2, r2, c2, p2 = keystat(); check('F8 tapped closes it', d2 - d1 == 1, 'toggles %d' % (d2 - d1))
hold(0x09, 0x0F); d3, r3, c3, p3 = keystat()
check('Tab held 1.5 s opens the player panel once and the release closes it (2 flips)', r3 - r2 == 2, 'flips %d' % (r3 - r2))
hold(ord('Z'), 0x2C); d4, r4, c4, p4 = keystat()
check('Z held 1.5 s flips the drawing canvas once', c4 - c3 == 1, 'flips %d' % (c4 - c3))
tap(ord('Z'), 0x2C); d5, r5, c5, p5 = keystat(); check('Z tapped flips it back', c5 - c4 == 1, 'flips %d' % (c5 - c4))
hold(ord('V'), 0x2F); d6, r6, c6, p6 = keystat()
check('V held 1.5 s pings once', p6 - p5 == 1, 'pings %d' % (p6 - p5))
print('ALL OK' if ok else 'FAILED', flush=True); sys.exit(0 if ok else 1)
