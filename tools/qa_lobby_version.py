#!/usr/bin/env python3
"""T2 scenario: the mod version in the lobby (launcher/lobby.h, launcher/main.cpp, tools/win/wlobby_version.py). Real launchers, no game and no slot: two built with the same stamped
version join each other, a third built with another stamped version is refused with both versions named. No GitHub call is made (update_check=0 in every test folder).
Mutant: a launcher whose host admits any version (the launcher mutant LOBBY_VERSION_ANY) must be caught.
usage: qa_lobby_version.py  (tools/qa.sh lobbyversion)"""
import os, shutil, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PYW = os.environ.get('PYW', 'python.exe')
OWNER = os.environ.get('WITNESS_RESOURCE_OWNER') or 'lobbyver'
RUN = os.path.join(os.environ.get('WITNESS_TMPDIR') or sys.exit('set WITNESS_TMPDIR (a scratch folder on the Windows disk: source local/env.sh)'), OWNER, 'lobbyver')      # a scratch folder on the Windows disk (local/env.sh sets WITNESS_TMPDIR)
win = lambda p: subprocess.check_output(['wslpath', '-w', p], text=True).strip()
VER_C = 'v9.9.9-test'

def build(buildname, version=None, defs=()):
    args = ['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', 'BUILD=' + buildname] + (['VERSION=' + version] if version else [])
    subprocess.run(args, check=True, capture_output=True)
    return os.path.join(ROOT, 'launcher', buildname, 'witness_launcher.exe')

def stamped(buildname):
    return [l.split('"')[1] for l in open(os.path.join(ROOT, 'launcher', buildname, 'version.h')) if 'MOD_VERSION' in l][0]

def folder(name, exe):
    d = os.path.join(RUN, name); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    open(os.path.join(d, 'witness64_d3d11.exe'), 'wb').close()
    open(os.path.join(d, 'witness_coop.cfg'), 'w').write('id=1\nlisten=47990\npeer=47991\nname=%s\nforce_active=1\n' % name)
    open(os.path.join(d, 'witness_launcher_update.cfg'), 'w').write('update_check=0\n')      # no GitHub call from a test
    shutil.copy(exe, os.path.join(d, 'witness_launcher_under_test.exe'))
    return d

def run(exeA, exeB, exeC, verA, verC, port):
    ds = {k: folder(k, e) for k, e in (('A', exeA), ('B', exeB), ('C', exeC))}
    out = subprocess.run([PYW, '-I', win(os.path.join(ROOT, 'tools', 'win', 'wlobby_version.py'))] + sum([[win(os.path.join(ds[k], 'witness_launcher_under_test.exe')), win(ds[k])] for k in 'ABC'], []) + [str(port), verA, verC], capture_output=True, text=True, timeout=240).stdout
    print(out, end=''); return 'ALL OK' in out

def main():
    exe = build('build'); verA = stamped('build'); exeC = build('build-lobbyver-c', VER_C)
    port = int(os.environ.get('WITNESS_PORT1', '47001')) + 150
    print('=== lobby versions: clean (A, B = %s, C = %s)' % (verA, VER_C), flush=True); ok = run(exe, exe, exeC, verA, VER_C, port); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if not ok or 'clean' in sys.argv[1:]: return 0 if ok else 1
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'build/witness_launcher_LOBBY_VERSION_ANY.exe'], check=True, capture_output=True)      # the host that admits any version
    mutexe = os.path.join(ROOT, 'launcher', 'build', 'witness_launcher_LOBBY_VERSION_ANY.exe')
    print('=== lobby versions: MUTANT host admits any version', flush=True); caught = not run(mutexe, exe, exeC, verA, VER_C, port + 1); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    print('ALL OK' if caught else 'FAILED'); return 0 if caught else 1

if __name__ == '__main__': sys.exit(main())
