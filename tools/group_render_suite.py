#!/usr/bin/env python3
"""One real game plus seven production UDP fixtures: verify real seven-avatar gait and shared progression."""
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
import group_suite as qa
from group_config import configs


def read_json(path):
    return json.loads(path.read_text())


def model_snapshot(client):
    path = client.directory / 'witness_models.json'
    path.unlink(missing_ok=True)
    client.send('modelstate')
    deadline = time.monotonic() + 15
    while not path.exists():
        if time.monotonic() > deadline: raise RuntimeError('no model bridge response')
        time.sleep(.05)
    return read_json(path)


def check_gaits(client, origin):
    moving_speed = 0
    observed_frames = set()
    for _ in range(24):
        snapshot = model_snapshot(client)
        rows = snapshot['players']
        observed_frames.add(snapshot['frame'])
        if {r['id'] for r in rows} != set(range(2, 9)) or len(rows) != 7:
            return False, 'submitted player ids differ from seven remote ids'
        for row in rows:
            if abs(row['pos'][1] - origin[1]) > .1 or abs(row['pos'][2] - origin[2]) > .1:
                return False, 'a submitted avatar has another player position'
            expected_x = origin[0] + 5 * (row['id'] - 2)
            if row['id'] == 2:
                if abs(row['pos'][0] - expected_x) > 1.7: return False, 'moving actor position invalid'
                moving_speed = max(moving_speed, row['speed'])
            elif abs(row['pos'][0] - expected_x) > .1 or row['speed'] > .05:
                return False, 'stationary actor inherited another actor gait'
        time.sleep(.4)
    return moving_speed > .3 and len(observed_frames) > 10, f'moving max speed {moving_speed:.3f}, independent frames {len(observed_frames)}'


def scenario(mutant):
    client = qa.Client(1)
    root = Path(qa.RUN) / ('actors-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    log = (root / 'manager.log').open('w')
    manager = subprocess.Popen([qa.PYW, '-I', qa.win(str(Path(qa.TOOLS) / 'win/group_actors.py')),
                                qa.win(str(Path(qa.ROOT) / 'proxy/core/build/group-transport.exe')),
                                qa.win(str(root)), str(client.port)], stdout=log, stderr=subprocess.STDOUT)
    before = qa.real_saves()
    try:
        qa.wait(lambda: (root / 'manifest.json').exists(), 'seven actor ports reserved', 30)
        ports = read_json(root / 'manifest.json')['ports']
        roster = [dict(id=1,host='127.0.0.1',port=client.port)]
        roster += [dict(id=i,host='127.0.0.1',port=port) for i,port in zip(range(2,9),ports)]
        config = configs(roster)[1] + f'no_group_avatar_state={int(mutant)}\n'
        qa.prepare(client, config)
        client.start()
        qa.wait(lambda: client.net()['ready'], 'real world reached stable gate', 180)
        state = client.state()
        assert '0x64' not in state['panels'] and '0x34eda' not in state['doors'], 'bot precondition failed: panel and door must start incomplete'
        panels = [0x64] + sorted(int(k,16) for k in state['power_src'] if k not in state['panels'] and k != '0x64')[:7]
        origin = list(map(float, qa.probe('wcam.py', client.pid).split()))[:3]
        tmp = root / 'start.tmp'
        tmp.write_text(json.dumps(dict(panels=panels,origin=origin)))
        tmp.replace(root / 'start.json')
        qa.wait(lambda: {r['id'] for r in client.net()['remotes']} == set(range(2,9)), 'real game receives all seven peers', 45)
        qa.wait(lambda: client.net()['submitted_models'] == 7, 'real D3D renderer submits seven avatars', 45)
        ok, detail = check_gaits(client, origin)
        if mutant:
            assert not ok, 'shared-avatar mutant survived the gait check'
            print('PASS shared-avatar mutant caught: ' + detail, flush=True)
            return dict(mutant=True,caught=True,detail=detail)
        assert ok, 'independent player gaits failed: ' + detail
        print('PASS seven independent avatar positions/gaits: ' + detail, flush=True)
        print(qa.probe('wbot.py',client.pid,qa.win(str(client.directory)),timeout=120),flush=True)
        expected = {hex(panel) for panel in panels}
        qa.wait(lambda: expected <= set(client.state()['panels']), 'progress from seven protocol actors reaches real game', 120)
        qa.wait(lambda: '0x34eda' in client.state()['doors'], 'normal local bot solve opens actual tutorial door', 60)
        def converged():
            if manager.poll() is not None: raise RuntimeError('actor manager exited')
            reports = read_json(root / 'reports.json')
            return all(r['ready'] and r['panels']==8 and r['poses']==7 and r['unacked']==0 and r['queued']==0 for r in reports)
        qa.wait(converged, 'real bot solve reaches every actor and all actor queues drain', 120)
        qa.wait(lambda: client.net()['unacked']==0 and client.net()['queued']==0, 'real game queues drain', 120)
        return dict(mutant=False,result='PASS',panels=sorted(expected),models=model_snapshot(client),network=client.net(),actors=read_json(root/'reports.json'))
    finally:
        (root / 'stop').write_text('stop')
        manager_status = manager.wait(timeout=30)
        log.close()
        if client.pid:
            qa.command(['taskkill.exe','/PID',client.pid,'/F'])
        assert qa.real_saves()==before, 'real saves changed'
        print('PASS real saves byte-identical',flush=True)
        assert manager_status == 0, 'actor manager failed; see manager.log'


if __name__=='__main__':
    qa.foreground=qa.probe('wfocus.py','save')
    qa.slots=os.environ['LAB_SLOTS'].split(',')
    results=[]
    # Prove the detector can fail before running the healthy implementation.
    for mutant in (True,False): results.append(scenario(mutant))
    (Path(qa.RUN)/'group-render-result.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS eight-player hybrid: one real game, seven protocol actors, independent real GPU avatars and bidirectional progression',flush=True)
