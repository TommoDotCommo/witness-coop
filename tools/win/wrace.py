# T2 check of the race mode, run with Windows Python. Two games (game_mode=race, race_goal=count:3): they wait for each other, count down together, and start at the same moment; what A
# solves before GO does not count; what A solves after GO is not solved in B's world (the panels are read from B's memory), but B sees A's progress; A reaches 3 and wins, B (1 panel)
# sees the result; both HUDs were drawn. The panels are solved with the solve test command in A's game, as a player's solve would.
# usage: wrace.py <pidA> <dirA> <pidB> <dirB> <expect_isolation 1|0>
import os, sys, time, re
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, isolated = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5] == '1'
A, B = attach(pa), attach(pb)
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.01)
    time.sleep(settle)
def rstat(d):
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('racestat: phase')); send(d, ['racestat']); t = time.time()
    while time.time() - t < 8:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('racestat: phase')]
        if len(idx) > n0:
            time.sleep(0.08); ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('racestat: phase')]
            m = re.match(r'racestat: phase (\w+) go_in (-?\d+) elapsed (\d+) solved (\d+) winner (\d+) decided (\d) goal (\S+) waiting (\d+) hud (\d+)', ls[idx[-1]])
            r = dict(phase=m[1], go_in=int(m[2]), elapsed=int(m[3]), solved=int(m[4]), winner=int(m[5]), decided=int(m[6]), goal=m[7], waiting=int(m[8]), hud=int(m[9]), st={})
            for l in ls[idx[-1] + 1:]:
                s = re.match(r'racestat: standing (\d+) place (\d+) phase (\w+) solved (\d+) finish (\d+) me (\d) gone (\d)', l)
                if not s: break
                r['st'][int(s[1])] = dict(place=int(s[2]), phase=s[3], solved=int(s[4]), finish=int(s[5]), me=int(s[6]), gone=int(s[7]))
            return r
        time.sleep(0.05)
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-104s %s  (%s)' % (name, 'ok' if cond else 'FAILED', detail), flush=True)
def panel_solved(P, eid):
    e = P.entity(eid); return bool(e) and P.u32(e + 0x294) == 0x3f800000 and P.u32(e + 0x298) == 1
def wait_for(pred, secs, what=''):
    end = time.time() + secs; last = None
    while time.time() < end:
        last = (rstat(da), rstat(db))
        if pred(*last): return last
        time.sleep(0.25)
    return last
PRE, MINE, THEIRS = 0x64, (0x182, 0x293, 0x295), 0x2C2
for e in (PRE,) + MINE + (THEIRS,):
    check('0. panel %#x is unsolved in both games at the start' % e, not panel_solved(A, e) and not panel_solved(B, e)) if e == PRE else None
a, b = wait_for(lambda a, b: a and b and a['phase'] == 'countdown' and b['phase'] == 'countdown', 120)
check('1. both worlds load, then they count down together (both in the countdown at once)', a and b and a['phase'] == 'countdown' and b['phase'] == 'countdown', a and b and (a['phase'], b['phase'], a['go_in'], b['go_in']))
if a and a['phase'] == 'countdown':
    send(da, ['solve %x' % PRE], 0.5)      # solved during the countdown: must not count
a, b = wait_for(lambda a, b: a and b and a['phase'] == 'running' and b['phase'] == 'running', 30)
check('2. the race starts in both games', a and b and a['phase'] == 'running' and b['phase'] == 'running', a and b and (a['phase'], b['phase']))
if a and b and a['phase'] == 'running':
    ea, eb = rstat(da), rstat(db)
    check('   they started at the same moment (the clocks differ by less than 400 ms)', ea and eb and abs(ea['elapsed'] - eb['elapsed']) < 400, ea and eb and (ea['elapsed'], eb['elapsed']))
    check('   the panel A solved in the countdown does not count', ea['solved'] == 0 and panel_solved(A, PRE), (ea['solved'], panel_solved(A, PRE)))
send(da, ['solve %x' % MINE[0], 'solve %x' % MINE[1]], 1.5)
a, b = rstat(da), rstat(db)
check('3. A solved 2 panels after GO: A counts 2 of 3', a and a['solved'] == 2 and a['phase'] == 'running', a and (a['solved'], a['phase']))
time.sleep(1.5); b = rstat(db)
check('   B sees A at 2 solved in the standings (A is first)', b and b['st'].get(1, {}).get('solved') == 2 and b['st'][1]['place'] == 1, b and b['st'].get(1))
SOLVED_BY_A = (PRE, MINE[0], MINE[1])
mine_in_b = [panel_solved(B, e) for e in SOLVED_BY_A]
check('   ... but those panels are NOT solved in B\'s world (read from B\'s memory)' if isolated else '   (leak mutant) those panels ARE solved in B\'s world', (not any(mine_in_b)) if isolated else all(mine_in_b), mine_in_b)
check('   they are solved in A\'s own world', all(panel_solved(A, e) for e in SOLVED_BY_A))
send(db, ['solve %x' % THEIRS], 1.0)
send(da, ['solve %x' % MINE[2]], 1.5)
a, b = rstat(da), rstat(db)
check('4. A solves its third panel and finishes first', a and a['phase'] == 'finished' and a['winner'] == 1 and a['st'][1]['finish'] > 0, a and (a['phase'], a['winner'], a['st'].get(1)))
check('   B knows: A has finished and won; B itself has 1 panel and is still racing', b and b['winner'] == 1 and b['st'][1]['phase'] == 'finished' and b['st'][2]['solved'] == 1 and b['phase'] == 'running' and b['st'][2]['place'] == 2, b and (b['winner'], b['st']))
check('   both agree on the time A took (the finish time A reports is what B shows)', a and b and a['st'][1]['finish'] == b['st'][1]['finish'], a and b and (a['st'][1]['finish'], b['st'][1]['finish']))
time.sleep(2.2); a, b = rstat(da), rstat(db)
check('5. the result is final for A after the grace period (B is still racing and may not have finished)', a and a['decided'] == 1 and b and b['decided'] == 1, a and b and (a['decided'], b['decided']))
check('6. both players\' HUDs were drawn (countdown, clock, standings, winner)', a and b and a['hud'] > 20 and b['hud'] > 20, a and b and (a['hud'], b['hud']))
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
