"""Survey the island for the walking map: for each leg, put the player on known ground (a teleport: it is a survey, not play) and walk from
there to the leg's target as the bot would (tools/win/wnav.py), looking as it goes. The walks are real; the map the walker learns is saved.

Usage: Windows Python wsurvey.py <pid> <game folder> <out.jsonl> <legs.json> [seconds budget]      (legs: tools/nav_coverage.py --legs)
Per leg one JSON line: leg, panel, result (walked / failed), why, metres, seconds, where it started and ended, whether the teleport landed.
A leg's walk ends at the target (within 1 m) or when the walker gives up; the player is looked round at the start and at the end (four
pictures, each only taken while the camera holds still). The cells walked are marked as stood on (the trail, every 0.5 m).
Nothing in the world is changed."""
import os, sys, json, math, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
import wnav
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))

pid, gdir, out, legs_file = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
budget = float(sys.argv[5]) if len(sys.argv) > 5 else 1e9
legs = json.load(open(legs_file)); p = attach(pid); c = Control(gdir); log = open(out, 'a')
walker = wnav.Walker(p, c, gdir, log=lambda m: print(m, file=sys.stderr, flush=True))
t_all = time.monotonic()


def tp(x, y, feet, yaw=0.0):
    """Put the player above the known ground and let it fall onto what is really there. The player is held there (hover) for a moment first:
    in an area whose collision is not loaded yet the game sends a free-falling player back to the spawn point, and the map's floors can be
    junk below the ground. True when the player stands still within 3 m of (x, y) afterwards."""
    c.send(['hover off'])
    for rise in (3.0, 8.0, 14.0):
        c.send(['hover %.4f %.4f %.4f %.5f 0.0' % (x, y, feet + rise, yaw)], 3.0)
        time.sleep(2.4)
        c.send(['hover off'], 0.5); time.sleep(1.5)
        q = p.camera()[0]
        if math.dist(q[:2], (x, y)) < 3.0:
            time.sleep(1.0); q2 = p.camera()[0]
            if abs(q2[2] - q[2]) < 0.05 and math.dist(q2[:2], (x, y)) < 3.0: return True      # (fallen and at rest)
    return False


for i, leg in enumerate(legs):
    if time.monotonic() - t_all > budget: break
    f, t = leg.get('from'), leg['to']
    rec = {'leg': i, 'panel': '%#x' % leg['panel'], 'to': t}
    if not f: rec.update(result='failed', why='no known ground near it'); log.write(json.dumps(rec) + '\n'); log.flush(); continue
    landed = tp(f[0], f[1], f[2], math.atan2(t[1] - f[1], t[0] - f[0]))
    rec['landed'] = landed; start = p.camera()[0]
    if not landed:
        rec.update(result='failed', why='the teleport did not land', end=[round(v, 2) for v in start]); log.write(json.dumps(rec) + '\n'); log.flush(); continue
    walker.walked = 0.0; walker.stuck = 0; walker.replans = 0; walker.looks = 0; walker.avoid = set(); walker.stuck_at = []; walker.backouts = 0; walker.sidesteps = 0
    walker.trail = []; walker.dear = set(); walker.cells = []
    d = math.dist(start[:2], t[:2])
    r = walker.go((t[0], t[1], t[2] + wnav.EYE), 1.0, min(200.0, 40.0 + 1.5 * d), ztol=1.8)
    end = p.camera()[0]; ok = r['ok']
    n = walker.map.walk_line(list(walker.trail)) if len(walker.trail) > 1 else 0      # (the trail: eye positions every 0.5 m, a real walk)
    walker.look_around()
    try: walker.map.save(walker.map_path)
    except OSError: pass
    rec.update(result='walked' if ok else 'failed', why=r['why'], metres=r['walked'], seconds=r['seconds'], stuck=r['stuck'], looks=r['looks'], blurred=r.get('blurred', 0),
               trail_cells=n, **{'from': [round(v, 2) for v in start]}, end=[round(v, 2) for v in end], cells=len(walker.map.cells))
    log.write(json.dumps(rec) + '\n'); log.flush(); print(json.dumps(rec), flush=True)
log.write(json.dumps({'done': True, 'seconds': round(time.monotonic() - t_all, 1)}) + '\n'); log.flush()
