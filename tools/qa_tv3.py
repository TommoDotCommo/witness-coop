#!/usr/bin/env python3
"""3-client live TV audience POV screen test.
Tests deployment by peer B of wearer A's backpack TV, observed simultaneously by B and C,
including independent audience subscriptions, static on no consent, live stream on consent,
stationary in-world placement while wearer moves, revocation wiping to static, and retraction.
"""
import hashlib, json, math, os, shlex, shutil, struct, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / 'local/tv3-evidence'
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

    def models(self):
        dest=self.directory/'witness_models.json'; dest.unlink(missing_ok=True)
        self.send('modelstate'); deadline=time.monotonic()+15
        while not dest.exists():
            if time.monotonic()>deadline: raise RuntimeError('model bridge timeout')
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
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    foreground = probe('wfocus.py', 'save')
    slots_env = os.environ.get('WITNESS_SLOTS', '')
    need_acquire = len(slots_env.split(',')) < count if slots_env else True
    try:
        if need_acquire:
            command([WITNESS_RESOURCES, 'acquire', '--slots', str(count), '--ttl', '3600', '--wait', '0'])
            command([WITNESS_RESOURCES, 'provision', 'witness'])
            env_text = command([WITNESS_RESOURCES, 'env', 'witness'])
            for line in env_text.splitlines():
                if '=' in line:
                    assignment=line.removeprefix('export ')
                    k,v=assignment.split('=',1)
                    os.environ[k.strip()]=shlex.split(v)[0]
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

            # Watch from the viewing side and aim up at the raised screen.
            center=c2.tv()['audience'][0]['center']
            for c,side,front in ((c2,2.5,5.0),(c3,-2.0,6.0)):
                x=center[0]+math.cos(yaw)*front+math.sin(yaw)*side
                y=center[1]+math.sin(yaw)*front-math.cos(yaw)*side
                look=math.atan2(center[1]-y,center[0]-x)
                pitch=math.atan2(center[2]-az,math.hypot(center[0]-x,center[1]-y))
                c.send(f'hover {x} {y} {az} {look} {pitch}')
            wait(lambda:all(c.tv()['audience'][0]['visible_pixels']>100 for c in (c2,c3)),'both viewers render visible world screen pixels',20)
            for c,name in ((c2,'tv3_visible_b'),(c3,'tv3_visible_c')):
                shot=c.directory/'shots'/f'{name}.bmp'; shot.unlink(missing_ok=True)
                c.send(f'shot {name}')
                wait(lambda:shot.exists(),f'client {c.id} screen capture completed',10)
                bmp=shot.read_bytes(); offset=struct.unpack_from('<I',bmp,10)[0]
                width,height=struct.unpack_from('<ii',bmp,18)
                assert struct.unpack_from('<H',bmp,28)[0]==32
                # Both cameras aim at screen centre. A central 64px square is
                # inside its surface; a zero avatar palette makes this all black.
                channels=[]
                for y in range(abs(height)//2-32,abs(height)//2+32):
                    row=y if height<0 else abs(height)-1-y
                    for x in range(width//2-32,width//2+32):
                        at=offset+(row*width+x)*4; channels.extend(bmp[at:at+3])
                mean=sum(channels)/len(channels)
                (EVIDENCE/f'visible-pixels-{c.id}.json').write_text(json.dumps(dict(mean_rgb=mean)))
                assert mean>10, f'client {c.id} visible live screen rendered black: {mean}'
                print(f'PASS client {c.id} actual captured live screen brightness {mean:.2f}',flush=True)

            # No wait/retry here: a disappearing/reappearing screen is a failure.
            # This spans more than four deployment expiry windows.
            baseline = {c.id:c.tv()['audience'][0] for c in clients}
            deadline=time.monotonic()+14
            samples=0
            with (EVIDENCE/'persistent.jsonl').open('w') as evidence:
                while time.monotonic()<deadline:
                    for c in clients:
                        state=c.tv(); evidence.write(json.dumps(dict(client=c.id,time=time.monotonic(),state=state))+'\n'); evidence.flush()
                        assert len(state['audience'])==1, f'client {c.id} screen disappeared'
                        screen=state['audience'][0]
                        assert screen['sequence']==baseline[c.id]['sequence'] and screen['fresh'] and screen['drawn_live'], f'client {c.id} lost persistent feed: {state}'
                        assert screen['draws']>=baseline[c.id]['draws']
                        if c.id!=1:
                            assert screen['visible_pixels']>100, f'client {c.id} screen invisible'
                            source=c1.tv()
                            assert dict(source['sent_hashes']).get(screen['uploaded_sequence'])==screen['upload_hash'], 'world texture differs from source snapshot'
                    samples+=1; time.sleep(.10)
            assert samples>=12
            for c in clients:
                current=c.tv()['audience'][0]
                assert current['draws']>baseline[c.id]['draws'] and current['decoded']>baseline[c.id]['decoded']
            print(f'PASS uninterrupted 14-second deployment/render/live feed on all 3 clients ({samples} samples)',flush=True)

            # Restart the third client: retained owner heartbeat must hydrate late join.
            try:
                c3.start(foreground,slots[2])
            except RuntimeError as error:
                print(f'RETRY late-join launch precondition: {error}',flush=True)
                c3.start(foreground,slots[2])
            wait(lambda:c3.net()['ready'],'late join stable-world gate',180)
            wait(lambda:len(c3.tv()['audience'])==1 and c3.tv()['audience'][0]['fresh'],'late join receives deployed live screen',30)

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

            c1.send('tvconsent 1')
            wait(live_streams,'consent re-enable restores both feeds',20)

            # Aim at the middle stool, use the real input binding, and inspect the
            # synchronized Sit emote and eye position from the other process.
            center=c2.tv()['audience'][0]['center']; ground=c2.tv()['audience'][0]['ground']
            stool=[center[0]+math.cos(yaw)*4,center[1]+math.sin(yaw)*4,ground]
            entry=[stool[0]+math.cos(yaw),stool[1]+math.sin(yaw),ground+1.71]
            face=yaw+math.pi; pitch=math.atan2(.5-1.71,1)
            c3.send(f'hover {entry[0]} {entry[1]} {entry[2]} {face} {pitch}')
            wait(lambda:abs(c3.camera()[0][0]-entry[0])<.1,'audience reached stool interaction',20)
            c3.send('hover off')
            c3.send('key K down\nkey K up')
            wait(lambda:c3.tv()['seated'],'aimed K sits in audience stool',15)
            def remote_seated():
                poses=c2.net()['remotes']
                return any(p['id']==3 and p['emote']==14 and abs(p['pos'][2]-(ground+1.31))<.15 for p in poses)
            wait(remote_seated,'seated eye position and Sit pose synchronize to peer',15)
            time.sleep(2)
            model=c2.models(); (EVIDENCE/'seated-model.json').write_text(json.dumps(model,indent=2))
            avatar=next(p for p in model['players'] if p['id']==3)
            assert avatar['emote']==14 and avatar['gesture']>.9 and abs(avatar['model_ground']-ground)<.1, f'seated avatar origin/pose incorrect: {avatar}'
            assert c3.tv()['audience'][0]['visible_pixels']>100,'seated player can actually see the screen'
            c2.send('shot tv3_seated_peer'); c3.send('shot tv3_seated_view')
            assert c3.tv()['seated']
            c3.send('key K down\nkey K up')
            wait(lambda:not c3.tv()['seated'],'K stands without immediately sitting again',15)
            c3.send('key K down\nkey K up')
            wait(lambda:c3.tv()['seated'],'K sits again from restored entry pose',15)
            c3.send('key W down\nkey W up')
            wait(lambda:not c3.tv()['seated'],'movement exits the stool',15)
            assert abs(c3.camera()[0][2]-entry[2])<.25,'stand restores standing camera eye'

            # Sit again, then remove the supporting screen: this must stand the
            # viewer safely at the entry pose rather than leave a held camera.
            c3.send(f'hover {entry[0]} {entry[1]} {entry[2]} {face} {pitch}')
            wait(lambda:abs(c3.camera()[0][0]-entry[0])<.1,'returned to stool entry',20)
            c3.send('hover off\ntvseat')
            wait(lambda:c3.tv()['seated'],'aimed command sits again',15)

            # Client 2 retracts screen
            c2.send('tvretract')
            def screens_cleared():
                return len(c2.tv().get('audience', [])) == 0 and len(c3.tv().get('audience', [])) == 0
            wait(screens_cleared, 'retraction clears audience screen from all clients in world', 20)
            wait(lambda:not c3.tv()['seated'],'retraction safely stands seated viewer',15)
            c2.send('shot tv3_ret_b'); c3.send('shot tv3_ret_c')
            wait(lambda:all(c.tv()['audience_streams']==0 and c.tv()['audience_resources']==0 and c.tv()['audience_stats']==0 for c in clients),'all audience streams, GPU references and stats cleaned after retraction',20)

            # A new deployment can choose another consenting source. Old source
            # textures and tickets must be discarded, not shown on the new screen.
            c3.send('tvconsent 1'); c2.send('tvdeploy 3')
            wait(lambda:all(c.tv()['audience'] and c.tv()['audience'][0]['source']==3 and c.tv()['audience'][0]['fresh'] for c in clients),'redeployment switches all clients to new source',20)
            assert all(c.tv()['audience_streams']==1 and c.tv()['audience_resources']==1 and c.tv()['audience_stats']==1 for c in clients)
            c2.send('tvretract')
            wait(lambda:all(c.tv()['audience_streams']==0 and c.tv()['audience_stats']==0 for c in clients),'source change resources cleared on retraction',20)

            # Source-only audience: after remote viewers disconnect their leases
            # expire before the last deployment heartbeat expires. Local capture
            # must continue in that interval without a network subscriber.
            c2.send('tvdeploy 1')
            wait(lambda:len(c1.tv()['audience'])==1 and c1.tv()['audience'][0]['fresh'],'source sees own audience feed',20)
            for c in (c2,c3):
                command(['taskkill.exe','/PID',c.pid,'/F']); c.pid=None
            def self_only():
                state=c1.tv()
                (EVIDENCE/'source-only.json').write_text(json.dumps(state,indent=2))
                return state['subscribers']==0 and state['audience'] and state['audience'][0]['fresh'] and state['audience'][0]['drawn_live']
            wait(self_only,'source-only audience remains live with zero remote subscriptions',5)
            wait(lambda:c1.tv()['audience_streams']==0 and c1.tv()['audience_stats']==0,'disconnected deployment owner expires and frees source resources',15)


            for c in clients:
                if c.pid: c.send('hover off')
            print('ALL OK: 3-client live TV audience POV test passed', flush=True)
        finally:
            for c in clients:
                dest=EVIDENCE/f'client{c.id}'; dest.mkdir(exist_ok=True)
                for name in ('witness_tv.json','witness_net.json','witness_models.json','witness_proxy.log'):
                    source=c.directory/name
                    if source.exists(): shutil.copy2(source,dest/name)
                for source in (c.directory/'shots').glob('tv3_*.bmp'):
                    shutil.copy2(source,dest/source.name)
            assert real_saves() == before, 'real saves changed'
            print('PASS real saves byte-identical', flush=True)
    finally:
        if need_acquire:
            command([WITNESS_RESOURCES, 'release'])
            print('RELEASED', flush=True)

if __name__ == '__main__':
    main()
