"""Live-renderer bounds oracle, key interactions and captures at supported screen sizes.
Native mode resizes only the supplied registered game window. Offscreen mode renders
the same scene at exact target sizes. Neither uses focus or desktop input.
usage: wcozy.py <pid> <instance folder> <capture folder> <width> <height>
"""
import os, sys, time, re, ctypes, ctypes.wintypes as wt, json
HERE = os.path.dirname(os.path.abspath(__file__))
exec(compile(open(os.path.join(HERE, 'wimg.py')).read(), 'wimg.py', 'exec'))
pid, directory, output = int(sys.argv[1]), sys.argv[2], sys.argv[3]
os.makedirs(output, exist_ok=True)
logpath = os.path.join(directory, 'witness_proxy.log')
offscreen = len(sys.argv) > 6 and sys.argv[6] == 'offscreen'

def send(lines, settle=.25):
    path = os.path.join(directory, 'witness_cmd.txt'); deadline = time.monotonic() + 10
    while os.path.exists(path):
        assert time.monotonic() < deadline, 'previous command not consumed'
        time.sleep(.03)
    with open(path + '.tmp', 'w') as f: f.write('\n'.join(lines) + '\n')
    os.replace(path + '.tmp', path)
    while os.path.exists(path):
        assert time.monotonic() < deadline, 'command not consumed'
        time.sleep(.03)
    time.sleep(settle)

def key(name):
    send(['key %s down' % name]); send(['key %s up' % name])

def guide_open(want):
    path = os.path.join(directory, 'witness_net.json')
    try: os.remove(path)
    except FileNotFoundError: pass
    send(['netstate'])
    with open(path) as f: state = json.load(f)
    assert state['guide'] == want, ('F1 hold state', state)
    assert state['paused'] is False, 'UI leaked input to pause menu'

u = ctypes.WinDLL('user32'); windows = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(hwnd, _):
    process = wt.DWORD(); u.GetWindowThreadProcessId(hwnd, ctypes.byref(process))
    r = wt.RECT(); u.GetClientRect(hwnd, ctypes.byref(r))
    if process.value == pid and u.IsWindowVisible(hwnd) and r.right > 200: windows.append(hwnd)
    return True
u.EnumWindows(cb, 0)
assert len(windows) == 1, ('game window', windows)
hwnd = windows[0]
original = wt.RECT(); u.GetWindowRect(hwnd, ctypes.byref(original))
manifest_path = os.path.join(output, 'captures.json')
manifest = list({item['file']: item for item in json.load(open(manifest_path))}.values()) if os.path.exists(manifest_path) else []
def capture(label, width, height):
    bmp = os.path.join(directory, 'shots', 'cozy_check.bmp')
    if os.path.exists(bmp): os.remove(bmp)
    if offscreen: send(['uishot %d %d cozy_check' % (width, height)])
    else: send(['uistat', 'shot cozy_check'])
    deadline = time.monotonic() + 15; last = -1
    while time.monotonic() < deadline:
        size = os.path.getsize(bmp) if os.path.exists(bmp) else 0
        if size > 54 and size == last: break
        last = size; time.sleep(.1)
    else: raise AssertionError('capture timed out')
    with open(logpath, errors='ignore') as f: matches = re.findall(r'uistat: size (\d+) (\d+) texts (\d+) overflow (\d+) minfont ([\d.]+) smalltargets (\d+)', f.read())
    assert matches, 'no renderer layout stats'
    w, h, texts, overflow, font, targets = map(float, matches[-1])
    assert (w, h) == (width, height), ('render target size', w, h, width, height)
    assert texts > 0 and overflow == 0, (label, 'text outside render target', matches[-1])
    assert targets == 0, (label, 'controls too small for UI scale', targets)
    assert font >= 10, (label, 'font too small', font)
    data = open(bmp, 'rb').read(54)
    assert struct.unpack_from('<ii', data, 18) == (width, -height), 'capture size'
    name = '%dx%d_%s.png' % (width, height, label)
    bmp_to_png(bmp, os.path.join(output, name))
    manifest[:] = [item for item in manifest if item['file'] != name]
    manifest.append(dict(file=name, screen=label, width=width, height=height, minfont=font, overflow=int(overflow), mode='offscreen live renderer' if offscreen else 'native back buffer'))
    print('PASS %dx%d %s, minimum font %.2f px' % (width, height, label, font), flush=True)
try:
    if len(sys.argv) > 6 and sys.argv[6] == 'input':
        key('F5'); key('ESC'); guide_open(False)
        print('PASS roster Escape consumed', flush=True)
        sys.exit(0)
    send(['menusel 293', 'savepreset cozy-layout-example'], .5)
    for width, height in [(int(sys.argv[4]), int(sys.argv[5]))]:
        r = wt.RECT(0, 0, width, height)
        u.AdjustWindowRectEx(ctypes.byref(r), u.GetWindowLongW(hwnd, -16), False, u.GetWindowLongW(hwnd, -20))
        if not offscreen: assert u.SetWindowPos(hwnd, 0, 0, 0, r.right-r.left, r.bottom-r.top, 0x0016), 'resize failed'
        time.sleep(1)
        for page in ('map', 'nearby', 'saves'):
            send(['menu ' + page], .5); capture(page, width, height)
        # Hover and selected states are captured separately on the map tabs.
        send(['menu map', 'menuat %d %d' % (int(width-260*min(width/1280,height/900)), int(40*min(width/1280,height/900)))])
        capture('hover', width, height)
        key('TAB'); capture('keyboard', width, height)
        send(['menu toggle'])
        send(['key F1 down']); guide_open(True); capture('guide', width, height)
        send(['key F1 up']); guide_open(False)
        key('F5'); capture('roster', width, height); key('ESC'); guide_open(False)
        send(['chat ' + 'Wide WWWW and narrow iii preserve measured wrapping. ' * 2,
              'typing on', 'typetext ' + 'MMMMiiii ' * 15])
        capture('chat', width, height); send(['typing off'])
        send(['wheel open', 'mouse 70 0']); capture('wheel', width, height); send(['wheel close'])
    print('ALL OK', flush=True)
finally:
    u.SetWindowPos(hwnd, 0, original.left, original.top, original.right-original.left, original.bottom-original.top, 0x0014)
    with open(manifest_path, 'w') as f: json.dump(manifest, f, indent=2)
