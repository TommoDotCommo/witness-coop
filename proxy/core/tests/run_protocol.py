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
def build_and_run(folder, name, text):      # each variant in a folder of its own, so they build side by side
    assert name == 'clean' or text != source, name
    root = Path(folder) / name; root.mkdir()
    cpp = root / 'sync_core.cpp'
    cpp.write_text(text)
    exe = root / name
    subprocess.run(['g++', '-std=c++17', '-O2', '-I', str(core), str(cpp), str(core / 'tests/protocol.cpp'), '-o', str(exe)], check=True)
    return subprocess.run([str(exe)], capture_output=True, text=True)

from concurrent.futures import ThreadPoolExecutor
with tempfile.TemporaryDirectory(prefix='witness-protocol-') as folder:
    with ThreadPoolExecutor(max_workers=len(variants)) as pool:
        runs = {name: pool.submit(build_and_run, folder, name, text) for name, text in variants.items()}
        for name, fut in runs.items():
            result = fut.result()
            assert (result.returncode == 0) == (name == 'clean'), f'{name}: {result.stdout} {result.stderr}'
            print(f'{name}: {"passed" if name == "clean" else "mutant rejected"}')
