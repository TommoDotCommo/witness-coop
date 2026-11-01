#!/usr/bin/env python3
"""The Windows updater (launcher/update_win.h, WinINet) against a local fake release server; no game, no real GitHub call.
Needs mingw (x86_64-w64-mingw32-g++) and WSL interop. The game folders are fixtures on the Windows disk (--root, default under the lab run folder).
Scenarios: success through the receipts, up to date, hash mismatch, truncated download, missing .sha256 asset, rate limit (429, kept as a hold),
pre-release rules, the once-a-day limit and the off switch (counted requests), a copy without receipts, a downgrade offered."""
import argparse, hashlib, http.server, json, os, shutil, subprocess, sys, threading
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_launcher_update as T

ROOT = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(); ap.add_argument('--root', type=Path, default=Path(os.environ.get('WITNESS_TMPDIR') or sys.exit('set WITNESS_TMPDIR (a scratch folder on the Windows disk: source local/env.sh)')) / os.environ.get('WITNESS_RESOURCE_OWNER', 'updater') / 'update-win')      # a scratch folder on the Windows disk (local/env.sh sets WITNESS_TMPDIR)
ap.add_argument('--mutants', action='store_true', help='also rebuild with each mutant of update_win.h: the scenarios must fail')
a = ap.parse_args()
MUTANT = os.environ.get('T71_MUTANT')
WMUTANTS = {
    'auto-ignores-daily-limit': [('if (!lu_can_check(&s, (uint64_t)time(nullptr), 0)) return; Start(false);', 'Start(false);'), ('if (!lu_can_check(&s, now, manual)) {', 'if (0) {')],
    'last-check-not-kept': ('t = SetKey(t, "update_last_check", std::to_string(sched_.last_check));', ''),
    'retry-after-not-kept': ('t = SetKey(t, "update_retry_after", std::to_string(sched_.retry_after));', ''),
    'download-not-verified': ('r = lu_verify_download(zip, zipLen, rel.zip_size, expect);', 'r = LU_OK;'),
    'sidecar-not-parsed': ('else r = lu_parse_sidecar((const char*)sha, shaLen, rel.zip_name, expect);', 'else { r = LU_OK; strcpy(expect, "0"); }'),
    'dev-build-asks-network': ('if (lu_is_dev_build(version_.c_str())) {      // a development build is never replaced: it does not even ask', 'if (0) {'),
    'auto-check-loud': ('if (!manual) { Set(kIdle, ""); return r; }', ''),
    'prerelease-pref-ignored': ('s->include_prereleases = !strcmp(v, "1") ? 1 : !strcmp(v, "0") ? 0 : lu_version_is_prerelease(current_ver);', 's->include_prereleases = lu_version_is_prerelease(current_ver);'),
}
shutil.rmtree(a.root, ignore_errors=True); a.root.mkdir(parents=True)
exe = a.root / 'update_win_test.exe'
src_root = ROOT
if MUTANT:      # a patched copy of the sources (update.h is patched for the mutants of lu_load_schedule, update_win.h for the rest)
    src_root = a.root / 'mutant-src'; shutil.copytree(ROOT / 'launcher', src_root / 'launcher', ignore=shutil.ignore_patterns('build')); (src_root / 'dist').mkdir(); shutil.copy(ROOT / 'dist/install-policy.h', src_root / 'dist')
    for before, after in (WMUTANTS[MUTANT] if isinstance(WMUTANTS[MUTANT], list) else [WMUTANTS[MUTANT]]):
        f = src_root / 'launcher' / ('update.h' if before in (ROOT / 'launcher/update.h').read_text() else 'update_win.h'); t = f.read_text(); assert t.count(before) == 1, MUTANT; f.write_text(t.replace(before, after))
subprocess.run(['x86_64-w64-mingw32-g++', '-std=c++17', '-O2', '-w', '-D_WIN32_WINNT=0x0601', '-static', '-o', str(exe), str(src_root / 'launcher/update_win_test.cpp'), '-lwininet', '-lws2_32'], check=True)
win = lambda p: subprocess.check_output(['wslpath', '-w', str(p)], text=True).strip()

state = {'mode': 'ok', 'requests': [], 'releases': []}
zips = {}


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_GET(self):
        state['requests'].append(self.path)
        if self.path.startswith('/releases'):
            if state['mode'] == 'ratelimit':
                self.send_response(429); self.send_header('Retry-After', '7200'); self.end_headers(); return
            body = release_json().encode(); self.send_response(200); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body); return
        name = self.path.rsplit('/', 1)[-1]
        if name.endswith('.sha256'):
            z = zips[name[:-7]]; h = hashlib.sha256(z).hexdigest()
            if state['mode'] == 'badhash': h = '0' * 64
            body = f'{h}  {name[:-7]}\n'.encode()
        else:
            body = zips[name]
            if state['mode'] == 'truncated': body = body[: len(body) // 2]
        self.send_response(200); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)


srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
base = f'http://127.0.0.1:{port}'
API = base + '/releases'


def release_json():
    out = []
    for r in state['releases']:
        tag = r['tag']; zn = f'witness-coop-{tag}.zip'
        assets = [{'name': zn, 'size': len(zips[zn]) if state['mode'] != 'sizelie' else len(zips[zn]) + 5, 'browser_download_url': f'{base}/dl/{zn}'}]
        if state['mode'] != 'nosidecar': assets.append({'name': zn + '.sha256', 'size': len(f'{hashlib.sha256(zips[zn]).hexdigest()}  {zn}\n'), 'browser_download_url': f'{base}/dl/{zn}.sha256'})
        out.append({'tag_name': tag, 'draft': False, 'prerelease': r.get('pre', False), 'published_at': '2026-11-01T10:00:00Z', 'body': f'Notes of {tag}\nline two', 'assets': assets})
    return json.dumps(out)


def offer(*rels):
    state['releases'] = list(rels)
    for r in rels:
        zips[f'witness-coop-{r["tag"]}.zip'] = T.make_zip(r['tag'], r['fill'])


def run(game, version, cmd, mode='ok'):
    state['mode'] = mode; state['requests'].clear()
    p = subprocess.run([str(exe), win(game), version, API, cmd], capture_output=True, text=True, timeout=120)
    d = {}
    for line in p.stdout.splitlines():
        if '=' in line: k, v = line.split('=', 1); d[k] = v.strip()
    d['_reqs'] = list(state['requests']); d['_out'] = p.stdout
    return d


fails = []; n = 0
def tree(g): return {k: v for k, v in T.tree(g).items() if k != 'witness_launcher_update.cfg'}      # (the check keeps its time in that file)
def check(what, ok):
    global n; n += 1
    if not ok: fails.append(what); print('FAILED:', what)

def game(name, **kw):
    g = a.root / name; T.make_game(g, **kw); return g

new = T.files('v0.2.0', 0x22)
offer({'tag': 'v0.2.0', 'fill': 0x22})
g = game('ok'); before = tree(g)
r = run(g, "v0.1.0", "both")
check('check finds v0.2.0 with its notes', r.get('check') == '0' and r.get('tag') == 'v0.2.0' and 'Notes of v0.2.0' in r.get('notes', ''))
check('apply through WinINet: downloaded, verified and applied', r.get('apply') == '0' and r.get('state') == '5')
after = tree(g)
check('every managed file is the new one and the game\'s own Ansel DLL is untouched', all(after[k] == T.sha(v) for k, v in new.items()) and after['AnselSDK64_real.dll'] == before['AnselSDK64_real.dll'] and after['AnselSDK64.dll.coop-original'] == before['AnselSDK64.dll.coop-original'])
rc = json.loads((g / 'AnselSDK64.dll.coop-receipt.json').read_text()); check('the receipt is the installer\'s form', rc == {'schema': 1, 'original': T.sha(T.ORIG_ANSEL), 'installed': T.sha(new['AnselSDK64.dll']), 'pending': None})
check('exactly the check, the .sha256 and the zip were requested, nothing else', [x.split('/')[1] for x in r['_reqs']] == ['releases?per_page=10'.split('?')[0], 'dl', 'dl'] or len(r['_reqs']) == 3)
cfg = (g / 'witness_launcher_update.cfg').read_text()
check('the time of the check is kept (so the next start does not ask again today)', 'update_last_check=' in cfg and 'update_last_check=0\n' not in cfg)

g = game('uptodate'); r = run(g, 'v0.2.0', 'both')
check('the installed version is the newest: "newest version", nothing downloaded', r.get('check') == '0' and r.get('state') == '2' and len(r['_reqs']) == 1 and tree(g) == tree(game('uptodate2')))
offer({'tag': 'v0.1.0', 'fill': 0x11}); g = game('downgrade', tag='v0.2.0', fill=0x22); b = tree(g); r = run(g, 'v0.2.0', 'both')
check('an older release listed is never installed over a newer one', r.get('state') == '2' and tree(g) == b)
offer({'tag': 'v0.2.0', 'fill': 0x22})
g = game('badhash'); b = tree(g); r = run(g, 'v0.1.0', 'both', 'badhash')
check('a .sha256 that does not match the download is refused and nothing changes', r.get('apply') == str(T.CODES.get('LU_ERR_CHECKSUM', 12)) if T.CODES else r.get('apply') not in (None, '0'))
check('...and the folder is exactly as before', tree(g) == b)
g = game('trunc'); b = tree(g); r = run(g, 'v0.1.0', 'both', 'truncated')
check('a download cut short is refused and nothing changes', r.get('apply') not in (None, '0') and tree(g) == b)
g = game('nosc'); b = tree(g); r = run(g, 'v0.1.0', 'both', 'nosidecar')
check('a release without its .sha256 asset is not offered', r.get('state') in ('6', '0', '2') and r.get('apply') is None and tree(g) == b)
g = game('norc', with_receipts=False); b = tree(g); r = run(g, 'v0.1.0', 'both')
check('a copy without receipts (not installed by Setup) is not changed, and says why', r.get('apply') not in (None, '0') and tree(g) == b and 'Setup' in r.get('message', ''))
g = game('rate'); r = run(g, 'v0.1.0', 'check', 'ratelimit')
cfg = (g / 'witness_launcher_update.cfg').read_text() if (g / 'witness_launcher_update.cfg').exists() else ''
check('HTTP 429 is kept as a hold (update_retry_after) so the next start does not ask', 'update_retry_after=' in cfg and 'update_retry_after=0\n' not in cfg)
r2 = run(g, 'v0.1.0', 'check', 'ok'); check('...and a manual check during the hold sends no request', len(r2['_reqs']) == 0)
# pre-releases and the daily limit
offer({'tag': 'v0.3.0-beta.1', 'fill': 0x33, 'pre': True}, {'tag': 'v0.2.0', 'fill': 0x22})
g = game('pre'); r = run(g, 'v0.1.0', 'check'); check('a pre-release is not offered to a release version', r.get('tag') == 'v0.2.0')
g = game('pre2', tag='v0.2.0-beta.1'); r = run(g, 'v0.2.0-beta.1', 'check'); check('a pre-release is offered to an installed pre-release', r.get('tag') == 'v0.3.0-beta.1')
g = game('pre3'); (g / 'witness_launcher_update.cfg').write_text('update_prereleases=1\n'); r = run(g, 'v0.1.0', 'check'); check('the setting update_prereleases=1 offers it to a release version', r.get('tag') == 'v0.3.0-beta.1')
g = game('daily'); r1 = run(g, 'v0.1.0', 'auto'); r2 = run(g, 'v0.1.0', 'auto')
check('the automatic check asks once, and not again the same day', len(r1['_reqs']) == 1 and len(r2['_reqs']) == 0)
g = game('quiet'); r = run(g, 'v0.1.0', 'auto', 'ratelimit')
check('a failing automatic check says nothing in the window (state idle, no message)', len(r['_reqs']) == 1 and r.get('state') == '0' and r.get('message', '') == '')
g = game('off'); (g / 'witness_launcher_update.cfg').write_text('update_check=0\n'); r = run(g, 'v0.1.0', 'auto')
check('update_check=0: the automatic check sends no request at all', len(r['_reqs']) == 0)
g = game('dev'); r = run(g, 'v0.1.0-5-g1a2b3c4', 'check'); check('a development build is told so, never offered an update, and does not even ask GitHub', r.get('state') == '2' and 'development' in r.get('message', '') and len(r['_reqs']) == 0)
srv.shutdown()
print(f'{n} checks, {len(fails)} failed')
if a.mutants and not MUTANT and not fails:
    survivors = []
    for name in WMUTANTS:
        r = subprocess.run([sys.executable, '-I', __file__, '--root', str(a.root.parent / (a.root.name + '-' + name))], env=dict(os.environ, T71_MUTANT=name), capture_output=True, text=True)
        print(('KILLED:   ' if r.returncode else 'SURVIVED: ') + name)
        if not r.returncode: survivors.append(name)
        shutil.rmtree(a.root.parent / (a.root.name + '-' + name), ignore_errors=True)
    if survivors: print('SURVIVORS:', survivors); sys.exit(1)
sys.exit(1 if fails else 0)
