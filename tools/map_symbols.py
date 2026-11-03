#!/usr/bin/env python3
"""Map a module offset from a crash report to a place in the proxy, with the linker map `make -C proxy` writes (proxy/dsound.map).

    python3 tools/map_symbols.py proxy/dsound.map 0x2a30 [0x...]           offsets (the number after dsound.dll+ in a crash line)
    python3 tools/map_symbols.py --stamp proxy/dsound.dll                  the DLL's link time stamp, to tell which build a map belongs to

A map belongs to one build: each link changes the addresses. The crash dump carries the DLL's link time stamp, so diag_report.py can
tell a matching map from another build's (--dll).
"""
import bisect
import re
import struct
import subprocess
import sys

SECTION = re.compile(r'^ (\.text\S*)\s*\n?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
SECTION_NAME_ONLY = re.compile(r'^ (\.text\S*)\s*$')
SECTION_BODY = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
SYMBOL = re.compile(r'^\s+0x([0-9a-f]+)\s+(\S.*)$')


def pe_timestamp(path):
    with open(path, 'rb') as f:
        head = f.read(4096)
    pe = struct.unpack_from('<I', head, 0x3C)[0]
    if head[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    return struct.unpack_from('<I', head, pe + 8)[0]


class SymbolMap:
    def __init__(self, text):
        self.base = None
        self.sections = []      # (rva, size, object file)
        syms = []               # (rva, name, object file)
        cur = None
        pending = None
        for line in text.splitlines():
            m = re.match(r'^\s+0x([0-9a-f]+)\s+__image_base__ = ', line)
            if m:
                self.base = int(m.group(1), 16)
            if self.base is None:
                continue
            m = SECTION.match(line)
            if not m:
                m2 = SECTION_NAME_ONLY.match(line)
                if m2:
                    pending = m2.group(1)
                    continue
                if pending:
                    m3 = SECTION_BODY.match(line)
                    pending = None
                    if m3:
                        cur = ((int(m3.group(1), 16) - self.base), int(m3.group(2), 16), m3.group(3).strip())
                        self.sections.append(cur)
                        continue
            else:
                cur = (int(m.group(2), 16) - self.base, int(m.group(3), 16), m.group(4).strip())
                self.sections.append(cur)
                continue
            if cur:
                m = SYMBOL.match(line)
                if m and not line.lstrip().startswith('0x0000000000000000') and ' = ' not in line and int(m.group(1), 16) >= self.base:
                    rva = int(m.group(1), 16) - self.base
                    if cur[0] <= rva < cur[0] + max(cur[1], 1):
                        syms.append((rva, m.group(2).strip(), cur[2]))
                elif line.strip() and not line.startswith(' '):
                    cur = None
        syms.sort()
        self.syms = syms
        self.keys = [s[0] for s in syms]
        self.sections.sort()
        self.skeys = [s[0] for s in self.sections]

    def lookup(self, rva):
        """(symbol or None, object file or None, distance into the symbol). Only global symbols are in a map: a static function is
        reported with the nearest global symbol before it in the same object file, which is why the object file is the reliable part."""
        i = bisect.bisect_right(self.skeys, rva) - 1
        obj = None
        if i >= 0 and rva < self.sections[i][0] + self.sections[i][1]:
            obj = self.sections[i][2]
        j = bisect.bisect_right(self.keys, rva) - 1
        if j >= 0 and (obj is None or self.syms[j][2] == obj or self.syms[j][0] >= self.sections[i][0]):
            return self.syms[j][1], obj, rva - self.syms[j][0]
        return None, obj, 0

    def describe(self, rva):
        sym, obj, d = self.lookup(rva)
        name = obj.rsplit('/', 1)[-1] if obj else 'unknown object'
        if sym:
            return '%s (+0x%x) in %s' % (self.demangle(sym), d, name)
        return 'in %s' % name

    @staticmethod
    def demangle(s):
        return s


def main(argv):
    if len(argv) == 2 and argv[0] == '--stamp':
        print('0x%08x' % pe_timestamp(argv[1]))
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    m = SymbolMap(open(argv[0], errors='replace').read())
    for a in argv[1:]:
        print('%s  %s' % (a, m.describe(int(a, 0))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
