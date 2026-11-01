#!/usr/bin/env python3
"""Pack a folder into the one-file setup's payload container (see dist/setup-core.h) and print its SHA-256.
  python3 -I tools/pack_payload.py <folder> <out.bin> [--skip name ...]
Files go in sorted order with '/' separators; the names must be ones the setup accepts (letters, digits . _ - +)."""
import hashlib, re, struct, sys
from pathlib import Path

NAME = re.compile(r'^[A-Za-z0-9._+-]+(/[A-Za-z0-9._+-]+)*$')


def pack(root, skip=()):
    root = Path(root)
    files = sorted(p for p in root.rglob('*') if p.is_file() and p.relative_to(root).as_posix() not in skip)
    assert files and len(files) <= 64, 'file count'
    out = bytearray(b'WCPAYLD1' + struct.pack('<I', len(files)))
    for p in files:
        name = p.relative_to(root).as_posix()
        assert NAME.match(name) and len(name) <= 120 and not p.is_symlink(), 'bad name or link: ' + name
        data = p.read_bytes()
        out += struct.pack('<H', len(name)) + name.encode() + struct.pack('<I', len(data)) + data
    return bytes(out)


def unpack(blob):
    """The same format read back (for tests and the release check): {name: bytes}."""
    assert blob[:8] == b'WCPAYLD1'
    n, = struct.unpack_from('<I', blob, 8)
    at, files = 12, {}
    for _ in range(n):
        nl, = struct.unpack_from('<H', blob, at); at += 2
        name = blob[at:at + nl].decode(); at += nl
        sz, = struct.unpack_from('<I', blob, at); at += 4
        files[name] = blob[at:at + sz]; at += sz
    assert at == len(blob), 'trailing bytes'
    return files


if __name__ == '__main__':
    root, dest = sys.argv[1], sys.argv[2]
    skip = [sys.argv[i + 1] for i, a in enumerate(sys.argv) if a == '--skip']
    blob = pack(root, skip)
    Path(dest).write_bytes(blob)
    print(hashlib.sha256(blob).hexdigest())
