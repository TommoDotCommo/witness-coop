#!/usr/bin/env python3
"""Game-free release check: checksum, exact inventory, Windows PE headers, and that the zip ships the proxy, not the game's own DLL.

Pass the .zip path; the unpacked folder of the same name (version dots included) is accepted too.
"""
import hashlib
from pathlib import Path
import sys
import zipfile

EXPECTED = ['Witness-Coop-Setup.exe', 'AnselSDK64.dll', 'witness_launcher.exe', 'witness_models/adventurer.glb',
            'witness_models/adventurer.glb.txt', 'witness-coop.ps1', 'installer-gui.ps1', 'install-state.ps1',
            'install-policy.exe', 'README.txt', 'LICENSE.txt']
ORIGINAL_ANSEL = '3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15'


def resolve_archive(arg):
    """The zip for an argument that is either the zip or its same-named folder (a version such as v0.1.0-alpha has dots of its own)."""
    path = Path(arg)
    return path if path.name.endswith('.zip') else path.with_name(path.name + '.zip')


def check(archive, policy_source=None):
    archive = resolve_archive(archive)
    checksum, name = archive.with_name(archive.name + '.sha256').read_text().split()
    assert name == archive.name, 'checksum names a different asset'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == checksum, 'checksum mismatch'
    root = archive.name[:-len('.zip')] + '/'
    with zipfile.ZipFile(archive) as package:
        assert set(package.namelist()) == {root + f for f in EXPECTED}, 'unexpected release inventory'
        for payload in ['Witness-Coop-Setup.exe', 'AnselSDK64.dll', 'witness_launcher.exe', 'install-policy.exe']:
            assert package.read(root + payload)[:2] == b'MZ', 'not a Windows PE'
        proxy = package.read(root + 'AnselSDK64.dll')
        assert hashlib.sha256(proxy).hexdigest() != ORIGINAL_ANSEL, 'the zip holds the game\'s own Ansel DLL, not the proxy'
        assert b'witness_coop.cfg' in proxy, 'AnselSDK64.dll is not the co-op proxy'
        readme = package.read(root + 'README.txt')
        assert b'@VERSION@' not in readme, 'README.txt version not substituted'
        source = policy_source or Path(__file__).resolve().parents[1] / 'dist/install-state.ps1'
        assert package.read(root + 'install-state.ps1').decode().replace('\r\n', '\n') == Path(source).read_text(), 'stale policy'
    return 'PASS: release checksum, exact eleven-file inventory, proxy payload, policy script and Windows PE headers'


if __name__ == '__main__':
    print(check(sys.argv[1]))
