#!/usr/bin/env python3
"""Game-free (T0) tests of the launcher's self-update (launcher/update.h), run through launcher/update_cli.cpp.
Fake GitHub responses, corrupt and truncated packages, hash mismatch, a rate limit, an apply through the installer's receipts (modified file,
no receipt, leftovers), an interrupted apply that startup recovery rolls back, a downgrade, the game running, and the settings; a folder in the old
layout (the proxy in AnselSDK64.dll, the game's file as AnselSDK64_real.dll) migrated to dsound.dll, refused when a hash is not the game's, and
an interrupted migration rolled back and tried again.
Every protection has a source mutant (the line is changed, the tests must fail): the list is MUTANTS."""
import hashlib, io, json, os, subprocess, sys, tempfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'launcher/update.h').read_text()
PREFIX = 'witness-coop'

MUTANTS = {
    'ver-major': ('if (a->major != b->major) return a->major > b->major ? 1 : -1;', ''),
    'ver-minor': ('if (a->minor != b->minor) return a->minor > b->minor ? 1 : -1;', ''),
    'ver-patch': ('if (a->patch != b->patch) return a->patch > b->patch ? 1 : -1;', ''),
    'ver-prerelease-below-release': ('if (a->pre[0] && !b->pre[0]) return -1;', ''),
    'ver-numeric-identifiers': ('return strlen(ia + sa) > strlen(ib + sb) ? 1 : -1;', 'return 0;'),
    'ver-dev-never-offered': ('if (lu_is_dev_build(current) || !lu_parse_semver(current, &cur)) return 0;', 'if (!lu_parse_semver(current, &cur)) return 0;'),
    'dev-describe-suffix': ('if (p - (g + 2) >= 7 && *p == 0) return 1;', ''),
    'throttle-interval': ('if (s->last_check && now < s->last_check + LU_INTERVAL_SEC) return 0;', ''),
    'throttle-retry-after': ('if (now < s->retry_after) return 0;', ''),
    'throttle-auto-flag': ('if (!s->auto_enabled) return 0;', ''),
    'http-429': ('status_code == 429 || ', ''),
    'http-403-ratelimit': ('(status_code == 403 && ratelimit_remaining == 0)', '0'),
    'json-nul-check': ('|| memchr(json, 0, len)', ''),
    'url-scheme-check': ('&& lu_url_valid(url))', '&& 1)'),
    'asset-both-required': ('if (!out->zip_url[0] || !out->zip_size || !out->sha_url[0] || !out->sha_size)', 'if (!out->zip_url[0])'),
    'prerelease-filter': ('if (!include_prereleases) return LU_UP_TO_DATE;', ''),
    'draft-filter': ('if (v && lu_json_bool(v, end))\n        return LU_UP_TO_DATE;', ''),
    'sidecar-name-match': ('|| strncmp(text + name_start, expected_zip_name, name_len)', ''),
    'download-size-check': ('actual_size != declared_size || ', ''),
    'download-sha256-check': ('if (strcmp(actual_hash, expected_sha256) != 0)', 'if (0)'),
    'zip-crc32-check': ('if (lu_crc32(buf, uncomp_sz) != crc)', 'if (0)'),
    'zip-path-safe': ('if (!lu_zip_path_safe((const char *)(zip + pos + 46), nlen)) return LU_ERR_ARCHIVE;', ''),
    'pe-magic-check': ("bytes[0] != 'M' || bytes[1] != 'Z'", '0'),
    'pe-machine-check': ('if (lu_u16(bytes + pe_off + 4) != machine) return 0;', ''),
    'apply-downgrade': ('if (ver == 0) return LU_ERR_DOWNGRADE;', ''),
    'apply-dev-build': ('if (lu_is_dev_build(current_ver) || !lu_parse_semver(current_ver, &curv)) return LU_ERR_DEV;', ''),
    'apply-game-running': ('if (game_running) return LU_ERR_RUNNING;', ''),
    'apply-receipt-state': ('else if (st != CI_CURRENT) { r = LU_ERR_RECEIPT; goto done; }', 'else if (st == CI_MISSING) { r = LU_ERR_RECEIPT; goto done; }'),
    'apply-leftover-refused': ('if (lu_exists(aux)) return CI_CONFLICT;\n    lu_path(aux, sizeof(aux), dir, t->name, ".coop-receipt.json.tmp");', 'lu_path(aux, sizeof(aux), dir, t->name, ".coop-receipt.json.tmp");'),
    'apply-journal-block': ('if (lu_journal_status(dir, NULL, 0)) return LU_ERR_RECEIPT;', ''),
    'apply-rollback-on-fail': ('fail_rollback:\n    if (lu_rollback(dir) != LU_OK) r = LU_ERR_ROLLBACK;', 'fail_rollback:\n    r = r;'),
    'receipt-original-kept': ('lu_write_receipt(target, rc[i].original, newh[i], NULL)', 'lu_write_receipt(target, NULL, newh[i], NULL)'),
    'receipt-installed-written': ('lu_write_receipt(target, rc[i].original, newh[i], NULL)', 'lu_write_receipt(target, rc[i].original, rc[i].installed, NULL)'),
    'recover-pending': ('if (lu_journal_status(dir, NULL, 0) == 1) return lu_rollback(dir) == LU_OK ? 1 : -1;', ''),
    'rollback-verifies-backup': ('if (!lu_file_sha256(prev, got) || strcmp(got, want) != 0) {', 'if (0) {'),
    'ansel-original-untouched': ('{ "witness_models/adventurer.glb.txt", 0, 0 }\n};', '{ "witness_models/adventurer.glb.txt", 0, 0 },\n    { "AnselSDK64.dll", 0, 0 }\n};'),
    'legacy-original-verified': ('static inline int lu_is_original(const char *h) { return h && h[0] && !strcmp(h, LU_ANSEL_SHA); }', 'static inline int lu_is_original(const char *h) { return h && h[0]; }'),
    'legacy-old-proxy-left-behind': ('if (legacy > 0 && !lu_migrate_legacy(dir)) { r = LU_ERR_LEGACY; goto fail_rollback; }      /* MUT:migrate */', ''),
    'legacy-added-file-rolled-back': ('            remove(target); remove(rpath);\n            continue;', '            continue;'),
}


def pe(fill, machine=0x8664):
    b = bytearray([fill] * 600)
    b[0:2] = b'MZ'; b[60:64] = (64).to_bytes(4, 'little'); b[64:68] = b'PE\0\0'; b[68:70] = machine.to_bytes(2, 'little')
    return bytes(b)


def sha(b): return hashlib.sha256(b).hexdigest()


def files(tag, fill, glb=b'glb-' ):
    return {'dsound.dll': pe(fill), 'witness_launcher.exe': pe(fill + 1), 'witness_models/adventurer.glb': glb + tag.encode() * 40, 'witness_models/adventurer.glb.txt': b'model ' + tag.encode()}


def make_zip(tag, fill, deflate=True, skip=(), machine=0x8664, extra=None, nomz=False):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED) as z:
        for name, data in files(tag, fill).items():
            if name in skip: continue
            if name == 'dsound.dll' and machine != 0x8664: data = pe(fill, machine)
            if name == 'dsound.dll' and nomz: data = b'XX' + data[2:]
            z.writestr(f'{PREFIX}-{tag}/{name}', data)
        z.writestr(f'{PREFIX}-{tag}/README.txt', b'readme')
        if extra: z.writestr(extra, b'evil')
    return buf.getvalue()


ORIG_ANSEL = b'the games own ansel dll' * 20        # the test build of the CLI takes its hash as the game's own (LU_ANSEL_SHA)
OTHER_DSOUND = b'another mods dsound.dll' * 20       # was in the game folder before Setup: kept as dsound.dll.coop-original and dsound.chain.dll
OLD_PROXY = pe(0x31) + b' real Ansel DLL %s'           # the v0.1.0-alpha proxy in AnselSDK64.dll


def receipt(path, original, installed):
    Path(str(path) + '.coop-receipt.json').write_text(json.dumps({'schema': 1, 'original': original, 'installed': installed, 'pending': None}, separators=(',', ':')))


def make_game(root, tag='v0.1.0', fill=0x11, with_receipts=True):
    """A game folder as Setup leaves it: the game's own AnselSDK64.dll untouched, managed files with receipts; Setup found another mod's
    dsound.dll there, so it is kept as dsound.chain.dll and as the proxy's original."""
    root.mkdir(parents=True, exist_ok=True)
    (root / 'witness_models').mkdir(exist_ok=True)
    (root / 'AnselSDK64.dll').write_bytes(ORIG_ANSEL)
    (root / 'dsound.chain.dll').write_bytes(OTHER_DSOUND)
    if with_receipts:
        (root / 'dsound.dll.coop-original').write_bytes(OTHER_DSOUND); receipt(root / 'dsound.chain.dll', None, sha(OTHER_DSOUND))
    for name, data in files(tag, fill).items():
        (root / name).write_bytes(data)
        if with_receipts: receipt(root / name, sha(OTHER_DSOUND) if name == 'dsound.dll' else None, sha(data))
    return root


def make_legacy(root, tag='v0.1.0', fill=0x11):
    """A game folder as the alpha's Setup left it: the proxy in AnselSDK64.dll (the game's file kept as AnselSDK64_real.dll and as its
    .coop-original), no dsound.dll, the launcher and the model with receipts."""
    root.mkdir(parents=True, exist_ok=True)
    (root / 'witness_models').mkdir(exist_ok=True)
    (root / 'AnselSDK64.dll').write_bytes(OLD_PROXY); receipt(root / 'AnselSDK64.dll', sha(ORIG_ANSEL), sha(OLD_PROXY))
    (root / 'AnselSDK64.dll.coop-original').write_bytes(ORIG_ANSEL)
    (root / 'AnselSDK64_real.dll').write_bytes(ORIG_ANSEL); receipt(root / 'AnselSDK64_real.dll', None, sha(ORIG_ANSEL))
    for name, data in files(tag, fill).items():
        if name == 'dsound.dll': continue
        (root / name).write_bytes(data); receipt(root / name, None, sha(data))
    return root


def tree(root):
    out = {}
    for p in sorted(Path(root).rglob('*')):
        if p.is_file(): out[str(p.relative_to(root))] = sha(p.read_bytes())
    return out


def release_json(tags, prerelease=(), draft=(), assets=True, sidecar=True, base='https://example.invalid/dl'):
    rel = []
    for t in tags:
        a = []
        if assets:
            a.append({'name': f'{PREFIX}-{t}.zip', 'size': 123456, 'browser_download_url': f'{base}/{t}/{PREFIX}-{t}.zip'})
            if sidecar: a.append({'name': f'{PREFIX}-{t}.zip.sha256', 'size': 90, 'browser_download_url': f'{base}/{t}/{PREFIX}-{t}.zip.sha256'})
            a.append({'name': 'Witness-Coop-Setup.exe', 'size': 99999, 'browser_download_url': f'{base}/{t}/Witness-Coop-Setup.exe'})
        rel.append({'tag_name': t, 'draft': t in draft, 'prerelease': t in prerelease, 'published_at': '2026-11-01T10:00:00Z', 'body': f'Notes for {t}\nsecond line', 'assets': a})
    return json.dumps(rel)


class Runner:
    def __init__(self, src_text, work):
        self.work = Path(work)
        (self.work / 'update.h').write_text(src_text)
        # the header includes ../dist/install-policy.h: keep the same relative layout inside the work folder
        (self.work / '../dist') if False else None
        d = self.work / 'launcher'; d.mkdir(exist_ok=True); (self.work / 'dist').mkdir(exist_ok=True)
        (d / 'update.h').write_text(src_text); (d / 'update_cli.cpp').write_text((ROOT / 'launcher/update_cli.cpp').read_text())
        (self.work / 'dist/install-policy.h').write_text((ROOT / 'dist/install-policy.h').read_text())
        self.exe = self.work / 'cli'
        r = subprocess.run(['g++', '-std=c++17', '-O1', '-w', '-DLU_ANSEL_SHA="%s"' % sha(ORIG_ANSEL), '-o', str(self.exe), str(d / 'update_cli.cpp')], capture_output=True, text=True)
        if r.returncode: raise SystemExit('compile failed:\n' + r.stderr[:2000])

    def run(self, *args):
        r = subprocess.run([str(self.exe), *map(str, args)], capture_output=True, text=True)
        assert r.returncode == 0, (args, r.stdout, r.stderr)
        d = {}
        for line in r.stdout.splitlines():
            if line.startswith('notes='): d['notes'] = line[6:]; continue
            for part in line.split(' '):
                if '=' in part: k, v = part.split('=', 1); d[k] = v
        return d


fails = []
checks = 0
quiet = False


def check(what, ok):
    global checks
    checks += 1
    if not ok:
        fails.append(what)
        if not quiet: print('FAILED:', what)


def suite(R, tmp):
    tmp = Path(tmp)
    # ---- versions
    newer = lambda cur, cand: int(R.run('newer', cur, cand)['newer'])
    check('v0.2.0 is newer than v0.1.0', newer('v0.1.0', 'v0.2.0') == 1)
    check('a newer minor beats a larger patch', newer('v0.1.9', 'v0.2.0') == 1 and newer('v0.2.0', 'v0.1.9') == 0)
    check('a newer major wins', newer('v0.9.9', 'v1.0.0') == 1 and newer('v1.0.0', 'v0.9.9') == 0)
    check('a newer patch wins', newer('v0.1.0', 'v0.1.1') == 1 and newer('v0.1.1', 'v0.1.0') == 0)
    check('the same version is not newer', newer('v0.1.0', 'v0.1.0') == 0)
    check('the release is newer than its own beta, not the other way', newer('v0.2.0-beta.1', 'v0.2.0') == 1 and newer('v0.2.0', 'v0.2.0-beta.1') == 0)
    check('beta.10 is newer than beta.9 (numbers, not text)', newer('v0.2.0-beta.9', 'v0.2.0-beta.10') == 1 and newer('v0.2.0-beta.10', 'v0.2.0-beta.9') == 0)
    check('beta.2 is newer than beta.1', newer('v0.2.0-beta.1', 'v0.2.0-beta.2') == 1)
    check('a tag that is not a version is -1', newer('v0.1.0', 'nightly') == -1 and newer('v0.1.0', 'v1.2') == -1)
    check('a development build (after a tag, dirty, a bare commit, dev) is never offered a replacement', all(newer(c, 'v9.0.0') == 0 for c in ('v0.1.0-3-g1a2b3c4', 'v0.1.0-dirty', '1a2b3c4', 'dev', 'v0.1.0-3-g1a2b3c4-dirty')))
    check('prerelease detection', [int(R.run('prerelease', v)['prerelease']) for v in ('v0.1.0', 'v0.2.0-beta.1', 'v0.1.0-3-g1a2b3c4', 'dev')] == [0, 1, 1, 1])
    # ---- the release list (fake GitHub API responses)
    jf = tmp / 'rel.json'
    def check_json(text, pre, cur='v0.1.0'):
        jf.write_text(text); return R.run('check', jf, PREFIX, pre, cur)
    r = check_json(release_json(['v0.2.0', 'v0.1.0']), 0)
    check('the newest release with both assets is found, with its notes', r['result'] == '0' and r['tag'] == 'v0.2.0' and r['newer'] == '1' and r['zip'] == 'witness-coop-v0.2.0.zip' and r['sha'] == 'witness-coop-v0.2.0.zip.sha256' and 'Notes for v0.2.0' in r['notes'])
    r = check_json(release_json(['v0.1.0', 'v0.2.0']), 0)
    check('an older release listed first does not hide the newer one', r['tag'] == 'v0.2.0')
    r = check_json(release_json(['v0.3.0-beta.1', 'v0.2.0'], prerelease=['v0.3.0-beta.1']), 0)
    check('a pre-release is not offered unless allowed', r['tag'] == 'v0.2.0' and r['prerelease'] == '0')
    r = check_json(release_json(['v0.3.0-beta.1', 'v0.2.0'], prerelease=['v0.3.0-beta.1']), 1)
    check('a pre-release is offered when allowed', r['tag'] == 'v0.3.0-beta.1' and r['prerelease'] == '1')
    r = check_json(release_json(['v0.3.0', 'v0.2.0'], draft=['v0.3.0']), 1)
    check('a draft is never offered', r['tag'] == 'v0.2.0')
    r = check_json(release_json(['v0.2.0'], sidecar=False), 0)
    check('a release without its .sha256 asset is refused (never an unverified update)', r['result'] == str(r_code('ASSET')))
    r = check_json(release_json(['v0.2.0'], assets=False), 0)
    check('a release without assets is refused', r['result'] == str(r_code('ASSET')))
    r = check_json(release_json(['v0.2.0'], base='http://example.invalid'), 0)
    check('an asset url that is not https (or a local test server) is refused', r['result'] == str(r_code('ASSET')))
    r = check_json(release_json(['v0.2.0'], base='http://127.0.0.1:8123'), 0)
    check('a local test server url is accepted', r['result'] == '0')
    r = check_json('{"tag_name": "v0.2.0"', 0)
    check('truncated JSON is an error, not a release', r['result'] != '0')
    r = check_json('[]', 0)
    check('an empty list is "up to date"', r['result'] == str(r_code('UP_TO_DATE')))
    jf.write_bytes(b'[{"tag_name":"v0.2.0"}\x00]'); r = R.run('check', jf, PREFIX, 0, 'v0.1.0')
    check('a NUL byte in the response is refused', r['result'] == str(r_code('JSON')))
    # ---- throttling and rate limits
    cfg = tmp / 'up.cfg'
    day = 86400
    def sched(text, cur, now, manual):
        cfg.write_text(text); return R.run('schedule', cfg, cur, now, manual)
    s = sched('update_last_check=1000\n', 'v0.1.0', 1000 + 3600, 0)
    check('at most one automatic check a day', s['can'] == '0')
    s = sched('update_last_check=1000\n', 'v0.1.0', 1000 + day + 1, 0)
    check('a check is due after a day', s['can'] == '1')
    s = sched('update_last_check=1000\n', 'v0.1.0', 1000 + 60, 1)
    check('a manual check is not held by the daily limit', s['can'] == '1')
    s = sched('update_check=0\n', 'v0.1.0', 99999999, 0)
    check('the setting update_check=0 turns the automatic check off (a manual one still works)', s['can'] == '0' and sched('update_check=0\n', 'v0.1.0', 99999999, 1)['can'] == '1')
    s = sched('update_retry_after=5000\n', 'v0.1.0', 4000, 1)
    check('a rate limit (retry_after) holds even a manual check', s['can'] == '0')
    check('pre-releases: auto follows the installed version', sched('', 'v0.1.0', 1, 0)['pre'] == '0' and sched('', 'v0.2.0-beta.1', 1, 0)['pre'] == '1')
    check('pre-releases: the setting overrides', sched('update_prereleases=1\n', 'v0.1.0', 1, 0)['pre'] == '1' and sched('update_prereleases=0\n', 'v0.2.0-beta.1', 1, 0)['pre'] == '0')
    c = R.run('classify', 429, -1, 120, 1000); check('HTTP 429 waits for Retry-After', c['result'] == str(r_code('RATELIMIT')) and c['retry_after'] == '1120')
    c = R.run('classify', 403, 0, 0, 1000); check('HTTP 403 with no quota left is a rate limit (an hour)', c['result'] == str(r_code('RATELIMIT')) and c['retry_after'] == '4600')
    c = R.run('classify', 403, 10, 0, 1000); check('HTTP 403 with quota left is a plain error', c['result'] == str(r_code('HTTP')))
    c = R.run('classify', 200, -1, 0, 1000); check('HTTP 200 stamps the time of the check', c['result'] == '0' and c['last_check'] == '1000')
    c = R.run('classify', 0, -1, 0, 1000); check('no answer is offline', c['result'] == str(r_code('OFFLINE')))
    # ---- sidecar and download verification
    z2 = make_zip('v0.2.0', 0x22); name = f'{PREFIX}-v0.2.0.zip'
    zf = tmp / name; zf.write_bytes(z2)
    sc = tmp / 'sc.txt'
    sc.write_text(f'{sha(z2)}  {name}\n'); s = R.run('sidecar', sc, name)
    check('the .sha256 file of sha256sum is read', s['result'] == '0' and s['hash'] == sha(z2))
    sc.write_text(f'{sha(z2)}  other.zip\n'); check('a .sha256 for another file name is refused', R.run('sidecar', sc, name)['result'] == str(r_code('SIDECAR')))
    sc.write_text(f'{sha(z2)}  witness-coop-v0.3.0.zip\n'); check('a .sha256 for another file of the same length is refused', R.run('sidecar', sc, name)['result'] == str(r_code('SIDECAR')))
    sc.write_text(f'{sha(z2)[:60]}  {name}\n'); check('a short hash is refused', R.run('sidecar', sc, name)['result'] == str(r_code('SIDECAR')))
    check('a verified download passes', R.run('verify', zf, sha(z2), len(z2), 0)['result'] == '0')
    check('a different hash is a checksum failure', R.run('verify', zf, '0' * 64, len(z2), 0)['result'] == str(r_code('CHECKSUM')))
    check('a truncated download (size differs from the release) is refused', R.run('verify', zf, sha(z2), len(z2) + 10, 0)['result'] == str(r_code('TRUNCATED')))
    # ---- the apply, through the receipts
    def apply(game, zipbytes, cur='v0.1.0', tag='v0.2.0', running=0, fail=0, crash=0, zname='pkg.zip'):
        zp = tmp / zname; zp.write_bytes(zipbytes)
        return int(R.run('apply', game, zp, PREFIX, cur, tag, running, fail, crash)['result'])
    OK, UPTODATE = 0, r_code('UP_TO_DATE')
    g = make_game(tmp / 'g-ok'); before = tree(g)
    check('a verified package updates the managed files', apply(g, z2) == OK)
    after = tree(g)
    new = files('v0.2.0', 0x22)
    check('every managed file is the new one', all(after[n] == sha(d) for n, d in new.items()))
    rc = json.loads((g / 'dsound.dll.coop-receipt.json').read_text())
    check('the receipt follows the installer: installed is the new hash, pending is null, original is kept', rc == {'schema': 1, 'original': sha(OTHER_DSOUND), 'installed': sha(new['dsound.dll']), 'pending': None})
    rc2 = json.loads((g / 'witness_models/adventurer.glb.coop-receipt.json').read_text())
    check('a file without an original keeps original null', rc2['original'] is None and rc2['installed'] == sha(new['witness_models/adventurer.glb']))
    check("the game's own AnselSDK64.dll, the chain file and the proxy's kept original are untouched", all(after[k] == before[k] for k in ('AnselSDK64.dll', 'dsound.chain.dll', 'dsound.dll.coop-original', 'dsound.chain.dll.coop-receipt.json')))
    leftovers = [n for n in after if n.endswith(('.coop-stage', '.coop-prev', '.coop-receipt.json.tmp', '.coop-receipt.json.coop-prev')) or n == 'witness_update.state']
    check('no stage, backup, temp or journal file is left (the installer would call them recovery artifacts)', not leftovers)
    check('the running launcher image is kept as .coop-old until the next start, then removed', after.get('witness_launcher.exe.coop-old') == before['witness_launcher.exe'] and R.run('recover', g)['recovered'] == '0' and not (g / 'witness_launcher.exe.coop-old').exists())
    check('the same package again is "already up to date"', apply(g, z2, cur='v0.1.0') == UPTODATE)
    # the installer's own decision for the new state: every file is "current" for the new package (receipt installed == file == package)
    # refusals leave the folder byte for byte as it was
    def refused(label, game, zipbytes, code, **kw):
        b = tree(game); r = apply(game, zipbytes, **kw); a = tree(game)
        check(label + ' (code %d, expected %d)' % (r, code), r == code and a == b)
    g = make_game(tmp / 'g-dl'); refused('a package older than the installed version is refused (downgrade)', g, make_zip('v0.0.9', 0x09), r_code('DOWNGRADE'), cur='v0.1.0', tag='v0.0.9')
    refused('the same version is refused', g, make_zip('v0.1.0', 0x11), r_code('DOWNGRADE'), cur='v0.1.0', tag='v0.1.0')
    refused('a development build is never replaced', g, z2, r_code('DEV'), cur='v0.1.0-3-g1a2b3c4')
    refused('the game running from the folder blocks the update', g, z2, r_code('RUNNING'), running=1)
    refused('a package with a missing member is refused', g, make_zip('v0.2.0', 0x22, skip=('witness_models/adventurer.glb',)), r_code('ARCHIVE'))
    refused('a 32-bit proxy in the package is refused', g, make_zip('v0.2.0', 0x22, machine=0x014c), r_code('PE'))
    bad = bytearray(z2); h = bad.index(b'PK\x03\x04'); i = h + 30 + int.from_bytes(bad[h + 26:h + 28], 'little') + int.from_bytes(bad[h + 28:h + 30], 'little') + 4; bad[i] ^= 0xFF
    refused('a corrupted package is refused (CRC)', g, bytes(bad), r_code('ARCHIVE'))
    st = bytearray(make_zip('v0.2.0', 0x22, deflate=False)); h = st.index(b'PK\x03\x04'); j = h + 30 + int.from_bytes(st[h + 26:h + 28], 'little') + 500; st[j] ^= 0xFF
    refused('a stored member whose content does not match its CRC is refused', g, bytes(st), r_code('ARCHIVE'))
    refused('a package with a path that climbs out (../) is refused as a whole', g, make_zip('v0.2.0', 0x22, extra='../evil.dll'), r_code('ARCHIVE'))
    refused('a proxy that is not an executable (no MZ) is refused', g, make_zip('v0.2.0', 0x22, nomz=True), r_code('PE'))
    refused('a truncated package is refused', g, z2[: len(z2) // 2], r_code('ARCHIVE'))
    refused('a stored (uncompressed) package works the same as a deflated one only if valid: empty input refused', g, b'', r_code('ARCHIVE'))
    g2 = make_game(tmp / 'g-mod'); (g2 / 'dsound.dll').write_bytes(pe(0x77))
    refused('a proxy changed by hand (not what the receipt says) is not replaced', g2, z2, r_code('RECEIPT'))
    g3 = make_game(tmp / 'g-norc', with_receipts=False)
    refused('files without receipts (not installed by Setup) are not replaced', g3, z2, r_code('RECEIPT'))
    g4 = make_game(tmp / 'g-stage'); (g4 / 'witness_launcher.exe.coop-stage').write_bytes(b'x')
    refused("a leftover of an interrupted setup (.coop-stage) is refused like the installer does", g4, z2, r_code('RECEIPT'))
    g5 = make_game(tmp / 'g-badrc'); (g5 / 'dsound.dll.coop-receipt.json').write_text('{"schema":2}')
    refused('a receipt of another schema is refused', g5, z2, r_code('RECEIPT'))
    g6 = make_game(tmp / 'g-orig'); (g6 / 'dsound.dll.coop-original').write_bytes(b'tampered')
    refused('a kept original that no longer matches its receipt is refused', g6, z2, r_code('RECEIPT'))
    g7 = make_game(tmp / 'g-journal'); (g7 / 'witness_update.state').write_text('[update]\nstatus=pending\n')
    refused('an unsettled earlier update blocks a new one', g7, z2, r_code('RECEIPT'))
    # an update that fails half way is put back
    for step in (1, 2, 3):
        g = make_game(tmp / f'g-fail{step}'); b = tree(g)
        r = apply(g, z2, fail=step); a = tree(g)
        check(f'a failure after {step} replaced file(s) is rolled back to the exact previous state (files, receipts, no leftovers)', r == r_code('IO') and a == b)
    # an update whose process dies half way: the next start rolls it back
    for step in (1, 3):
        g = make_game(tmp / f'g-crash{step}'); b = tree(g)
        r = apply(g, z2, fail=step, crash=1); mid = tree(g)
        check(f'(setup) a crash after {step} files leaves a half-updated folder and a pending journal', r == r_code('IO') and mid != b and 'witness_update.state' in mid)
        rec = R.run('recover', g)['recovered']; a = tree(g)
        check(f'startup recovery of a crash after {step} files restores the previous state exactly', rec == '1' and a == b)
        check('after recovery the update can be tried again', apply(g, z2) == OK)
    g = make_game(tmp / 'g-nobk'); apply(g, z2, fail=2, crash=1); (g / 'dsound.dll.coop-prev').write_bytes(b'corrupt backup')
    check('a damaged backup is not copied over the file by a rollback (the rollback reports failure)', R.run('recover', g)['recovered'] == '-1' and tree(g)['dsound.dll'] == sha(new['dsound.dll']))
    # ---- the old layout (up to v0.1.0-alpha), migrated by the update when the game is closed
    def migrated(game):
        t = tree(game)
        return (t.get('dsound.dll') == sha(new['dsound.dll']) and t.get('AnselSDK64.dll') == sha(ORIG_ANSEL) and
                not any(n.startswith(('AnselSDK64_real', 'AnselSDK64.dll.')) for n in t) and
                json.loads((game / 'dsound.dll.coop-receipt.json').read_text()) == {'schema': 1, 'original': None, 'installed': sha(new['dsound.dll']), 'pending': None} and
                all(t[n] == sha(d) for n, d in new.items()) and 'witness_update.state' not in t and not any(n.endswith(('.coop-stage', '.coop-prev')) for n in t))
    g = make_legacy(tmp / 'g-legacy')
    check('an old-layout folder is updated to the new layout: dsound.dll added, the game\'s own AnselSDK64.dll back, the old proxy and receipts gone', apply(g, z2) == OK and migrated(g))
    g = make_legacy(tmp / 'g-legacy-cut'); (g / 'AnselSDK64.dll').write_bytes(ORIG_ANSEL)
    check('a migration interrupted after the restore (the game\'s file back, leftovers there) finishes', apply(g, z2) == OK and migrated(g))
    g = make_legacy(tmp / 'g-legacy-run')
    refused('the old layout is not touched while the game runs', g, z2, r_code('RUNNING'), running=1)
    g = make_legacy(tmp / 'g-legacy-tamper'); (g / 'AnselSDK64_real.dll').write_bytes(b'not the game\'s file')
    refused('an AnselSDK64_real.dll that is not the game\'s own blocks the update, nothing changed', g, z2, r_code('LEGACY'))
    g = make_legacy(tmp / 'g-legacy-foreign'); (g / 'AnselSDK64.dll').write_bytes(pe(0x55)); (g / 'AnselSDK64.dll.coop-receipt.json').unlink()
    refused('an AnselSDK64.dll that is not this mod\'s old proxy blocks the update, nothing changed', g, z2, r_code('LEGACY'))
    g = make_legacy(tmp / 'g-legacy-crash'); b = tree(g)
    r = apply(g, z2, fail=1, crash=1); rec = R.run('recover', g)['recovered']; a = tree(g)
    check('a migration whose process dies after adding dsound.dll is rolled back at the next start (dsound.dll gone, the old layout as it was)', r == r_code('IO') and rec == '1' and a == b)
    check('after that recovery the migration can be tried again', apply(g, z2) == OK and migrated(g))


CODES = {}


def r_code(name):
    return CODES['LU_' + ('ERR_' if name not in ('UP_TO_DATE', 'THROTTLED') else '') + name]


def load_codes():
    import re
    body = re.search(r'enum lu_result \{(.*?)\};', SRC, re.S).group(1)
    n = 0
    for line in body.split('\n'):
        m = re.match(r'\s*(LU_[A-Z_]+)', line)
        if m: CODES[m.group(1)] = n; n += 1


def main():
    load_codes()
    with tempfile.TemporaryDirectory() as t:
        t = Path(t); (t / 'clean').mkdir()
        R = Runner(SRC, t / 'clean'); (t / 'clean/w').mkdir()
        suite(R, t / 'clean/w')
        print(f'{checks} checks, {len(fails)} failed')
        if fails: return 1
        survivors = []
        only = sys.argv[1:]
        for name, (before, after) in MUTANTS.items():
            if only and name not in only: continue
            assert SRC.count(before) == 1, (name, SRC.count(before))
            d = t / ('m-' + name); d.mkdir(); (d / 'w').mkdir()
            n0 = len(fails)
            try:
                M = Runner(SRC.replace(before, after), d)
                saved = list(fails); fails.clear(); globals()['quiet'] = True
                try: suite(M, d / 'w')
                except AssertionError as e: fails.append('assertion: ' + str(e)[:80])
                except Exception as e: fails.append('error: ' + str(e)[:80])
                killed = bool(fails); fails.clear(); fails.extend(saved)
            except SystemExit:
                killed = True      # does not even compile
            print(('KILLED:   ' if killed else 'SURVIVED: ') + name)
            if not killed: survivors.append(name)
        if survivors: print('SURVIVORS:', survivors); return 1
    print('launcher update: all checks passed, all mutants killed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
