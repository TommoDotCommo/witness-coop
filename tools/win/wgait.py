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
    m = re.search(r'visible_px (\d+) drawn (\d) clip (-?\d+) speed ([-0-9.]+) anim ([-0-9.]+) from (-?\d+) blend ([-0-9.]+)', l)
    return dict(vis=int(m[1]), drawn=int(m[2]), clip=int(m[3]), speed=float(m[4]), anim=float(m[5]), frm=int(m[6]), blend=float(m[7])) if m else None
def ask(wait=2.0):          # one fresh modelstat line
    n0 = len(stats()); send(da, ['modelstat']); t = time.time()
    while time.time() - t < wait:
        s = stats()
        if len(s) > n0: return parse(s[-1])
        time.sleep(0.01)
    return None
results = []
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

(cx, cy, cz), (th, ph) = A.camera(); fx, fy = math.cos(th), math.sin(th); lx, ly = -math.sin(th), math.cos(th)       # forward and left, world
FEET = cz - 1.69
def put(d, x, y, yaw): send(d, ['tp %.4f %.4f %.4f %.5f %.5f' % (x, y, FEET, yaw, 0.0)])
send(da, ['tp %.4f %.4f %.4f %.5f %.5f' % (cx, cy, FEET, th, 0.0)], 1.0)

# ---- 1. eye level: B stands facing A, camera pitch 0: the eyes are on the middle row of the screen at any distance
print('eye level')
for dist in (3.0, 6.0):
    put(db, cx + fx * dist, cy + fy * dist, th + math.pi); time.sleep(2.5)
    path = shot('eye_%dm' % dist); w, h, rows = read_bmp(path)
    mask = {(x, y) for y in range(0, h, 1) for x in range(w // 4, 3 * w // 4) if rows[y][x * 4 + 2] > 120 and rows[y][x * 4 + 1] < 60 and rows[y][x * 4] < 50 and rows[y][x * 4 + 2] - rows[y][x * 4 + 1] > 90}
    blobs = []; seen = set()      # connected red regions (4-neighbours): the eyes are a pair of them on one row; the red gem on the book is a single blob
    for q in sorted(mask):
        if q in seen: continue
        stack = [q]; seen.add(q); pts = []
        while stack:
            x, y = stack.pop(); pts.append((x, y))
            for nb in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if nb in mask and nb not in seen: seen.add(nb); stack.append(nb)
        if len(pts) >= 6: blobs.append((sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts), len(pts)))
    pairs = [(a, b) for i, a in enumerate(blobs) for b in blobs[i + 1:] if abs(a[1] - b[1]) < 6 and 8 < abs(a[0] - b[0]) < 260 and 0.5 < a[2] / b[2] < 2]
    cyy = sum(a[1] + b[1] for a, b in pairs[:1]) / 2 if pairs else -1
    check('the frog\'s two red eyes sit on the camera\'s eye line at %.0f m' % dist, bool(pairs) and abs(cyy - h / 2) < h * 0.02, '%d red blobs, eye pair centre row %.1f, screen middle %d (tolerance %d px)' % (len(blobs), cyy, h // 2, int(h * 0.02)))
    write_png(os.path.join(out, 'eye_%dm.png' % dist), w, h, rgb_rows(w, h, rows, 1, h // 2))      # the cyan line is the camera's eye line

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
check('standing: the idle clip plays and its phase advances', idle0 and idle1 and idle1['clip'] == 0 and idle1['speed'] < 0.3 and idle1['anim'] > idle0['anim'] + 0.2, (idle0, idle1))
n_before = len(stats()); stream(1.6, 3.2, None, 'walk', True); time.sleep(0.2)
ws = [parse(l) for l in stats()[n_before:]]; ws = [s for s in ws if s]
mid = [s for s in ws if s['speed'] > 1.2]
check('walking at 1.6 m/s: the walk clip plays with a running phase', len(mid) >= 3 and all(s['clip'] == 1 for s in mid[-3:]) and mid[-1]['anim'] != mid[-3]['anim'], [(s['clip'], round(s['speed'], 2), round(s['anim'], 2), round(s['blend'], 2)) for s in mid[-4:]])
fade_in = [s for s in ws if s['clip'] == 1 and s['frm'] == 0 and 0 < s['blend'] < 1]
check('starting to walk: the pose is cross-faded from the idle (0 < blend < 1 seen)', len(fade_in) >= 1, [(round(s['blend'], 2), s['frm']) for s in ws[:8]])
time.sleep(1.5); n_before = len(stats()); stream(5.0, 2.0, None, 'run', True); time.sleep(0.2)
rs = [parse(l) for l in stats()[n_before:]]; rs = [s for s in rs if s]; runm = [s for s in rs if s['speed'] > 3.8]
check('running at 5 m/s (the run clip is made for 4.5): the run clip (2) plays', len(runm) >= 2 and all(s['clip'] == 2 for s in runm[-2:]), [(s['clip'], round(s['speed'], 2), round(s['anim'], 2), round(s['blend'], 2)) for s in rs[-4:]])
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
