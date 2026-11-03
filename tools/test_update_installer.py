#!/usr/bin/env python3
"""The updater and the real installer agree: a folder set up by the PowerShell installer (witness-coop.ps1, package A) is updated by the Windows updater (update_win.h, against a local fake
release server) to package B; then B's own installer must call every managed file "current" (-StatusOnly), a further install must change nothing, and its uninstall must leave the game's own
files as they were. Then the same from the old layout: a folder installed by the v0.1.0-alpha installer (its scripts from the git tag: the proxy in AnselSDK64.dll, the game's file renamed
AnselSDK64_real.dll) is updated by the Windows updater, which must migrate it (dsound.dll added, the game's own AnselSDK64.dll back, verified with its real hash), and B's installer must agree.
No game is started; the game folder is a disposable copy of the exe and the Ansel DLL. Needs a built package (tools/release.sh v0.0.0-test, as tools/qa_installer.sh does).
Mutant (T71_MUTANT=receipt-installed): the updater leaves the receipt's installed hash at the old value; the installer must then see the files as outdated.
usage: test_update_installer.py <folder of the supported build> [package folder, default local/release/witness-coop-v0.0.0-test]"""
import hashlib, http.server, json, os, shutil, subprocess, sys, threading
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
args = [a for a in sys.argv[1:] if not a.startswith('--')]
src = Path(args[0]); pkgA = Path(args[1]) if len(args) > 1 else ROOT / 'local/release/witness-coop-v0.0.0-test'
OWNER = os.environ.get('WITNESS_RESOURCE_OWNER') or 'updater'
base = Path(os.environ.get('WITNESS_TMPDIR') or sys.exit('set WITNESS_TMPDIR (a scratch folder on the Windows disk: source local/env.sh)')) / OWNER / 'update-installer'      # a scratch folder on the Windows disk (local/env.sh sets WITNESS_TMPDIR)
shutil.rmtree(base, ignore_errors=True); base.mkdir(parents=True)
win = lambda p: subprocess.check_output(['wslpath', '-w', str(p)], text=True).strip()
MUT = os.environ.get('T71_MUTANT')
sha = lambda b: hashlib.sha256(b).hexdigest()
ANSEL = '3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15'
fails = []; n = 0
def check(what, ok, detail=''):
    global n; n += 1; print(('  ok   ' if ok else '  FAILED ') + what + ('' if ok else '  ' + str(detail)[:300]), flush=True)
    if not ok: fails.append(what)
def ps(script, *args):
    r = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', win(script), *args], capture_output=True, text=True, timeout=300)
    return r.returncode, r.stdout.replace('\r', ''), r.stderr.replace('\r', '')
# package B: package A with every managed file changed (a byte appended to the executables, text changed)
pkgB = base / 'package-B'; shutil.copytree(pkgA, pkgB)
for f in ('dsound.dll', 'witness_launcher.exe'): (pkgB / f).write_bytes((pkgA / f).read_bytes() + b'\0\0\0\0')
(pkgB / 'witness_models/adventurer.glb').write_bytes((pkgA / 'witness_models/adventurer.glb').read_bytes() + b'B')
(pkgB / 'witness_models/adventurer.glb.txt').write_text((pkgA / 'witness_models/adventurer.glb.txt').read_text(errors='ignore') + '\nupdated\n')
import zipfile
TAG = 'v0.0.1-test'; ZN = f'witness-coop-{TAG}.zip'
zp = base / ZN
with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in ('dsound.dll', 'witness_launcher.exe', 'witness_models/adventurer.glb', 'witness_models/adventurer.glb.txt'): z.write(pkgB / f, f'witness-coop-{TAG}/{f}')
zb = zp.read_bytes(); sidecar = f'{sha(zb)}  {ZN}\n'.encode()
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_GET(self):
        if self.path.startswith('/releases'):
            body = json.dumps([{'tag_name': TAG, 'draft': False, 'prerelease': False, 'published_at': '2026-11-01T00:00:00Z', 'body': 'test', 'assets': [
                {'name': ZN, 'size': len(zb), 'browser_download_url': f'{base_url}/dl/{ZN}'}, {'name': ZN + '.sha256', 'size': len(sidecar), 'browser_download_url': f'{base_url}/dl/{ZN}.sha256'}]}]).encode()
        else: body = sidecar if self.path.endswith('.sha256') else zb
        self.send_response(200); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); base_url = f'http://127.0.0.1:{srv.server_address[1]}'; threading.Thread(target=srv.serve_forever, daemon=True).start()
# the updater (with the mutant, when asked)
sroot = ROOT
if MUT:
    sroot = base / 'mutant-src'; shutil.copytree(ROOT / 'launcher', sroot / 'launcher', ignore=shutil.ignore_patterns('build')); (sroot / 'dist').mkdir(); shutil.copy(ROOT / 'dist/install-policy.h', sroot / 'dist')
    f = sroot / 'launcher/update.h'; t = f.read_text(); a = 'lu_write_receipt(target, rc[i].original, newh[i], NULL)'; assert t.count(a) == 1; f.write_text(t.replace(a, 'lu_write_receipt(target, rc[i].original, rc[i].installed, NULL)'))
exe = base / 'update_win_test.exe'
subprocess.run(['x86_64-w64-mingw32-g++', '-std=c++17', '-O2', '-w', '-D_WIN32_WINNT=0x0601', '-static', '-o', str(exe), str(sroot / 'launcher/update_win_test.cpp'), '-lwininet', '-lws2_32'], check=True)
# 1. package A installed by the real installer into a disposable game folder
game = base / 'game'; game.mkdir(); shutil.copy(src / 'witness64_d3d11.exe', game); shutil.copy(src / 'AnselSDK64.dll', game)
rc, out, err = ps(pkgA / 'witness-coop.ps1', '-Yes', '-GameDir', win(game)); check('package A installs with the real installer', rc == 0 and (game / 'witness_launcher.exe').exists() and (game / 'dsound.dll').exists(), out[-300:] + err[-200:])
before = {p.name: sha(p.read_bytes()) for p in game.iterdir() if p.is_file() and p.name in ('AnselSDK64.dll', 'witness64_d3d11.exe')}
# 2. the updater, from A's version to B's
(game / 'witness_launcher_update.cfg').write_text('update_check=0\n')
r = subprocess.run([str(exe), win(game), 'v0.0.0-test', base_url + '/releases', 'both'], capture_output=True, text=True, timeout=120); out = r.stdout.replace('\r', '')
check('the updater finds %s and applies it' % TAG, 'check=0' in out and 'apply=0' in out, out)
check('the managed files are package B\'s', all(sha((game / f).read_bytes()) == sha((pkgB / f).read_bytes()) for f in ('dsound.dll', 'witness_launcher.exe', 'witness_models/adventurer.glb', 'witness_models/adventurer.glb.txt')))
check("the game's own files are untouched (AnselSDK64.dll, the exe)", all(sha((game / k).read_bytes()) == v for k, v in before.items()))
left = [p.name for p in game.rglob('*') if p.is_file() and (p.name.endswith(('.coop-stage', '.coop-prev', '.tmp')) or p.name == 'witness_update.state')]
check('no staging, backup, temp or journal file is left behind', not left, left)
# 3. B's own installer agrees
rc, out, err = ps(pkgB / 'witness-coop.ps1', '-StatusOnly', '-GameDir', win(game)); st = json.loads(out) if out.strip().startswith('{') else {}
items = {i['item']: i['status'] for i in st.get('items', [])}
managed = ['dsound.dll', 'witness_launcher.exe', 'witness_models\\adventurer.glb', 'witness_models\\adventurer.glb.txt']
check("package B's installer calls every managed file current, and offers no install, update or repair", all(items.get(m) == 'current' for m in managed) and st.get('actions', {}).get('install') in ('', None) and st.get('actions', {}).get('uninstall') is True, (items, st.get('actions')))
game2 = base / 'game2'; shutil.copytree(game, game2)      # (a second copy: the uninstall must work on the folder exactly as the updater left it, without a re-install in between that would repair a stale receipt)
rc, out, err = ps(pkgB / 'witness-coop.ps1', '-Yes', '-GameDir', win(game)); check('running package B\'s installer on the updated folder succeeds and changes nothing', rc == 0 and all(sha((game / f).read_bytes()) == sha((pkgB / f).read_bytes()) for f in ('dsound.dll', 'witness_launcher.exe')), out[-200:] + err[-200:])
(game2 / 'witness_launcher.exe.coop-old').unlink(missing_ok=True); game = game2
rc, out, err = ps(pkgB / 'witness-coop.ps1', '-Uninstall', '-Yes', '-GameDir', win(game))
check('its uninstall leaves the game\'s own Ansel DLL and removes the proxy, the launcher and the model', rc == 0 and sha((game / 'AnselSDK64.dll').read_bytes()) == ANSEL and not (game / 'dsound.dll').exists() and not (game / 'witness_launcher.exe').exists() and not (game / 'witness_models/adventurer.glb').exists(), out[-300:] + err[-200:])
# 4. the old layout: the v0.1.0-alpha installer (its scripts from the git tag, today's policy helper) installs an AnselSDK64.dll proxy, then the updater migrates
alpha = base / 'package-alpha'; (alpha / 'witness_models').mkdir(parents=True)
for f in ('witness-coop.ps1', 'install-state.ps1'):
    text = subprocess.run(['git', '-C', str(ROOT), 'show', f'v0.1.0-alpha:dist/{f}'], capture_output=True, text=True, check=True).stdout
    (alpha / f).write_text(text.replace('\r\n', '\n').replace('\n', '\r\n'))
shutil.copy(pkgA / 'install-policy.exe', alpha); shutil.copy(pkgA / 'witness_launcher.exe', alpha)
for f in ('adventurer.glb', 'adventurer.glb.txt'): shutil.copy(pkgA / 'witness_models' / f, alpha / 'witness_models')
(alpha / 'AnselSDK64.dll').write_bytes((pkgA / 'dsound.dll').read_bytes().replace(b'witness-coop:proxy:dsound', b'real Ansel DLL %s loaded.'))      # stands in for the alpha's proxy
old = base / 'game-alpha'; old.mkdir(); shutil.copy(src / 'witness64_d3d11.exe', old); shutil.copy(src / 'AnselSDK64.dll', old)
rc, out, err = ps(alpha / 'witness-coop.ps1', '-Yes', '-GameDir', win(old))
check('the v0.1.0-alpha installer makes the old layout', rc == 0 and sha((old / 'AnselSDK64_real.dll').read_bytes()) == ANSEL and (old / 'AnselSDK64.dll.coop-receipt.json').exists() and not (old / 'dsound.dll').exists(), out[-300:] + err[-200:])
(old / 'witness_launcher_update.cfg').write_text('update_check=0\n')
r = subprocess.run([str(exe), win(old), 'v0.0.0-test', base_url + '/releases', 'both'], capture_output=True, text=True, timeout=120); out = r.stdout.replace('\r', '')
check('the updater applies %s to the old layout' % TAG, 'check=0' in out and 'apply=0' in out, out)
names = sorted(p.name for p in old.iterdir())
check('the updater migrated it: dsound.dll is package B\'s, AnselSDK64.dll is the game\'s own again, no AnselSDK64_real.dll, old receipt or backup',
      sha((old / 'dsound.dll').read_bytes()) == sha((pkgB / 'dsound.dll').read_bytes()) and sha((old / 'AnselSDK64.dll').read_bytes()) == ANSEL and
      not any(n.startswith(('AnselSDK64_real', 'AnselSDK64.dll.')) for n in names), names)
rc, out, err = ps(pkgB / 'witness-coop.ps1', '-StatusOnly', '-GameDir', win(old)); st = json.loads(out) if out.strip().startswith('{') else {}
items = {i['item']: i['status'] for i in st.get('items', [])}
check("package B's installer calls the migrated folder current, with no old layout left", all(items.get(m) == 'current' for m in managed) and not st.get('legacy') and st.get('actions', {}).get('install') in ('', None), (items, st.get('actions'), st.get('legacy')))
srv.shutdown()
print(f'{n} checks, {len(fails)} failed'); 
if not MUT and not fails and '--mutant' in sys.argv:
    r = subprocess.run([sys.executable, '-I', __file__, str(src), str(pkgA)], env=dict(os.environ, T71_MUTANT='receipt-installed'), capture_output=True, text=True)
    print(('KILLED:   ' if r.returncode else 'SURVIVED: ') + 'receipt-installed (the receipt keeps the old installed hash)'); sys.exit(0 if r.returncode else 1)
sys.exit(1 if fails else 0)
