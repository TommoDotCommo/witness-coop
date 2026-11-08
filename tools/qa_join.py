#!/usr/bin/env python3
"""T2 scenario: finding each other and the invite (co-op standard B, C; launcher/netfind.c, launcher/joinglue.h, tools/win/wjoin.py). Two real launchers, no game:
A hosts Direct, B's Join list shows A from the LAN within 6 s and B joins from the row; B joins again from a pasted WIT- code; A's Friends can join by says Same network OK;
B remembers A as a recent host; an invite-only lobby is not listed and its code still joins. The router port is off in the test folders (upnp=0: the test never asks the
real router for a mapping; tools/test_coop_host.py runs that code against a fake router) and Tailscale is off (nothing leaves this PC's own networks).
Mutants: B ignores every CPS2 answer (the launcher build NF_MUT_IGNORE_REPLIES): A is not listed, so the run must fail;
A's Friends can join by rows are not laid out again when they appear (FJ_NO_RELAYOUT): rows without room, so the run must fail.
usage: qa_join.py [clean | pictures]  (tools/qa.sh join: leases two slots for their ports and window places)
  pictures: the clean run only, with "Friends reach this PC at" 127.0.0.1 in A's folder (no address of this PC in the pictures), and docs/showcase/launcher_join.jpg and
  launcher_lobby.jpg from window pictures of the two launchers"""
import os, random, shutil, subprocess, sys, time
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PYW = os.environ.get('PYW', 'python.exe')
OWNER = os.environ.get('WITNESS_RESOURCE_OWNER') or 'join'
RES = os.environ.get('WITNESS_RESOURCES', '')
RUN = os.path.join(os.environ.get('WITNESS_TMPDIR') or sys.exit('set WITNESS_TMPDIR (a scratch folder on the Windows disk: source local/env.sh)'), OWNER, 'join')
win = lambda p: subprocess.check_output(['wslpath', '-w', p], text=True).strip()

def folder(name, exe, lport, gport, player, extra='', face=False):
    d = os.path.join(RUN, name)
    for _ in range(20):      # (the last run's launchers may still hold their folder for a moment on the Windows disk)
        shutil.rmtree(d, ignore_errors=True)
        try: os.makedirs(d); break
        except FileExistsError: time.sleep(0.5)
    else: os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'witness64_d3d11.exe'), 'wb').close()
    open(os.path.join(d, 'witness_coop.cfg'), 'w').write('id=1\nlisten=%d\nname=%s\nforce_active=1\nlobby_port=%d\nupnp=0\nfind_tailscale=0\nvoice_mode=off\nvoice_mute_output=1\n%s' % (gport, player, lport, extra))
    open(os.path.join(d, 'witness_launcher_update.cfg'), 'w').write('update_check=0\n')      # no GitHub call from a test
    shutil.copy(exe, os.path.join(d, 'witness_launcher_under_test.exe'))
    src = os.path.join(os.environ.get('WITNESS_DIR1', ''), 'data-pc.zip')
    if face and os.path.exists(src):      # pictures: the game's archive (a hard link to the leased slot's copy, read only) so the launcher draws in the game's own face
        try: os.link(src, os.path.join(d, 'data-pc.zip'))
        except OSError as e: print('no game face for the pictures: %s' % e, flush=True)
    return d

def run(exeA, exeB, lport, mutant=False, pics=''):
    p1 = int(os.environ.get('WITNESS_PORT1', '47001')); nameA = 'Fox' if pics else 'Alpha%04d' % random.randint(0, 9999)
    dA, dB = folder('A', exeA, lport, p1, nameA, 'lobby_advertise=127.0.0.1\n' if pics else '', bool(pics)), folder('B', exeB, lport, p1 + 1, 'Owl' if pics else 'Beta', '', bool(pics))
    slots = (os.environ.get('WITNESS_SLOTS', '0,1').split(',') + ['0', '1'])[:2]
    rects = [(subprocess.run([RES, 'rect', s], capture_output=True, text=True).stdout.strip() if RES else '') or '0 0 1280 760' for s in slots]
    args = [PYW, '-I', win(os.path.join(ROOT, 'tools', 'win', 'wjoin.py')), win(os.path.join(dA, 'witness_launcher_under_test.exe')), win(dA), win(os.path.join(dB, 'witness_launcher_under_test.exe')), win(dB), str(lport), nameA, rects[0], rects[1]] + (['mutant'] if mutant else ['pictures', win(pics)] if pics else [])
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True); out = []
    for line in proc.stdout:
        print(line, end='', flush=True); out.append(line)
        if line.startswith('LAUNCHERPID ') and RES: subprocess.run([RES, 'register-pid', line.split()[1]], capture_output=True)      # a failed run cannot leave them behind
    proc.wait(timeout=300)
    return 'ALL OK' in ''.join(out)

def main():
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', 'build/witness_launcher_NF_MUT_IGNORE_REPLIES.exe', 'build/witness_launcher_FJ_NO_RELAYOUT.exe'], check=True, capture_output=True)
    fjmut = os.path.join(ROOT, 'launcher', 'build', 'witness_launcher_FJ_NO_RELAYOUT.exe')
    exe = os.path.join(ROOT, 'launcher', 'build', 'witness_launcher.exe'); mut = os.path.join(ROOT, 'launcher', 'build', 'witness_launcher_NF_MUT_IGNORE_REPLIES.exe')
    lport = int(os.environ.get('WITNESS_PORT1', '47001')) + 160
    if 'pictures' in sys.argv[1:]:
        pics = os.path.join(RUN, 'pictures'); shutil.rmtree(pics, ignore_errors=True); os.makedirs(pics)
        print('=== join: pictures', flush=True); ok = run(exe, exe, lport + 8, pics=pics)
        dst = os.path.join(ROOT, 'docs', 'showcase')
        ps = ("Add-Type -AssemblyName System.Drawing; $c = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | ? { $_.MimeType -eq 'image/jpeg' }; $p = New-Object System.Drawing.Imaging.EncoderParameters(1); "
              "$p.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]85); Get-ChildItem '%s' -Filter *.bmp | ForEach-Object { $i = [System.Drawing.Image]::FromFile($_.FullName); "
              "$i.Save('%s\\' + $_.BaseName + '.jpg', $c, $p); $i.Dispose() }; 'done'") % (win(pics), win(dst))
        print(subprocess.run(['powershell.exe', '-NoProfile', '-Command', ps], capture_output=True, text=True, timeout=120).stdout.strip())
        return 0 if ok else 1
    print('=== join: clean', flush=True); ok = run(exe, exe, lport); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if not ok or 'clean' in sys.argv[1:]: return 0 if ok else 1
    print('=== join: MUTANT B ignores CPS2 answers', flush=True); caught = not run(exe, mut, lport + 4, mutant=True); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    print('=== join: MUTANT A hosts and its Friends can join by rows are not laid out again when they appear', flush=True); caught2 = not run(fjmut, exe, lport + 12, mutant=True); print('mutant: %s' % ('caught' if caught2 else 'NOT CAUGHT'), flush=True)
    print('ALL OK' if caught and caught2 else 'FAILED'); return 0 if caught and caught2 else 1

if __name__ == '__main__': sys.exit(main())
