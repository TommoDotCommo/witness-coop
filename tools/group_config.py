#!/usr/bin/env python3
"""Generate a full-mesh 2–16 player configuration from a JSON roster.

Input: [{"id": 1, "host": "127.0.0.1", "port": 47001}, ...]
Output: player-<id>.cfg files in an explicitly chosen output directory.
Copy each file to witness_coop.cfg beside that player's proxy DLL.
"""
import argparse
import ipaddress
import json
from pathlib import Path


def configs(players):
    if not 2 <= len(players) <= 16:
        raise ValueError('a group needs 2 to 16 players')
    ids, endpoints = set(), set()
    for p in players:
        if type(p['id']) is not int or not 1 <= p['id'] <= 0x7fffffff or p['id'] in ids:
            raise ValueError('player ids must be distinct positive 32-bit integers')
        host = str(ipaddress.IPv4Address(p['host']))
        if type(p['port']) is not int or not 1 <= p['port'] <= 65535:
            raise ValueError('ports must be in 1..65535')
        endpoint = (host, p['port'])
        if endpoint in endpoints:
            raise ValueError('duplicate endpoint')
        ids.add(p['id']); endpoints.add(endpoint)
    result = {}
    for p in players:
        lines = [f"id={p['id']}", f"listen={p['port']}", 'save_dir=instance', 'share_zip=1']
        for peer in sorted(players, key=lambda x: x['id']):
            if peer['id'] != p['id']:
                lines.append(f"peer={peer['id']},{peer['host']},{peer['port']}")
        result[p['id']] = '\n'.join(lines) + '\n'
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('roster', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    generated = configs(json.loads(args.roster.read_text()))
    args.output.mkdir(parents=True, exist_ok=True)
    for player, config in generated.items():
        (args.output / f'player-{player}.cfg').write_text(config)
    print(f'Generated {len(generated)} player configs')


if __name__ == '__main__':
    main()
