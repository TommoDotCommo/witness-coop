#!/usr/bin/env python3
"""Game-free (T0) test of the playthrough's divergence check (divergence.py) on recorded runs (data/recorded/, events trimmed to the
solves, propagations and divergences of three real runs).

 - 20261030-030632 (4 bots): only the Shadows timer panels differed, on 45 comparisons: known (allow-list), the check passes;
 - 20261030-130023 (2 bots): door 0x14724 stayed shut in one world for 10 minutes (a door that shut and opened again within 100 ms; the
   core's open dedupe, fixed since): unknown persistent, the check fails;
 - 20261030-014435 (4 bots): hedge maze doors 0x199a, 0x19e6, 0x1a0e differed on several comparisons: unknown, not on the allow-list.
Mutants that must fail: one persistent divergence injected into the clean run; the check with persistence ignored (a transient one
injected must not fail it, the classifier must keep it transient); a resolved divergence must not fail, an unresolved one must.
usage: python3 test_divergence.py"""
import copy, os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import divergence as D

REC = os.path.join(HERE, 'data', 'recorded')
fails = 0
def check(ok, what):
    global fails
    print(('PASS  ' if ok else 'FAIL  ') + what); fails += not ok

clean = D.load(os.path.join(REC, '20261030-030632.jsonl'))
c = D.classify(clean)
check(set(c) == {0x334db, 0x334dc} and all(v['persistent'] and v['allowed'] for v in c.values()) and not D.unknown(c),
      'a run where only the timer panels differed: both known, nothing unknown')

door = D.classify(D.load(os.path.join(REC, '20261030-130023.jsonl')))
check(D.unknown(door) == [0x14724] and door[0x14724]['what'] == 'door' and door[0x14724]['persistent'] == 12, 'the door left shut in one world (0x14724) is an unknown persistent divergence')

hedge = D.unknown(D.classify(D.load(os.path.join(REC, '20261030-014435.jsonl'))))
check({0x199a, 0x19e6, 0x1a0e} <= set(hedge) and 0x19b24 not in hedge, 'hedge maze doors are unknown, the Shadows timed door is known')

# mutant: one persistent divergence injected into the clean run must fail the check
inj = copy.deepcopy(clean); t = inj[-1]['t'] + 1
inj.append(dict(t=t, kind='divergence', persistent=True, between=[1, 3], panels_only_here=[], panels_missing=['0x24'], doors_differ=[]))
check(D.unknown(D.classify(inj)) == [0x24], 'injected persistent divergence (panel 0x24 missing on one world) fails the check')
inj2 = copy.deepcopy(clean); inj2.append(dict(t=t, kind='divergence', panel='0x8d', origin=2, missing_on=1, after=60))
check(D.unknown(D.classify(inj2)) == [0x8d], 'injected solve that never arrived fails the check')

# a transient difference (in flight) is not a failure
tr = copy.deepcopy(clean); tr.append(dict(t=t, kind='divergence', persistent=False, between=[1, 2], panels_only_here=['0x24'], panels_missing=[], doors_differ=['0x1a29']))
ct = D.classify(tr)
check(not D.unknown(ct) and ct[0x24]['persistent'] == 0 and ct[0x1a29]['transient'] == 1, 'an injected transient difference stays transient, the check passes')

# resolved: a later comparison no longer lists it (a slow solve, not a divergence); unresolved: the last comparison still does
rs = copy.deepcopy(inj); rs.append(dict(t=t + 60, kind='worlds compared', differing=[]))
check(not D.unknown(D.classify(rs)) and D.classify(rs)[0x24]['resolved'] is True, 'a persistent divergence that a later comparison no longer shows has resolved')
ur = copy.deepcopy(inj); ur.append(dict(t=t + 60, kind='worlds compared', differing=['0x24']))
check(D.unknown(D.classify(ur)) == [0x24], 'one the last comparison still shows is unresolved and fails')

# the allow-list itself: every entry has a reason, keyed by an entity id
allow = D.allow_list()
check(allow and all(isinstance(h, int) and v.get('reason') for h, v in allow.items()), 'allow-list entries are entity ids with a reason')

# mutant of the checker: a classifier that ignores the persistent flag must be caught by the cases above
orig = D.classify
def no_flag(events, allow=None):
    ev = [dict(e, persistent=False) if e['kind'] == 'divergence' and 'missing_on' not in e else e for e in events]
    return orig(ev, allow)
D.classify = no_flag
check(D.unknown(D.classify(inj)) == [], 'mutant classifier (persistent flag ignored) misses the injected divergence: the test above would fail it')
D.classify = orig

print('%d failed' % fails)
sys.exit(1 if fails else 0)
