#!/usr/bin/env python3
"""coop-mod.json (tools/mod_manifest.py) and the VERSIONINFO of every binary, against a real release build, and mutants the check
(run by check_release_package.py) must reject:
  wrong-hash     the manifest's hash of dsound.dll does not match the zip
  wrong-size     the manifest's size of the model does not match
  root-differs   the copy at the zip root differs from the one in the top folder
  no-root-copy   the root copy is missing
  other-build    the manifest names a game build Setup does not check
  old-version    the launcher's VERSIONINFO names another version (the whole release check rejects it)
Game-free. Usage: test_mod_manifest.py [release zip]   (without one, builds a release into a temporary folder)"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_release_package  # noqa: E402
import mod_manifest  # noqa: E402
import versioninfo  # noqa: E402

TAG = 'v9.8.6-manifesttest'


def rewrite(src, dst, edit):
    """Copy the release zip with edit(name, bytes) -> bytes|None applied to each member; refresh the sidecar."""
    with zipfile.ZipFile(src) as a, zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as b:
        for info in a.infolist():
            data = edit(info.filename, a.read(info))
            if data is not None:
                b.writestr(info, data)
    dst.with_name(dst.name + '.sha256').write_text('%s  %s\n' % (hashlib.sha256(dst.read_bytes()).hexdigest(), dst.name))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        if len(sys.argv) > 1:
            zip_path = Path(sys.argv[1])
        else:
            out = ROOT / 'local/release'
            subprocess.run(['bash', str(ROOT / 'tools/release.sh'), TAG], check=True, capture_output=True)
            zip_path = out / ('witness-coop-%s.zip' % TAG)
        failures = []
        try:
            check_release_package.check(zip_path)
        except AssertionError as e:
            failures.append('real release rejected: %s' % e)
        top = zip_path.name[:-len('.zip')] + '/'

        def manifest_edit(fn, both=True):
            def edit(name, data):
                if name == 'coop-mod.json' or (both and name == top + 'coop-mod.json'):
                    m = json.loads(data)
                    fn(m)
                    return json.dumps(m, indent=2).encode() + b'\n'
                return data
            return edit

        def bad_hash(m):
            m['files'][0]['sha256'] = '0' * 64

        def bad_size(m):
            m['files'][2]['size'] += 1

        def other_home(m):
            m['homepage'] = 'https://example.invalid/'

        def other_build(m):
            m['game']['builds'][0]['sha256'] = '1' * 64
        mutants = {
            'wrong-hash': manifest_edit(bad_hash),
            'wrong-size': manifest_edit(bad_size),
            'root-differs': manifest_edit(other_home, both=False),
            'no-root-copy': lambda name, data: None if name == 'coop-mod.json' else data,
            'other-build': manifest_edit(other_build),
        }
        for label, edit in mutants.items():
            d = tmp / label
            d.mkdir()
            bad = d / zip_path.name
            rewrite(zip_path, bad, edit)
            try:       # the manifest check itself (check_release_package.check calls it), so no other check hides a survivor
                mod_manifest.check(bad)
                failures.append('mutant %s survived' % label)
            except (AssertionError, KeyError):
                pass
        # VERSIONINFO: every binary carries the zip's version and the product name (read back from the built files)
        with zipfile.ZipFile(zip_path) as z:
            version = zip_path.name[len('witness-coop-'):-len('.zip')]
            for pe in ('dsound.dll', 'witness_launcher.exe', 'install-policy.exe', 'Witness-Coop-Setup.exe'):
                data = z.read(top + pe)
                if versioninfo.read(data, 'ProductName') != 'The Witness Co-op' or versioninfo.read(data) != version:
                    failures.append('VERSIONINFO of %s: %r %r' % (pe, versioninfo.read(data, 'ProductName'), versioninfo.read(data)))
        # mutant: a binary whose ProductVersion is another version must fail the release check's metadata test
        with zipfile.ZipFile(zip_path) as z:
            launcher = z.read(top + 'witness_launcher.exe')
        stale = launcher.replace(version.encode('utf-16-le'), ('v0.0.1'.ljust(len(version), 'x')).encode('utf-16-le'))
        try:
            check_release_package.check_metadata(stale, version, 'witness_launcher.exe')
            failures.append('mutant old-version survived')
        except AssertionError:
            pass
    if failures:
        print('\n'.join('FAIL: ' + f for f in failures))
        return 1
    print('PASS: coop-mod.json matches the release zip; VERSIONINFO on every binary; %d manifest mutants and the old-version mutant rejected' % len(mutants))
    return 0


if __name__ == '__main__':
    sys.exit(main())
