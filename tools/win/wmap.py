# T2 check of the map picture, run with Windows Python against one game: starts the fly-over, waits for it, and reports the log's numbers and what the files hold.
# usage: wmap.py <pid> <game dir>
import os, sys, time, re, math, struct
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, d = int(sys.argv[1]), sys.argv[2]
P = attach(pid)
def send(lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    time.sleep(settle)
log = os.path.join(d, 'witness_proxy.log')
def lines(prefix): return [l.rstrip() for l in open(log, errors='ignore') if l.startswith(prefix)]
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-96s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
send(['menusel c335', 'menudo teleport'], 5.0)
home = P.camera()[0]
send(['mapgen start'], 1.0); t0 = time.time(); done = None; tries = 1
while time.time() - t0 < 420:
    ls = lines('mapgen: done')
    if ls: done = ls[-1]; break
    if len([l for l in lines('mapgen:') if l.startswith('mapgen: the world')]) >= tries and tries < 4: tries += 1; time.sleep(3); send(['mapgen start'], 1.0)      # the census was not up yet
    time.sleep(3)
plan = (lines('mapgen:') or [''])[0]
print('  ', plan, flush=True)
m = re.search(r'done (\d+)x(\d+), covered ([0-9.]+)%, luminance sd ([0-9.]+), seams mean_ncc (-?[0-9.]+) over (\d+), overlay frames during capture (\d+), tiles (\d+) in (\d+) s', done or '')
check('the fly-over finished and wrote the picture', bool(m), done)
if m:
    W, H, cov, sd, ncc, npairs, ovf, tiles, secs = int(m[1]), int(m[2]), float(m[3]), float(m[4]), float(m[5]), int(m[6]), int(m[7]), int(m[8]), int(m[9])
    out = os.path.join(d, 'witness_map'); txt = open(os.path.join(out, 'underlay.txt')).read().split(); bmp = open(os.path.join(out, 'underlay.bmp'), 'rb').read()
    check('underlay.txt gives the world rectangle and the size, underlay.bmp holds exactly that many pixels', len(txt) == 6 and int(txt[4]) == W and int(txt[5]) == H and len(bmp) == 54 + W * H * 4 and float(txt[2]) > float(txt[0]) and float(txt[3]) > float(txt[1]), txt)
    px = bmp[54:]; a = px[3::4]; cov2 = sum(1 for v in a[::7] if v) / len(a[::7]) * 100
    check('the picture covers a good part of its rectangle (the island), checked on the file itself', cov > 20 and abs(cov2 - cov) < 3, (cov, round(cov2, 1)))
    lum = [0.3 * px[i + 2] + 0.59 * px[i + 1] + 0.11 * px[i] for i in range(0, len(px), 4 * 31) if px[i + 3]]
    mean = sum(lum) / len(lum); sd2 = math.sqrt(sum((v - mean) ** 2 for v in lum) / len(lum))
    check('it is a picture, not a flat colour (luminance spread above 15, from the file)', sd2 > 15 and sd > 15, (round(sd2, 1), sd))
    check('neighbouring tiles agree where they overlap (mean normalised correlation above 0.3, %d pairs)' % npairs, npairs >= 10 and ncc > 0.3, ncc)
    check('nothing of the mod was drawn while the frames were captured', ovf == 0, ovf)
    time.sleep(4.0); end = P.camera()[0]
    check('the player is back where it started (within 3 m)', math.dist(home, end) < 3.0, round(math.dist(home, end), 2))
    send(['menu map'], 2.5); send(['mapstat'], 0.5)
    ms = [l for l in lines('mapstat:')]; mm = re.search(r'underlay loaded (\d) drawn (\d+)', ms[-1]) if ms else None
    check('the dev menu map loads the picture and draws it', mm and mm[1] == '1' and int(mm[2]) > 0, ms[-1] if ms else None)
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
