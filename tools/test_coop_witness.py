#!/usr/bin/env python3
"""The Witness's wiring of the shared invite and discovery code (common/VENDORED.md), game-free: the vendored files are the MDK2 copies (apart from the documented
#ifndef defaults), the launcher includes them only through common/coop_witness.h, the lobby answers probes through the one shared answer (joinglue.h -> lj_host_reply)
with its invite-only setting, the Join list scans through lj_candidates / cdw_scan and never the limited broadcast address, the clipboard check neither stores, logs nor
sends the text, the router mappings go when the lobby closes and when the launcher quits, and the paste box glue test (launcher joinglue-test, with mutants) passes.
Usage: test_coop_witness.py   (the shared suites: tools/test_coop_discovery.py, tools/test_coop_join.py, tools/test_coop_host.py)"""
from pathlib import Path
import os, re, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
COMMON, LAUNCHER = ROOT / 'common', ROOT / 'launcher'
MDK2 = os.environ.get('MDK2_COOP_DIR')      # optional: a checkout of the MDK2 co-op mod, to compare the vendored copies byte for byte
problems = []
def need(cond, text):
    if not cond: problems.append(text)

main = (LAUNCHER / 'main.cpp').read_text(errors='ignore')
lobby_win = (LAUNCHER / 'lobby_win.cpp').read_text(errors='ignore')
netfind = (LAUNCHER / 'netfind.c').read_text(errors='ignore')
glue = (LAUNCHER / 'joinglue.h').read_text(errors='ignore')

# the shared headers only through coop_witness.h (the first include of netfind.c; joinglue.h's only include of them)
need(netfind.index('coop_witness.h') < netfind.index('coop_discover_win.h'), 'netfind.c includes a shared header before coop_witness.h')
for name, text in (('main.cpp', main), ('lobby_win.cpp', lobby_win), ('joinglue.h', glue)):
    need(not re.search(r'#include "[^"]*(coop_invite|coop_discover|launcher_join|launcher_host_core)\.h"', text), f'{name} includes a shared header directly (not through coop_witness.h)')
# one probe answer: the lobby socket answers through jg::Answer (lj_host_reply) with the invite-only setting
need('jg::Answer(' in lobby_win and 'hi.inviteOnly = inviteOnly_' in lobby_win, 'the lobby socket does not answer probes through jg::Answer with its invite-only setting')
need('lj_host_reply(' in glue and 'cd_answer(' not in lobby_win + main, 'a second probe answer path besides lj_host_reply')
need('SetInviteOnly(inviteOnly_)' in lobby_win, 'the Direct lobby is not told its visibility')
# the Join list: every adapter's broadcast through the shared candidates and prober; never 255.255.255.255
need('lj_candidates(' in netfind and 'cdw_scan(' in netfind and 'cd_refresh_due(' in netfind, 'the Join list does not scan through lj_candidates / cdw_scan / cd_refresh_due')
need('255.255.255.255' not in netfind + main and 'INADDR_BROADCAST' not in netfind + main, 'the Join list sends to the limited broadcast address')
# B8: the clipboard text is only checked
clip = main.split('void ClipboardCheck()', 1)[-1].split('\nvoid ClipboardJoin()', 1)[0]
need(clip and not re.search(r'Dlog|WriteFile|fopen|sendto|Status\(', clip), 'the clipboard check writes, logs, shows or sends the text')
# C2: mappings removed when the lobby closes and when the launcher quits
need('nf_host_close(); g_voice.Stop(); g_sess.Stop()' in main and 'void LeaveLobby() { nf_host_close();' in main, 'the router mappings are not removed on Leave and on quit')
need('lhw_router_close(&router' in netfind, 'the host checks never remove the mapping')
# vendored copies
if MDK2:
    for f in ('coop_invite.h', 'coop_discover.h', 'coop_discover_win.h', 'launcher_host_win.h'):
        need((COMMON / f).read_bytes() == (Path(MDK2) / 'bridge' / f).read_bytes(), f'common/{f} differs from MDK2\'s bridge/{f}')
r = subprocess.run(['make', '-C', str(LAUNCHER), 'joinglue-test'], capture_output=True, text=True)
print(r.stdout[-1500:].strip())
need(r.returncode == 0, 'launcher joinglue-test failed')
if problems:
    raise SystemExit('FAIL: ' + '; '.join(problems))
print('PASS: the Witness wiring of invites and finding each other')
