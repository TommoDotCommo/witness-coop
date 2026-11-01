#!/usr/bin/env python3
"""T2 scenario for the diagnostics (common/diag_log.h, common/crash_handler.h, proxy/diag.cpp, launcher Send diagnostics, dist/diag.ps1, tools/diag_report.py):
two real games play a short scripted session (A walks and solves a panel, B follows), then
  * the real F2 key (a window message to A's window) makes A save its flight recording, and B, told by the marker, saves its own of the same moment;
  * a desync the sync core reports makes B dump;
  * an exception the game handles itself (IsBadReadPtr) on A leaves no crash marker and no dump, and A carries on;
  * a real crash on B (test_crash=1, the command testcrash) leaves the crash lines, <session>.crash and a minidump, and the session has no clean-exit line;
  * both launchers press Send diagnostics; tools/diag_report.py on the two zips shows the marker on both, the crash with module and offset on B (mapped to the proxy's
    object file), and the zips hold no Windows user name, computer name or address.
Mutants (a second pair of games): crash_handler=0 on B (the crash must leave nothing), diag_first_chance=1 on A (a handled exception must not leave a dump: this
mutant makes one), no_diag_mark_send=1 on A (B must hear the marker: it does not). Each of the three checks must FAIL in the mutant run.
usage: qa_diag.py [clean]   (called by tools/qa.sh diag; evidence in local/qa/<date>-diag)"""
import json, os, re, subprocess, sys, time, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, RUN, PYW, pid, win
import qa_sweep
import diag_report, minidump, map_symbols

LDIR = os.path.join(ROOT, 'launcher', 'build')
EVID = os.path.join(ROOT, 'local', 'qa', time.strftime('%Y%m%d-%H%M%S') + '-diag'); os.makedirs(EVID, exist_ok=True)
WITNESS_RESOURCES = os.environ.get('WITNESS_RESOURCES', '')
CFG = 'diag_dir=instance\ntest_crash=1\n'
LOGS = 'witness-coop-logs'      # diag_dir=instance: beside the DLL, not in the game's own logs folder

def send(d, lines):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    open(cmd + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmd + '.tmp', cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)

def log(d): return open(os.path.join(d, 'witness_proxy.log'), errors='ignore').read()
def files(d, suffix): return sorted(f for f in os.listdir(os.path.join(d, LOGS)) if f.endswith(suffix)) if os.path.isdir(os.path.join(d, LOGS)) else []
def session_log(d):
    """This run's session log (the game's, not the launcher's), as text."""
    fs = [f for f in files(d, '.log') if '_launcher_' not in f]
    return open(os.path.join(d, LOGS, fs[-1]), errors='ignore').read() if fs else ''
def alive(p): return str(p) in subprocess.run(['tasklist.exe', '/FI', 'PID eq %s' % p, '/NH'], capture_output=True, text=True).stdout
def wait(cond, timeout, step=0.3):
    t = time.time()
    while time.time() - t < timeout:
        if cond(): return True
        time.sleep(step)
    return False

def post_key(p, vk):
    """A real key press for the game's window: WM_KEYDOWN / WM_KEYUP to the window of process p (the path the F1 guide and the marker take)."""
    code = ("import ctypes,sys\nu=ctypes.windll.user32;p=int(sys.argv[1]);vk=int(sys.argv[2]);found=[]\n"
            "E=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)\n"
            "def cb(h,_):\n pid=ctypes.c_ulong();u.GetWindowThreadProcessId(h,ctypes.byref(pid))\n if pid.value==p and u.IsWindowVisible(h) and u.GetWindowTextLengthW(h)>0: found.append(h)\n return True\n"
            "u.EnumWindows(E(cb),0)\nfor h in found[:1]:\n u.PostMessageW(h,0x100,vk,0x003C0001);u.PostMessageW(h,0x101,vk,0xC03C0001)\nprint(len(found))")
    return subprocess.run([PYW, '-I', '-c', code, str(p), str(vk)], capture_output=True, text=True).stdout.strip()

def names_to_grep():
    ps = subprocess.run(['powershell.exe', '-NoProfile', '-Command', '$env:USERNAME; $env:COMPUTERNAME'], capture_output=True, text=True).stdout.replace('\r', '').split()
    wsl = [subprocess.run(c, capture_output=True, text=True).stdout.strip() for c in (['whoami'], ['hostname'])]
    return sorted({n for n in ps + wsl if len(n) >= 3})

def launcher_bundle(which, d, outdir):
    """Start this game folder's launcher minimised, press Send diagnostics, wait for the zip, close the launcher. Returns the zip path."""
    exe = os.path.join(d, 'witness_launcher_diag.exe'); subprocess.run(['cp', os.path.join(LDIR, 'witness_launcher.exe'), exe], check=True)
    cmdf = os.path.join(d, 'launcher_cmd.txt'); os.makedirs(outdir, exist_ok=True)
    args = "'--dir','%s','--no-activate','--cmd-file','%s','--diag-out','%s'" % (win(d), win(cmdf), win(outdir))
    out = subprocess.run(['powershell.exe', '-NoProfile', '-Command', "(Start-Process -FilePath '%s' -ArgumentList %s -WindowStyle Minimized -PassThru).Id" % (win(exe), args)], capture_output=True, text=True).stdout.strip()
    lp = int(out); open(os.path.join(RUN, 'launcher%s.pid' % which), 'w').write(str(lp))
    if WITNESS_RESOURCES: subprocess.run([WITNESS_RESOURCES, 'register-pid', str(lp)], capture_output=True)
    time.sleep(3); open(cmdf, 'w').write('diag\n')
    ok = wait(lambda: any(f.startswith('witness-coop-diag_') and f.endswith('.zip') for f in os.listdir(outdir)), 90)
    open(cmdf, 'w').write('quit\n'); wait(lambda: not alive(lp), 15)
    if alive(lp): subprocess.run(['taskkill.exe', '/PID', str(lp), '/F'], capture_output=True)
    zips = sorted(f for f in os.listdir(outdir) if f.endswith('.zip'))
    return os.path.join(outdir, zips[-1]) if ok and zips else None

def scenario(label, cfg_a, cfg_b, bundle):
    """One pair of games through the whole script; returns {check name: bool}."""
    r = {}
    def check(name, ok, detail=''):
        r[name] = bool(ok); print(('  PASS  ' if ok else '  FAIL  ') + name + ('' if ok else '   ' + str(detail)[:200]), flush=True)
    print('=== diag: %s' % label, flush=True)
    for d in (DIR1, DIR2): subprocess.run(['rm', '-rf', os.path.join(d, LOGS)])
    qa_sweep.restart(cfg_a=CFG + cfg_a, cfg_b=CFG + cfg_b)
    pa, pb = int(pid('A')), int(pid('B'))
    check('both games started with their own session log', len(files(DIR1, '.log')) == 1 and len(files(DIR2, '.log')) == 1, (files(DIR1, '.log'), files(DIR2, '.log')))
    check('witness_proxy.log is still the last run (the test tools read it)', 'proxy loaded' in log(DIR1))
    # --- a short scripted session: A walks, solves the first panel; B follows
    send(DIR1, ['key W down']); time.sleep(3); send(DIR1, ['key W up']); send(DIR1, ['solve 64'])
    check('the session is live: B applied A\'s solve', wait(lambda: 'remote panel 0x64 applied as solved' in log(DIR2), 30), log(DIR2)[-300:])
    time.sleep(2)      # the recorder has samples of the two players
    # --- the marker: the real F2 key on A
    n = post_key(pa, 0x71)
    check('A has a window to press F2 in', n == '1', n)
    check('A logged its marker', wait(lambda: 'MARK 1 pressed here' in log(DIR1), 15), log(DIR1)[-300:])
    check('B heard the marker', wait(lambda: 'MARK 1 (from player 1)' in log(DIR2), 15), log(DIR2)[-300:])
    check('A saved a flight recording of the marker', wait(lambda: files(DIR1, '.flight-1-mark.txt'), 10))
    check('B saved one of the same moment', wait(lambda: files(DIR2, '.flight-1-markp1.txt'), 10))
    fa = open(os.path.join(DIR1, LOGS, files(DIR1, '.flight-1-mark.txt')[0]), errors='ignore').read() if files(DIR1, '.flight-1-mark.txt') else ''
    srows = [l for l in fa.splitlines() if l.startswith('S ')]
    check('the recording has 10 samples a second with the other player and a round trip', len(srows) >= 15 and any(re.search(r'\| 2 \d+ \d+ [\d.]+ ', l) for l in srows) and any('| 2 -1 ' not in l for l in srows[-5:]), srows[-2:])
    check('the recording carries the log tail', '# log tail' in fa and 'applied as solved' in fa or 'solved -> event' in fa)
    # --- a desync the sync core reports
    send(DIR2, ['diagdesync'])
    check('B logged the desync and dumped', wait(lambda: 'DESYNC ' in log(DIR2) and any('desync' in f for f in files(DIR2, '.txt')), 10), files(DIR2, '.txt'))
    # --- a handled exception on A: no marker, no dump, still alive
    send(DIR1, ['testhandled']); handled = wait(lambda: 'handled access violation done' in log(DIR1), 10)
    time.sleep(1.5)
    check('a handled exception happened on A', handled)
    check('a handled exception leaves no crash marker and no dump, and A carries on', handled and not files(DIR1, '.crash') and not files(DIR1, '.dmp') and alive(pa), (files(DIR1, '.crash'), files(DIR1, '.dmp')))
    # --- a real crash on B
    send(DIR2, ['testcrash'])
    died = wait(lambda: not alive(pb), 60)
    check('B died from the injected crash', died)
    time.sleep(1)
    sl = session_log(DIR2)
    check('B left a crash marker', len(files(DIR2, '.crash')) == 1, files(DIR2, '.crash'))
    check('B left a minidump', any(os.path.getsize(os.path.join(DIR2, LOGS, f)) > 10000 for f in files(DIR2, '.dmp')), files(DIR2, '.dmp'))
    m = re.search(r'crash: unhandled exception (0x[0-9a-f]+) at (\S+)\+0x([0-9a-f]+)', sl)
    check('the session log names the exception, module and offset', m and m.group(2) == 'AnselSDK64.dll', sl[-400:])
    check('the crashed session has no clean exit line', 'session end: clean exit' not in sl)
    if m and files(DIR2, '.dmp'):
        try:
            dmp = minidump.Minidump(open(os.path.join(DIR2, LOGS, files(DIR2, '.dmp')[0]), 'rb').read())
            check('the dump agrees with the log (exception code, module and offset)', dmp.exception['code'] == int(m.group(1), 16) and dmp.locate(dmp.exception['address']) == 'AnselSDK64.dll+0x' + m.group(3), dmp.locate(dmp.exception['address']))
            mp = os.path.join(ROOT, 'proxy', 'AnselSDK64.map')
            if os.path.exists(mp):
                desc = map_symbols.SymbolMap(open(mp, errors='replace').read()).describe(int(m.group(3), 16)); print('  map: AnselSDK64.dll+0x%s -> %s' % (m.group(3), desc))
                check('the map puts the crash in proxy/diag.cpp', 'diag.o' in desc, desc)
        except Exception as e: check('the dump is readable', False, e)
    # --- Send diagnostics from both launchers
    if bundle:
        names = names_to_grep(); zips = {}
        for which, d in (('A', DIR1), ('B', DIR2)): zips[which] = launcher_bundle(which, d, os.path.join(EVID, 'bundle' + which))
        check('both launchers made a zip', all(zips.values()), zips)
        if all(zips.values()):
            rep = subprocess.run([sys.executable, os.path.join(TOOLS, 'diag_report.py'), zips['A'], zips['B'], '--map', os.path.join(ROOT, 'proxy', 'AnselSDK64.map'), '--dll', os.path.join(ROOT, 'proxy', 'AnselSDK64.dll'), '--timeline', os.path.join(EVID, 'timeline.txt'), '--align'], capture_output=True, text=True)
            open(os.path.join(EVID, 'report.txt'), 'w').write(rep.stdout); print(rep.stdout)
            ra, rb = [json.loads(subprocess.run([sys.executable, os.path.join(TOOLS, 'diag_report.py'), zips[w], '--json', '--map', os.path.join(ROOT, 'proxy', 'AnselSDK64.map')], capture_output=True, text=True).stdout)[0] for w in 'AB']
            ga = [s for s in ra['sessions'] if s['program'] == 'game'][0]; gb = [s for s in rb['sessions'] if s['program'] == 'game'][0]
            check('the report shows the marker in A\'s zip', any('pressed here' in t for _u, t in ga['marks']), ga['marks'])
            check('the report shows the marker in B\'s zip', any('from player 1' in t for _u, t in gb['marks']), gb['marks'])
            c = gb.get('crash') or {}
            check('the report shows the crash in B\'s zip with module and offset, mapped', gb['crashed'] and c.get('module') == 'AnselSDK64.dll' and c.get('offset') and c.get('map') and 'diag.o' in c['map'] and c.get('dump'), c)
            check('A\'s zip has no crash', not ga['crashed'] and 'crash' not in ga)
            check('the report pairs the marker across the two zips', 'mark 1 pressed on' in rep.stdout, rep.stdout[-300:])
            check('both zips have a launcher log', any(s['program'] == 'launcher' for s in ra['sessions']) and any(s['program'] == 'launcher' for s in rb['sessions']))
            check('the chains are intact in both zips', all(s['chain'] == 'ok' for s in ra['sessions'] + rb['sessions']), [(s['session'], s['chain_problem']) for s in ra['sessions'] + rb['sessions'] if s['chain'] != 'ok'])
            hits = {}
            for w, zp in zips.items():
                z = zipfile.ZipFile(zp)
                for nm in z.namelist():
                    blob = z.read(nm)
                    for n in names:
                        if re.search(rb'(?<![A-Za-z0-9])' + re.escape(n.encode()) + rb'(?![A-Za-z0-9])', blob, re.I) or re.search(re.escape(n.encode('utf-16-le')), blob, re.I): hits.setdefault(w, []).append((nm, n))
            print('  grep of both zips for %s: %s' % (names, hits or 'no match'))
            open(os.path.join(EVID, 'grep.txt'), 'w').write('names searched (word-bounded, ASCII and UTF-16): %s\nmatches: %s\n' % (names, hits or 'none'))
            check('the zips hold none of the workstation\'s user or computer names', not hits, hits)
    for p in (pa, pb):
        if alive(p): subprocess.run(['taskkill.exe', '/PID', str(p), '/F'], capture_output=True)
    return r

def main():
    subprocess.run(['make', '-C', os.path.join(ROOT, 'proxy')], check=True, capture_output=True)
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher'], check=True, capture_output=True)
    clean = scenario('clean build', '', '', True)
    bad = [k for k, v in clean.items() if not v]
    print('clean: %s' % ('PASS' if not bad else 'FAIL %s' % bad), flush=True)
    results = [('clean', not bad)]
    if 'clean' not in sys.argv[1:]:
        mut = scenario('MUTANTS: crash_handler=0 on B, diag_first_chance=1 and no_diag_mark_send=1 on A', 'diag_first_chance=1\nno_diag_mark_send=1\n', 'crash_handler=0\n', False)
        for name in ('B left a crash marker', 'a handled exception leaves no crash marker and no dump, and A carries on', 'B heard the marker'):
            caught = mut.get(name) is False; results.append(('mutant: ' + name, caught)); print('mutant "%s": %s' % (name, 'caught' if caught else 'NOT CAUGHT'), flush=True)
    failed = [n for n, ok in results if not ok]
    print('evidence: %s' % EVID); print('ALL OK' if not failed else 'FAILED: %s' % failed)
    return 1 if failed else 0

if __name__ == '__main__': sys.exit(main())
