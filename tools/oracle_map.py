#!/usr/bin/env python3
"""Build a walking map (tools/win/wnav.py format) from the game's own collision, with the proxy's oracle (proxy/oracle.cpp). Needs a running game
(from a lab lease: eval "$(lab env witness)"; python3 tools/oracle_live.py start SAVE) and its folder in WITNESS_DIR1.

usage: python3 tools/oracle_map.py X0 Y0 X1 Y1 OUT.bin [--tile 50] [--wait 3.5] [--keep DIR]     (metres, the rectangle to map)
The collision exists only near the player (about 250 m for the ground, less for the small things), so the player is held (the proxy's `hover`) at
the middle of each tile of the rectangle, in turn, nearest first, and the tile's cells are scanned (the `scan` command: per 0.5 m cell the
surfaces of its column and what stands in the way of a body on each, see proxy/core/oracle_cell.h). The tile files go to the game folder
(witness_scan_<id>.bin) and are joined in OUT.bin. The player is put back where it stood. A tile whose hover failed (the game moved the player to
the spawn: no collision there yet) is tried again with a longer wait, then scanned from where the player is, and counted as `far`."""
import math, os, sys, time, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from oracle_live import Oracle

CELL = 0.5


def feet_of(o):
    w = o.batch(['where'])[0]
    return [float(v) for v in w.split('feet=')[1].split()[0].split(',')], [float(v) for v in w.split('yaw=')[1].split()[0:1]] + [float(w.split('pitch=')[1].split()[0])]


def ground_near(o, x, y, tries=((0, 0), (4, 0), (-4, 0), (0, 4), (0, -4), (8, 8), (-8, -8), (8, -8), (-8, 8))):
    """The first ground found at or near (x, y): (x, y, z) or None (sea, rock face, or no collision loaded there)."""
    res = o.batch(['ground %.2f %.2f 250 310' % (x + dx, y + dy) for dx, dy in tries])
    for (dx, dy), r in zip(tries, res):
        if r.startswith('ground z='): return x + dx, y + dy, float(r.split()[1][2:])
    return None


def build(rect, out, tile=50.0, wait=3.5, game_dir=None, log=print):
    d = game_dir or os.environ['WITNESS_DIR1']; o = Oracle(d); x0, y0, x1, y1 = rect
    home, ang = feet_of(o)
    tiles = [(x0 + (i + .5) * tile, y0 + (j + .5) * tile, i, j) for i in range(int(math.ceil((x1 - x0) / tile))) for j in range(int(math.ceil((y1 - y0) / tile)))]
    cur = (home[0], home[1]); done = []; stats = dict(tiles=0, hovered=0, far=0, cells=0, with_ground=0); t0 = time.time()
    while tiles:
        tiles.sort(key=lambda t: math.hypot(t[0] - cur[0], t[1] - cur[1])); cx, cy, i, j = tiles.pop(0)
        g = ground_near(o, cx, cy)
        hovered = False
        if g:
            for attempt, w in enumerate((wait, wait * 2)):
                o.send('hover %.2f %.2f %.2f 0 0' % (g[0], g[1], g[2] + 1.69 + 0.05)); time.sleep(w)
                f, _ = feet_of(o)
                if math.hypot(f[0] - g[0], f[1] - g[1]) < 3.0 and abs(f[2] - g[2]) < 2.5: hovered = True; cur = (g[0], g[1]); break
        stats['hovered'] += hovered; stats['far'] += (not hovered)
        cx0, cy0 = int(math.floor((x0 + i * tile) / CELL)), int(math.floor((y0 + j * tile) / CELL)); n = int(round(tile / CELL))
        sid = 'tile_%d_%d' % (i, j); fn = os.path.join(d, 'witness_scan_%s.bin' % sid)
        if os.path.exists(fn): os.remove(fn)
        o.send('scan %s %d %d %d %d' % (sid, cx0, cy0, n, n)); dl = time.time() + 600
        while not os.path.exists(fn):
            if time.time() > dl: raise RuntimeError('scan %s did not finish' % sid)
            time.sleep(.2)
        time.sleep(.2); data = open(fn, 'rb').read(); done.append(data)
        recs = len(data) // 17; wg = sum(struct.unpack_from('<f', data, 17 * k + 4)[0] > -50 for k in range(recs))
        stats['tiles'] += 1; stats['cells'] += recs; stats['with_ground'] += wg
        log('tile %d,%d at %.0f,%.0f: %s, %d cells, %d with ground  (%.0f s)' % (i, j, cx, cy, 'held' if hovered else ('FAR (no ground)' if not g else 'FAR (hover failed)'), recs, wg, time.time() - t0))
    with open(out, 'wb') as f:
        for data in done: f.write(data)
    o.send('hover off'); time.sleep(.3); o.send('hover %.2f %.2f %.2f %.3f %.3f' % (home[0], home[1], home[2] + 1.69 + 0.05, ang[0], ang[1])); time.sleep(2); o.send('hover off')
    return stats


if __name__ == '__main__':
    a = sys.argv[1:]; kw = {}
    for k in ('--tile', '--wait'):
        if k in a: i = a.index(k); kw[k[2:]] = float(a[i + 1]); del a[i:i + 2]
    st = build(tuple(map(float, a[:4])), a[4], **kw); print(st)
