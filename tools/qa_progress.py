#!/usr/bin/env python3
"""T2 scenario for the new-game option "start with these parts of the map completed" (proxy/progress, devmenu.cpp StartProgressTick). Game A starts with
start_progress=area:Symmetry Island armed in its cfg; once its world is up the mod must solve exactly what the game needs for that area (its panels and everything before them:
the tutorial gate, the Glass Factory that opens the island, ...), open exactly the doors those panels open, write the list next to the saves and disarm the setting;
game B (no setting) must follow through the sync. The expected set is computed here from tools' data file (an independent reading of proxy/progress/progression.txt),
and the games' own memory is read from outside (wstatedump.py): solved panels and open doors in both games.
Mutants: the setting is ignored (no_start_progress), only the area's own panels are solved (progress_no_prereq), the save's sidecar is not written (no_save_meta).
usage: qa_progress.py (called by tools/qa.sh progress)"""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, ROOT, pid, pyw, real_saves
import qa_sweep

SPEC = 'area:Symmetry Island'
AREA = 'Symmetry_Island'

def expected():
    """(solved panels, doors that must be open) for the spec, from the data file only"""
    area, need, name, opt, doors = {}, {}, {}, set(), []
    for l in open(os.path.join(ROOT, 'proxy', 'progress', 'progression.txt')):
        p = l.split()
        if not p: continue
        if p[0] == 'area': area[p[2]] = int(p[1])
        elif p[0] == 'panel': need[int(p[1], 16)] = {int(x, 16) for x in p[4:]}; name[int(p[1], 16)] = int(p[2])
        elif p[0] == 'optional': opt.add(int(p[1], 16))
        elif p[0] == 'door': doors.append((int(p[1], 16), [[int(x, 16) for x in alt.split('&')] for alt in p[2].split('|')]))
    mine = {i for i in name if name[i] == area[AREA] and i not in opt}
    want = set(mine)
    for i in mine: want |= need[i]
    opened = {d for d, alts in doors if any(all(x in want for x in alt) for alt in alts)}
    return want, opened, {d for d, _ in doors}, mine

def state(which):
    j = json.loads(pyw('wstatedump.py', pid(which)).strip().splitlines()[-1])
    return {int(k, 16) for k in j['panels']}, {int(k, 16) for k in j['doors']}

def send(d, lines):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    open(cmd + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmd + '.tmp', cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)

def run(cfg_a='', cfg_b=''):
    want, opened, known, mine = expected()
    qa_sweep.restart(('name=Alpha\nstart_progress=%s\n' % SPEC + cfg_a).strip(), ('name=Beta\n' + cfg_b).strip())
    t0 = time.time(); sa = sb = None
    while time.time() - t0 < 120:      # the mod waits for a stable world (6 s), then solves a few panels per frame
        sa, da = state('A')
        if sa >= want or time.time() - t0 > 60: break
        time.sleep(2)
    time.sleep(3); sa, da = state('A'); t1 = time.time(); sb, db = state('B')
    while sb != sa and time.time() - t1 < 40: time.sleep(2); sb, db = state('B')
    ok = True
    def check(what, cond, detail=''):
        nonlocal ok; ok = ok and bool(cond); print('  %-92s %s  %s' % (what, 'ok' if cond else 'FAILED', detail), flush=True)
    check('A solved every panel of the area and everything the game needs before it (%d panels)' % len(want), sa >= want, 'missing %d: %s' % (len(want - sa), sorted(hex(x) for x in (want - sa))[:6]))
    check('A solved nothing else (no panel outside the chosen part and what it needs)', sa <= want, 'extra %s' % sorted(hex(x) for x in (sa - want))[:6])
    check('the tutorial gate (0x3629) and the Glass Factory (0x343A) are solved: the way to the island was made', {0x3629, 0x343A} <= sa)
    open_known = da & known
    check('A: exactly the doors those panels open are open (%d doors of the %d the data knows)' % (len(opened), len(known)), open_known == opened, 'wrong: %s' % sorted(hex(x) for x in (open_known ^ opened))[:8])
    check('B (no setting) holds the same solved panels as A through the sync', sb == sa, 'only A %d, only B %d' % (len(sa - sb), len(sb - sa)))
    check('B: the same doors are open', (db & known) == open_known)
    cfg = open(os.path.join(DIR1, 'witness_coop.cfg')).read().splitlines()
    check('the setting is disarmed in the cfg (a normal start next time)', not any(l.startswith('start_progress=') for l in cfg) and any(l.startswith('# applied: start_progress=') for l in cfg))
    side = os.path.join(DIR1, 'appdata', 'The Witness', 'coop_progress.txt')
    n = len([l for l in open(side)]) if os.path.exists(side) else -1
    check('the list is kept next to the saves (coop_progress.txt)', n == len(want), n)
    # the save: the pause menu makes the game write its campaign; the mod writes the sidecar beside it
    saves = os.path.join(DIR1, 'appdata', 'The Witness')
    send(DIR1, ['key ESC down']); time.sleep(0.2); send(DIR1, ['key ESC up'])
    meta = None; t0 = time.time()
    while time.time() - t0 < 40 and not meta:
        time.sleep(1)
        try:
            for f in os.listdir(saves):
                if f.endswith('.coop.txt'): meta = open(os.path.join(saves, f)).read()
        except OSError: pass
    send(DIR1, ['key ESC down']); time.sleep(0.2); send(DIR1, ['key ESC up'])
    lines = (meta or '').splitlines(); pl = [l.split('\t') for l in lines if l.startswith('player\t')]
    prog = next((l.split() for l in lines if l.startswith('progress ')), None)
    check('a sidecar <save>.coop.txt is written next to the game save', meta is not None)
    check('it names both players who were in the game (Alpha and Beta)', {p[2] for p in pl} == {'Alpha', 'Beta'}, [p[2] for p in pl])
    sym = next((l.split() for l in lines if l.startswith('area %s ' % AREA)), None)
    check('it records the progress: the panels solved in game A, and the area complete', prog and int(prog[1]) == len(sa) and sym and sym[2] == sym[3], (prog, sym))
    return ok

def main():
    results = []
    print('=== progress: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca in (('no_start_progress (the setting is ignored)', 'no_start_progress=1'), ('progress_no_prereq (only the area, not what the game needs first)', 'progress_no_prereq=1'), ('no_save_meta (no sidecar next to the save)', 'no_save_meta=1')):
        print('=== progress: MUTANT %s' % name, flush=True); caught = not run(ca); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
