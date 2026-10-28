#!/usr/bin/env python3
"""Build the wire contract on Linux and prove it detects migration regressions."""
from pathlib import Path
import subprocess
import tempfile

core = Path(__file__).resolve().parent.parent
source = (core / 'sync_core.cpp').read_text()
variants = {
    'clean': source,
    'old-presence-magic': source.replace('kMagicPres = 0x36535457', 'kMagicPres = 0x35535457'),
    'model-name-payload': source.replace('kPresLen = 36', 'kPresLen = 60').replace('    send(finish(std::move(b)));\n}\nvoid SyncCore::onPresence', '    b.insert(b.end(), 24, 0);\n    send(finish(std::move(b)));\n}\nvoid SyncCore::onPresence'),
    'group-magic-collision': source.replace('0x37505457', '0x36505457'),
}
with tempfile.TemporaryDirectory(prefix='witness-protocol-') as folder:
    root = Path(folder)
    for name, text in variants.items():
        assert name == 'clean' or text != source, name
        cpp = root / 'sync_core.cpp'
        cpp.write_text(text)
        exe = root / name
        subprocess.run(['g++', '-std=c++17', '-O2', '-I', str(core), str(cpp), str(core / 'tests/protocol.cpp'), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        assert (result.returncode == 0) == (name == 'clean'), f'{name}: {result.stdout} {result.stderr}'
        print(f'{name}: {"passed" if name == "clean" else "mutant rejected"}')
