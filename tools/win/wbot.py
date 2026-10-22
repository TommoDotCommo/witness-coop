# Bot for the first puzzle, run with Windows Python: usage  wbot.py <pid> <game folder>
# Everything goes through the proxy inside the game (witness_cmd.txt: tp, button, mouse, shot), so no window needs the focus,
# no desktop input is used and several bots can run at once. State is read from the game's memory.
import os, sys, time, math, struct
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir = int(sys.argv[1]), sys.argv[2]
p = attach(pid); CMD = os.path.join(gdir, 'witness_cmd.txt')

def send(lines, settle=0.0):
    """Hand commands to the proxy (it polls every 250 ms and takes the whole file at once)."""
    t = time.time()
    while os.path.exists(CMD) and time.time() - t < 8: time.sleep(0.05)
    tmp = CMD + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, CMD)
    t = time.time()
    while os.path.exists(CMD) and time.time() - t < 8: time.sleep(0.05)      # taken
    time.sleep(settle)
def click(hold=0.15, settle=0.3): send(['button L down'], hold); send(['button L up'], settle)
def solved(e): return p.u32(e + 0x294) == 0x3F800000 and p.u32(e + 0x298) == 1

e = p.entity(0x64); px, py, pz = struct.unpack('<3f', p.rd(e + 0x24, 12))
sx, sy, sz = px - 0.15, py + 3.34, 3.727                       # where the owner stood for the first hand solve: 3.34 m in front
yaw = math.atan2(py - sy, px - sx); pitch = math.atan2(pz - sz, math.hypot(px - sx, py - sy))
d = math.dist((sx, sy), (px, py))
print('teleport to (%.2f, %.2f, %.2f) facing panel 0x64 (yaw %.3f pitch %.3f)' % (sx, sy, sz, yaw, pitch), flush=True)
send(['tp %.4f %.4f %.4f %.5f %.5f' % (sx, sy, sz, yaw, pitch)], 1.5)
click(settle=2.5)                                               # enter the puzzle: the cursor appears at the screen centre
px_left = 0.218 * 624 / d                                       # the start circle is 0.218 m left of the panel centre; 624 px per radian
n = int(round(px_left * 3.6 / 18)); print('cursor to the start circle: %d x 18 counts left' % n, flush=True)
send(['mouse -18 0'] * n, 1.0)
click(settle=0.5)                                               # press and release on the circle: the line starts
send(['mouse 15 0'] * 30, 2.0)                                  # drag along the line to the end bump
click(hold=0.12, settle=3.0)                                    # click to finish
send(['shot solved_by_bot'], 1.0)
print('panel 0x64 solved:', solved(e), '| position now', struct.unpack('<3f', p.rd(e + 0x24, 12)), flush=True)
sys.exit(0 if solved(e) else 1)
