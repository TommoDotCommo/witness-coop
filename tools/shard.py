#!/usr/bin/env python3
# Splitting the shared-state suite over parallel shards (qa_par.sh, suite.py --shard), game-free; tested by tools/test_shard.py.
#   shard.py count                      reads the resource provider status on stdin, prints how many shards fit now (0 = none)
#   shard.py names [FILTER ...]         the suite's scenario names that the filters select (as suite.py selects them), one per line
#   shard.py queue FILE [FILTER ...]    writes the selected scenarios to FILE, longest first, for shards that pull work (suite.py --queue)
#   shard.py summary LOGDIR [WALL_S]    merges the TIME lines of LOGDIR/shard*.log into one table and LOGDIR/durations.json
import fcntl, json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TABLE = os.path.join(HERE, 'qa_durations.json')     # typical seconds per scenario, measured from real runs
FALLBACK_S = 120                                              # a scenario missing from the table
SLOTS_PER_SHARD, GB_PER_SHARD = 2, 4.0                        # two games per shard; a pair of games uses about 3.6 GB

def load_durations(path=None):
    path = path or os.environ.get('QA_DURATIONS') or DEFAULT_TABLE
    try:
        with open(path) as f: return {k: float(v) for k, v in json.load(f).items() if not k.startswith('_')}
    except (OSError, ValueError): return {}

def plan(names, durations, n):
    # Greedy longest-first: each scenario, longest first, goes to the shard with the least total so far. Ties break on the
    # scenario's position and the lower shard number, so every shard computes the same plan. Returns n lists in suite order.
    n = max(1, n); order = {name: i for i, name in enumerate(names)}
    est = {name: durations.get(name, FALLBACK_S) for name in names}
    bins = [[] for _ in range(n)]; load = [0.0] * n
    for name in sorted(names, key=lambda x: (-est[x], order[x])):
        k = min(range(n), key=lambda i: (load[i], i)); bins[k].append(name); load[k] += est[name]
    return [sorted(b, key=order.get) for b in bins]

def longest_first(names, durations):
    order = {name: i for i, name in enumerate(names)}
    return sorted(names, key=lambda x: (-durations.get(x, FALLBACK_S), order[x]))

def pop(path):
    # Takes the next scenario off a shared queue file (one name per line), under a lock: the online form of the plan above, where the
    # longest scenario left goes to whichever shard is free first, and a shard that never got its games takes nothing.
    with open(path, 'r+') as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        names = [n for n in f.read().split('\n') if n]
        f.seek(0); f.truncate(); f.write(''.join(n + '\n' for n in names[1:]))
        return names[0] if names else None

def shards_that_fit(status, cap):
    # From the resource provider status text: '<n> of <m> slots free', 'RAM free <x> / <y> GB' and, when it has one, its own floor
    # '... refused below <z> GB', which a new shard must leave free (under that, games starve: frame-cap checks failed at 2.7 GB free).
    # Unreadable -> 0 (wait).
    slots = re.search(r'(\d+) of \d+ slots free', status); ram = re.search(r'RAM free ([0-9.]+)', status)
    floor_gb = re.search(r'refused below ([0-9.]+) GB', status)
    if not slots or not ram: return 0
    spare = float(ram[1]) - (float(floor_gb[1]) if floor_gb else 0.0)
    return max(0, min(int(slots[1]) // SLOTS_PER_SHARD, int(math.floor(spare / GB_PER_SHARD)), cap))

def suite_names(only=()):
    with open(os.path.join(HERE, 'suite.py')) as f: names = re.findall(r"^  \('([^']+)'", f.read(), re.M)
    return [n for n in names if not only or any(o in n for o in only)]

TIME_RE = re.compile(r'^TIME\t(.+)\t([0-9.]+)\t(PASS|FAIL)$')
SHARD_RE = re.compile(r'^SHARD\t\d+/\d+\t(.*)$', re.M)

def parse_times(text):
    # -> [(scenario, seconds, PASS|FAIL|NOT RUN)]: a scenario the shard planned but never reported (the shard stopped early) is NOT RUN
    done = [(m[1], float(m[2]), m[3]) for m in map(TIME_RE.match, text.splitlines()) if m]
    planned = SHARD_RE.search(text); seen = {d[0] for d in done}
    return done + [(n, 0.0, 'NOT RUN') for n in (planned[1].split('; ') if planned and planned[1] else []) if n not in seen]

def summary(logdir, wall=None):
    rows = []
    logs = sorted((int(m[1]), fn) for fn in os.listdir(logdir) for m in [re.match(r'shard(\d+)\.log$', fn)] if m)
    started = {}
    for k, fn in logs:
        with open(os.path.join(logdir, fn), errors='replace') as f: text = f.read()
        rows += [(name, k, s, r) for name, s, r in parse_times(text)]
        started.update((m, k) for m in re.findall(r'^=== (.+)$', text, re.M))
    planned = os.path.join(logdir, 'planned')      # queue mode: every selected scenario; one no shard reported is NOT RUN
    if os.path.exists(planned):
        seen = {r[0] for r in rows}
        with open(planned) as f: rows += [(n, started.get(n, 0), 0.0, 'NOT RUN') for n in f.read().split('\n') if n and n not in seen]
    lines = ['%-58s %5s %7s  %s' % ('scenario', 'shard', 'seconds', 'result')]
    lines += ['%-58s %5s %7.0f  %s' % (n, k or '-', s, r) for n, k, s, r in rows]
    per = {}
    for name, k, s, r in rows:
        if r != 'NOT RUN': per.setdefault(k, 0.0); per[k] += s
    lines.append('shard totals: ' + ', '.join('%d: %.0f s' % (k, per[k]) for k in sorted(per)))
    if wall is not None: lines.append('wall time: %.0f s (%.1f min); serial sum of scenarios: %.0f s (%.1f min)' % (wall, wall / 60, sum(per.values()), sum(per.values()) / 60))
    with open(os.path.join(logdir, 'durations.json'), 'w') as f: json.dump({name: round(s) for name, _, s, r in rows if r != 'NOT RUN'}, f, indent=1, sort_keys=True)
    return '\n'.join(lines), rows

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'count': print(shards_that_fit(sys.stdin.read(), int(os.environ.get('QA_SHARDS_MAX') or 4)))
    elif cmd == 'queue':
        names = longest_first(suite_names(sys.argv[3:]), load_durations())
        with open(sys.argv[2], 'w') as f: f.write(''.join(n + '\n' for n in names))
        print(len(names))
    elif cmd == 'names': print('\n'.join(suite_names(sys.argv[2:])))
    elif cmd == 'summary':
        text, _ = summary(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else None); print(text)
    else: print('usage: shard.py count < status | names [FILTER ...] | queue FILE [FILTER ...] | summary LOGDIR [WALL_S]', file=sys.stderr); sys.exit(2)
