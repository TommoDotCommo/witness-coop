#!/usr/bin/env python3
"""Builds proxy/progress/progression.txt: the natural order of The Witness' puzzles, for the new-game option "start with these parts of the map completed".

The input is the logic file of the Archipelago randomizer for The Witness (worlds/witness/data/WitnessLogic.txt, MIT licence, https://github.com/ArchipelagoMW/Archipelago):
it lists every panel with the game's own entity id, its area, the panels that have to be solved before it can be (its requirement), the doors with the panels that open them, the
lasers, and the regions of the island with the panels or doors that connect them. For every puzzle panel this tool works out the full set of panels that have to be solved before it
(the cheapest way in when there are several), so completing any part of the map completes exactly what the game needs to reach it, and nothing else.

usage: gen_progression.py WitnessLogic.txt out.txt [out.cpp]      (out.cpp: the same text as a C++ string, for the proxy and the launcher)
Output lines:  area <n> <name>   |   panel <hex id> <area n> <name with spaces as _> <prerequisite hex ids...>   |   laser <hex id of the panel that lights it> <area n>
"""
import re, sys

LASERS_NEEDED = 7        # the mountain opens with this many lasers lit (the game's own rule)
def parse(path):
    areas, regions, panels, doors, lasers, edges = [], {}, {}, {}, {}, []
    area = None; region = None
    for raw in open(path, encoding='utf-8'):
        line = raw.rstrip('\n')
        if not line.strip(): continue
        m = re.match(r'^==(.+)==$', line)
        if m: area = m[1]; areas.append(area); continue
        m = re.match(r'^Door - (0x[0-9A-Fa-f]+) \((.+?)\) - (.+)$', line)
        if m: doors[int(m[1], 16)] = m[3]; continue
        m = re.match(r'^Laser - (0x[0-9A-Fa-f]+) \(Laser\) - (.+)$', line)
        if m: lasers[int(m[1], 16)] = m[2]; continue
        m = re.match(r'^(\d+) - (0x[0-9A-Fa-f]+) \((.+?)\) - (.+?) - (.+)$', line)
        if m:
            pid, name, req = int(m[2], 16), m[3], m[4]
            if name.endswith('EP') or name == 'Obelisk' or name.startswith('Obelisk Side') or pid >= 0xFF000 or 'Reached' in name or 'Laser Activated' in name or 'Pet the Dog' in name: continue
            if m[5].strip() == 'True' and name in ('Gate EP',): continue
            panels[pid] = dict(id=pid, area=area, name=name, req=req, region=region); continue
        m = re.match(r'^(.+?) \((.+?)\)((?: - .+?)*):$', line)
        if m:
            region = m[1]; regions[region] = dict(name=region, area=m[2], section=area)
            parts = m[3].split(' - ')[1:]
            for i in range(0, len(parts) - 1, 2): edges.append((region, parts[i], parts[i + 1]))
    return areas, regions, panels, doors, lasers, edges

def main():
    src, out = sys.argv[1], sys.argv[2]
    areas, regions, panels, doors, lasers, edges = parse(src)
    # a connection works both ways with the same requirement, unless it is marked TrueOneWay (this is how the randomizer reads the file)
    both = []
    for a, b, e in edges:
        both.append((a, b, e))
        if e.strip() != 'TrueOneWay': both.append((b, a, e))
    laser_panels = sorted({int(re.search(r'0x[0-9A-Fa-f]+', e)[0], 16) for e in lasers.values()})
    bad = set(); acc = {'Entry': frozenset()}      # acc: the cheapest panels to get into a region, settled one region at a time (cheapest first), so no way in depends on a panel that is only reachable through it
    def prereq(i, stack):      # the panels (the panel itself included) needed to solve panel i; None: not possible yet
        p = panels[i]; r = acc.get(p['region'])
        if r is None: return None
        e = expr_set(p['req'], stack + (i,))
        return None if e is None else frozenset({i} | e | r)
    def term_set(t, stack):
        if t in ('True', 'TrueOneWay') or t.startswith('Theater to'): return frozenset()
        m = re.fullmatch(r'(\d+) Lasers( \+ Redirect)?', t)
        if t == 'Lasers' or m:
            n = LASERS_NEEDED if t == 'Lasers' else int(m[1]); s = lit(n)
            if s is None: return None
            if m and m[2]:
                r = term_set('0x09F98', stack)
                if r is None: return None
                s = s | r
            return s
        if re.fullmatch(r'0x[0-9A-Fa-f]+', t):
            i = int(t, 16)
            if i in doors: return expr_set(doors[i], stack + (i,)) if i not in stack else frozenset()
            if i in panels: return prereq(i, stack) if i not in stack else frozenset()
            return frozenset()      # an environmental pattern, an event: nothing to solve
        if t in regions: return acc.get(t)      # a region as a term: being able to get there
        bad.add(t); return frozenset()
    def expr_set(expr, stack=()):
        best = None
        for alt in expr.split('|'):
            parts = [term_set(t.strip(), stack) for t in alt.split('&')]
            if any(p is None for p in parts): continue
            s = frozenset().union(*parts)
            if best is None or len(s) < len(best): best = s
        return best
    lit_cache = {}
    def lit(n):      # the panels needed to light n lasers: the cheapest ones, one after the other (None: not that many reachable yet)
        n = min(n, len(laser_panels))
        key = (n, len(acc))
        if key in lit_cache: return lit_cache[key]
        chosen = frozenset(); pool = [p for p in laser_panels if p in panels]
        for _ in range(n):
            cands = [(len(chosen | c), p, c) for p in pool for c in [prereq(p, ())] if c is not None]
            if not cands: lit_cache[key] = None; return None
            _, best, c = min(cands, key=lambda x: (x[0], x[1])); chosen = chosen | c; pool.remove(best)
        lit_cache[key] = chosen; return chosen
    while True:      # Dijkstra over regions by the size of what they need
        best = None
        for a, b, e in both:
            if a not in acc or b in acc: continue
            x = expr_set(e)
            if x is None: continue
            cand = acc[a] | x
            if best is None or len(cand) < len(best[1]) or (len(cand) == len(best[1]) and b < best[0]): best = (b, cand)
        if best is None: break
        acc[best[0]] = best[1]
    res = {i: (prereq(i, ()) or frozenset({i})) - {i} for i in panels}
    optional = [i for i in panels if panels[i]['name'] == 'Gate Close']      # the tutorial's closing panel waits for the whole island: not part of finishing the area
    for _ in range(30):      # the full chain: the prerequisites of a prerequisite are prerequisites too
        grown = False
        for i in panels:
            s = set(res[i])
            for q in res[i]: s |= res[q] if q in res else set()
            s.discard(i)
            if len(s) > len(res[i]): res[i] = s; grown = True
        if not grown: break
    area_ix = {a: n for n, a in enumerate(areas)}
    with open(out, 'w') as f:
        f.write('# generated by tools/gen_progression.py from the Archipelago randomizer logic (MIT): do not edit\n')
        for n, a in enumerate(areas): f.write('area %d %s\n' % (n, a.replace(' ', '_')))
        for i in sorted(panels):
            p = panels[i]; f.write('panel %X %d %s %s\n' % (i, area_ix[p['area']], p['name'].replace(' ', '_'), ' '.join('%X' % x for x in sorted(res[i]))))
        for i in optional: f.write('optional %X\n' % i)
        for e in sorted(laser_panels):
            if e in panels: f.write('laser %X %d\n' % (e, area_ix[panels[e]['area']]))
    if len(sys.argv) > 3:
        with open(sys.argv[3], 'w') as g:
            g.write('// generated by tools/gen_progression.py: do not edit\n#include "progress.h"\nnamespace wprog {\nconst char* BuiltinText() { return R"PROG(' + open(out).read() + ')PROG"; }\n}\n')
    print('areas %d panels %d lasers %d, unknown terms %s' % (len(areas), len(panels), len(laser_panels), sorted(bad)))
    print('most prerequisites: %d; unreached regions: %s' % (max(len(s) for s in res.values()), sorted(r for r in regions if r not in acc)))

main()
