#!/usr/bin/env python3
"""Run one game from a save and talk to the proxy's collision oracle (proxy/oracle.cpp). Needs a lab lease (eval "$(lab env witness)").

usage: python3 tools/oracle_live.py start SAVE        start the game from the save (leaves it running; pid in <game dir>/oracle_pid.txt)
       python3 tools/oracle_live.py q "ray 0 0 2 1 0 0 50" "ground 10 20" ...   one batch, prints the result lines
       python3 tools/oracle_live.py stop             kill the game this tool started (by its pid)
As a module: Oracle(dir).batch(lines) -> list of result strings (one witness_oracle_in.txt, one `oracle` command, waits for the answer file)."""
import os, sys, time, shutil
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
    elif cmd == 'stop':
        import subprocess
        if pidf.exists(): subprocess.run(['taskkill.exe', '/PID', pidf.read_text().strip(), '/F'], capture_output=True); pidf.unlink()


if __name__ == '__main__': main()
