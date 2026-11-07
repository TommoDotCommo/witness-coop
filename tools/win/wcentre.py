# T1 oracle: text is centred in its button. Opens the emote wheel, renders the UI offscreen (uishot) at 1920x1080 and 2560x1440, and
# measures, in each of the eight buttons, the first letter of the label (a capital) against the button's middle. Before 2026-11-06 the
# capitals sat 4 px low at scale 1 in every box of the mod (twin: ui_cap_shift_off=1). usage: wcentre.py <dir of the game>
import math, os, struct, sys, time
d = sys.argv[1]
def send(lines, settle=0.4):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.03)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.03)
    time.sleep(settle)
def capture(w, h):
    bmp = os.path.join(d, 'shots', 'centre_check.bmp')
    if os.path.exists(bmp): os.remove(bmp)
    send(['uishot %d %d centre_check' % (w, h)], 0.3); last = -1; t = time.time()
    while time.time() - t < 15:
        size = os.path.getsize(bmp) if os.path.exists(bmp) else 0
        if size > 54 and size == last: break
        last = size; time.sleep(0.1)
    data = open(bmp, 'rb').read(); off = struct.unpack_from('<I', data, 10)[0]; bw, bh = struct.unpack_from('<ii', data, 18); bpp = struct.unpack_from('<H', data, 28)[0] // 8
    assert (bw, abs(bh)) == (w, h), ('capture size', bw, bh)
    stride = (w * bpp + 3) & ~3
    def lum(x, y):
        row = y if bh < 0 else h - 1 - y; i = off + row * stride + x * bpp
        return (data[i] + data[i + 1] + data[i + 2]) / 3
    return lum
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-70s %s  %s' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
send(['wheel open'], 0.6)
try:
    for W, H in [(1920, 1080), (2560, 1440)]:
        lum = capture(W, H); k = min(W / 1280.0, H / 900.0)      # social.cpp: the scale of the social UI
        R = 150 * k; bw, bh = 112 * k, 34 * k; offsets = []
        for i in range(8):      # the wheel's geometry (proxy/social.cpp): slot i at angle -90 + 45 i degrees, R from the middle
            a = -math.pi / 2 + i * math.pi / 4; px, py = W / 2 + math.cos(a) * R, H / 2 + math.sin(a) * R
            x0, x1, y0, y1 = int(px - bw / 2) + 2, int(px + bw / 2) - 2, int(py - bh / 2) + 2, int(py + bh / 2) - 2
            bg = lum(x0 + 1, y0 + 1); ink = lambda x, y: lum(x, y) > bg + 70
            cols = [x for x in range(x0, x1) if any(ink(x, y) for y in range(y0, y1))]
            if not cols: offsets.append(None); continue
            first = range(cols[0], cols[0] + int(9 * k))      # the capital: the label's first columns
            rows = [y for y in range(y0, y1) if any(ink(x, y) for x in first)]
            offsets.append((rows[0] + rows[-1] + 1) / 2 - py)
        check('%dx%d: every emote label has ink' % (W, H), None not in offsets, offsets)
        if None in offsets: continue
        worst = max(offsets, key=abs)
        check('%dx%d: capitals centred in their buttons (within 1.5 px)' % (W, H), abs(worst) <= 1.5, ' '.join('%+.1f' % o for o in offsets))
finally:
    send(['wheel close'])
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
