#!/usr/bin/env python3
"""Find a string in the PE exe and print its virtual address (used to locate code by its text; see docs on the collision oracle)."""
import struct, sys
def sections(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
    s = pe + 24 + opt; out = []
    for i in range(n):
        name, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, s + 40 * i)
        out.append((name.rstrip(b'\0').decode(), va, vs, ro, rs))
    return base, out
def find(path, needle):
    d = open(path, 'rb').read(); base, secs = sections(d); res = []; i = -1
    while True:
        i = d.find(needle.encode(), i + 1)
        if i < 0: break
        for nm, va, vs, ro, rs in secs:
            if ro <= i < ro + rs: res.append((nm, base + va + (i - ro)))
    return res
if __name__ == '__main__':
    for nm, a in find(sys.argv[1], sys.argv[2]): print(f"{nm} {a:#x} (rva {a-0x140000000:#x})")
