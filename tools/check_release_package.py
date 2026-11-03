#!/usr/bin/env python3
"""Game-free release check: checksum, exact inventory, Windows PE headers, that the zip ships the proxy (not the game's own DLL), and that the
one-file Setup.exe carries exactly the same files as the zip, under the checksum compiled into it.

  check_release_package.py <zip> [--single <Witness-Coop-Setup.exe>] [--require-signed]

coop-mod.json (tools/mod_manifest.py) is checked against the zip's files, and every binary's VERSIONINFO against the zip's version.

Pass the .zip path; the unpacked folder of the same name (version dots included) is accepted too.
--require-signed: every .exe and .dll (the zip's, the one-file setup's, and those inside its payload) must carry a signature block.
"""
import hashlib
from pathlib import Path
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_payload import unpack
from pe_signature import is_signed
import mod_manifest
import versioninfo

EXPECTED = ['Witness-Coop-Setup.exe', 'dsound.dll', 'witness_launcher.exe', 'witness_models/adventurer.glb',
            'witness_models/adventurer.glb.txt', 'witness-coop.ps1', 'installer-gui.ps1', 'install-state.ps1',
            'install-policy.exe', 'install-linux.sh', 'README.txt', 'LICENSE.txt', 'coop-mod.json']
ROOT_FILES = ['coop-mod.json']      # beside the top folder as well, for mod managers
ONE_FILE_SKIPS = {'Witness-Coop-Setup.exe', 'install-linux.sh'}      # in the zip only
PE_FILES = ['Witness-Coop-Setup.exe', 'dsound.dll', 'witness_launcher.exe', 'install-policy.exe']
ORIGINAL_ANSEL = '3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15'


def resolve_archive(arg):
    """The zip for an argument that is either the zip or its same-named folder (a version such as v0.1.0-alpha has dots of its own)."""
    path = Path(arg)
    return path if path.name.endswith('.zip') else path.with_name(path.name + '.zip')


def check_metadata(data, version, name):
    """Every binary says what it is and which version (VERSIONINFO): mod managers, file properties and diagnostics read it."""
    assert versioninfo.read(data, 'ProductName') == 'The Witness Co-op', 'ProductName of %s is not The Witness Co-op' % name
    assert versioninfo.read(data) == version, 'ProductVersion of %s is not %s' % (name, version)


def one_file_payload(exe):
    """({name: bytes} of the payload in a one-file Setup.exe, container bytes). The checksum compiled in beside it must be the container's."""
    at = exe.find(b'WCPAYLD1')
    assert at > 0, 'the one-file setup holds no payload'
    assert exe.find(b'WCPAYLD1', at + 1) < 0, 'the payload tag occurs twice'
    files, used = unpack(exe[at:], exact=False)
    container = exe[at:at + used]
    digest = hashlib.sha256(container).hexdigest().encode()
    assert exe.count(digest) == 1, 'the checksum compiled into the setup is not the one of its payload'
    return files, container


def check(archive, policy_source=None, single=None, require_signed=False):
    archive = resolve_archive(archive)
    checksum, name = archive.with_name(archive.name + '.sha256').read_text().split()
    assert name == archive.name, 'checksum names a different asset'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == checksum, 'checksum mismatch'
    root = archive.name[:-len('.zip')] + '/'
    with zipfile.ZipFile(archive) as package:
        assert set(package.namelist()) == {root + f for f in EXPECTED} | set(ROOT_FILES), 'unexpected release inventory'
        version = archive.name[len('witness-coop-'):-len('.zip')]
        for payload in PE_FILES:
            assert package.read(root + payload)[:2] == b'MZ', 'not a Windows PE'
            check_metadata(package.read(root + payload), version, payload)
        assert not any('ansel' in n.lower() for n in package.namelist()), 'the zip holds an Ansel DLL: the game\'s own file must never be shipped or replaced'
        proxy = package.read(root + 'dsound.dll')
        assert hashlib.sha256(proxy).hexdigest() != ORIGINAL_ANSEL, 'the zip holds the game\'s own Ansel DLL, not the proxy'
        assert b'witness_coop.cfg' in proxy and b'witness-coop:proxy:dsound' in proxy, 'dsound.dll is not the co-op proxy'
        assert b'DirectSoundCreate8' in proxy and b'DirectSoundEnumerateA' in proxy, 'dsound.dll does not export the DirectSound functions the game imports'
        readme = package.read(root + 'README.txt')
        assert b'@VERSION@' not in readme, 'README.txt version not substituted'
        source = policy_source or Path(__file__).resolve().parents[1] / 'dist/install-state.ps1'
        assert package.read(root + 'install-state.ps1').decode().replace('\r\n', '\n') == Path(source).read_text(), 'stale policy'
        script = package.read(root + 'install-linux.sh')
        assert b'\r' not in script and script.startswith(b'#!/bin/sh'), 'install-linux.sh must have LF line endings (it runs under sh)'
        assert script.decode() == (Path(__file__).resolve().parents[1] / 'dist/install-linux.sh').read_text(), 'stale install-linux.sh'
        if require_signed:
            unsigned = [f for f in PE_FILES if not is_signed(package.read(root + f))]
            assert not unsigned, 'not signed in the zip: ' + ', '.join(unsigned)
        mod_manifest.check(archive)
        notes = ['checksum', 'exact inventory', 'coop-mod.json against the zip', 'VERSIONINFO of every binary', 'proxy payload', 'policy script', 'Windows PE headers']
        if single:
            exe = Path(single).read_bytes()
            assert exe[:2] == b'MZ', 'the one-file setup is not a Windows PE'
            check_metadata(exe, version, 'the one-file Setup.exe')
            sums = Path(single).with_name(Path(single).name + '.sha256')
            if sums.exists():
                assert sums.read_text().split()[0] == hashlib.sha256(exe).hexdigest(), 'the one-file setup\'s checksum file is stale'
            files, _ = one_file_payload(exe)
            assert set(files) == {f for f in EXPECTED if f not in ONE_FILE_SKIPS}, 'the one-file setup holds a different file set: ' + ', '.join(sorted(files))
            for rel, data in files.items():
                assert data == package.read(root + rel), 'the one-file setup and the zip differ in ' + rel
            if require_signed:
                assert is_signed(exe), 'the one-file Setup.exe is not signed'
                unsigned = [f for f in PE_FILES if f in files and not is_signed(files[f])]
                assert not unsigned, 'not signed inside the one-file setup (signing must come before embedding): ' + ', '.join(unsigned)
            notes.append('one-file setup carries the same files under its own checksum')
        if require_signed:
            notes.append('signatures')
    return 'PASS: release ' + ', '.join(notes)


if __name__ == '__main__':
    args = sys.argv[1:]
    single = args[args.index('--single') + 1] if '--single' in args else None
    print(check(args[0], single=single, require_signed='--require-signed' in args))
