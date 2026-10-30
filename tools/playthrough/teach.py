#!/usr/bin/env python3
"""Learn from recordings of a person playing (tools/record.sh): for each panel they solved, the line the game accepted, where every node of
it is in the world, and where they stood and looked when the line started. Written to data/taught.json, which run.py and wdraw.py use in
place of the solver's line and the start search.

usage: teach.py [session.jsonl or recording folder ...]      (default: every folder under local/recordings/)

The traced-edge records (panel +0x230, 0x34 bytes each) hold index_a, index_b, two ids, t, then two floats and the world positions of
both ends (+0x18 and +0x24); the last ones logged before the solve are the accepted line (where the game keeps its own, +0x454, they
match it: Shadows Far 1, Desert Surface 3)."""
import glob, json, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, 'data', 'taught.json')


def records(hexstr):
    raw = bytes.fromhex(hexstr)
    return [struct.unpack_from('<4I3f3f3f', raw, k * 0x34) for k in range(len(raw) // 0x34)]


def lessons(path):
    """{panel: lesson} for every solve in one session.jsonl."""
    ev = [json.loads(l) for l in open(path) if l.strip()]
    out = {}
    for i, x in enumerate(ev):
        if x['event'] != 'solved': continue
        h = x['panel']; samples = [s for s in ev[:i] if s['event'] == 'sample' and s['panel'] == h]
        with_line = [s for s in samples if s.get('records')]
        if not with_line: continue
        recs = records(with_line[-1]['records'])
        if not recs or any(abs(r[4] - 1.0) > 1e-3 for r in recs): continue      # (a line still being drawn)
        line = [recs[0][0]] + [r[1] for r in recs]
        if any(recs[k][1] != recs[k + 1][0] for k in range(len(recs) - 1)): continue      # not one line: something else was logged
        nodes = {}
        for r in recs:
            nodes[str(r[0])] = [round(v, 4) for v in r[6:9]]; nodes[str(r[1])] = [round(v, 4) for v in r[9:12]]
        # where the line started from: the last sample before the traced count first went up, in the attempt that was accepted
        k = max(j for j, s in enumerate(samples) if s['traced'] == 0) if any(s['traced'] == 0 for s in samples) else 0
        start = samples[min(k + 1, len(samples) - 1)]
        task = next((t for t in ev[:i] if t['event'] == 'task' and t['panel'] == h), {})
        out[h] = {'name': task.get('name', h), 'line': line, 'nodes': nodes, 'stand': start['pose'], 'seconds': x.get('seconds'),
                  'from': os.path.basename(os.path.dirname(path))}
    return out


def main(args):
    files = []
    for a in args or sorted(glob.glob(os.path.join(ROOT, 'local', 'recordings', '*'))):
        files += [a] if a.endswith('.jsonl') else glob.glob(os.path.join(a, 'session.jsonl'))
    try: taught = json.load(open(OUT))
    except (OSError, ValueError): taught = {}
    for f in sorted(files):
        for h, l in lessons(f).items():
            taught[h] = l; print('%-8s %-36s line of %2d nodes, stood at %s' % (h, l['name'], len(l['line']), l['stand'][:3]))
    json.dump(taught, open(OUT, 'w'), indent=1, sort_keys=True)
    print('%d panels taught -> %s' % (len(taught), OUT))


if __name__ == '__main__':
    main(sys.argv[1:])
