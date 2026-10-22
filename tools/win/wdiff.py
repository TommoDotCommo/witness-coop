# usage: wdiff.py <before.pkl> <before2.pkl> <after.pkl> [max]
# Words that changed between before2 and after, ignoring words that already changed between before and before2 (idle noise).
import sys, pickle, struct
a, b, c = (pickle.load(open(x, 'rb')) for x in sys.argv[1:4])
def words(x, y):
    out = {}
    for eid, (vt, rx) in x.items():
        if eid not in y or rx == y[eid][1]: continue
        ry = y[eid][1]
        for o in range(0, len(rx), 4):
            if rx[o:o+4] != ry[o:o+4]: out[(eid, 0x20 + o)] = (struct.unpack('<I', rx[o:o+4])[0], struct.unpack('<I', ry[o:o+4])[0])
    return out
noise = set(words(a, b)); real = {kk: v for kk, v in words(b, c).items() if kk not in noise}
by = {}
for (eid, off), (o, n) in real.items(): by.setdefault(eid, []).append((off, o, n))
fl = lambda w: struct.unpack('<f', struct.pack('<I', w))[0]
print('real changes: %d words in %d entities (noise ignored: %d words)' % (len(real), len(by), len(noise)))
for eid, ch in sorted(by.items(), key=lambda kv: (len(kv[1]), kv[0]))[:int(sys.argv[4]) if len(sys.argv) > 4 else 40]:
    print('id %#7x type %#x : %s' % (eid, c[eid][0], '  '.join('+%#x %08x->%08x (%.4g->%.4g)' % (o_, a_, b_, fl(a_), fl(b_)) for o_, a_, b_ in sorted(ch)[:8])))
