#!/usr/bin/env python3
"""Real-game group QA. Run through qa_group.sh, which owns leasing and teardown."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from group_config import configs
from witness import ROOT, TOOLS, RUN, PYW, win, real_saves

LAB = os.environ.get('LAB', os.path.expanduser('~/source/coop-lab/lab'))
EXE_HASH = '8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5'
DLL_HASH = '3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15'


def command(args, timeout=120):
    return subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=timeout,check=True).stdout.strip()


def probe(script,*args,timeout=120):
    return command([PYW,'-I',win(os.path.join(TOOLS,'win',script)),*args],timeout)


def replace_copy(src,dst):
    # Lab files may be hard-linked. Always replace the directory entry, never overwrite a link.
    dst = Path(dst)
    dst.unlink(missing_ok=True)
    shutil.copy2(src,dst)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Client:
    def __init__(self,index):
        self.id=index
        self.directory=Path(os.environ[f'WITNESS_DIR{index}'])
        self.port=int(os.environ[f'WITNESS_PORT{index}'])
        self.pid=None
    def send(self,text):
        dest=self.directory/'witness_cmd.txt'
        deadline=time.monotonic()+15
        while dest.exists():
            if time.monotonic()>deadline: raise RuntimeError(f'player {self.id} command not consumed')
            time.sleep(.1)
        tmp=dest.with_suffix('.tmp');tmp.write_text(text+'\n');tmp.replace(dest)
    def state(self):
        text=probe('wstatedump.py',self.pid)
        return json.loads(text.splitlines()[-1])
    def net(self):
        dest=self.directory/'witness_net.json';dest.unlink(missing_ok=True)
        self.send('netstate')
        deadline=time.monotonic()+15
        while not dest.exists():
            if time.monotonic()>deadline:raise RuntimeError(f'player {self.id} no network bridge dump')
            time.sleep(.1)
        return json.loads(dest.read_text())
    def start(self):
        import re
        free = float(re.search(r'RAM free ([0-9.]+)', command([LAB,'status']))[1])
        if not self.pid and free < 4.8:
            raise RuntimeError('RAM changed after preflight; preserving shared reserve')
        if self.pid: command(['taskkill.exe','/PID',self.pid,'/F']);time.sleep(2)
        self.pid=probe('launch.py',win(str(self.directory)),'-skip_config_dialog','-windowed','-new_game').splitlines()[-1]
        command([LAB,'register-pid',self.pid])
        # Guard only this newly launched process, with the foreground saved before any games started.
        guard_stop=Path(RUN)/f'group-guard-{self.id}.stop';guard_stop.unlink(missing_ok=True)
        subprocess.Popen([PYW,'-I',win(os.path.join(TOOLS,'win/wfocus.py')),'guard',foreground,'180',win(str(guard_stop)),self.pid],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        rect=command([LAB,'rect',slots[self.id-1]]).split()
        # Wait for the game's window before placing it, without taking focus.
        wait(lambda:'placed True' in probe('wplace.py',self.pid,*rect),f'player {self.id} placed on lab monitor',90)


def wait(predicate,description,timeout=180):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            if predicate():print('PASS',description,flush=True);return
        except (ValueError,IndexError,subprocess.CalledProcessError,FileNotFoundError):pass
        time.sleep(2)
    raise RuntimeError('FAIL '+description)


def prepare(c, config):
    d=c.directory
    assert digest(d/'witness64_d3d11.exe')==EXE_HASH,'unsupported game build'
    if not (d/'AnselSDK64_real.dll').exists():
        assert digest(d/'AnselSDK64.dll')==DLL_HASH,'unrecognized original DLL'
        (d/'AnselSDK64.dll').rename(d/'AnselSDK64_real.dll')
    replace_copy(Path(ROOT)/'proxy/AnselSDK64.dll',d/'AnselSDK64.dll')
    models=d/'witness_models';models.mkdir(exist_ok=True)
    for asset in (Path(ROOT)/'assets/models').iterdir():
        if asset.suffix in ('.glb','.txt'):replace_copy(asset,models/asset.name)
    config=config+'force_active=1\ncap_fps=20\ndev_menu=1\nno_partner_video=1\ndrop_pct=25\ndrop_seed='+str(c.id)+'\n'
    cfg=d/'witness_coop.cfg';cfg.unlink(missing_ok=True);cfg.write_text(config)
    for name in ('witness_cmd.txt','witness_proxy.log','witness_net.json'):(d/name).unlink(missing_ok=True)
    saves=d/'appdata/The Witness'
    if saves.exists():
        for file in saves.iterdir():
            if file.suffix in ('.witness_campaign','.png'):file.unlink()


def run():
    count=int(sys.argv[1]);assert 2<=count<=8
    clients=[Client(i) for i in range(1,count+1)]
    roster=[dict(id=c.id,host='127.0.0.1',port=c.port) for c in clients]
    generated=configs(roster)
    before=real_saves()
    try:
        for c in clients:
            prepare(c, generated[c.id])
            c.start()
        wait(lambda:all(c.net()['ready'] for c in clients),'all clients reached stable-world gate',240)
        time.sleep(16)
        def poses():
            for c in clients:
                net=c.net();remotes=net['remotes']
                if net['submitted_models'] != count-1:return False
                if {r['id'] for r in remotes}!={p.id for p in clients if p!=c}:return False
                if any(r['age_ms']>2500 for r in remotes):return False
            return True
        wait(poses,'every client sees every other pose and submits every avatar')
        before_bot=clients[0].state()
        assert '0x64' not in before_bot['panels'] and '0x34eda' not in before_bot['doors'], 'bot precondition failed: panel and door must start incomplete'
        # A real input-driven solve proves the normal gameplay -> detector -> group path.
        print(probe('wbot.py',clients[0].pid,win(str(clients[0].directory)),timeout=120),flush=True)
        wait(lambda:all('0x64' in c.state()['panels'] and '0x34eda' in c.state()['doors'] for c in clients),'bot solve and tutorial door reach every world')
        initial=clients[0].state()
        candidates=sorted((int(k,16) for k in initial['power_src'] if k not in initial['panels']))[:count]
        assert len(candidates)==count
        for c,entity in zip(clients,candidates):c.send(f'solve {entity:x}')
        expected={hex(i) for i in candidates}|{'0x64'}
        wait(lambda:all(expected<=set(c.state()['panels']) for c in clients),'progress originating on every client reaches every world')
        wait(lambda:all(c.net()['unacked']==0 for c in clients),'independent ACK streams drain after packet loss')
        clients[-1].start()
        wait(lambda:clients[-1].net()['ready'],'restarted client reaches stable-world gate',180)
        wait(lambda:expected<=set(clients[-1].state()['panels']) and '0x34eda' in clients[-1].state()['doors'],'restarted client recovers group progression',120)
        wait(poses,'all poses recover after one peer restart')
        wait(lambda:all(c.net()['unacked']==0 and c.net()['queued']==0 for c in clients),'all reliable and apply queues drain',120)
        evidence={'players':count,'panels':sorted(expected),'network':[c.net() for c in clients],'result':'PASS'}
        Path(RUN).mkdir(parents=True,exist_ok=True)
        (Path(RUN)/'group-result.json').write_text(json.dumps(evidence,indent=2)+'\n')
        print(f'PASS {count}-player group live QA',flush=True)
    finally:
        assert real_saves()==before,'real saves changed'
        print('PASS real saves byte-identical',flush=True)


if __name__=='__main__':
    foreground=probe('wfocus.py','save')
    slots=os.environ['LAB_SLOTS'].split(',')
    run()
