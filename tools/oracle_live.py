#!/usr/bin/env python3
"""Run one game from a save and talk to the proxy's collision oracle (proxy/oracle.cpp). Needs a lab lease (eval "$(lab env witness)").

usage: python3 tools/oracle_live.py start SAVE        start the game from the save (leaves it running; pid in <game dir>/oracle_pid.txt)
       python3 tools/oracle_live.py q "ray 0 0 2 1 0 0 50" "ground 10 20" ...   one batch, prints the result lines
       python3 tools/oracle_live.py check SPOTS.json [OUT.json]   the geometry battery of tools/test_oracle.py on this game (records OUT.json)
       python3 tools/oracle_live.py stop             kill the game this tool started (by its pid)
As a module: Oracle(dir).batch(lines) -> list of result strings (one witness_oracle_in.txt, one `oracle` command, waits for the answer file)."""
import json, os, sys, time, shutil
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'playthrough'))


class Oracle:
    def __init__(self, gamedir):
        self.dir = Path(gamedir)

    def send(self, text, timeout=15):
        dest = self.dir / 'witness_cmd.txt'; deadline = time.monotonic() + timeout
        while dest.exists():
            if time.monotonic() > deadline: raise RuntimeError('command not consumed')
            time.sleep(.02)
        tmp = dest.with_suffix('.tmp'); tmp.write_text(text + '\n'); tmp.replace(dest)

    def batch(self, lines, timeout=60):
        (self.dir / 'witness_oracle_in.txt').write_text('\n'.join(lines) + '\n'); (self.dir / 'witness_oracle_out.txt').unlink(missing_ok=True)
        self.send('oracle'); deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try: head, *rest = (self.dir / 'witness_oracle_out.txt').read_text().splitlines()
            except OSError: head, rest = '', []
            parts = head.split()
            if len(parts) >= 3 and parts[1] == 'seq' and len(rest) >= len(lines): return rest
            time.sleep(.02)
        raise RuntimeError('no oracle answer')


def record(o, spots, record_path=None, log=print):
    """The battery of tools/test_oracle.py on a running game: stand at the places 3 m in front of some panels (spots: [{panel, spot, ppos}] as
    walk_bench legs give them), let the game's own physics settle the player there, ask the oracle. Returns the record (and writes it)."""
    import math
    walls = []; stand = None
    for sp in spots:
        x, y = sp['spot']; pz = sp['ppos'][2]; got = None
        for attempt in range(3):
            o.send('hover %.4f %.4f %.4f %.5f 0.0' % (x, y, pz + 0.3, math.atan2(sp['ppos'][1] - y, sp['ppos'][0] - x))); time.sleep(4.0)      # (a plain tp to a far place is undone: no collision there yet; held in place it loads, then the game's own physics keeps the player)
            o.send('hover off'); time.sleep(0.6)
            w = o.batch(['where'])[0]; f = [float(v) for v in w.split('feet=')[1].split()[0].split(',')]
            if math.hypot(f[0] - x, f[1] - y) < 1.0: got = w; break
        if not got: log('panel %s: the player did not stay at the spot (no collision there yet)' % sp['panel']); continue
        c = [float(v) for v in got.split('cam=')[1].split()[0].split(',')]; f = [float(v) for v in got.split('feet=')[1].split()[0].split(',')]
        dx, dy = sp['ppos'][0] - c[0], sp['ppos'][1] - c[1]; L = math.hypot(dx, dy); dx, dy = dx / L, dy / L
        res = o.batch(['ground %.4f %.4f %.4f 5' % (f[0], f[1], f[2] + 1.5), 'ray %.4f %.4f %.4f 0 0 -1 5' % tuple(c), 'ray %.4f %.4f %.4f 0 0 1 100' % tuple(c),
                       'ray %.4f %.4f %.4f %.5f %.5f 0 8' % (c[0], c[1], c[2], dx, dy), 'ray %.4f %.4f %.4f %.5f %.5f 0 8' % (c[0], c[1], c[2], -dx, -dy)])
        if stand is None: stand = dict(where=got, ground=res[0], down=res[1]); sky = res[2]
        walls.append(dict(panel=sp['panel'], spot=sp['spot'], ppos=sp['ppos'], toward=res[3], away=res[4]))
    if stand is None: raise RuntimeError('no spot could be stood on')
    rec = dict(note='recorded from a live game by tools/oracle_live.py check; build 11370412', stand=stand, sky=sky, walls=walls)
    if record_path: json.dump(rec, open(record_path, 'w'), indent=1)
    return rec


def main():
    import run
    from witness import win
    cmd = sys.argv[1]; b = run.Bot(1); pidf = b.dir / 'oracle_pid.txt'
    if cmd == 'start':
        from group_suite import replace_copy
        save = Path(sys.argv[2]).resolve()
        b.prepare('id=1\nlisten=%d\nsave_dir=instance\nshare_zip=1\n' % b.port, 60, 0)
        saves = b.dir / 'appdata' / 'The Witness'; saves.mkdir(parents=True, exist_ok=True); replace_copy(save, saves / save.name)
        b.start(new_game=False); pidf.write_text(b.pid)
        run.wait(lambda: 'resources ready' in (b.dir / 'witness_proxy.log').read_text(errors='ignore'), 'game loaded', 400)
        time.sleep(15); print('pid', b.pid)
    elif cmd == 'q':
        for r in Oracle(b.dir).batch(sys.argv[2:]): print(r)
    elif cmd == 'check':      # check SPOTS.json [OUT.json]: spots as written by walk_bench legs (panel, spot, ppos)
        sys.path.insert(0, str(HERE)); import test_oracle
        rec = record(Oracle(b.dir), json.load(open(sys.argv[2])), sys.argv[3] if len(sys.argv) > 3 else None)
        bad = test_oracle.verify(rec); print('\n'.join(bad) or 'oracle agrees with the known geometry'); sys.exit(1 if bad else 0)
    elif cmd == 'stop':
        import subprocess
        if pidf.exists(): subprocess.run(['taskkill.exe', '/PID', pidf.read_text().strip(), '/F'], capture_output=True); pidf.unlink()


if __name__ == '__main__': main()
