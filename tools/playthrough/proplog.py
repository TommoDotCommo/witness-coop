#!/usr/bin/env python3
"""Propagation times from the games' own event logs, with the reason for each slow one.

usage: proplog.py <witness_events.log> <witness_events.log> [...] [--after MS] [--slow S]

Every game of a session writes witness_events.log with the machine's performance counter in milliseconds (shared by all games on one PC).
A solve is the sender's 'local panel X solved' line; it arrived when another log has 'remote panel X applied as solved' within 120 s. A
solve slower than --slow seconds (default 1) is explained when it can be: 'receiver re-gating' (the receiver suspected a save load and was
waiting for a stable world: no remote event is applied until it is), 'receiver starting' (its first stable world came later), else
'unexplained'. Lines before --after (a counter value) are ignored: logs keep earlier sessions."""
import re, sys

SOLVED = re.compile(r'state: local panel (0x[0-9a-f]+) solved')
APPLIED = re.compile(r'state: remote panel (0x[0-9a-f]+) applied as solved')


def load(path, after=0.0):
    out = []
    for line in open(path, errors='ignore'):
        t, _, msg = line.partition(' ')
        try: t = float(t)
        except ValueError: continue
        if t >= after: out.append((t, msg.strip()))
    return out


def arrivals(logs):
    """logs: name -> [(ms, message)]. Returns [(seconds, panel, sender, receiver, reason)] for every solve that reached another log."""
    out = []
    for a, la in logs.items():
        for t, m in la:
            r = SOLVED.match(m)
            if not r: continue
            for b, lb in logs.items():
                if b == a: continue
                got = next((u for u, m2 in lb if u >= t and u < t + 120000 and APPLIED.match(m2) and APPLIED.match(m2).group(1) == r.group(1)), None)
                if got is None: continue
                out.append(((got - t) / 1000, r.group(1), a, b, reason(lb, t, got)))
    return out


def reason(lb, t, got):
    stable = [u for u, m in lb if 'world stable' in m and t <= u <= got + 50]
    if not stable: return ''
    suspected = [u for u, m in lb if 'save reload suspected' in m and u <= stable[0]]
    first = min(u for u, m in lb if 'world stable' in m)
    if stable[0] == first: return 'receiver starting'
    return 'receiver re-gating' if suspected else 'receiver starting'


def main(a):
    def opt(k, d): return float(a[a.index(k) + 1]) if k in a else d
    after, slow = opt('--after', 0.0), opt('--slow', 1.0)
    files = [x for i, x in enumerate(a) if not x.startswith('--') and (i == 0 or a[i - 1] not in ('--after', '--slow'))]
    got = arrivals({f: load(f, after) for f in files})
    if not got: print('no solve reached another game'); return 1
    v = sorted(x[0] for x in got)
    print('arrivals %d: p50 %.3f s, p95 %.3f s, max %.3f s' % (len(v), v[len(v) // 2], v[int(0.95 * (len(v) - 1))], v[-1]))
    for s, p, x, y, why in sorted(got, reverse=True):
        if s > slow: print('  %7.2f s  %-8s %s -> %s  %s' % (s, p, x, y, (why or 'unexplained') + ('' if why else '  <- look here')))
    return 0


if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
