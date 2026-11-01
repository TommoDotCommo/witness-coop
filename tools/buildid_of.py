#!/usr/bin/env python3
"""Print the build fingerprint of a Windows exe the way common/buildid.h computes it: header fields and the SHA-256 of the
first code section's file bytes. Used to derive the constants for a new game build and to check the ones in the header.
  python3 -I tools/buildid_of.py <exe>            print the constants
  python3 -I tools/buildid_of.py <exe> --check    exit 1 unless common/buildid.h holds exactly these constants
"""
import hashlib, re, struct, sys
from pathlib import Path


def fingerprint(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe + 4] == b'PE\0\0', 'not a PE'
    nsec, = struct.unpack_from('<H', data, pe + 6)
    osz, = struct.unpack_from('<H', data, pe + 20)
    opt = pe + 24
    ts, = struct.unpack_from('<I', data, pe + 8)
    code, = struct.unpack_from('<I', data, opt + 4)
    entry, = struct.unpack_from('<I', data, opt + 16)
    image, = struct.unpack_from('<I', data, opt + 56)
    sec = opt + osz
    for i in range(nsec):
        o = sec + 40 * i
        vs, va, rs, rp, ch = struct.unpack_from('<IIII', data, o + 8) + struct.unpack_from('<I', data, o + 36)
        if ch & 0x20000020 == 0x20000020:      # contains code + executable
            n = min(vs, rs) if vs else rs
            return dict(timestamp=ts, size_of_image=image, entry_rva=entry, size_of_code=code,
                        text_sha256=hashlib.sha256(data[rp:rp + n]).hexdigest(), text_bytes=n)
    raise SystemExit('no code section')


def header_constants(text):
    def num(name):
        return int(re.search(r'#define\s+%s\s+(0x[0-9a-fA-F]+)u?' % name, text).group(1), 16)
    return dict(timestamp=num('WITNESS_BUILD_TIMESTAMP'), size_of_image=num('WITNESS_BUILD_IMAGE_SIZE'),
                entry_rva=num('WITNESS_BUILD_ENTRY_RVA'), size_of_code=num('WITNESS_BUILD_CODE_SIZE'),
                text_sha256=re.search(r'#define\s+WITNESS_BUILD_TEXT_SHA256\s+"([0-9a-f]{64})"', text).group(1))


if __name__ == '__main__':
    fp = fingerprint(Path(sys.argv[1]).read_bytes())
    if '--check' in sys.argv:
        have = header_constants((Path(__file__).resolve().parents[1] / 'common/buildid.h').read_text())
        bad = {k: (have[k], fp[k]) for k in have if have[k] != fp[k]}
        if bad:
            print('common/buildid.h does not match this exe:', bad); sys.exit(1)
        print('common/buildid.h matches this exe'); sys.exit(0)
    print('#define WITNESS_BUILD_TIMESTAMP 0x%08xu' % fp['timestamp'])
    print('#define WITNESS_BUILD_IMAGE_SIZE 0x%xu' % fp['size_of_image'])
    print('#define WITNESS_BUILD_ENTRY_RVA 0x%xu' % fp['entry_rva'])
    print('#define WITNESS_BUILD_CODE_SIZE 0x%xu' % fp['size_of_code'])
    print('#define WITNESS_BUILD_TEXT_SHA256 "%s"   /* %d bytes */' % (fp['text_sha256'], fp['text_bytes']))
