# T2 check of a character's gait and eye height, run with Windows Python. A is the viewer, B the player A sees; B's pose is streamed with `tp` commands at a
# set speed, so the avatar's measured speed (and with it the idle / walk / run clip and the cross-fades between them) is under the script's control.
# usage: wgait.py <pidA> <dirA> <pidB> <dirB> <out dir for the shots>
# Asserts: the model's eyes are on the camera's eye line (pixels of the model's red eyes against the screen's middle row, camera pitch 0, at two distances); the
# clips idle / walk / run are played at rest / 1.6 m/s / 4.5 m/s with a running animation phase; a change of state is cross-faded (0 < blend < 1 on the way, 1 after).
import os, sys, time, math, re, struct, zlib
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, out = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
os.makedirs(out, exist_ok=True)
A, B = attach(pa), attach(pb)
log = os.path.join(da, 'witness_proxy.log')
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.004)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.004)
    time.sleep(settle)
def stats():
    return [l for l in open(log, errors='ignore') if l.startswith('modelstat:')]
def parse(l):
    m = re.search(r'visible_px (\d+) drawn (\d) clip (-?\d+) speed ([-0-9.]+) anim ([-0-9.]+) from (-?\d+) blend ([-0-9.]+) eye ([-0-9.]+) yaw ([-0-9.]+)', l)
    return dict(vis=int(m[1]), drawn=int(m[2]), clip=int(m[3]), speed=float(m[4]), anim=float(m[5]), frm=int(m[6]), blend=float(m[7]), eye=float(m[8]), yaw=float(m[9])) if m else None
def ask(wait=2.0):          # one fresh modelstat line
    n0 = len(stats()); send(da, ['modelstat']); t = time.time()
    while time.time() - t < wait:
        s = stats()
        if len(s) > n0: return parse(s[-1])
        time.sleep(0.01)
    return None
results = []
def span(v):
    v = [x['eye'] for x in v]; return '%d samples, %.3f .. %.3f' % (len(v), min(v), max(v)) if v else 'no samples'
def check(name, cond, detail):
    results.append(bool(cond)); print('  %-74s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)

def read_bmp(path):
    d = open(path, 'rb').read(); off = struct.unpack_from('<I', d, 10)[0]; w, h = struct.unpack_from('<ii', d, 18); top = h < 0; h = abs(h)
    rows = []
    for y in range(h):
        yy = y if top else h - 1 - y; rows.append(d[off + yy * w * 4: off + (yy + 1) * w * 4])
    return w, h, rows        # BGRA rows, top first
def write_png(path, w, h, rows_rgb):
    raw = b''.join(b'\x00' + bytes(r) for r in rows_rgb)
    def chunk(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
def rgb_rows(w, h, rows, step=1, hline=None):
    o = []
    for y in range(0, h, step):
        r = rows[y]; line = bytearray()
        for x in range(0, w, step): line += bytes((r[x * 4 + 2], r[x * 4 + 1], r[x * 4]))
        if hline is not None and abs(y - hline) < step: line = bytearray(bytes((0, 255, 255)) * (len(line) // 3))
        o.append(line)
    return o
def shot(name):
    send(da, ['shot ' + name]); return os.path.join(da, 'shots', name + '.bmp')

def grab(name):      # a shot that is read at once: wait until the game has written the file and it has stopped growing
    path = shot(name); t = time.time(); last = -1
    while time.time() - t < 6:
        try: sz = os.path.getsize(path)
        except OSError: sz = -1
        if sz > 0 and sz == last: return path
        last = sz; time.sleep(0.15)
    raise TimeoutError('no shot ' + name)
(cx, cy, cz), (th, ph) = A.camera(); fx, fy = math.cos(th), math.sin(th); lx, ly = -math.sin(th), math.cos(th)       # forward and left, world
FEET = cz - 1.69
def put(d, x, y, yaw): send(d, ['tp %.4f %.4f %.4f %.5f %.5f' % (x, y, FEET, yaw, 0.0)])
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, FEET, th, 0.0)], 1.0)

# ---- 1. eye level and facing: B stands facing A, camera pitch 0: the eyes are on the middle row of the screen at any distance; turned away, no face is seen
def find_eyes(w, h, rows):
    """The red eye pair of the model: two red blobs on one row (the red gem on the book is a single blob). Returns (number of blobs, centre row of the first pair or -1)."""
    mask = {(x, y) for y in range(0, h, 1) for x in range(w // 4, 3 * w // 4) if rows[y][x * 4 + 2] > 120 and rows[y][x * 4 + 1] < 60 and rows[y][x * 4] < 50 and rows[y][x * 4 + 2] - rows[y][x * 4 + 1] > 90}
    blobs = []; seen = set()
    for q in sorted(mask):
        if q in seen: continue
        stack = [q]; seen.add(q); pts = []
        while stack:
            x, y = stack.pop(); pts.append((x, y))
            for nb in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if nb in mask and nb not in seen: seen.add(nb); stack.append(nb)
        if len(pts) >= 6: blobs.append((sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts), len(pts)))
    pairs = [(a, b) for i, a in enumerate(blobs) for b in blobs[i + 1:] if abs(a[1] - b[1]) < 6 and 8 < abs(a[0] - b[0]) < 260 and 0.5 < a[2] / b[2] < 2]
    return len(blobs), (sum(a[1] + b[1] for a, b in pairs[:1]) / 2 if pairs else -1)
def frog_px(rows, w, h):      # green skin pixels: proof that the frog is on screen (a missing frog must not pass as "no face")
    return sum(1 for y in range(0, h, 3) for x in range(0, w, 3) if rows[y][x * 4 + 1] > rows[y][x * 4 + 2] + 30 and rows[y][x * 4 + 1] > rows[y][x * 4] + 30 and rows[y][x * 4 + 1] > 80)
def fake(dist, yaw): send(da, ['fakepose %.4f %.4f %.4f %.5f' % (cx + fx * dist, cy + fy * dist, cz + 0.02, yaw)])
print('facing and eye level (the remote pose is given with fakepose: its heading is exactly what is asked)')
fake(3.0, th); time.sleep(1.5)                                           # heading = A's heading: the frog turns its back to A
w, h, rows = read_bmp(grab('face_away')); nb_, cy_ = find_eyes(w, h, rows); gp = frog_px(rows, w, h)
check("turned away the frog shows its back: on screen, no eye pair", gp > 150 and cy_ < 0, '%d green px (frog on screen), %d red blobs, eye pair row %.1f' % (gp, nb_, cy_))
write_png(os.path.join(out, 'face_away.png'), w, h, rgb_rows(w, h, rows, 1))
for dist in (3.0, 6.0):
    fake(dist, th + math.pi); time.sleep(1.5)
    w, h, rows = read_bmp(grab('eye_%dm' % dist)); nbl, cyy = find_eyes(w, h, rows)
    check('turned toward A the frog shows its two red eyes, on the camera\'s eye line at %.0f m' % dist, cyy >= 0 and abs(cyy - h / 2) < h * 0.02, '%d red blobs, eye pair centre row %.1f, screen middle %d (tolerance %d px)' % (nbl, cyy, h // 2, int(h * 0.02)))
    write_png(os.path.join(out, 'eye_%dm.png' % dist), w, h, rgb_rows(w, h, rows, 1, h // 2))      # the cyan line is the camera's eye line
fake(3.0, th); time.sleep(1.5); fake(3.0, th + math.pi); time.sleep(1.5)
w, h, rows = read_bmp(grab('face_again')); nbl, cyy = find_eyes(w, h, rows)
check('away, then toward A again: the face is back (the heading is followed, not latched)', cyy >= 0, '%d red blobs, eye pair row %.1f' % (nbl, cyy))
send(da, ['fakepose off']); time.sleep(0.5)

print('the heading of a real second game reaches the puppet (B turned with real mouse input)')
def angdiff(a, b): return abs((a - b + math.pi) % (2 * math.pi) - math.pi)
put(db, cx + fx * 3, cy + fy * 3, th); time.sleep(2.0)
def pair():
    s1 = ask(); (bx, by, bz), (bth, bph) = B.camera(); return s1, bth
s1, bth1 = pair()
check('B standing: the puppet\'s heading is B\'s own camera heading', s1 and angdiff(s1['yaw'], bth1) < 0.05, 'puppet yaw %.3f, B camera heading %.3f' % (s1['yaw'] if s1 else -9, bth1))
for _ in range(25): send(db, ['mouse 14 0'] * 14)
time.sleep(1.0); s2, bth2 = pair()
check('B turned with the mouse (%.2f rad): the puppet turned with it' % angdiff(bth2, bth1), s2 and angdiff(bth2, bth1) > 0.3 and angdiff(s2['yaw'], bth2) < 0.05, 'puppet yaw %.3f, B camera heading %.3f (was %.3f)' % (s2['yaw'] if s2 else -9, bth2, bth1))

# ---- 2. idle, then a walk, then a run, then stopping: the clip, its phase and the fades between
def stream(speed, seconds, shots_every, tag, samples):
    """B crosses A's view from right to left, 4 m ahead, at `speed` m/s (positions follow the clock, so the speed is right whatever the command rate)."""
    t0 = time.time(); start = -speed * seconds / 2; n = 0; nextshot = 0; paths = []
    while True:
        t = time.time() - t0
        if t > seconds: break
        s = start + speed * t; send(da, ['fakepose %.4f %.4f %.4f %.5f' % (cx + fx * 4 + lx * -s, cy + fy * 4 + ly * -s, cz + 0.02, th - math.pi / 2)])    # heading: to the right of A's view (the way the frog moves)
        if shots_every and t >= nextshot: paths.append(shot('%s_%02d' % (tag, len(paths)))); nextshot += shots_every
        if samples is not None: send(da, ['modelstat'])
        n += 1
    return paths
send(da, ['fakepose %.4f %.4f %.4f %.5f' % (cx + fx * 4 + lx * 3, cy + fy * 4 + ly * 3, cz + 0.02, th - math.pi / 2)]); time.sleep(1.5)
idle0 = ask(); time.sleep(0.6); idle1 = ask()
check('standing: the posed eyes are at the camera\'s height (+-0.08 m)', idle1 and abs(idle1['eye'] - 1.71) < 0.08, idle1 and idle1['eye'])
check('standing: the idle clip plays and its phase advances', idle0 and idle1 and idle1['clip'] == 0 and idle1['speed'] < 0.3 and idle1['anim'] > idle0['anim'] + 0.2, (idle0, idle1))
n_before = len(stats()); stream(1.6, 3.2, None, 'walk', True); time.sleep(0.2)
ws = [parse(l) for l in stats()[n_before:]]; ws = [s for s in ws if s]
mid = [s for s in ws if s['speed'] > 1.2]
check('walking at 1.6 m/s: the walk clip plays with a running phase', len(mid) >= 3 and all(s['clip'] == 1 for s in mid[-3:]) and mid[-1]['anim'] != mid[-3]['anim'], [(s['clip'], round(s['speed'], 2), round(s['anim'], 2), round(s['blend'], 2)) for s in mid[-4:]])
check('walking: the posed eyes stay within 0.12 m of the camera\'s height all the way (' + span(mid) + ')', mid and all(abs(s['eye'] - 1.71) < 0.12 for s in mid), 'eye heights of the samples')
fade_in = [s for s in ws if s['clip'] == 1 and s['frm'] == 0 and 0 < s['blend'] < 1]
check('starting to walk: the pose is cross-faded from the idle (0 < blend < 1 seen)', len(fade_in) >= 1, [(round(s['blend'], 2), s['frm']) for s in ws[:8]])
time.sleep(1.5); n_before = len(stats()); stream(5.0, 2.0, None, 'run', True); time.sleep(0.2)
rs = [parse(l) for l in stats()[n_before:]]; rs = [s for s in rs if s]; runm = [s for s in rs if s['speed'] > 3.8]
check('running at 5 m/s (the run clip is made for 4.5): the run clip (2) plays', len(runm) >= 2 and all(s['clip'] == 2 for s in runm[-2:]), [(s['clip'], round(s['speed'], 2), round(s['anim'], 2), round(s['blend'], 2)) for s in rs[-4:]])
check('running: the posed eyes stay within 0.15 m of the camera\'s height (' + span(runm) + ')', runm and all(abs(s['eye'] - 1.71) < 0.15 for s in runm), 'eye heights of the samples')
fade_run = [s for s in rs if s['clip'] == 2 and s['frm'] == 1 and 0 < s['blend'] < 1]
check('walk to run: cross-faded from the walk', len(fade_run) >= 1, [(s['clip'], s['frm'], round(s['blend'], 2)) for s in rs[:10]])
n_before = len(stats()); samples = []
for i in range(40):
    samples.append(ask(0.5)); time.sleep(0.03)
ss = [s for s in samples if s]; faded = [s for s in ss if s['clip'] == 0 and s['frm'] >= 0 and 0 < s['blend'] < 1]
check('stopping: back to idle through a cross-fade, finished at blend 1', len(faded) >= 1 and ss[-1]['clip'] == 0 and ss[-1]['blend'] == 1.0, [(s['clip'], s['frm'], round(s['speed'], 2), round(s['blend'], 2)) for s in ss[:12:2]] + ['last', (ss[-1]['clip'], ss[-1]['blend'])] if ss else None)

# ---- the pictures: the same two streams again, a shot every few frames
time.sleep(2.0); walk_paths = stream(1.6, 3.2, 0.14, 'walk', None); time.sleep(2.0); run_paths = stream(5.0, 2.0, 0.07, 'run', None); time.sleep(0.3)
# ---- contact sheets of the walk and the run, one frame per shot
def sheet(paths, name, cols=4, step=3):
    imgs = []
    for p in paths:      # the frames where the frog is on screen (its green skin), spread evenly over the crossing
        if os.path.exists(p):
            w, h, rows = read_bmp(p); g = sum(1 for y in range(0, h, 3) for x in range(0, w, 3) if rows[y][x * 4 + 1] > rows[y][x * 4 + 2] + 30 and rows[y][x * 4 + 1] > rows[y][x * 4] + 30 and rows[y][x * 4 + 1] > 80)
            if g > 250: imgs.append((w, h, rows))
    if len(imgs) > cols * 2: imgs = [imgs[int(i * (len(imgs) - 1) / (cols * 2 - 1))] for i in range(cols * 2)]
    if not imgs: return
    tiles = [rgb_rows(w, h, rows, step) for w, h, rows in imgs]; tw = len(tiles[0][0]) // 3; th_ = len(tiles[0])
    rows_out = []
    for r in range((len(tiles) + cols - 1) // cols):
        for y in range(th_):
            line = bytearray()
            for c in range(cols):
                i = r * cols + c; line += tiles[i][y] if i < len(tiles) else bytes(tw * 3)
            rows_out.append(line)
    write_png(os.path.join(out, name), tw * cols, len(rows_out), rows_out)
sheet(walk_paths, 'walk_sheet.png'); sheet(run_paths, 'run_sheet.png')
for p in walk_paths + run_paths:
    try: w, h, rows = read_bmp(p); write_png(os.path.join(out, os.path.basename(p)[:-4] + '.png'), w, h, rgb_rows(w, h, rows, 1))
    except OSError: pass
print('RESULT %d of %d checks passed' % (sum(results), len(results))); print('ALL OK' if all(results) else 'FAILED'); sys.exit(0 if all(results) else 1)
