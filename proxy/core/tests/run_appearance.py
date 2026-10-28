#!/usr/bin/env python3
"""Native appearance contract and one broken twin per protection."""
from pathlib import Path
import subprocess
import tempfile
core = Path(__file__).resolve().parent.parent
source = (core / 'sync_core.cpp').read_text()
variants = {
    'clean': ('', source),
    'clamp': ('WA_MUT_CLAMP', source),
    'text': ('WA_MUT_TEXT', source),
    'length': ('WA_MUT_LENGTH', source),
    'version': ('WA_MUT_VERSION', source),
    'reserved': ('WA_MUT_RESERVED', source),
    'no-send': ('', source.replace('EncodeAppearance(pres_.appearance)', 'EncodeAppearance({})')),
    'no-expose': ('', source.replace('pose.appearance = p.second.pres.appearance;', 'pose.appearance = {};')),
    'stale': ('', source.replace('if (!newer && !f_.noPresenceSeq) return;', 'if (false && !newer && !f_.noPresenceSeq) return;')),
    'checksum': ('', source.replace('(!f_.noChecksum &&', '(false && !f_.noChecksum &&')),
    'no-heartbeat': ('', source.replace('(presDirty_ || now >= nextPres_)', 'presDirty_')),
    'default': ('', source),
}
with tempfile.TemporaryDirectory(prefix='witness-appearance-') as folder:
    root = Path(folder)
    for name, (define, text) in variants.items():
        if name not in ('clean', 'default') and not define:
            assert text != source, name
        # Header mutation models accidental ABI default drift.
        header = (core / 'appearance.h').read_text()
        if name == 'default': header = header.replace('choice{};', 'choice{1};')
        (root / 'appearance.h').write_text(header)
        (root / 'sync_core.h').write_text((core / 'sync_core.h').read_text())
        cpp = root / 'sync_core.cpp'; cpp.write_text(text)
        exe = root / name
        cmd = ['g++', '-std=c++17', '-O2', '-I', str(root), '-I', str(core), str(cpp), str(core / 'tests/appearance.cpp'), '-o', str(exe)]
        # The test's relative include resolves to the real header. Mirror tests so
        # every translation unit sees exactly the same mutated definition.
        tests = root / 'tests'; tests.mkdir(exist_ok=True)
        for f in ('appearance.cpp', 'protocol.cpp'): (tests / f).write_text((core / 'tests' / f).read_text())
        cmd[cmd.index(str(core / 'tests/appearance.cpp'))] = str(tests / 'appearance.cpp')
        if define: cmd.insert(1, '-D' + define)
        subprocess.run(cmd, check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        assert (result.returncode == 0) == (name == 'clean'), f'{name}: {result.stdout} {result.stderr}'
        print(name + (': passed' if name == 'clean' else ': mutant rejected'))
