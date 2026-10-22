# usage: wnames.py <pid>   entity types: descriptor, count and name (name via descriptor+0x08), read-only
import os, sys, collections
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); tbl = p.entity_table(); groups = collections.defaultdict(list)
for i, e in enumerate(tbl):
    if e: groups[p.type_of(e)].append(i)
def name(e):
    ti = p.u64(e + 8); np_ = p.u64(ti + 8)
    r = p.rd(np_ if np_ and np_ > 0x10000 else ti + 8, 40); return r.split(b'\0')[0].decode('latin1')
for t, ids in sorted(groups.items(), key=lambda kv: -len(kv[1])):
    print('descriptor %#x  n=%5d  %-22s e.g. %#x' % (p.u64(tbl[ids[0]] + 8) - p.base, len(ids), name(tbl[ids[0]]), ids[0]))
