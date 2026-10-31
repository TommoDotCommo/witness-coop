#!/usr/bin/env python3
"""Print float/dword/qword at virtual addresses of the PE exe: re_const.py exe 0x140513f10 ..."""
import struct, sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from re_find import sections
d = open(sys.argv[1], 'rb').read(); base, secs = sections(d)
for a in sys.argv[2:]:
    va = int(a, 16)
    for nm, sva, vs, ro, rs in secs:
        if base + sva <= va < base + sva + rs:
            o = ro + va - base - sva; print(f"{va:#x} {nm}: f32 {struct.unpack_from('<f', d, o)[0]:.6g}  u32 {struct.unpack_from('<I', d, o)[0]:#x}  f64 {struct.unpack_from('<d', d, o)[0]:.6g}"); break
