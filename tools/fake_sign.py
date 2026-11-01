#!/usr/bin/env python3
"""Stand-in signer for tools/test_release_signing.sh: appends a meaningless certificate block in place.
FAKE_SIGN_SKIP=<text>: leave files whose name contains the text unsigned (the mutant: a signer that misses a file)."""
import os, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_signature import fake_sign
f = Path(sys.argv[1])
if os.environ.get('FAKE_SIGN_SKIP') and os.environ['FAKE_SIGN_SKIP'] in f.name:
    sys.exit(0)
f.write_bytes(fake_sign(f.read_bytes()))
