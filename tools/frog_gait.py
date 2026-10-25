#!/usr/bin/env python3
"""Adds `walk` and `run` loops to the Magic Frog glb (CC BY 4.0 model, not in this repo) and names its own reading loop `idle`.

The frog (a 151-joint Auto-Rig-Pro skeleton) ships with one 20 s clip that only moves the arms, the book and the eyes. The new clips are built on that
clip's first frame (the arms stay where they hold the book) and add a gait: thigh / knee / ankle swing, hip drop and sway, counter-rotating spine, head held
level, tail sway. The stride starts from the leg length and is then calibrated against the planted foot's speed measured by the loader (see the gains in build()):
a walk cycle of 1.0 s at `speed=1.6` m/s and a run cycle of 0.66 s at `run_speed=4.5` m/s (the keys of the model's `<file>.glb.txt`).

usage: frog_gait.py in.glb out.glb          (standard library only; the maths is in vertex units, `tools/glbtools.py` has the glb and quaternion helpers)
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from glbtools import *

EYE_M = 1.71                 # the camera's height above the feet: the proxy sizes the frog so its eyes are here (proxy/model FitToEye)
DROP = float(os.environ.get("DROP", 0.7))      # share of the geometric hip drop used in the walk (less: the planted foot no longer sinks into the floor)
X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)      # model space: x = the model's left, y = up, z = front

def build(inp, outp, walk_speed=1.6, run_speed=4.5, walk_T=1.0, run_T=0.66, fps=30, walk_gain=1.15, run_gain=1.0):      # gains calibrated against the planted foot speed (proxy/model gait probe)
    j, b = read_glb(inp); N = j['nodes']; sk = j['skins'][0]; J = sk['joints']
    par = {}
    for i, n in enumerate(N):
        for c in n.get('children', []): par[c] = i
    idle = j['animations'][0]; idle['name'] = 'idle'
    # ---- scale: vertex units -> metres, from the eye height the proxy will fit
    ymin = min(j['accessors'][p['attributes']['POSITION']]['min'][1] for m in j['meshes'] for p in m['primitives'])
    ibm = read_acc(j, b, sk['inverseBindMatrices'])
    def origin_y(name):                      # model-space height of a joint: the translation of the inverse of its inverse bind matrix
        k = next(i for i, n in enumerate(J) if N[n].get('name', '').startswith(name)); M = ibm[k]
        a, b_, c, d, e, f, g, h, i = M[0], M[4], M[8], M[1], M[5], M[9], M[2], M[6], M[10]
        det = a * (e * i - f * h) - b_ * (d * i - f * g) + c * (d * h - e * g)
        inv = [(e * i - f * h), -(b_ * i - c * h), (b_ * f - c * e), -(d * i - f * g), (a * i - c * g), -(a * f - c * d), (d * h - e * g), -(a * h - b_ * g), (a * e - b_ * d)]
        t = (-M[12], -M[13], -M[14]); return sum(inv[3 + k] * t[k] for k in range(3)) / det
    eye_y = (origin_y('c_eye.l') + origin_y('c_eye.r')) / 2; hip_y = origin_y('thigh_stretch.l')
    k = EYE_M / (eye_y - ymin)                # metres per vertex unit when the eyes are at the camera height
    L = (hip_y - ymin) * k                    # leg length, hip to the sole, metres
    print('vertex-space estimate: eye %.1f ground %.1f hip %.1f, %.4f mm per unit, leg %.2f m (the loader fits the rest pose, so its scale differs a little: the gains below are calibrated against the loader)' % (eye_y, ymin, hip_y, k * 1000, L))
    # ---- base pose: the rest pose with the idle clip's first frame on top (the arms hold the book)
    base_t = [list(n.get('translation', (0, 0, 0))) for n in N]; base_r = [tuple(n.get('rotation', (0, 0, 0, 1))) for n in N]; base_s = [list(n.get('scale', (1, 1, 1))) for n in N]
    idle_paths = []                           # (node, path) pairs the idle clip animates, with the value at its first key
    for ch in idle['channels']:
        s = idle['samplers'][ch['sampler']]; v = read_acc(j, b, s['output'])[0]; node, path = ch['target']['node'], ch['target']['path']
        if path == 'translation': base_t[node] = list(v)
        elif path == 'rotation': base_r[node] = tuple(v)
        elif path == 'scale': base_s[node] = list(v)
        idle_paths.append((node, path, tuple(v)))
    order = []; seen = set()
    def visit(i):
        if i in seen: return
        if i in par: visit(par[i])
        seen.add(i); order.append(i)
    for i in range(len(N)): visit(i)
    Wb = {}
    for i in order: Wb[i] = qmul(Wb[par[i]], base_r[i]) if i in par else base_r[i]
    def find(prefix):
        for i in J:
            if N[i].get('name', '').startswith(prefix): return i
        raise KeyError(prefix)
    pelvis = find('root.x'); thigh = {'l': find('thigh_stretch.l'), 'r': find('thigh_stretch.r')}; shin = {'l': find('leg_stretch.l'), 'r': find('leg_stretch.r')}
    foot = {'l': find('foot.l'), 'r': find('foot.r')}; spine = [find('spine_0%d.x' % n) for n in range(1, 6)]; head = find('head.x')
    tails = [[find('c_tail_0%d.x' % n) for n in range(4)], [find('c_tail_0%d_dupli_001.x' % n) for n in range(4)]]
    rx = lambda a: qaxis(X, a); ry = lambda a: qaxis(Y, a); rz = lambda a: qaxis(Z, a)
    # a forward swing of a leg is a negative turn about +x (the sole goes toward +z); a bend of the knee is a positive one; toes down is positive

    def cycle(T, speed, run):
        n = int(round(T * fps)); times = [i / fps for i in range(n + 1)]
        A = (math.asin(min(0.95, speed * T / (4 * L))) * walk_gain) if not run else math.radians(40) * run_gain        # thigh swing: the stance foot moves back at `speed` (walk); a run has a flight phase
        K = math.radians(62 if run else 38); Tt = math.radians(32 if run else 24); lean = math.radians(11 if run else 2.5); twist = math.radians(9 if run else 5)
        drop = L * (1 - math.cos(A)) / k * (DROP if not run else 0.0); bounce = (0.07 if run else 0.0) / k; sway = (0.016 if run else 0.022) / k
        rows = {}                              # node -> list of per-key local rotations (or translations for the pelvis)
        trans = []
        for fi in range(n + 1):
            ph = 2 * math.pi * fi / n; D = {}; dt_world = (0, 0, 0)
            tw = twist * math.sin(ph)                                  # pelvis turns toward the forward leg
            D[pelvis] = qmul(ry(tw), rx(lean * 0.0))
            for side, off in (('l', 0.0), ('r', math.pi)):
                p = ph + off; f = A * math.sin(p)                      # + = forward
                swing = max(0.0, math.cos(p))                          # the swinging half of the cycle
                kn = K * (0.04 + swing ** 1.2 * (0.96 if not run else 0.9)) + (K * 0.18 * max(0.0, -math.cos(p)) if run else K * 0.12 * max(0.0, -math.cos(p)))
                toe = Tt * max(0.0, -math.sin(p)) * (1.0 if math.cos(p) < 0 else 0.35)
                D[thigh[side]] = rx(-f); D[shin[side]] = rx(-f + kn); D[foot[side]] = rx(toe + (0.12 * lean))
            for i, s in enumerate(spine, 1):
                D[s] = qmul(ry(tw * (1 - 0.4 * i)), rx(lean * i / 5.0))
            D[head] = rx(-lean * 0.6)
            for ci, chain in enumerate(tails):
                for ti, t_ in enumerate(chain): D[t_] = ry(math.radians(5 + 4 * ti) * math.sin(ph - 0.6 * ti + ci * math.pi))
            Wp = {}; loc = {}
            for i in order:
                pw = Wp[par[i]] if i in par else (0, 0, 0, 1)
                q = qnorm(qmul(qconj(pw), qmul(D[i], Wb[i]))) if i in D else base_r[i]
                loc[i] = q; Wp[i] = qmul(pw, q)
            for i in D:
                prev = rows[i][-1] if i in rows else loc[i]; rows.setdefault(i, []).append(qslerp_fix(loc[i], prev))
            # pelvis: drop at the widest stride, rise through the passing pose; sway toward the stance leg; a run bounces (highest in flight)
            dy = -drop * math.sin(ph) ** 2 + (-bounce * math.cos(2 * ph) if run else 0); dx = sway * -math.cos(ph)
            pw = Wp[par[pelvis]] if pelvis in par else (0, 0, 0, 1); v = qrot(qconj(pw), (dx, dy, 0))
            trans.append(tuple(base_t[pelvis][c] + v[c] for c in range(3)))
        return times, rows, trans

    def add_anim(name, T, speed, run):
        times, rows, trans = cycle(T, speed, run)
        tin = add_acc(j, b, [(t,) for t in times], 'SCALAR', True); tin2 = add_acc(j, b, [(0.0,), (times[-1],)], 'SCALAR', True)
        samplers = []; channels = []
        def chan(node, path, tacc, vals, typ):
            samplers.append({'input': tacc, 'output': add_acc(j, b, vals, typ), 'interpolation': 'LINEAR'}); channels.append({'sampler': len(samplers) - 1, 'target': {'node': node, 'path': path}})
        over = {(i, 'rotation') for i in rows} | {(pelvis, 'translation')}
        for node, path, v in idle_paths:           # everything the idle clip animates stays at its first frame (arms on the book, eyes, ...)
            if (node, path) in over: continue
            chan(node, path, tin2, [v, v], 'VEC4' if path == 'rotation' else 'VEC3')
        for i, q in rows.items(): chan(i, 'rotation', tin, q, 'VEC4')
        chan(pelvis, 'translation', tin, trans, 'VEC3')
        j['animations'].append({'name': name, 'samplers': samplers, 'channels': channels})
        print('%s: %d keys over %.2f s, %d channels' % (name, len(times), times[-1], len(channels)))
    add_anim('walk', walk_T, walk_speed, False); add_anim('run', run_T, run_speed, True)
    write_glb(outp, j, b)

if __name__ == '__main__':
    if len(sys.argv) != 3: sys.exit(__doc__)
    build(sys.argv[1], sys.argv[2], walk_gain=float(os.environ.get("WALK_GAIN", 1.15)), run_gain=float(os.environ.get('RUN_GAIN', 1.0)))
