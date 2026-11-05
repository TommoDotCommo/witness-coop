#!/usr/bin/env python3
"""Before/after review of the adventurer: look-dev sheets (tools/char_lookdev.cpp) of two builds and the character gate's
numbers for each, side by side in one page.
Usage: python3 tools/char_review.py [--before <glb>] [--after <glb>] [out dir]
Defaults: before is the model in git at origin/main (or main), after is assets/models/adventurer.glb, out is local/char_review/."""
from pathlib import Path
import html, json, subprocess, sys, tempfile
root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'tools')); sys.path.insert(0, str(root / 'models/src'))
import check_character
from character_spec import TARGETS
args = sys.argv[1:]
def opt(name):
    if name in args: i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
before, after = opt('--before'), opt('--after') or str(root / 'assets/models/adventurer.glb')
out = Path(args[0]) if args else root / 'local/char_review'; out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='char-review-') as tmp:
    tmp = Path(tmp)
    if not before:
        for ref in ('origin/main', 'main'):
            r = subprocess.run(['git', 'show', ref + ':assets/models/adventurer.glb'], cwd=root, capture_output=True)
            if r.returncode == 0:
                before = str(tmp / 'before.glb'); Path(before).write_bytes(r.stdout)
                Path(before + '.txt').write_bytes(subprocess.run(['git', 'show', ref + ':assets/models/adventurer.glb.txt'], cwd=root, capture_output=True).stdout); break
    exe = tmp / 'lookdev'
    subprocess.run(['g++', '-std=c++17', '-O2', str(root / 'tools/char_lookdev.cpp'), str(root / 'proxy/model/model.cpp'), '-lz', '-o', str(exe)], check=True)
    rows = []
    for tag, glb in (('before', before), ('after', after)):
        for sheet in ('face', 'body', 'hands'):
            subprocess.run([str(exe), glb, str(out / ('%s_%s.png' % (tag, sheet))), '--sheet', sheet, '--size', '1.2' if sheet == 'face' else '1', '--light', 'island'], check=True, stdout=subprocess.DEVNULL)
        d, r, misses = check_character.check(glb); rows.append((tag, r, misses))
def cell(name, v):
    lo, hi = TARGETS[name]; ok = lo <= v <= hi
    return '<td class="%s">%s</td>' % ('ok' if ok else 'miss', ('%.3f' % v) if abs(v) < 1000 else '%d' % v)
table = '<tr><th>Target</th><th>Range</th><th>Before</th><th>After</th></tr>' + ''.join(
    '<tr><td>%s</td><td>%g – %g</td>%s%s</tr>' % (html.escape(n.replace('_', ' ')), lo, hi, cell(n, rows[0][1][n]), cell(n, rows[1][1][n])) for n, (lo, hi) in TARGETS.items() if n in rows[0][1])
page = (root / 'tools/char_review.html').read_text().replace('<!--TABLE-->', table).replace('<!--VERDICT-->',
    'Before misses %d of %d targets; after misses %d.' % (len(rows[0][2]), len([n for n in TARGETS if n in rows[0][1]]), len(rows[1][2])))
(out / 'index.html').write_text(page)
print(out / 'index.html')
