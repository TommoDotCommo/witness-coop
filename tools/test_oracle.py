#!/usr/bin/env python3
"""The collision oracle (proxy/oracle.cpp) against geometry the walking bot already knows, as recorded from a live game; game-free.

usage: python3 tools/test_oracle.py            replays tools/testdata/oracle_recorded.json through the checks, then the mutants (each must fail)
       python3 tools/oracle_live.py check      records the same battery from a running game (and runs these checks on it)
Known geometry: the floor the player stands on (the game's own feet position: the ground query must give it, a ray straight down from the eye must
meet it at the eye height and report an upward normal), and the wall in front of a panel (the bot's walk goal is 3 m straight out from the panel
face: a ray from there at eye height toward the panel meets a surface about 3 m away, facing the player; the ray the other way does not)."""
import copy, json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__)); RECORDED = os.path.join(HERE, 'testdata', 'oracle_recorded.json')
NUM = r'(-?[\d.]+(?:e-?\d+)?)'


def parse_where(s):
    m = re.match(r'where feet=%s,%s,%s cam=%s,%s,%s' % ((NUM,) * 6), s)
    v = list(map(float, m.groups())); return dict(feet=v[:3], cam=v[3:])


def parse_ground(s):
    m = re.match(r'ground z=%s type=(\S*)' % NUM, s)
    return dict(z=float(m.group(1)), type=m.group(2)) if m else None


def parse_ray(s):
    m = re.match(r'ray hit dist=%s at=%s,%s,%s n=%s,%s,%s type=(\S*)' % ((NUM,) * 7), s)
    if not m: return None
    v = list(map(float, m.groups()[:7])); return dict(dist=v[0], at=v[1:4], n=v[4:7], type=m.group(8))


def verify(rec):
    """-> list of failure texts (empty: the oracle agrees with the known geometry)"""
    bad = []; st = rec['stand']; w = parse_where(st['where']); F, C = w['feet'], w['cam']
    g = parse_ground(st['ground'])
    if not g or abs(g['z'] - F[2]) > 0.03: bad.append('ground under the standing player: %s, the player stands at z=%.3f' % (st['ground'], F[2]))
    d = parse_ray(st['down'])
    if not d: bad.append('a ray straight down from the eye missed the floor')
    else:
        if abs(d['at'][2] - F[2]) > 0.03: bad.append('the ray down meets the floor at z=%.3f, the player stands at %.3f' % (d['at'][2], F[2]))
        if abs(d['dist'] - (C[2] - F[2])) > 0.03: bad.append('the ray down: distance %.3f, the eye is %.3f above the feet' % (d['dist'], C[2] - F[2]))
        if d['n'][2] < 0.9: bad.append('the floor normal is not up: %s' % d['n'])
    if g and d and abs(g['z'] - d['at'][2]) > 0.03: bad.append('ground query and ray down disagree: %.3f against %.3f' % (g['z'], d['at'][2]))
    if rec['sky'].startswith('ray hit') and parse_ray(rec['sky']) and parse_ray(rec['sky'])['dist'] < 20: bad.append('a ray up from open ground hit something at %s' % rec['sky'])
    for wl in rec['walls']:
        tw, aw = parse_ray(wl['toward']), parse_ray(wl['away'])
        dx, dy = wl['ppos'][0] - wl['spot'][0], wl['ppos'][1] - wl['spot'][1]; L = math.hypot(dx, dy); dx, dy = dx / L, dy / L
        if not tw: bad.append('panel %s: the ray toward the panel hit nothing' % wl['panel']); continue
        if not 2.4 <= tw['dist'] <= 3.3: bad.append('panel %s: the surface in front of the panel is %.2f m away, expected about 3' % (wl['panel'], tw['dist']))
        if abs(tw['n'][2]) > 0.5: bad.append('panel %s: the wall normal is not horizontal: %s' % (wl['panel'], tw['n']))
        if tw['n'][0] * dx + tw['n'][1] * dy > -0.3: bad.append('panel %s: the wall does not face the player: n=%s toward (%.2f, %.2f)' % (wl['panel'], tw['n'], dx, dy))
        if aw and aw['dist'] < 3.4 and abs(aw['n'][2]) < 0.5: bad.append('panel %s: a second wall only %.2f m behind the player (the check needs open space)' % (wl['panel'], aw['dist']))
    return bad


def mutants(rec):
    def m_ground(r): r = copy.deepcopy(r); r['stand']['ground'] = re.sub(r'z=[-\d.]+', lambda m: 'z=%.4f' % (float(m.group(0)[2:]) + 0.5), r['stand']['ground']); return r
    def m_down_normal(r): r = copy.deepcopy(r); r['stand']['down'] = re.sub(r'n=([-\d.]+),([-\d.]+),([-\d.]+)', lambda m: 'n=%s,%s,%.3f' % (m.group(1), m.group(2), -float(m.group(3))), r['stand']['down']); return r
    def m_down_dist(r): r = copy.deepcopy(r); r['stand']['down'] = re.sub(r'dist=([-\d.]+)', lambda m: 'dist=%.4f' % (float(m.group(1)) * 0.5), r['stand']['down'], 1); return r
    def m_wall_miss(r): r = copy.deepcopy(r); r['walls'][0]['toward'] = 'ray miss cand=3'; return r
    def m_wall_far(r): r = copy.deepcopy(r); r['walls'][0]['toward'] = re.sub(r'dist=([-\d.]+)', 'dist=9.0000', r['walls'][0]['toward']); return r
    def m_wall_flip(r):
        r = copy.deepcopy(r); r['walls'][0]['toward'] = re.sub(r'n=([-\d.]+),([-\d.]+),([-\d.]+)', lambda m: 'n=%.3f,%.3f,%s' % (-float(m.group(1)), -float(m.group(2)), m.group(3)), r['walls'][0]['toward']); return r
    def m_sky(r): r = copy.deepcopy(r); r['sky'] = 'ray hit dist=2.0000 at=0,0,0 n=0,0,-1 type=Inanimate ent=0x1 cand=1'; return r
    return [('ground query off by 0.5 m', m_ground), ('floor normal pointing down', m_down_normal), ('ray distance halved', m_down_dist),
            ('the wall in front of a panel missed', m_wall_miss), ('the wall 9 m away', m_wall_far), ('the wall normal flipped', m_wall_flip), ('sky is solid', m_sky)]


def check_finish(dilate=1):
    """tools/oracle_map.py finish(): water cells and their neighbours lose their ground, others keep it. -> list of failures"""
    import struct; sys.path.insert(0, HERE); import oracle_map
    R = struct.Struct('<hhfQB'); cells = {(x, y): (5.0, 1, 0) for x in range(10) for y in range(10)}; cells[(4, 4)] = (4.0, 1, oracle_map.WATER)
    out, wet = oracle_map.finish(b''.join(R.pack(x, y, *v) for (x, y), v in cells.items()), dilate)
    got = {(cx, cy): (z, fl) for cx, cy, z, m, fl in R.iter_unpack(out)}; bad = []
    if wet != 1: bad.append('one water cell counted as %d' % wet)
    for c in [(4, 4), (3, 3), (5, 5), (4, 5)]:
        if got[c][0] > -50: bad.append('cell %s next to the water keeps its ground' % (c,))
    for c in [(2, 2), (6, 6), (0, 9)]:
        if got[c][0] != 5.0: bad.append('dry cell %s lost its ground' % (c,))
    if any(fl & oracle_map.WATER for z, fl in got.values()): bad.append('the water flag is left in the planner records')
    return bad


def main():
    rec = json.load(open(RECORDED)); bad = verify(rec); ok = not bad
    f = check_finish(); ok &= not f; print('water cells and the shore buffer: %s' % ('ok' if not f else 'FAILED')); [print('  ' + b) for b in f]
    c = bool(check_finish(0)); ok &= c; print('mutant %-40s %s' % ('no shore buffer', 'caught' if c else 'NOT CAUGHT'))
    print('recorded geometry: %s' % ('ok' if not bad else 'FAILED'))
    for b in bad: print('  ' + b)
    for name, f in mutants(rec):
        caught = bool(verify(f(rec))); ok &= caught; print('mutant %-40s %s' % (name, 'caught' if caught else 'NOT CAUGHT'))
    return 0 if ok else 1


if __name__ == '__main__': sys.exit(main())
