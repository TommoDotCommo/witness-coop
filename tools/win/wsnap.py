# usage: wsnap.py <pid> <out.pkl>   snapshot bytes +0x20..+0x400 of every entity (read-only); diff with wdiff.py
import os, sys, pickle
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); snap = {}
for i, e in enumerate(p.entity_table()):
    if e:
        r = p.rd(e + 0x20, 0x400 - 0x20)
        if r: snap[i] = (p.type_of(e), r)
pickle.dump(snap, open(sys.argv[2], 'wb')); print('entities', len(snap))
