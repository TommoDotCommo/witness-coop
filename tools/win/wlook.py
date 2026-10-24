# Puts B in front of A, facing A, and takes a screenshot on A. usage: wlook.py <pidA> <dirA> <pidB> <dirB> <metres> [shotname] [lateral metres]
import os, sys, time, math, struct
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]; dist = float(sys.argv[5]); name = sys.argv[6] if len(sys.argv) > 6 else 'look'
lat = float(sys.argv[7]) if len(sys.argv) > 7 else 0.0
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
(cx, cy, cz), (th, ph) = A.camera()
fx, fy = math.cos(th), math.sin(th); rx, ry = math.sin(th), -math.cos(th)
bx, by = cx + fx * dist + rx * lat, cy + fy * dist + ry * lat
print('A camera', cx, cy, cz, th, ph, '-> B at', bx, by, cz)
send(db, ['tp %.4f %.4f %.4f %.5f %.5f' % (bx, by, cz - 1.69, th + math.pi, 0.0)], 2.0)
send(da, ['shot ' + name], 1.5)
(bx2, by2, bz2), _ = B.camera(); print('B camera now', bx2, by2, bz2)
