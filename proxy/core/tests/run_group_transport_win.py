# Windows-side black-box integration runner. Only starts/kills its own non-game fixtures.
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

root=Path(tempfile.mkdtemp(prefix='witness-group-transport-'))
clients=[]
logs=[]
ports=[]
reservations=[]

def start(i):
    d=root/str(i+1)
    for name in ('transport.json','witness_net.json'):(d/name).unlink(missing_ok=True)
    log=(d/'stdout.log').open('a');logs.append(log)
    p=subprocess.Popen([str(d/'client.exe'),str(i+1)],cwd=d,stdout=log,stderr=subprocess.STDOUT)
    return p

def reports():
    return [json.loads((root/str(i+1)/'transport.json').read_text()) for i in range(8)]

def wait_all(description,timeout):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        for p in clients:
            if p.poll() is not None:raise RuntimeError('fixture exited with '+str(p.returncode))
        try:
            data=reports()
            if all(d['ready'] and d['panels']==8 and d['doors']==8 and d['power']==8 and d['lit']==0 and d['poses']==7 and d['unacked']==0 and d['queued']==0 for d in data):
                print('PASS',description,flush=True);return data
        except (OSError,ValueError):pass
        time.sleep(.5)
    for d in reports():print(d,flush=True)
    raise RuntimeError('FAIL '+description)

try:
    for _ in range(8):
        s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('127.0.0.1',0));ports.append(s.getsockname()[1]);reservations.append(s)
    for i in range(8):
        d=root/str(i+1);d.mkdir()
        shutil.copy2(sys.argv[1],d/'client.exe')
        cfg=f'id={i+1}\nlisten={ports[i]}\ndrop_pct=25\ndrop_seed={i+1}\n'
        cfg+=''.join(f'peer={j+1},127.0.0.1,{ports[j]}\n' for j in range(8) if i!=j)
        (d/'witness_coop.cfg').write_text(cfg)
    for i in range(8):
        reservations[i].close();clients.append(start(i))
    # Client 2 reverses client 1's cable within its five-second remote echo window.
    deadline=time.monotonic()+36
    while time.monotonic()<deadline:
        if any(p.poll() is not None for p in clients): raise RuntimeError('fixture exited during rapid-reversal stage')
        try:
            data=reports()
            if data[1]['rapid_off'] and all(d['shared']==0 for d in data):break
        except (OSError,ValueError):pass
        time.sleep(.2)
    else: raise RuntimeError('rapid local reversal was suppressed or did not reach all eight clients')
    print('PASS production UDP rapid local reversal during remote echo guard reaches all eight clients',flush=True)
    # The first full convergence may precede the cable-on stage: wait until each local toggle has happened.
    time.sleep(8)
    wait_all('eight production UDP clients: config, all-player progress, poses, cable on/off, 25% loss and drained queues',40)
    clients[-1].terminate();clients[-1].wait(timeout=10)
    clients[-1]=start(7)
    final=wait_all('production UDP restart restores all progress without disturbing seven survivors',45)
    for i in range(8):
        net=json.loads((root/str(i+1)/'witness_net.json').read_text())
        assert {p['id'] for p in net['remotes']}==set(range(1,9))-{i+1}
        assert all(p['age_ms']<2000 for p in net['remotes'])
    print('PASS production netstate bridge has all seven fresh remote ids on every client',flush=True)
    print('ALL OK',flush=True)
except Exception:
    print('Evidence retained at',root,flush=True)
    raise
finally:
    for s in reservations:s.close()
    for p in clients:
        if p.poll() is None:p.terminate()
    for p in clients:p.wait(timeout=10)
    for f in logs:f.close()
