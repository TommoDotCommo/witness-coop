# Finds a flag in the game's static memory that follows a state the player toggles with a key (here: the pause menu, Esc). Run with Windows Python, read-only.
# Snapshots the writable pages of the game's image in state OFF, ON, OFF, ON (three snapshots each, 0.4 s apart); an aligned dword is a candidate when it is stable
# inside every state, equal across the two OFF states, equal across the two ON states, and different between OFF and ON.
# usage: wflagfind.py <pid> <game dir> [key, default ESC] [settle seconds, default 2.5]
import os, sys, time, ctypes, ctypes.wintypes as wt, struct
from array import array
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir = int(sys.argv[1]), sys.argv[2]; KEY = sys.argv[3] if len(sys.argv) > 3 else 'ESC'; SETTLE = float(sys.argv[4]) if len(sys.argv) > 4 else 2.5
P = Proc(pid)
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', wt.DWORD), ('PartitionId', wt.WORD), ('RegionSize', ctypes.c_size_t),
                ('State', wt.DWORD), ('Protect', wt.DWORD), ('Type', wt.DWORD)]
k.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
def regions():
    out = []; a = int(P.base); end = a + 0x1000000
    while a < end:
        m = MBI()
        if not k.VirtualQueryEx(P.h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)): break
        if m.State == 0x1000 and m.Protect in (0x04, 0x08, 0x40, 0x80): out.append((int(m.BaseAddress), int(m.RegionSize)))
        a = int(m.BaseAddress) + int(m.RegionSize)
    return out
R = regions(); print('writable regions of the image:', [(hex(a - int(P.base)), hex(n)) for a, n in R], flush=True)
def snap():
    return [array('I', P.rd(a, n) or bytes(n)) for a, n in R]
def stable(snaps):          # per region: list of (index, value) where all snapshots agree, as a dict
    out = {}
    for ri in range(len(R)):
        base = snaps[0][ri]
        same = set(range(len(base)))
        for s in snaps[1:]:
            o = s[ri]
            if o != base: same = {i for i in same if base[i] == o[i]}
        out[ri] = {i: base[i] for i in same}
    return out
def three():
    s = []
    for _ in range(3): s.append(snap()); time.sleep(0.4)
    return stable(s)
def press():
    cmd = os.path.join(gdir, 'witness_cmd.txt')
    for line, wait in (('key %s down' % KEY, 0.15), ('key %s up' % KEY, 0.0)):
        t = time.time()
        while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
        open(cmd + '.tmp', 'w').write(line + '\n'); os.replace(cmd + '.tmp', cmd); time.sleep(wait)
    time.sleep(SETTLE)
states = []
for name in ('off', 'on', 'off', 'on', 'off'):
    if states: press()
    states.append((name, three())); print('state', name, 'sampled', sum(len(v) for v in states[-1][1].values()), 'stable dwords', flush=True)
cand = []
for ri in range(len(R)):
    offs = set(states[0][1][ri]) & set(states[1][1][ri])
    for i in sorted(offs):
        vo = [s[1][ri].get(i) for s in states if s[0] == 'off']; vn = [s[1][ri].get(i) for s in states if s[0] == 'on']
        if None in vo or None in vn: continue
        if len(set(vo)) == 1 and len(set(vn)) == 1 and vo[0] != vn[0]: cand.append((R[ri][0] + i * 4 - int(P.base), vo[0], vn[0]))
print('candidates:', len(cand))
flag = [c for c in cand if c[1] in (0, 1) and c[2] in (0, 1)]
print('0/1 candidates (module offset, off value, on value):'); [print('  0x%X  %d -> %d' % c) for c in flag[:60]]
print('other candidates (first 25):'); [print('  0x%X  %d -> %d' % c) for c in [c for c in cand if c not in flag][:25]]
