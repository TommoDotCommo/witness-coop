# Probe: samples the trace records of one panel in a running game about 20 times a second and prints how they change (count, the 13 words of the last record as floats), for <seconds>.
# usage: wtracewatch.py <pid> <panel id hex> <seconds>
import struct, sys, time, os
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, eid, secs = int(sys.argv[1]), int(sys.argv[2], 16), float(sys.argv[3])
p = attach(pid); e = p.entity(eid); t0 = time.time(); last = None
px, py, pz = struct.unpack('<3f', p.rd(e + 0x24, 12)); lastpos = None
print('panel at %.3f %.3f %.3f' % (px, py, pz), flush=True)
while time.time() - t0 < secs:
    n = p.u32(e + 0x228); ptr = p.u64(e + 0x230); recs = []
    if n and ptr and n < 600:
        raw = p.rd(ptr, n * 52)
        if raw: recs = [struct.unpack('<13I', raw[i * 52:(i + 1) * 52]) for i in range(n)]
    cur = struct.unpack('<3f', p.rd(e + 0x24, 12)); quat = struct.unpack('<4f', p.rd(e + 0x34, 16))
    solved = p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
    desc = (n, solved, tuple(recs[-1]) if recs else None, tuple(round(v, 2) for v in cur))
    if desc != last:
        last = desc; s = ''
        if recs:
            r = recs[-1]; f = lambda w: struct.unpack('<f', struct.pack('<I', w))[0]
            s = 'nodes %d->%d  w2-5 %s  start(%.3f %.3f %.3f) end(%.3f %.3f %.3f)  w12 %d' % (r[0], r[1], [('%.3f' % f(w)) if 1e-6 < abs(f(w)) < 1e6 else w for w in r[2:6]], *[v - c for v, c in zip((f(r[6]), f(r[7]), f(r[8])), (px, py, pz))], *[v - c for v, c in zip((f(r[9]), f(r[10]), f(r[11])), (px, py, pz))], r[12])
        w = ''
        if recs: r = recs[-1]; f = lambda x: struct.unpack('<f', struct.pack('<I', x))[0]; w = ' WORLD start(%.3f %.3f) end(%.3f %.3f)' % (f(r[6]), f(r[7]), f(r[9]), f(r[10]))
        print('%6.2fs panel(%.3f %.3f %.3f) quat(%.3f %.3f %.3f %.3f) n=%d solved=%d%s' % (time.time() - t0, *cur, *quat, n, solved, w), flush=True)
    time.sleep(0.05)
