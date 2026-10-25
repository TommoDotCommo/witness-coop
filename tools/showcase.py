#!/usr/bin/env python3
"""Showcase driver, run by tools/showcase.sh (which owns the lab lease): N real games in one full-mesh group, each player with its own character model (model_by_player=1:
player i is drawn with the i-th model of the list, in alphabetical order, by every other game), then the scenes of tools/win/wshowcase.py take the pictures.
usage: showcase.py <N> <scenes: all | comma list>"""
import os, shutil, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import group_suite as gs
from group_config import configs
from witness import ROOT, TOOLS, win, real_saves

PREFERRED = ['cesium-man', 'fox', 'rigged-figure', 'magic-frog', 'michelle', 'xbot', 'robot-expressive', 'brainstem']
NAMES = {'cesium-man': 'Cesium Man', 'fox': 'Fox', 'rigged-figure': 'Rigged Figure', 'magic-frog': 'Magic Frog', 'michelle': 'Michelle', 'xbot': 'Xbot', 'robot-expressive': 'Robot', 'brainstem': 'Brainstem'}
SPOT = os.environ.get('SHOWCASE_SPOT', 'c335')       # the panel the players are teleported to: the stone pillar in the garden

def model_files(n):
    avail = {}
    for folder in (Path(ROOT) / 'assets/models', Path(os.environ.get('WITNESS_EXTRA_MODELS', ''))):
        if folder.is_dir():
            for f in sorted(folder.glob('*.glb')): avail.setdefault(f.stem, f)
    chosen = [m for m in PREFERRED if m in avail] + sorted(m for m in avail if m not in PREFERRED)
    chosen = chosen[:max(n, 1)]
    if len(chosen) < n: print('WARNING: only %d models found for %d players: models repeat (tools/get_showcase_models.sh adds one; put more .glb files into $WITNESS_EXTRA_MODELS)' % (len(chosen), n), flush=True)
    return sorted(chosen, key=str.lower), avail

def prepare(c, config, names, avail):
    d = c.directory
    assert gs.digest(d / 'witness64_d3d11.exe') == gs.EXE_HASH, 'unsupported game build'
    if not (d / 'AnselSDK64_real.dll').exists():
        assert gs.digest(d / 'AnselSDK64.dll') == gs.DLL_HASH, 'unrecognized original DLL'
        (d / 'AnselSDK64.dll').rename(d / 'AnselSDK64_real.dll')
    gs.replace_copy(Path(ROOT) / 'proxy/AnselSDK64.dll', d / 'AnselSDK64.dll')
    if (Path(ROOT) / 'launcher/build/witness_launcher.exe').exists(): gs.replace_copy(Path(ROOT) / 'launcher/build/witness_launcher.exe', d / 'witness_launcher.exe')
    models = d / 'witness_models'; models.mkdir(exist_ok=True)
    for f in models.iterdir(): f.unlink()
    for m in names:
        gs.replace_copy(avail[m], models / (m + '.glb'))
        if Path(str(avail[m]) + '.txt').exists(): gs.replace_copy(str(avail[m]) + '.txt', models / (m + '.glb.txt'))
    mine = names[(c.id - 1) % len(names)]
    config += 'name=%s\nmodel=%s\nmodel_by_player=1\nforce_active=1\ncap_fps=30\ndev_menu=1\nvoice_mute_output=1\n' % (NAMES.get(mine, mine), names[0])
    config += 'voice_mode=open\nvoice_test_tone=300,900,1700\n' if c.id % 2 == 0 else 'voice_mode=ptt\n'      # the even players are talking (a test voice: no microphone is needed)
    cfg = d / 'witness_coop.cfg'; cfg.unlink(missing_ok=True); cfg.write_text(config)
    for name in ('witness_cmd.txt', 'witness_proxy.log', 'witness_net.json'): (d / name).unlink(missing_ok=True)
    for leftover in ('shots', 'presets', 'savepoints'): shutil.rmtree(d / leftover, ignore_errors=True)      # pictures and save points of earlier runs
    saves = d / 'appdata/The Witness'
    if saves.exists():
        for file in saves.iterdir():
            if file.suffix in ('.witness_campaign', '.png'): file.unlink()

def draw_plan():
    """The maze's solution as a node list for wdraw (from the panel manifest and the rule solver), 'id:node,node,...' or '' when there is no manifest."""
    try:
        import sweep, solver
        manifest = {q['id']: q for q in sweep.load(sweep.MANIFEST, [])}; q = manifest[0x293]; path = solver.solve(q)
        return '293:' + ','.join(str(n) for n in path) if path else ''
    except Exception as e: print('no drawing plan: %s' % e, flush=True); return ''

def main():
    count = int(sys.argv[1]); scenes = sys.argv[2] if len(sys.argv) > 2 else 'all'
    out = Path(os.environ.get('SHOWCASE_OUT', Path(ROOT) / 'showcase')); out.mkdir(parents=True, exist_ok=True)
    gs.foreground = gs.probe('wfocus.py', 'save'); gs.slots = os.environ['LAB_SLOTS'].split(',')
    names, avail = model_files(count)
    print('models (player i is drawn with model i, in this order): %s' % ', '.join(names), flush=True)
    clients = [gs.Client(i) for i in range(1, count + 1)]
    generated = configs([dict(id=c.id, host='127.0.0.1', port=c.port) for c in clients])
    before = real_saves()
    try:
        for c in clients:
            prepare(c, generated[c.id], names, avail); c.start()
        gs.wait(lambda: all(c.net()['ready'] for c in clients), 'all clients reached the stable-world gate', 300)
        time.sleep(10)
        gs.wait(lambda: all(c.net()['submitted_models'] == count - 1 for c in clients), 'every client draws every other player', 120)
        r = subprocess.run([gs.PYW, '-I', win(os.path.join(TOOLS, 'win', 'wshowcase.py')), win(str(out)), scenes, SPOT, draw_plan() or '-', *['%s|%s' % (c.pid, win(str(c.directory))) for c in clients]], capture_output=True, text=True, timeout=3000)
        print(r.stdout, flush=True)
        if r.returncode: print('wshowcase.py failed (%d):\n%s' % (r.returncode, r.stderr[-3000:]), flush=True); sys.exit(1)
    finally:
        assert real_saves() == before, 'real saves changed'
        print('real saves byte-identical', flush=True)

if __name__ == '__main__': main()
