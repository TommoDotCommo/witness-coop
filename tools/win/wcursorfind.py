# Finds the game's puzzle cursor (its position in a panel, in panel units) in the game's static memory. Run with Windows Python, read-only,
# while a line is held part drawn on a panel (wdraw.py with WDRAW_HOLD_FRACTION): it moves the mouse right and back, then down and back
# (in-process input, the command file) and snapshots the writable pages of the image after each move. A float is an x candidate when it moved
# with the right move, came back with the left one and stayed put for the vertical moves; a y candidate the other way round.
# usage: wcursorfind.py <pid> <game dir> [counts, default 120]
import os, sys, time, ctypes, ctypes.wintypes as wt
from array import array
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir = int(sys.argv[1]), sys.argv[2]; N = int(sys.argv[3]) if len(sys.argv) > 3 else 120
P = Proc(pid)
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', wt.DWORD), ('PartitionId', wt.WORD), ('RegionSize', ctypes.c_size_t),
                ('State', wt.DWORD), ('Protect', wt.DWORD), ('Type', wt.DWORD)]
k.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
def regions(heap):
    out = []; a = int(P.base) if not heap else 0x10000; end = a + 0x1000000 if not heap else 0x7fffffff0000; total = 0
    while a < end:
        m = MBI()
        if not k.VirtualQueryEx(P.h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)): break
        ok = m.State == 0x1000 and m.Protect in (0x04, 0x08, 0x40, 0x80) and (not heap or (m.Type == 0x20000 and m.RegionSize <= 0x4000000))
        if ok and total < 1500 * 2 ** 20: out.append((int(m.BaseAddress), int(m.RegionSize))); total += int(m.RegionSize)
        a = int(m.BaseAddress) + int(m.RegionSize)
    return out
HEAP = '--heap' in sys.argv
R = regions(HEAP); print('regions', len(R), 'MB', sum(n for _, n in R) >> 20, flush=True)
def snap(): return [array('f', P.rd(a, n) or bytes(n)) for a, n in R]
def move(dx, dy, chunk=10):
    n = max(1, max(abs(dx), abs(dy)) // chunk); lines = ['mouse %d %d' % (dx // n, dy // n)] * n
    cmd = os.path.join(gdir, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 5: time.sleep(0.02)
    open(cmd + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmd + '.tmp', cmd)
    time.sleep(0.05 + n * 0.03 + 0.5)
s0 = snap(); move(N, 0); s1 = snap(); move(-N, 0); s2 = snap(); move(0, N); s3 = snap(); move(0, -N); s4 = snap()
def near(a, b): return abs(a - b) <= 1e-4 * max(1.0, abs(a))
xs, ys = [], []
for ri, (a, n) in enumerate(R):
    A, B, C, D, E = s0[ri], s1[ri], s2[ri], s3[ri], s4[ri]
    for i in range(len(A)):
        a0 = A[i]
        if a0 != a0 or abs(a0) > 1e6: continue
        if not near(a0, C[i]) or not near(a0, E[i]): continue
        if abs(B[i] - a0) > 1e-5 and near(a0, D[i]): xs.append((a + 4 * i, a0, B[i]))
        if abs(D[i] - a0) > 1e-5 and near(a0, B[i]): ys.append((a + 4 * i, a0, D[i]))
base = int(P.base)
def show(v): return [('exe+%#x' % (ad - base) if base <= ad < base + 0x1000000 else hex(ad), round(x0, 4), round(x1, 4)) for ad, x0, x1 in v[:40]]
print('x candidates', len(xs), show(xs)); print('y candidates', len(ys), show(ys))
