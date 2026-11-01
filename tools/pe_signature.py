#!/usr/bin/env python3
"""Does a Windows program carry an Authenticode signature block? A structural check only (the certificate table entry of the PE header
points at a well-formed WIN_CERTIFICATE inside the file); a real verification needs the signing tool (SIGN_VERIFY_CMD in tools/release.sh).
  python3 -I tools/pe_signature.py <file> ...     exit 1 if any file is unsigned"""
import struct, sys
from pathlib import Path


def cert_entry(data):
    pe, = struct.unpack_from('<I', data, 0x3c)
    assert data[pe:pe + 4] == b'PE\0\0'
    opt = pe + 24
    magic, = struct.unpack_from('<H', data, opt)
    ddir = opt + (112 if magic == 0x20b else 96)
    return ddir + 4 * 8                      # IMAGE_DIRECTORY_ENTRY_SECURITY


def is_signed(data):
    try:
        at = cert_entry(data)
        off, size = struct.unpack_from('<II', data, at)
        if not off or size < 8 or off + size > len(data):
            return False
        length, revision, kind = struct.unpack_from('<IHH', data, off)
        return 8 <= length <= size and revision in (0x100, 0x200) and kind == 2
    except (AssertionError, struct.error):
        return False


def fake_sign(data):
    """Test only: append a well-formed but meaningless certificate block (what tools/fake_sign.py uses for the release test)."""
    data = bytearray(data)
    if len(data) % 8:
        data += b'\0' * (8 - len(data) % 8)
    off = len(data)
    block = struct.pack('<IHH', 8 + 16, 0x200, 2) + b'FAKE-SIGNATURE!!'
    data += block
    at = cert_entry(data)
    struct.pack_into('<II', data, at, off, len(block))
    return bytes(data)


if __name__ == '__main__':
    bad = [f for f in sys.argv[1:] if not is_signed(Path(f).read_bytes())]
    for f in bad:
        print('not signed:', f)
    sys.exit(1 if bad else 0)
