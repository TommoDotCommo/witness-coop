#!/usr/bin/env python3
"""Emote dailies: render every clip in assets/emotes on the real character and run the quality gate, then write a review page.
Usage: python3 tools/emote_review.py [out dir] [--fps 12] [--size 160x208] [--ids 1,2,...]
Out dir defaults to local/emote_review/ (ignored by git): index.html plus one sprite sheet per emote (WebP when the system has
libwebp, else PNG). Open index.html in a browser. Game-free: the same pose code, springs and foot IK as the proxy.
"""
from pathlib import Path
import datetime, json, re, subprocess, sys, tempfile

root = Path(__file__).resolve().parent.parent
args = sys.argv[1:]
def opt(name, default):
    if name in args: i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
    return default
fps, size, ids = opt('--fps', '12'), opt('--size', '160x208'), opt('--ids', '')
out = Path(args[0]) if args else root / 'local/emote_review'
out.mkdir(parents=True, exist_ok=True)
clips = sorted((root / 'assets/emotes').glob('*.emote'))
glb = root / 'assets/models/adventurer.glb'

with tempfile.TemporaryDirectory(prefix='emote-review-') as tmp:
    tmp = Path(tmp)
    text = ''.join(p.read_text() + '\n' for p in clips); (tmp / 'all.emote').write_text(text)
    common = ['g++', '-std=c++17', '-O2', str(root / 'proxy/model/model.cpp')]
    subprocess.run(common + [str(root / 'tools/emote_render.cpp'), '-lz', '-ldl', '-o', str(tmp / 'render')], check=True)
    subprocess.run(common + [str(root / 'proxy/model/tests/emote_quality.cpp'), '-o', str(tmp / 'gate')], check=True)
    gate = subprocess.run([str(tmp / 'gate'), str(glb), str(tmp / 'all.emote'), '--json', str(tmp / 'report.json')], capture_output=True, text=True)
    print(gate.stdout, end='')
    report = json.loads((tmp / 'report.json').read_text())
    cmd = [str(tmp / 'render'), str(glb), str(out), '--clips', str(tmp / 'all.emote'), '--fps', fps, '--size', size]
    if ids: cmd += ['--ids', ids]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

# Each clip's source by id (one clip per file, its header comment says what it is for), for the source panel and the dope-sheet ticks
sources = {}
for p in clips:
    for m in re.finditer(r'(?m)^emote (\d+) ', p.read_text()): sources[int(m.group(1))] = p.read_text()
emotes = []
for j in sorted(out.glob('emote_[0-9][0-9][0-9].json')):
    e = json.loads(j.read_text()); e['source'] = sources.get(e['id'], ''); emotes.append(e)
checks = len(set(re.findall(r'Flag\(id, "([a-z-]+)"', (root / "proxy/model/tests/emote_quality.cpp").read_text()))) + 1      # + distinctness (flagged per pair)
data = {'emotes': emotes, 'report': report, 'checks': checks, 'built': datetime.date.today().isoformat()}
page = (root / 'tools/emote_review.html').read_text().replace('/*DATA*/null', json.dumps(data, separators=(',', ':')))
(out / 'index.html').write_text(page)
total = sum(f.stat().st_size for f in out.iterdir() if f.is_file())
print('%s: %d emotes, %.1f MB with its sheets; the gate %s' % (out / 'index.html', len(emotes), total / 1e6, 'passes' if gate.returncode == 0 else 'FAILS'))
sys.exit(gate.returncode)
