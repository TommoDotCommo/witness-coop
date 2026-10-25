"""Own seven tiny protocol fixtures beside one real game. Never opens a game or desktop input."""
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time

exe, root, game_port = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
root.mkdir(parents=True, exist_ok=True)
reservations, ports, clients, logs = [], [], [], []

def publish(name, data):
    target = root / name
    tmp = target.with_suffix('.tmp')
    tmp.write_text(json.dumps(data))
    tmp.replace(target)

try:
    for _ in range(7):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0))
        reservations.append(sock)
        ports.append(sock.getsockname()[1])
    endpoints = [(1, game_port)] + list(zip(range(2, 9), ports))
    for player, port in endpoints[1:]:
        directory = root / str(player)
        directory.mkdir()
        shutil.copy2(exe, directory / 'client.exe')
        text = f'id={player}\nlisten={port}\ndrop_pct=25\ndrop_seed={player}\n'
        text += ''.join(f'peer={other},127.0.0.1,{dest}\n' for other, dest in endpoints if other != player)
        (directory / 'witness_coop.cfg').write_text(text)
    publish('manifest.json', dict(ports=ports))
    deadline = time.monotonic() + 300
    while not (root / 'start.json').exists():
        if (root / 'stop').exists(): sys.exit(0)
        if time.monotonic() > deadline: raise RuntimeError('no start specification')
        time.sleep(.1)
    spec = json.loads((root / 'start.json').read_text())
    world = ' '.join(f'{panel:x}' for panel in spec['panels']) + '\n'
    world += ' '.join(map(str, spec['origin'])) + '\n'
    for index, player in enumerate(range(2, 9)):
        directory = root / str(player)
        (directory / 'world.txt').write_text(world)
        log = (directory / 'stdout.log').open('w')
        logs.append(log)
        reservations[index].close()
        clients.append(subprocess.Popen([str(directory / 'client.exe'), str(player), 'world.txt'], cwd=directory, stdout=log, stderr=subprocess.STDOUT))
    while not (root / 'stop').exists():
        if any(p.poll() is not None for p in clients): raise RuntimeError('protocol fixture exited')
        if time.monotonic() > deadline: raise RuntimeError('actor test exceeded deadline')
        data = []
        for player in range(2, 9):
            try: data.append(json.loads((root / str(player) / 'transport.json').read_text()))
            except (OSError, ValueError): break
        if len(data) == 7: publish('reports.json', data)
        time.sleep(.2)
finally:
    for sock in reservations: sock.close()
    for client in clients:
        if client.poll() is None: client.terminate()
    for client in clients: client.wait(timeout=10)
    for log in logs: log.close()
