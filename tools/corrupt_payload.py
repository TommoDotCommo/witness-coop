#!/usr/bin/env python3
"""Test helper: copy a one-file Setup.exe with one byte of its embedded payload flipped (the exe stays a valid program; only its contents no longer match the compiled-in checksum)."""
import sys
from pathlib import Path
data = bytearray(Path(sys.argv[1]).read_bytes())
at = data.find(b'WCPAYLD1')
assert at > 0, 'no payload in this exe'
data[at + 40] ^= 0x55
Path(sys.argv[2]).write_bytes(data)
