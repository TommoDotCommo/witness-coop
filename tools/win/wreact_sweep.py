"""Solve sweep: what does a remote solve miss? For each panel with a behaviour name (+0x58), both players stand at the panel, both worlds are
snapshotted, A solves it the game's own way (`nativesolve`: the solved fields, the per-name reactions exe+0xB7F80, the dispatcher, the Bridge),
B gets the solve by sync, both settle and are snapshotted again. Reported: every entity field that changed in one game and not in the other
(a field that changed in both, such as a timer, is ignored). Read-only apart from the commands.
usage: wreact_sweep.py <pidA> <dirA> <pidB> <dirB> <out.json> [hexid ...]      (no ids: every named panel except the excluded behaviours)"""
import os, sys, time, struct, json, collections
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db, outp = attach(int(sys.argv[1])), sys.argv[2], attach(int(sys.argv[3])), sys.argv[4], sys.argv[5]
EXCLUDE = (':endgame', 'video_', 'boat_', 'lotus_start', 'hub_bird_')      # credits / full-screen video / boat rides / the challenge timer / needs a solve context
FULL = {'Door', 'Bridge', 'Force_Bridge', 'Force_Bridge_Segment', 'Double_Ramp', 'Machine_Panel', 'Power_Cable', 'Laser', 'Pressure_Plate', 'Gauge', 'Boat',
        'Light', 'Lake', 'Multipanel', 'Pylon', 'Record_Player', 'Video_Player', 'Video_Screen', 'Obelisk', 'Force_Field', 'Slab', 'Landing_Signal', 'Speaker'}
POSE = {'Inanimate', 'Collision_Volume', 'Touch_Collision', 'Occluder', 'Collision_Path'}
def tname(p, e, cache={}):
    ti = p.u64(e + 8)
    if (id(p), ti) not in cache:
        np_ = p.u64(ti + 8); cache[(id(p), ti)] = p.rd(np_ if np_ and np_ > 0x10000 else ti + 8, 40).split(b'\0')[0].decode('latin1')
    return cache[(id(p), ti)]
def layout(p):      # entity index -> (type, read offset, length)
    out = {}
    for i, e in enumerate(p.entity_table()):
        if not e: continue
        t = tname(p, e)
        if t in FULL: out[i] = (t, 0x20, 0x3E0)
        elif t in POSE: out[i] = (t, 0x24, 0x1C)
    return out
LA, LB = layout(pa), layout(pb)
IDS = sorted(set(LA) & set(LB))
def snap(p):
    t = p.entity_table(); s = {}
    for i in IDS:
        e = t[i]
        if not e: continue
        _, off, n = LA[i]
        try: s[i] = p.rd(e + off, n)
        except Exception: pass
    return s
def is_ptr(b, o):      # the dword at o is part of a pointer (qword with a user-space high half)
    q = o - (o % 8)
    if q + 8 > len(b): return False
    hi = struct.unpack_from('<I', b, q + 4)[0]
    return 0 < hi < 0x8000
def changed(b0, b1):      # dword offsets whose value moved (floats by more than a little)
    out = set()
    for o in range(0, min(len(b0), len(b1)) - 3, 4):
        x, y = b0[o:o + 4], b1[o:o + 4]
        if x == y or is_ptr(b0, o) or is_ptr(b1, o): continue
        fx, fy = struct.unpack('<f', x)[0], struct.unpack('<f', y)[0]
        if fx == fx and fy == fy and abs(fx) < 1e7 and abs(fy) < 1e7 and abs(fx - fy) < 1e-3 * max(1.0, abs(fx)): continue
        out.add(o)
    return out
def send(d, lines, settle=0.0):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    tmp = cmd + '.tmp'; open(tmp, 'w').write('\n'.join(lines) + '\n'); os.replace(tmp, cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(settle)
def tail(d, name, since):
    try:
        with open(os.path.join(d, name), 'rb') as f: f.seek(since); return f.read().decode('latin1', 'replace')
    except OSError: return ''
def size(d, name):
    try: return os.path.getsize(os.path.join(d, name))
    except OSError: return 0
def cstr(p, ptr):
    try: return p.rd(ptr, 64).split(b'\0')[0].decode('latin1') if ptr and ptr > 0x10000 else ''
    except Exception: return ''
ta = pa.entity_table()
named = {i: cstr(pa, pa.u64(e + 0x58)) for i, e in enumerate(ta) if e and tname(pa, e) == 'Machine_Panel'}
named = {i: n for i, n in named.items() if n}
REACT = ('broken_laser_horizontal', 'bridge_control', ':flickering_lamp', 'vr_swipe_lightswitch', 'end2_light', 'hub_bird', 'boat_summon', 'lotus_start', 'monastery_shutters',
         'end_laser_progress', ':to_the_underground', ':endgame', 'reset_pressure_plates', 'tunneler', 'violet_swipe', 'open_pressure_plate_door', 'erasers_', 'hub_slider',
         'entry_area_cable_switcher', 'elevators4', 'spec_', 'colored_lights_hub_control', 'hub_container', 'floodgate_control', 'groupers_bridge_control', 'khatz_crazyhorse',
         'vase', 'water_level_', 'windmill_control', 'giant_floor_lifter_control', ':slider_cable', 'junction_force_field', 'video_', 'trapdoor_ctrl')
todo = [int(h, 16) for h in sys.argv[6:]] or [i for i, n in sorted(named.items()) if n.startswith(REACT) and not n.startswith(EXCLUDE)]
print('%d panels to sweep (%d named, %d entities compared)' % (len(todo), len(named), len(IDS)), flush=True)
def diff(a0, a1, b0, b1, noise=frozenset()):
    onlyA, onlyB = collections.defaultdict(list), collections.defaultdict(list)
    for i in IDS:
        if i not in a0 or i not in a1 or i not in b0 or i not in b1: continue
        ca, cb = changed(a0[i], a1[i]), changed(b0[i], b1[i]); base = LA[i][1]
        for o in sorted(ca - cb):
            if (i, base + o) not in noise: onlyA[i].append(base + o)
        for o in sorted(cb - ca):
            if (i, base + o) not in noise: onlyB[i].append(base + o)
    return onlyA, onlyB
# control: the same steps without a solve, twice; whatever differs then is noise (things that move by themselves, unloaded areas)
noise = set()
for rnd in range(2):
    for d in (da, db): send(d, ['menusel %x' % todo[0], 'menudo teleport'], 0.0)
    time.sleep(3.0); a0, b0 = snap(pa), snap(pb); time.sleep(8.5); a1, b1 = snap(pa), snap(pb)
    oa, ob = diff(a0, a1, b0, b1)
    noise |= {(i, o) for dd in (oa, ob) for i, offs in dd.items() for o in offs}
print('control: %d noisy fields ignored' % len(noise), flush=True)
results = []
for n_, pid_ in enumerate(todo):
    nm = named.get(pid_, '')
    for d in (da, db): send(d, ['menusel %x' % pid_, 'menudo teleport'], 0.0)
    time.sleep(3.0)
    # wait for the previous panel's effects to finish moving in A (up to 30 s), then ignore whatever still moves in a 2 s window before the solve
    t = time.time(); prev = snap(pa)
    while time.time() - t < 30:
        time.sleep(2.0); cur = snap(pa)
        if not any(changed(prev[i], cur[i]) for i in IDS if i in prev and i in cur): break
        prev = cur
    p0a, p0b = snap(pa), snap(pb); time.sleep(2.0)
    a0, b0 = snap(pa), snap(pb)
    moving = {(i, LA[i][1] + o) for i in IDS if i in p0a and i in a0 and i in p0b and i in b0 for o in changed(p0a[i], a0[i]) | changed(p0b[i], b0[i])}
    la, lb = size(da, 'witness_proxy.log'), size(db, 'witness_events.log')
    send(da, ['nativesolve %x' % pid_], 0.3)
    t = time.time(); arrived = ''
    while time.time() - t < 15:
        tb = tail(db, 'witness_events.log', lb)
        if ('remote panel %#x applied as solved' % pid_) in tb: arrived = 'applied'; break
        if ('panel %#x' % pid_) in tb and 'already solved' in tb: arrived = 'already solved'; break
        time.sleep(0.25)
    time.sleep(8.0)      # doors, ramps, shutters and bridges take a few seconds
    a1, b1 = snap(pa), snap(pb)
    ra = tail(da, 'witness_proxy.log', la); react = 1 if 'react 1' in ra else 0 if 'react 0' in ra else -1
    onlyA, onlyB = diff(a0, a1, b0, b1, noise | moving)
    def describe(dd, cap=12):
        return [{'id': '%#x' % i, 'type': LA[i][0], 'fields': ['%#x' % o for o in offs[:8]]} for i, offs in sorted(dd.items(), key=lambda kv: -len(kv[1]))[:cap]]
    r = {'panel': '%#x' % pid_, 'name': nm, 'react': react, 'b': arrived or 'not seen', 'onlyA': len(onlyA), 'onlyB': len(onlyB),
         'onlyA_types': dict(collections.Counter(LA[i][0] for i in onlyA)), 'onlyB_types': dict(collections.Counter(LA[i][0] for i in onlyB)),
         'onlyA_top': describe(onlyA), 'onlyB_top': describe(onlyB)}
    results.append(r)
    print('%3d/%d %-8s %-32s react %2d  B %-14s only in A: %4d %s' % (n_ + 1, len(todo), r['panel'], nm[:32], react, r['b'], r['onlyA'],
          ', '.join('%s %d' % kv for kv in sorted(r['onlyA_types'].items(), key=lambda kv: -kv[1])[:4])), flush=True)
    json.dump(results, open(outp, 'w'), indent=1)
print('DONE', flush=True)
