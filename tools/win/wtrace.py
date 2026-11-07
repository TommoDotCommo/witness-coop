"""The typed trace of a walk or a run: one JSON object per line, flushed at every line, so a stalled run can be read while it runs.

Every line: {"t": seconds (wall clock, ms), "kind": <one of KINDS>, ...fields}. A kind must carry its REQUIRED fields (emit() raises
ValueError otherwise: a typo in a kind or a missing field is a bug in the writer, not something to find later in a summary).
  walk_start {leg, from, to}                  a walk begins (leg: the caller's number or a name; from/to: x y z)
  replan     {reason, cells}                  a new plan (cells: how many)
  bump       {cell, class}                    a cell found blocked (class: ahead | pressed | pocket | wall | foliage | open)
  progress   {dist_left}                      the progress clock was reset (nearer by 0.5 m, a new cell on the way, a bump that changed the map)
  stall      {since_t, seconds, tried}        the progress clock ran out; tried: what was done about it before (the ladder)
  giveup     {why, dist_left, stuck_class, last_way}   the walk ends without arriving (the WalkResult of tools/win/wnav.py)
  teleport   {reason}                         the caller moved the player by a teleport (always after a giveup)
  panel_start / panel_end {panel, ok, why}    the playthrough's panel (panel_start has ok and why empty)
  verdict    {leg, outcome, seconds}          the benchmark's result of a leg: walked | teleported | failed
tools/trace_summary.py turns a trace into the benchmark rows."""
import json, os, time

KINDS = {
    'walk_start': ('leg', 'from', 'to'),
    'replan': ('reason', 'cells'),
    'bump': ('cell', 'class'),
    'progress': ('dist_left',),
    'stall': ('since_t', 'seconds', 'tried'),
    'giveup': ('why', 'dist_left', 'stuck_class', 'last_way'),
    'teleport': ('reason',),
    'panel_start': ('panel', 'ok', 'why'),
    'panel_end': ('panel', 'ok', 'why'),
    'verdict': ('leg', 'outcome', 'seconds'),
}


def check(rec):
    """ValueError when rec is not a well-formed trace line."""
    k = rec.get('kind')
    if k not in KINDS: raise ValueError('unknown trace kind %r' % (k,))
    missing = [f for f in KINDS[k] if f not in rec]
    if missing: raise ValueError('trace kind %s lacks %s' % (k, ', '.join(missing)))
    if not isinstance(rec.get('t'), (int, float)): raise ValueError('trace line without a time')
    return rec


class Trace:
    """Appends typed lines to `path` (None: kept in memory only, in .lines). `clock`: seconds, wall time unless a test passes its own."""
    def __init__(self, path=None, clock=time.time, extra=None):
        self.path, self.clock, self.lines, self.extra = path, clock, [], dict(extra or {})
        self.f = open(path, 'a') if path else None

    def emit(self, kind, **kv):
        rec = check(dict(t=round(self.clock(), 3), kind=kind, **self.extra, **kv))
        self.lines.append(rec)
        if self.f: self.f.write(json.dumps(rec) + '\n'); self.f.flush()
        return rec

    def close(self):
        if self.f: self.f.close(); self.f = None


def teleport(tr, reason, why='not at the standing place', dist_left=0.0):
    """Write a teleport line to the trace `tr`, always after a giveup: when its last line is not one (walking is off, or the walker arrived but not
    at the place wanted) the giveup is written first, with `why`. Returns the teleport line."""
    if not tr.lines or tr.lines[-1]['kind'] != 'giveup': tr.emit('giveup', why=why, dist_left=dist_left, stuck_class='', last_way=[])
    return tr.emit('teleport', reason=reason)


def read(path):
    """The well-formed lines of a trace file, in order (a half-written last line is skipped; a malformed one raises ValueError)."""
    out = []
    for n, line in enumerate(open(path, errors='ignore').read().splitlines(), 1):
        if not line.strip(): continue
        try: rec = json.loads(line)
        except ValueError:
            if n == len(open(path, errors='ignore').read().splitlines()): continue
            raise ValueError('%s:%d: not JSON' % (path, n))
        out.append(check(rec))
    return out
