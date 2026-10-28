# T2 atlas oracle: real GDI metrics must be proportional, all 95 slots must be separated and bounded,
# and padding around the rendered glyphs must be free of neighbouring ink. usage: watlas.py <dir of the game>
import os, sys, re, struct, time
d = sys.argv[1]
def send(lines):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
send(['atlasdump atlas_check']); time.sleep(1.5)
log = open(os.path.join(d, 'witness_proxy.log'), errors='ignore').read()
m = re.findall(r'ui: atlas (\d+)x(\d+) line (\d+) pitch (\d+) glyphs (\d+) advances ([0-9.]+)\.\.([0-9.]+)', log)
if not m: print('FAILED: no proportional atlas line in the log'); sys.exit(1)
W, H, line, pitch, count = [int(v) for v in m[-1][:5]]; minadv, maxadv = [float(v) for v in m[-1][5:]]
glyphs = {int(v[0]): tuple([int(x) for x in v[1:5]] + [float(v[5])]) for v in re.findall(r'uiglyph: (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9.]+)', log)}
data = open(os.path.join(d, 'shots', 'atlas_check.bmp'), 'rb').read(); off = struct.unpack_from('<I', data, 10)[0]
def ink(x, y): return data[off + (y * W + x) * 4] > 0
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-88s %s  %s' % (name, 'ok' if cond else 'FAILED', detail))
check('all printable ASCII glyphs have measured advances', count == 95 and len(glyphs) == 95, len(glyphs))
check('font is proportional (i is narrower than M)', ord('i') in glyphs and ord('M') in glyphs and glyphs[ord('i')][4] < glyphs[ord('M')][4], (glyphs.get(ord('i')), glyphs.get(ord('M'))))
check('space has a real advance and metrics vary', glyphs.get(32, (0,0,0,0,0))[4] > 0 and maxadv > minadv * 2, (minadv, maxadv))
check('line spacing leaves room for descenders', pitch >= line + 4, (line, pitch))
rects = []
for ch, (x, y, w, h, advance) in glyphs.items():
    if x < 0 or y < 0 or w <= 0 or advance <= 0 or x + w > W - 4 or y + pitch > H - 4:
        check('glyph %r stays inside atlas' % chr(ch), False, (x,y,w,h,advance))
    rects.append((x,y,x+w,y+pitch,ch))
for i, a in enumerate(rects):
    for b in rects[i+1:]:
        if max(a[0],b[0]) < min(a[2],b[2]) and max(a[1],b[1]) < min(a[3],b[3]):
            check('glyph slots do not overlap', False, (chr(a[4]),chr(b[4]))); break
bad_guards = []
for ch, (x, y, w, h, advance) in glyphs.items():
    if ch == 32: continue
    right = sum(ink(x + w - 1, yy) for yy in range(y, min(y + pitch, H)))
    bottom = sum(ink(xx, yy) for yy in range(y + pitch - 2, y + pitch) for xx in range(x, x + w))
    if right or bottom: bad_guards.append((chr(ch), right, bottom))
check('right and bottom glyph guards contain no neighbouring ink', not bad_guards, bad_guards[:12])
check('solid white block remains available for shapes', all(ink(x,y) for x in range(W-4,W) for y in range(H-4,H)))
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
