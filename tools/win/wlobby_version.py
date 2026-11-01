# T2 check of the mod version in the lobby, run with Windows Python: real launchers only (no game, no slot). Launchers A and B carry the same version, C another one.
# B joins A (same version): admitted, and both lists show the version. C joins A: refused with the result "version mismatch", its window says both versions in words,
# the member list of A does not change, and the Update button of the lobby page is shown. A launcher of the old lobby protocol is not part of this check (lobby_test covers its JOIN).
# usage: wlobby_version.py <launcher A> <dir A> <launcher B> <dir B> <launcher C (another version)> <dir C> <lobby port> <version of A> <version of C>
import os, re, subprocess, sys, time
exeA, dirA, exeB, dirB, exeC, dirC, lport, verA, verC = sys.argv[1:10]; lport = int(lport)
def sp(d): return os.path.join(d, 'lobby_stat.txt')
def cp(d): return os.path.join(d, 'launcher_cmd.txt')
def cmd(d, *lines):
    p = cp(d); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.05)
    open(p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(p + '.tmp', p)
    t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.05)
def stat(d):
    try: txt = open(sp(d), errors='ignore').read()
    except OSError: return None
    r = {'members': {}, 'mv': {}, 'lobby': {}, 'status': '', 'version': {}}
    for l in txt.splitlines():
        if l.startswith('lobby '): v = l.split(); r['lobby'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
        elif l.startswith('member '):
            m = re.match(r'member (\d+) name (.*) host (\d)', l)
            if m: r['members'][int(m[1])] = m[2]
        elif l.startswith('member_version '): _, i, v = l.split(' ', 2); r['mv'][int(i)] = v
        elif l.startswith('lobbystatus '): r['status'] = l[12:]
        elif l.startswith('version mod '): m = re.match(r'version mod (\S+) host (\S+)', l); r['version'] = {'mod': m[1], 'host': m[2]}
    return r
def wait(d, pred, secs):
    end = time.time() + secs; s = None
    while time.time() < end:
        s = stat(d)
        try:
            if s and pred(s): return s
        except (KeyError, IndexError): pass
        time.sleep(0.15)
    return s
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-112s %s  (%s)' % (name, 'ok' if cond else 'FAILED', str(detail)[:220]), flush=True)
for d in (dirA, dirB, dirC):
    for f in (sp(d), cp(d)):
        if os.path.exists(f): os.remove(f)
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
procs = []
try:
    for exe, d in ((exeA, dirA), (exeB, dirB), (exeC, dirC)):
        procs.append(subprocess.Popen([exe, '--dir', d, '--no-activate', '--stat-file', sp(d), '--cmd-file', cp(d)], startupinfo=si))
    a = wait(dirA, lambda s: True, 20); b = wait(dirB, lambda s: True, 20); c = wait(dirC, lambda s: True, 20)
    check('0. the three launchers are up; each reports the version it was built with', a and b and c and a['version']['mod'] == verA and b['version']['mod'] == verA and c['version']['mod'] == verC and verA != verC, (a and a['version'], c and c['version']))
    cmd(dirA, 'host %d' % lport); a = wait(dirA, lambda s: s['lobby']['active'], 10)
    inv = [l[7:].strip() for l in open(sp(dirA), errors='ignore').read().splitlines() if l.startswith('invite ')][0]
    cmd(dirB, 'join ' + inv); a = wait(dirA, lambda s: len(s['members']) == 2 and len(s['mv']) == 2, 15); b = wait(dirB, lambda s: s['lobby']['joined'] and len(s['members']) == 2 and len(s['mv']) == 2, 15)
    check('1. B (the same version) joins: both lists show two members', a and b and len(a['members']) == 2 and len(b['members']) == 2 and b['lobby']['joined'], (a and a['members'], b and b['lobby']))
    check('   the lobby list shows every member\'s version, on the host and on the client (here both %s)' % verA, a and b and set(a['mv'].values()) == {verA} and set(b['mv'].values()) == {verA}, (a and a['mv'], b and b['mv']))
    cmd(dirC, 'join ' + inv); c = wait(dirC, lambda s: s['lobby']['refused'] == 4, 15); time.sleep(1.5); a = stat(dirA); c = stat(dirC)
    check('2. C (another version) is refused with the version-mismatch result (4), not as a bad invite (1), a full lobby (2) or a bad protocol (3)', c and c['lobby']['refused'] == 4 and not c['lobby']['joined'], c and c['lobby'])
    check('   the window of C says both versions in words (the host\'s %s, its own %s) and that both must update' % (verA, verC), c and verA in c['status'] and verC in c['status'] and 'update' in c['status'].lower(), c and c['status'])
    check('   C knows the host\'s version', c and c['version']['host'] == verA, c and c['version'])
    check('   the lobby of A did not change: still two members, C is not in it', a and len(a['members']) == 2 and verC not in a['mv'].values(), a and (a['members'], a['mv']))
finally:
    for p in procs:
        try: p.terminate()
        except Exception: pass
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
