#!/usr/bin/env python3
"""3-client live TV audience POV screen test.
Tests deployment by peer B of wearer A's backpack TV, observed simultaneously by B and C,
including independent audience subscriptions, static on no consent, live stream on consent,
stationary in-world placement while wearer moves, revocation wiping to static, and retraction.
"""
import hashlib, json, math, os, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / 'tools'
sys.path.insert(0, str(TOOLS))
from group_config import configs
from witness import PYW, win, real_saves

WITNESS_RESOURCES = os.environ.get('WITNESS_RESOURCES', '')
EXE_HASH = '8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5'
DLL_HASH = '3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15'

def command(args, timeout=120):
    return subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=timeout, check=True).stdout.strip()

def probe(script, *args, timeout=120):
    return command([PYW, '-I', win(os.path.join(TOOLS, 'win', script)), *args], timeout)

def replace_copy(src, dst):
    dst = Path(dst)
    dst.unlink(missing_ok=True)
    shutil.copy2(src, dst)

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

class Client:
    def __init__(self, index):
        self.id = index
        self.directory = Path(os.environ[f'WITNESS_DIR{index}'])
        self.port = int(os.environ[f'WITNESS_PORT{index}'])
        self.pid = None

    def send(self, text):
        dest = self.directory / 'witness_cmd.txt'
        deadline = time.monotonic() + 15
        while dest.exists():
            if time.monotonic() > deadline: raise RuntimeError(f'player {self.id} command not consumed')
            time.sleep(.05)
        tmp = dest.with_suffix('.tmp'); tmp.write_text(text + '\n'); tmp.replace(dest)

    def net(self):
        dest = self.directory / 'witness_net.json'; dest.unlink(missing_ok=True)
        self.send('netstate')
        deadline = time.monotonic() + 15
        while not dest.exists():
            if time.monotonic() > deadline: raise RuntimeError(f'player {self.id} no network bridge dump')
            time.sleep(.05)
        return json.loads(dest.read_text())

    def tv(self):
        dest = self.directory / 'witness_tv.json'; dest.unlink(missing_ok=True)
        self.send('tvstate')
        deadline = time.monotonic() + 15
        while not dest.exists():
            if time.monotonic() > deadline: raise RuntimeError(f'player {self.id} no tv bridge dump')
            time.sleep(.05)
        return json.loads(dest.read_text())

    def camera(self):
        txt = probe('wcam.py', self.pid)
        for line in txt.splitlines():
            line = line.strip()
            if line and not line.startswith('game:'):
                nums = [float(x) for x in line.split()]
                if len(nums) == 5:
                    return (nums[0], nums[1], nums[2]), (nums[3], nums[4])
        raise RuntimeError(f'player {self.id} camera read failed')

    def start(self, foreground, slot):
        if self.pid: command(['taskkill.exe', '/PID', self.pid, '/F']); time.sleep(1)
        self.pid = probe('launch.py', win(str(self.directory)), '-skip_config_dialog', '-windowed', '-new_game').splitlines()[-1]
        command([WITNESS_RESOURCES, 'register-pid', self.pid])
        guard_stop = Path(self.directory) / 'guard.stop'; guard_stop.unlink(missing_ok=True)
        subprocess.Popen([PYW, '-I', win(os.path.join(TOOLS, 'win/wfocus.py')), 'guard', foreground, '180', win(str(guard_stop)), self.pid], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        rect = command([WITNESS_RESOURCES, 'rect', slot]).split()
        wait(lambda: 'placed True' in probe('wplace.py', self.pid, *rect), f'player {self.id} placed on monitor', 90)

def wait(predicate, description, timeout=180):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if predicate(): print('PASS', description, flush=True); return
        except Exception: pass
        time.sleep(1)
    raise RuntimeError('FAIL ' + description)

def prepare(c, config):
    d = c.directory
    assert digest(d / 'witness64_d3d11.exe') == EXE_HASH, 'unsupported game build'
    if not (d / 'AnselSDK64_real.dll').exists():
        assert digest(d / 'AnselSDK64.dll') == DLL_HASH, 'unrecognized original DLL'
        (d / 'AnselSDK64.dll').rename(d / 'AnselSDK64_real.dll')
    replace_copy(ROOT / 'proxy/AnselSDK64.dll', d / 'AnselSDK64.dll')
    models = d / 'witness_models'; models.mkdir(exist_ok=True)
    for asset in (ROOT / 'assets/models').iterdir():
        if asset.suffix in ('.glb', '.txt'): replace_copy(asset, models / asset.name)
    extra = '\nforce_active=1\ncap_fps=30\ndev_menu=1\nvoice_mode=off\nvoice_mute_output=1\npov_tv=1\npov_tv_consent=0\n'
    cfg = d / 'witness_coop.cfg'; cfg.unlink(missing_ok=True); cfg.write_text(config + extra)
    for name in ('witness_cmd.txt', 'witness_proxy.log', 'witness_net.json', 'witness_tv.json'):
        (d / name).unlink(missing_ok=True)
    saves = d / 'appdata/The Witness'
    if saves.exists():
        for file in saves.iterdir():
            if file.suffix in ('.witness_campaign', '.png'): file.unlink()

def main():
    assert WITNESS_RESOURCES, 'WITNESS_RESOURCES not configured'
    count = 3
    foreground = probe('wfocus.py', 'save')
    slots_env = os.environ.get('WITNESS_SLOTS', '')
    need_acquire = len(slots_env.split(',')) < count if slots_env else True
    if need_acquire:
        command([WITNESS_RESOURCES, 'acquire', '--slots', str(count), '--ttl', '3600', '--wait', '0'])
        command([WITNESS_RESOURCES, 'provision', 'witness'])
        env_text = command([WITNESS_RESOURCES, 'env', 'witness'])
        for line in env_text.splitlines():
            if '=' in line:
                k, v = line.split('=', 1)
                os.environ[k.strip()] = v.strip()
    try:
        slots = os.environ['WITNESS_SLOTS'].split(',')
        clients = [Client(i) for i in range(1, count + 1)]
        roster = [dict(id=c.id, host='127.0.0.1', port=c.port) for c in clients]
        generated = configs(roster)
        before = real_saves()
        try:
            for c in clients:
                prepare(c, generated[c.id])
                c.start(foreground, slots[c.id - 1])
            wait(lambda: all(c.net()['ready'] for c in clients), 'all 3 clients reached stable-world gate', 180)
            time.sleep(6)
            c1, c2, c3 = clients[0], clients[1], clients[2]

            # Move client 1 (wearer) to panel c335
            c1.send('menusel c335\nmenudo teleport')
            time.sleep(3)
            (ax, ay, az), (yaw, _) = c1.camera()

            # Client 2 (deployer) positions behind C1 aiming at C1
            c2.send(f'hover {ax-math.cos(yaw)*3.5:.5f} {ay-math.sin(yaw)*3.5:.5f} {az:.5f} {yaw:.6f} -0.10')
            wait(lambda: abs(c2.camera()[0][0] - (ax-math.cos(yaw)*3.5)) < .3, 'client 2 reached aim position', 30)

            # Client 3 (audience member) positions behind C1 offset to side
            c3.send(f'hover {ax-math.cos(yaw)*4.2+math.sin(yaw)*2.0:.5f} {ay-math.sin(yaw)*4.2-math.cos(yaw)*2.0:.5f} {az:.5f} {yaw:.6f} -0.10')
            wait(lambda: abs(c3.camera()[0][0] - (ax-math.cos(yaw)*4.2+math.sin(yaw)*2.0)) < .4, 'client 3 reached audience position', 30)
            time.sleep(2)

            # Client 2 deploys Client 1's sky TV
            c2.send('tvdeploy 1')
            def screen_deployed():
                tv2 = c2.tv(); tv3 = c3.tv()
                return (tv2.get('deploy_target') == 1 and len(tv2.get('audience', [])) >= 1
                        and len(tv3.get('audience', [])) >= 1)
            wait(screen_deployed, 'client 2 deployment received by both audience clients (B and C)', 20)

            tv2 = c2.tv(); tv3 = c3.tv()
            scr2 = tv2['audience'][0]; scr3 = tv3['audience'][0]
            assert scr2['owner'] == 2 and scr2['source'] == 1
            assert scr3['owner'] == 2 and scr3['source'] == 1
            assert abs(scr2['width'] - 4.0) < 1e-3 and abs(scr2['height'] - 2.5) < 1e-3
            assert abs(scr2['bottom'] - (scr2['ground'] + 2.0)) < 1e-3
            behind = (scr2['center'][0] - ax) * math.cos(yaw) + (scr2['center'][1] - ay) * math.sin(yaw)
            assert abs(behind + 3.0) < 0.35, f'placement behind={behind}'
            print('PASS audience screen size 4.0x2.5, bottom +2.0m, behind +3.0m verified across clients', flush=True)

            # Both B and C show static noise without consent
            assert not scr2['fresh'] and not scr2['drawn_live'] and scr2['draws'] > 0
            assert not scr3['fresh'] and not scr3['drawn_live'] and scr3['draws'] > 0
            print('PASS both audience clients display static noise when wearer consent is off', flush=True)
            c2.send('shot tv3_static_b'); c3.send('shot tv3_static_c')

            # Client 1 enables consent
            c1.send('tvconsent 1')
            def live_streams():
                t2 = c2.tv(); t3 = c3.tv()
                return (t2.get('audience') and t2['audience'][0]['fresh'] and t2['audience'][0]['drawn_live'] and t2['audience'][0]['upload_hash'] != 0
                        and t3.get('audience') and t3['audience'][0]['fresh'] and t3['audience'][0]['drawn_live'] and t3['audience'][0]['upload_hash'] != 0)
            wait(live_streams, 'both audience clients B and C receive live source POV independently', 20)
            c2.send('shot tv3_live_b'); c3.send('shot tv3_live_c')

            # Client 1 moves around: screen stays stationary in-world
            orig_center = list(c2.tv()['audience'][0]['center'])
            c1.send(f'hover {ax+4.0:.5f} {ay+4.0:.5f} {az:.5f} {yaw+0.8:.6f} 0')
            wait(lambda: abs(c1.camera()[0][0] - (ax+4.0)) < .3, 'wearer client 1 moved to new position', 20)
            time.sleep(2)
            wait(live_streams, 'screen streaming continues live while wearer moves', 15)
            s2_moved = c2.tv()['audience'][0]
            assert abs(s2_moved['center'][0] - orig_center[0]) < 1e-3 and abs(s2_moved['center'][1] - orig_center[1]) < 1e-3
            print('PASS screen remains stationary in-world at deployment pose across all clients while wearer moves', flush=True)
            c2.send('shot tv3_move_b'); c3.send('shot tv3_move_c')

            # Client 1 revokes consent
            c1.send('tvconsent 0')
            def streams_revoked():
                t2 = c2.tv(); t3 = c3.tv()
                return (t2.get('audience') and not t2['audience'][0]['fresh'] and not t2['audience'][0]['drawn_live']
                        and t3.get('audience') and not t3['audience'][0]['fresh'] and not t3['audience'][0]['drawn_live'])
            wait(streams_revoked, 'consent revocation wipes audience screen back to static on all clients', 20)
            c2.send('shot tv3_rev_b'); c3.send('shot tv3_rev_c')

            # Client 2 retracts screen
            c2.send('tvretract')
            def screens_cleared():
                return len(c2.tv().get('audience', [])) == 0 and len(c3.tv().get('audience', [])) == 0
            wait(screens_cleared, 'retraction clears audience screen from all clients in world', 20)
            c2.send('shot tv3_ret_b'); c3.send('shot tv3_ret_c')

            for c in clients: c.send('hover off')
            print('ALL OK: 3-client live TV audience POV test passed', flush=True)
        finally:
            assert real_saves() == before, 'real saves changed'
            print('PASS real saves byte-identical', flush=True)
    finally:
        if need_acquire:
            command([WITNESS_RESOURCES, 'release'])
            print('RELEASED', flush=True)

if __name__ == '__main__':
    main()
