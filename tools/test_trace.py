#!/usr/bin/env python3
"""Game-free test of the typed trace (tools/win/wtrace.py) and its summary (tools/trace_summary.py) on recorded traces (tools/testdata/), with mutants.
usage: python3 tools/test_trace.py            run the checks and the mutants
       python3 tools/test_trace.py --record   write tools/testdata/bench_sim.jsonl again (the walker on five simulated levels, in the benchmark's protocol:
                                              walk_start .. giveup, teleport after a giveup, verdict); the numbers below then need a look"""
import json, os, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, 'win'))
DATA = os.path.join(HERE, 'testdata')
LEVELS = ('open_field', 'wall_gap', 'closed_room', 'locked_view', 'shore_bay', 'wall_80')


def record(path):
    """The benchmark's protocol (tools/win/wbench.py) on simulated levels: one trace file with a leg per level."""
    import navsim, wnav, wtrace
    tr = wtrace.Trace(path); tr.f.truncate(0)
    for i, name in enumerate(LEVELS):
        lv = navsim.BY_NAME[name](); sim = navsim.Sim(lv)
        with tempfile.TemporaryDirectory() as d:
            w = navsim.SimWalker(sim, d); w.tr = wtrace.Trace(None, clock=lambda: round(1000 + sim.t, 3)); r = w.go((lv.goal[0], lv.goal[1], wnav.EYE), lv.tol, lv.seconds, ztol=1.8, leg=i)
            if not r.ok: w.tr.emit('teleport', reason='the walk gave up (%s)' % r.why)
            w.tr.emit('verdict', leg=i, outcome='walked' if r.ok else 'teleported', seconds=r.seconds, panel='0x%x' % (0x100 + i), metres=r.walked, stuck=r.stuck, replans=r.replans, why=r.why)
            for rec in w.tr.lines: tr.f.write(json.dumps(rec) + '\n')
    tr.close()


CHECK = r'''
import json, os, sys, tempfile
sys.path.insert(0, D); sys.path.insert(0, os.path.join(HERE, 'win'))
import wtrace, trace_summary
fails = []
def check(name, ok, why=''):
    print('%-12s %s %s' % (name, 'ok' if ok else 'FAILED', why)); ok or fails.append(name)
def bad(**kv):
    try: wtrace.check(dict(t=1.0, **kv)); return False
    except ValueError: return True
check('schema', bad(kind='giveup', why='stuck') and bad(kind='nonsense') and bad(kind='bump', cell=[1, 2]) and not bad(kind='progress', dist_left=3.0), 'unknown kinds and missing fields are refused')
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, 't.jsonl'); t = wtrace.Trace(p, clock=lambda: 5.0); t.emit('teleport', reason='x'); t.emit('progress', dist_left=1.0)
    open(p, 'a').write('{"t": 6.0, "kind": "prog')      # (a line half written when the run was stopped)
    check('read', [r['kind'] for r in wtrace.read(p)] == ['teleport', 'progress'], 'a half-written last line is skipped, flushed lines are all there')
lines = wtrace.read(os.path.join(DATA, 'bench_sim.jsonl'))
rows, problems = trace_summary.summarize(lines)
res = {r['leg']: r['result'] for r in rows}
check('recorded', res == {0: 'walked', 1: 'walked', 2: 'teleported', 3: 'teleported', 4: 'walked', 5: 'teleported'} and not problems, 'rows %s, problems %s' % (res, problems))
by = {r['leg']: r for r in rows}
check('giveups', by[2]['giveup']['why'] in ('stuck', 'no way') and by[3]['giveup']['why'] == 'view does not turn' and by[5]['giveup']['stuck_class'] == 'wall' and by[0]['giveups'] == 0, 'closed room %s, locked view %s, wall_80 against %s' % (by[2]['giveup'], by[3]['giveup']['why'], by[5]['giveup']['stuck_class']))
check('why', by[2]['why'] == by[2]['giveup']['why'] and by[3]['why'] == 'view does not turn' and by[0]['why'] in ('', 'arrived'), 'row why %r %r' % (by[2]['why'], by[3]['why']))
check('counts', by[2]['stalls'] >= 1 and by[2]['bumps'] >= 3 and by[0]['replans'] >= 1 and by[1]['teleports'] == 0 and by[2]['teleports'] == 1, 'stalls %d bumps %d' % (by[2]['stalls'], by[2]['bumps']))
# a teleport without a giveup before it, and a walked leg with a giveup, are named
broken = [dict(t=1.0, kind='walk_start', leg=7, **{'from': [0, 0, 1], 'to': [1, 1, 1]}), dict(t=2.0, kind='teleport', reason='x'), dict(t=3.0, kind='verdict', leg=7, outcome='teleported', seconds=2.0)]
_, pr = trace_summary.summarize(broken)
check('no giveup', len(pr) == 1 and 'without a giveup' in pr[0], str(pr))
broken2 = [broken[0], dict(t=2.0, kind='giveup', why='stuck', dist_left=3.0, stuck_class='wall', last_way=[]), dict(t=3.0, kind='verdict', leg=7, outcome='walked', seconds=2.0)]
_, pr = trace_summary.summarize(broken2)
check('walked+giveup', len(pr) == 1 and 'walked, yet a giveup' in pr[0], str(pr))
# the table feeds tools/walk_bench.py
import walk_bench
legs_ = {r['panel']: r for r in rows}
check('walk_bench', 'total:' in walk_bench.table(legs_) , 'the rows are results.jsonl lines')
sys.exit(1 if fails else 0)
'''

MUTANTS = [
    ('tools/trace_summary.py', "        if r['kind'] == 'teleport' and not seen_give: bad.append(", "        if False: bad.append("),      # a teleport without a giveup goes unnoticed
    ('tools/trace_summary.py', "    elif v['outcome'] == 'walked' and any(r['kind'] == 'giveup' for r in L): bad.append(", "    elif False: bad.append("),
    ('tools/trace_summary.py', "result=(verdict or {}).get('outcome') or", "result='unknown' or"),      # (the verdict is not read)
    ('tools/trace_summary.py', "why=(last['why'] if last else '') or (verdict or {}).get('why', '')", "why=''"),
    ('tools/win/wtrace.py', "    if missing: raise ValueError(", "    if False: raise ValueError("),      # a line without its fields is accepted
    ('tools/win/wtrace.py', "    if k not in KINDS: raise ValueError(", "    if False: raise ValueError("),
    ('tools/win/wtrace.py', "        if self.f: self.f.write(json.dumps(rec) + '\\n'); self.f.flush()", "        if self.f: self.f.write(json.dumps(rec) + '\\n')"),      # not flushed (checked below by a file read before close)
    ('tools/win/wtrace.py', "            if n == len(open(path, errors='ignore').read().splitlines()): continue", "            if False: continue"),
]


def run_with(root):
    r = subprocess.run([sys.executable, '-c', 'D=%r\nHERE=%r\nDATA=%r\n' % (os.path.join(root, 'tools'), os.path.join(root, 'tools'), DATA) + CHECK], capture_output=True, text=True, timeout=300)
    return r.returncode == 0, ' | '.join(l for l in (r.stdout + r.stderr).strip().splitlines() if 'FAILED' in l or 'Error' in l)[:200]


def flush_check():
    import wtrace
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 't.jsonl'); t = wtrace.Trace(p); t.emit('teleport', reason='x')
        return len(open(p).read().splitlines()) == 1      # (read while still open: flushed at every line)


def main():
    if '--record' in sys.argv: record(os.path.join(DATA, 'bench_sim.jsonl')); print('recorded'); return 0
    root = os.path.dirname(HERE); bad = False
    ok, out = run_with(root); print('clean:', 'passed' if ok else 'FAILED %s' % out); bad |= not ok
    f = flush_check(); print('flush:', 'ok' if f else 'FAILED'); bad |= not f
    for rel, old, new in MUTANTS:
        with tempfile.TemporaryDirectory(prefix='trace-mut-') as d:
            os.makedirs(os.path.join(d, 'tools', 'win'))
            for g in ('tools/trace_summary.py', 'tools/walk_bench.py', 'tools/win/wtrace.py'): shutil.copy(os.path.join(root, g), os.path.join(d, g))
            path = os.path.join(d, rel); text = open(path).read()
            if old not in text: print('mutant text not found: %s' % old[:60]); bad = True; continue
            open(path, 'w').write(text.replace(old, new, 1))
            if 'self.f.flush()' in old or 'self.f.write' in old:
                sys.path.insert(0, os.path.join(d, 'tools', 'win'))
                import importlib, wtrace; importlib.reload(wtrace); killed = not flush_check(); sys.path.pop(0); importlib.reload(wtrace)
                if not killed: pass
            else: ok, out = run_with(d); killed = not ok
            print('mutant %-62s %s' % (old.strip()[:62], 'rejected' if killed else 'SURVIVED')); bad |= not killed
    print('ALL OK' if not bad else 'FAILED'); return 1 if bad else 0


if __name__ == '__main__': sys.exit(main())
