import os, sys
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); T = p.entity_table()
def words(e, a, n): return ' '.join('%08x' % p.u32(e + a + 4 * i) for i in range(n))
for pid_ in (0xA3B5, 0x3629, 0x2C2):
    e = T[pid_]; print('panel %x type %s solved %08x/%08x target %08x' % (pid_, p.type_of(e), p.u32(e + 0x294), p.u32(e + 0x298), p.u32(e + 0x2B4)))
e = T[0x3BA2]; print('door 3ba2 entity', hex(e) if e else None)
if e: print(' type', p.type_of(e), 'is door', p.type_of(e) == TI_DOOR, '1d0', hex(p.u32(e + 0x1d0)), 'words', words(e, 0x1c0, 12))
for did in (0x1A29, 0x17F3E):
    e = T[did]; print('door %x type %s open %08x' % (did, p.type_of(e), p.u32(e + 0x1d0)))
# who targets 3ba2
for i, e in enumerate(T):
    if e and p.type_of(e) == TI_PANEL and p.u32(e + 0x2B4) in (0x3BA2, 0x3BA2 + 1): print('panel %x targets 3ba2 (+base?)' % i, hex(p.u32(e + 0x2B4)))
