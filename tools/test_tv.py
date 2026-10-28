#!/usr/bin/env python3
"""Game-free TV contract and mutation tests. Every mutation must fail the same oracle."""
from pathlib import Path
import concurrent.futures
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
header = (root / 'proxy/tv/tv.h').read_text()
mesh = (root / 'proxy/tv/mesh.h').read_text()
mutations = {
    'consent': ('#ifndef TV_MUT_CONSENT', '#if 0'),
    'subscriber-cap': ('#ifndef TV_MUT_SUB_CAP', '#if 0'),
    'stale-picture': ('#ifdef TV_MUT_STALE', '#if 1'),
    'ingress-budget': ('#ifndef TV_MUT_RATE', '#if 0'),
    'wrong-player-ticket': ('#ifndef TV_MUT_TARGET', '#if 0'),
    'frame-rate': ('#ifndef TV_MUT_FRAME_RATE', '#if 0'),
    'spring-bound': ('#ifndef TV_MUT_SPRING', '#if 0'),
    'lease-expiry': ('now-i->second.at>Lease', 'false'),
    'send-rate': ('(sent_ && now-last_<Period)', 'false'),
    'revoke': ('if(!v) subs.clear();', 'if(false) subs.clear();'),
    'checksum': ('get(p+n-4)==hash(p,n-4)', 'true'),
    'replay': ('if(done_ && int32_t(seq-done_)<=0)', 'if(false)'),
    'fragment-timeout': ('now-start_>Fresh', 'false'),
    'deny-clears-image': ('reset(); if(!++ticket) ++ticket; } }', 'if(!++ticket) ++ticket; } }'),
    'reserved-header': ('|| p[19] || mode>1', '|| mode>1'),
    'decoded-size': ('if(out.size()!=Pixels) return false;', 'if(false) return false;'),
    'rle-overflow': ('out.size()+data[i]>Pixels', 'false'),
}
mesh_mutations = {
    'screen-behind-case': ('1.21f-sh,-.481f', '1.21f-sh,-.40f'),
    'screen-uv': ('{1,1},{1,0}', '{0,1},{0,0}'),
    'screen-power': ('.015f+.985f*motion.power', '1.f'),
    'chest-weight': ('p.joints[0]=uint8_t(chest)', 'p.joints[0]=0'),
}
def run(name, text, geometry):
    with tempfile.TemporaryDirectory(prefix='witness-tv-') as folder:
        p = Path(folder)
        (p / 'tv').mkdir(); (p / 'model').mkdir()
        (p / 'tv/tv.h').write_text(text); (p / 'tv/mesh.h').write_text(geometry)
        shutil.copyfile(root / 'proxy/model/model.h', p / 'model/model.h')
        exe = p / 'test'
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-I', str(p), str(root / 'proxy/tv/test.cpp'), '-o', str(exe)], check=True, capture_output=True, text=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        assert (r.returncode == 0) == (name == 'clean'), f'{name}: {r.stdout} {r.stderr}'
        return f'{name}: {"passed" if name == "clean" else "mutant rejected"}'
if __name__ == '__main__':
    print(run('clean', header, mesh), flush=True)
    jobs = []
    for name, (old, new) in mutations.items():
        assert old in header, name
        jobs.append((name, header.replace(old, new), mesh))
    for name, (old, new) in mesh_mutations.items():
        assert old in mesh, name
        jobs.append((name, header, mesh.replace(old, new)))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for line in pool.map(lambda args: run(*args), jobs): print(line, flush=True)
