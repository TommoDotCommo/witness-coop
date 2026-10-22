# usage: wfocusflag.py <pid> <phase> <out.pkl>   dump the exe's .data section (read-only). Run it twice with the window focused and
# twice unfocused, then `wfocusflag.py --diff a1 a2 b1 b2` lists words that are stable within each state but differ across them.
import os, sys, pickle, struct
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
if sys.argv[1] == '--diff':
    a1, a2, b1, b2 = (pickle.load(open(x, 'rb')) for x in sys.argv[2:6]); out = []
    for off in a1:
        if off not in a2 or off not in b1 or off not in b2: continue
        x1, x2, y1, y2 = a1[off], a2[off], b1[off], b2[off]
        for i in range(0, len(x1), 4):
            if x1[i:i+4] == x2[i:i+4] and y1[i:i+4] == y2[i:i+4] and x1[i:i+4] != y1[i:i+4]:
                out.append((off + i, struct.unpack('<I', x1[i:i+4])[0], struct.unpack('<I', y1[i:i+4])[0]))
    print(len(out), 'words stable in each state and different across states')
    for o, a, b in out[:60]: print('  base+%#x  focused=%#x  unfocused=%#x' % (o, a, b))
else:
    p = attach(int(sys.argv[1])); lo, size = 0x61c000, 0x4083b60; d = {}
    for off in range(0, size, 0x100000):
        r = p.rd(p.base + lo + off, min(0x100000, size - off))
        if r: d[lo + off] = r
    pickle.dump(d, open(sys.argv[3], 'wb')); print('dumped', sum(len(v) for v in d.values()) // 2**20, 'MB')
