"""Map the island for walking once, from the air, in a throwaway game (the playthrough itself only walks; owner's OK 2026-10-30).

Usage: Windows Python wsurvey.py <pid> <game folder> <out map> [part/parts, e.g. 1/2] [spacing m]
The player is held (`hover`) over points `spacing` m apart (16 by default) within 60 m of a panel (further out over the sea the game moves
the player back), and at each point takes eight depth pictures (`depthdump`): four from 25 m up looking down as far as the game allows (the
ground, roofs, the shape of the land) and four at the eye height of the nearest panel looking a little down (walls, fences, the ground under
trees). They go into the walking map of tools/win/wnav.py, saved to <out map> every 20 points; `part` splits the points between games
(each writes its own map; the orchestrator merges maps). Prints progress on stderr and one JSON line at the end."""
import os, sys, math, time, json, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
import wnav
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))

pid, gdir, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
part, parts = (int(v) for v in (sys.argv[4] if len(sys.argv) > 4 else '1/1').split('/'))
SPACING = float(sys.argv[5]) if len(sys.argv) > 5 else 16.0
p = attach(pid); c = Control(gdir); depth = os.path.join(gdir, 'witness_depth.bin')
wnav.RANGE = 60.0      # from the air the ground is 30-50 m away

panels = []
for e in p.entity_table():
    if e and p.type_of(e) == TI_PANEL: panels.append(struct.unpack('<3f', p.rd(e + 0x24, 12)))
xs = [q[0] for q in panels]; ys = [q[1] for q in panels]
points = []
y = min(ys) - 40
while y <= max(ys) + 40:
    x = min(xs) - 40
    while x <= max(xs) + 40:
        near = min(panels, key=lambda q: math.hypot(q[0] - x, q[1] - y))
        if math.hypot(near[0] - x, near[1] - y) < 60: points.append((x, y, near[2]))
        x += SPACING
    y += SPACING
points = points[part - 1::parts]
def done_before():      # points already done (<out map>.idx, written with every save): the game crashes after a thousand or so hovers, is started again and goes on
    try: return int(open(out + '.idx').read())
    except (OSError, ValueError): return 0
SKIP = int(os.environ.get('WSURVEY_SKIP') or done_before())
m = wnav.Map(); m.load(out)
print('%d panels, %d points for this part' % (len(panels), len(points)), file=sys.stderr, flush=True)


def look():
    try: before = os.path.getmtime(depth)
    except OSError: before = 0
    c.send(['depthdump']); t0 = time.monotonic()
    while time.monotonic() - t0 < 1.5:
        try:
            if os.path.getmtime(depth) != before:
                data = open(depth, 'rb').read()
                if len(data) >= 44: return m.integrate(wnav.depth_points(data, 1)[1])
        except (OSError, ValueError, struct.error): pass
        time.sleep(0.01)
    return 0


t0 = time.monotonic(); looks = moved_back = 0
for idx, (x, y, z) in enumerate(points):
    if idx < SKIP: continue
    for hz, pitch in ((z + 25.0, -0.93), (z, -0.3)):
        for yaw in (0.0, math.pi / 2, math.pi, -math.pi / 2):
            c.send(['hover %.3f %.3f %.3f %.4f %.3f' % (x, y, hz, yaw, pitch)], 0.3)
            cam = p.camera()[0]
            if math.hypot(cam[0] - x, cam[1] - y) > 5: moved_back += 1; continue      # the game put the player back somewhere: not this point's view
            looks += 1 if look() else 0
    if idx % 20 == 19 or idx == len(points) - 1:
        m.save(out); open(out + '.idx', 'w').write(str(idx + 1))
        print('%d/%d points, %d looks, %d cells, %.0f s' % (idx + 1, len(points), looks, len(m.cells), time.monotonic() - t0), file=sys.stderr, flush=True)
c.send(['hover off'])
print(json.dumps({'points': len(points), 'looks': looks, 'moved_back': moved_back, 'cells': len(m.cells), 'seconds': round(time.monotonic() - t0)}), flush=True)
