#!/usr/bin/env python3
"""Mutation oracles for the versioned precision wire contract."""
from pathlib import Path
import resource
import subprocess
import tempfile
resource.setrlimit(resource.RLIMIT_CORE, (0,0))
core=Path(__file__).resolve().parent.parent
source=(core/'sync_core.cpp').read_text()
variants={
    'clean':source,
    'no-same-sequence-upgrade':source.replace('(seq < l.seq || (seq == l.seq && (!fine || l.fine)))','seq <= l.seq'),
    'allow-precision-downgrade':source.replace('(!fine || l.fine)','(fine && l.fine)'),
    'legacy-wrong-units':source.replace('LineLegacy(p.d[k], s.fine)','p.d[k]'),
    'magic-collision':source.replace('kMagicLineFine = 0x374C5457','kMagicLineFine = 0x364C5457'),
    'ignore-unit-changes':source.replace('mix(l.fine ? 1 : 0); ',''),
    'accept-invalid-flags':source.replace('if (flags & ~1u) return;',''),
    'accept-invalid-values':source.replace('if (v < -32000 || v > 32000) return;',''),
    'accept-invalid-nodes':source.replace('if (uint16_t(p[0] | p[1] << 8) > 4000) return;',''),
}
# Keep generated build artifacts in this checkout.
(core/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=core/'build',prefix='line-mutants-') as d:
    for name,text in variants.items():
        assert name=='clean' or text!=source,name
        cpp=Path(d)/'sync_core.cpp';cpp.write_text(text);exe=Path(d)/name
        subprocess.run(['g++','-std=c++17','-O2','-I',str(core),str(cpp),str(core/'tests/line_precision.cpp'),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True)
        assert (r.returncode==0)==(name=='clean'),f'{name}: {r.stdout} {r.stderr}'
        print(name+(': passed' if name=='clean' else ': mutant rejected'),flush=True)
