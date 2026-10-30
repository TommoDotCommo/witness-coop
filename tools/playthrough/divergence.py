#!/usr/bin/env python3
"""Classify a playthrough's divergences between the worlds, and fail on one nobody has explained.

usage: divergence.py <run folder or events.jsonl> [...]      (exit 1 when a run has an unknown persistent divergence)

Two kinds of records in events.jsonl say that the worlds differ (run.py): compare_worlds() reads every world twice, a few seconds apart,
and records what differed both times as persistent and what differed only the first time as transient (a door still moving, a solve
still travelling); propagation() records a solve that never reached a world within its limit. Each entity (panel or door) is classed:
  transient    only ever in transient records: in flight, not a co-op failure;
  persistent   in at least one persistent record or a solve that never arrived; 'resolved' when a later comparison of the worlds no longer
               lists it (runs that record 'worlds compared'), else 'unresolved' (still differing at its last comparison, or not known).
A persistent entity listed in data/divergence_allow.json (keyed by entity id, with the reason) is known: reported, not a failure; one
that resolved is a slow propagation (the latency is checked elsewhere), reported, not a failure. Every other persistent entity is unknown
and fails the check: unresolved, or a run too old to say (no 'worlds compared' records)."""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ALLOW_FILE = os.path.join(HERE, 'data', 'divergence_allow.json')


def allow_list(path=ALLOW_FILE):
    return {int(k, 16): v for k, v in json.load(open(path)).items()}


def load(path):
    f = os.path.join(path, 'events.jsonl') if os.path.isdir(path) else path
    return [json.loads(l) for l in open(f) if l.strip()]


def entities(e):
    """(entity, what) pairs a divergence record names."""
    if 'missing_on' in e: return [(int(e['panel'], 16), 'panel never arrived')]
    out = [(int(x, 16), 'panel') for x in e.get('panels_only_here', []) + e.get('panels_missing', [])]
    return out + [(int(x, 16), 'door') for x in e.get('doors_differ', [])]


def classify(events, allow=None):
    """entity -> dict(what, persistent, transient, first_t, last_t, pairs, resolved, allowed)."""
    allow = allow_list() if allow is None else allow
    out = {}; compares = [e for e in events if e['kind'] == 'worlds compared']
    for e in events:
        if e['kind'] != 'divergence': continue
        persistent = e.get('persistent', True)      # (a propagation failure has no flag: it is persistent by definition)
        pair = tuple(e['between']) if 'between' in e else (e.get('origin'), e.get('missing_on'))
        for h, what in entities(e):
            c = out.setdefault(h, dict(what=what, persistent=0, transient=0, first_t=e['t'], last_t=e['t'], pairs=set(), resolved=None, allowed=None))
            c['persistent' if persistent else 'transient'] += 1; c['last_t'] = e['t']; c['pairs'].add(pair)
            if persistent: c['last_persistent_t'] = e['t']
    for h, c in out.items():
        c['allowed'] = allow.get(h, {}).get('reason')
        if c['persistent'] and compares:      # resolved: a comparison after its last persistent record no longer lists it
            later = [k for k in compares if k['t'] > c['last_persistent_t']]
            c['resolved'] = bool(later) and all('%#x' % h not in k.get('differing', []) for k in later[-1:])
        c['pairs'] = sorted(c['pairs'], key=str)
    return out


def unknown(classes):
    """Persistent, not on the allow-list, and not seen to resolve: the check fails on these."""
    return sorted(h for h, c in classes.items() if c['persistent'] and not c['allowed'] and not c['resolved'])


def summary(events):
    """For summary.json: the unknown persistent entities, the known ones (allow-list), and those that resolved by themselves."""
    c = classify(events)
    return dict(unknown=['%#x' % h for h in unknown(c)], known=sorted('%#x' % h for h, v in c.items() if v['persistent'] and v['allowed']),
                resolved=sorted('%#x' % h for h, v in c.items() if v['persistent'] and v['resolved'] and not v['allowed']),
                transient=sorted('%#x' % h for h, v in c.items() if not v['persistent']))


def report(name, classes):
    bad = unknown(classes)
    print('%s: %d entities differed: %d persistent (%d unknown), %d transient only' % (
        name, len(classes), sum(1 for c in classes.values() if c['persistent']), len(bad), sum(1 for c in classes.values() if not c['persistent'])))
    for h, c in sorted(classes.items()):
        state = 'transient' if not c['persistent'] else ('UNKNOWN persistent' if h in bad else 'known persistent' if c['allowed'] else 'resolved')
        res = '' if c['resolved'] is None else (', resolved' if c['resolved'] else ', unresolved')
        print('  %-8s %-5s %-18s %d persistent, %d transient, t %.0f-%.0f s%s%s' % ('%#x' % h, c['what'][:5], state, c['persistent'], c['transient'],
                                                                                 c['first_t'], c['last_t'], res, ('  (' + c['allowed'][:60] + '...)') if c['allowed'] else ''))
    return bad


def main(args):
    failed = False
    for a in args:
        if report(a, classify(load(a))): failed = True
    return 1 if failed else 0


if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
