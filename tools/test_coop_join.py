#!/usr/bin/env python3
"""The launcher's Join side, game-free (bridge/launcher_join.h): bridge/tests/launcher_join_test.c with
sanitisers, then source mutants that must fail it, and a 32-bit MinGW build of the test (the Witness launcher's
wiring is checked by tools/test_coop_witness.py). No game, no Windows process needed."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / 'common'      # the shared headers vendored from MDK2 (bridge/), tests in common/tests
TEST = BRIDGE / 'tests' / 'launcher_join_test.c'
CFLAGS = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
MUTANTS = {
    # B1: the scan must use every adapter's directed broadcast; 255.255.255.255 leaves by one adapter only
    'broadcast-only-limited': ('cd_add_adapters(c, in->adapters, in->nadapters, in->ports[p]);',
                               'cd_add(c, 0xffffffffu, in->ports[p], CD_LAN, 1, 0, -1);'),
    'first-adapter-only': ('cd_add_adapters(c, in->adapters, in->nadapters, in->ports[p]);',
                           'cd_add_adapters(c, in->adapters, 1, in->ports[p]);'),
    'tailscale-setting-ignored': ('if (in->tailscale_on && in->ts)', 'if (in->ts)'),
    'tailscale-never': ('if (in->tailscale_on && in->ts) cd_add_tailscale', 'if (0) cd_add_tailscale'),
    'recents-not-probed': ('cd_add_recent(c, &in->recents->r[i], in->recent_ip ? in->recent_ip[i] : 0);', ';'),
    'configured-port-only': ('if (!n || out[0] != LJ_DEFAULT_PORT) out[n++] = LJ_DEFAULT_PORT;', 'if (!n) out[n++] = LJ_DEFAULT_PORT;'),
    'default-port-only': ('if (configured >= 1 && configured <= 65532) out[n++] = configured;', ''),
    'invite-only-listed': ('if (h->invite_only && n == 8) return 0;', ''),
    'no-rate-limit': ('if (!cd_rate_allow(rate, from_ip, now)) return 0;', ''),
    'cpq1-dropped': ('(n == 8 && !memcmp(q, "CPQ1", 4)) || ', ''),
    'cpq2-dropped': (' || (n == CD_QUERY_LEN && !memcmp(q, "CPQ2", 4))', ''),
    'mismatch-listed-first': ('if (a->mismatch != b->mismatch) return !a->mismatch;', 'if (a->mismatch != b->mismatch) return a->mismatch;'),
    'full-not-last': ('if (lj_row_full(a) != lj_row_full(b)) return !lj_row_full(a);', ''),
    'route-order-lost': ('if (a->route != b->route) return a->route < b->route;', ''),
    'row-version-hidden': ('"version %s  -  %s  -  %d of %d  -  %s  -  %d ms", r->version,', '"%s%s  -  %d of %d  -  %s  -  %d ms", "",'),
    'no-preference-window': ('int a = cd_he_choice(&j->he, now);', 'int a = j->he.first;'),
    'failure-without-who': ('who = who && who[0] ? who : "the host";', 'who = "the host";'),
    'version-no-update': ('return LJ_N_UPDATE | LJ_N_DIAGNOSTICS;', 'return LJ_N_DIAGNOSTICS;'),
    'migration-skipped': ('if (have_profiles && cd_recents_decode(out, profiles, nprofiles)) { *migrated = 1; return 1; }', ''),
    'rejoin-other-version': ('if (l->r[row].mismatch || lj_row_full(&l->r[row])) return 0;', 'if (lj_row_full(&l->r[row])) return 0;'),
    'rejoin-oldest': ('s->r[i].when > s->r[best].when', 's->r[i].when < s->r[best].when'),
    'link-keeps-quote': ("cmdline[k] != ' ' && cmdline[k] != '\"'", "cmdline[k] != ' '"),
    'link-overflow': ('if (k - i >= cap) return 0;', 'if (k - i >= cap) k = i + cap - 1;'),
    'link-scheme-case': ('for (k = 0; k < n; k++) out[k] = LJ_SCHEME[k];', ''),
    'link-trailing-slash': ("for (k = strlen(out); k > n && out[k - 1] == '/'; k--) out[k - 1] = 0;", 'k = strlen(out);'),
}


def run(cmd, timeout=300):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def build(out, include, extra=(), compiler='cc'):
    r = run([compiler, *CFLAGS, *extra, '-I', str(include), '-I', str(BRIDGE), str(TEST), '-o', str(out)])
    if r.returncode:
        raise SystemExit(f'FAIL: build {TEST.name} ({compiler})\n{r.stderr}')


source = (BRIDGE / 'launcher_join.h').read_text()
with tempfile.TemporaryDirectory(prefix='launcher-join-') as temp:
    temp = Path(temp)
    exe = temp / 'test'
    build(exe, BRIDGE, ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'])
    r = run([str(exe)])
    print(r.stdout.strip())
    if r.returncode:
        raise SystemExit('FAIL: launcher_join_test\n' + r.stderr[-3000:])
    for name, (before, after) in MUTANTS.items():
        if source.count(before) != 1:
            raise SystemExit(f'FAIL: mutant text found {source.count(before)} times: {name}: {before!r}')
        folder = temp / f'm-{name}'
        folder.mkdir()
        (folder / 'launcher_join.h').write_text(source.replace(before, after))
        build(folder / 'test', folder, ['-O1', '-Wno-unused-but-set-variable', '-Wno-unused-variable', '-Wno-unused-function'])
        r = run([str(folder / 'test')], timeout=60)
        if r.returncode == 0:
            raise SystemExit(f'SURVIVED: {name}\n{r.stdout[-2000:]}')
        print('KILLED:', name)
    if shutil.which('g++'):
        cpp = temp / 'cpp.cc'
        cpp.write_text('#include "launcher_join.h"\nint main(){unsigned p[2];return lj_ports(1,p);}\n')
        r = run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(BRIDGE), '-c', str(cpp), '-o', str(temp / 'cpp.o')])
        if r.returncode:
            raise SystemExit('FAIL: launcher_join.h does not compile as C++\n' + r.stderr)
        print('PASS: launcher_join.h compiles as C++')
    if shutil.which('i686-w64-mingw32-gcc'):
        build(temp / 'test.exe', BRIDGE, ['-O2', '-static-libgcc'], 'i686-w64-mingw32-gcc')
        print('PASS: 32-bit MinGW build of launcher_join_test')

sys.exit(0)
