# T2 check of the UI glyph atlas, run with Windows Python: no glyph slot may hold ink in its top rows (that ink belongs to the glyph above: '[' over 't', ']' over 'v' showed as
# little bits of pixels over those letters). usage: watlas.py <dir of the game>
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
m = re.findall(r'ui: atlas (\d+)x(\d+) cell (\d+)x(\d+) pitch (\d+) cols (\d+) first (\d+)', log)
if not m: print('FAILED: no atlas line in the log'); sys.exit(1)
W, H, cw, ch, pitch, cols, first = [int(v) for v in m[-1]]
data = open(os.path.join(d, 'shots', 'atlas_check.bmp'), 'rb').read(); off = struct.unpack_from('<I', data, 10)[0]
def ink(x, y): return data[off + (y * W + x) * 4] > 0
bad = []
for i in range(0, 126 - first + 1):
    cx, cy = (i % cols) * cw, (i // cols) * pitch
    top = sum(1 for y in range(cy, cy + 2) for x in range(cx, cx + cw) if ink(x, y))
    if top: bad.append((chr(first + i), top))
print('glyph slots with ink in their top two rows: %s' % (bad or 'none'))
for c in 'tv': print("  '%s' slot: %s" % (c, 'clean' if c not in [b[0] for b in bad] else 'HAS STRAY INK'))
ok = not bad; print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
