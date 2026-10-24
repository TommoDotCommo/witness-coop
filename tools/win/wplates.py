"""Which panels are floor puzzles: count the pressure plates that list each panel (+0xe4 of a plate is its panel's engine id).
Usage: Windows Python wplates.py <pid> <out json file>   (output: {panel table index: number of plates})"""
import os, sys, json, collections
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); base_id = p.u32(p.u64(p.base + GLOBALS) + 8); cnt = collections.Counter()
tab = p.entity_table()
for i, e in enumerate(tab):
    if not e: continue
    d = p.u64(e + 8); np_ = p.u64(d + 8) if d else 0; raw = p.rd(np_, 16) if np_ else None
    if raw and raw.startswith(b'Pressure_Plate'):
        t = p.u32(e + 0xe4) - base_id
        if 0 <= t < len(tab) and tab[t]: cnt[t] += 1
json.dump({str(k): v for k, v in cnt.items()}, open(sys.argv[2], 'w')); print(len(cnt), 'panels have plates;', sum(cnt.values()), 'plates')
