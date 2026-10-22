# usage: wfps.py <pid> [seconds]   frames per second of a running game (global frame counter, build 11370412), read-only
import os, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5
a = p.u32(p.base + 0x63954C); t = time.time(); time.sleep(secs); b = p.u32(p.base + 0x63954C)
print('%.1f' % ((b - a) / (time.time() - t)))
