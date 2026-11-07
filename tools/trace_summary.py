#!/usr/bin/env python3
"""Turn a typed trace (tools/win/wtrace.py) into the benchmark's rows.

usage: python3 tools/trace_summary.py TRACE.jsonl [TRACE.jsonl ...]     the table of legs and the checks, text
       python3 tools/trace_summary.py --jsonl TRACE.jsonl                the rows as results.jsonl lines (tools/walk_bench.py table / compare read them)
Legs are cut at walk_start (its `leg`); a leg ends at its verdict (the playthrough's walks have none: walked = no giveup, teleported = a giveup and a teleport, else failed). Row: panel, result (the verdict's outcome: walked | teleported | failed),
metres, seconds, stuck, why (the giveup's reason, or the verdict's), plus giveup (why, dist_left, stuck_class), stalls, replans, bumps, teleports.
Checks: a teleport must follow a giveup line of its leg (the last resort comes after the walk said it cannot), a teleported leg must have a teleport
line and a giveup, a walked leg no giveup. Exit 1 when a check fails (the text names the leg).
The playthrough's trace (<run>/trace.jsonl) is read the same way: its panel_start / panel_end lines give per panel ok, why and the giveups and
teleports in between."""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, 'win'))
import wtrace


def legs(lines):
    """[{leg, lines}] in order: the lines from a walk_start up to the next one (verdict and teleport lines stay with their leg)."""
    out = []; cur = None
    for rec in lines:
        if rec['kind'] == 'walk_start':
            cur = dict(leg=rec['leg'], lines=[rec]); out.append(cur)
        elif cur is not None: cur['lines'].append(rec)
    return out


def row(leg):
    L = leg['lines']; kinds = [r['kind'] for r in L]
    verdict = next((r for r in reversed(L) if r['kind'] == 'verdict'), None); give = [r for r in L if r['kind'] == 'giveup']; tele = [r for r in L if r['kind'] == 'teleport']
    last = give[-1] if give else None
    out = dict(leg=leg['leg'], panel=(verdict or {}).get('panel'), result=(verdict or {}).get('outcome') or ('walked' if not give else ('teleported' if tele else 'failed')),      # (no verdict: the playthrough's walks have none)
               metres=(verdict or {}).get('metres'), seconds=(verdict or {}).get('seconds'), stuck=(verdict or {}).get('stuck'),
               why=(last['why'] if last else '') or (verdict or {}).get('why', ''), giveups=len(give), teleports=len(tele), stalls=kinds.count('stall'), replans=kinds.count('replan'), bumps=kinds.count('bump'))
    if last: out['giveup'] = dict(why=last['why'], dist_left=last['dist_left'], stuck_class=last['stuck_class'])
    return out


def problems(leg):
    """What is wrong with a leg's lines (the checks of the usage text)."""
    L = leg['lines']; bad = []; seen_give = False
    for r in L:
        if r['kind'] == 'giveup': seen_give = True
        if r['kind'] == 'teleport' and not seen_give: bad.append('leg %s: a teleport without a giveup before it' % (leg['leg'],))
    v = next((r for r in reversed(L) if r['kind'] == 'verdict'), None)
    if v is None: pass
    elif v['outcome'] == 'teleported' and not any(r['kind'] == 'teleport' for r in L): bad.append('leg %s: teleported with no teleport line' % (leg['leg'],))
    elif v['outcome'] == 'walked' and any(r['kind'] == 'giveup' for r in L): bad.append('leg %s: walked, yet a giveup line' % (leg['leg'],))
    return bad


def summarize(lines):
    """(rows, problems) of a list of trace lines."""
    ls = legs(lines); rows = [row(l) for l in ls]; bad = [p for l in ls for p in problems(l)]
    return rows, bad


def table(rows):
    out = ['%-5s %-9s %-11s %7s %7s %5s %6s %5s  %s' % ('leg', 'panel', 'result', 'metres', 'seconds', 'stuck', 'stalls', 'bumps', 'why / against')]
    for r in rows:
        g = r.get('giveup') or {}
        out.append('%-5s %-9s %-11s %7s %7s %5s %6s %5s  %s' % (r['leg'], r['panel'] or '', r['result'], r['metres'], r['seconds'], r['stuck'], r['stalls'], r['bumps'],
                                                                (r['why'] + (' (%s, %.0f m left)' % (g.get('stuck_class') or '-', g.get('dist_left', 0)) if g else ''))[:70]))
    n = lambda k: sum(1 for r in rows if r['result'] == k)
    out.append('total: %d walked, %d teleported, %d failed; %d legs' % (n('walked'), n('teleported'), n('failed'), len(rows)))
    return '\n'.join(out)


def main(argv):
    as_jsonl = '--jsonl' in argv; paths = [a for a in argv if not a.startswith('--')]
    if not paths: print(__doc__); return 2
    lines = [r for p in paths for r in wtrace.read(p)]; rows, bad = summarize(lines)
    if as_jsonl:
        for r in rows: print(json.dumps(r))
    else:
        print(table(rows))
        for b in bad: print('PROBLEM', b)
    return 1 if bad else 0


if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
