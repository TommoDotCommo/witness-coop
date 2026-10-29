#!/usr/bin/env python3
"""Play the game through with N bots in one co-op world, in the game's own order, and record everything.

usage: tools/playthrough.sh [N] [--goal credits|all] [--minutes M] [--fps F] [--drop PCT] [--start SPEC] [--resume SAVE]
       (the wrapper leases N game slots, provisions them, runs this, and always releases them)

Every bot is a real game (a full-mesh group of N players; one game alone for N=1). The planner (planner.py) decides, from the
solved panels the games report, which available panel each free bot takes: never one the logic says is still locked. A bot moves with
the dev menu's teleport to its panel (only to panels the logic says are reachable now) and draws the solution with in-process input
(tools/win/wdraw.py, or wwalk.py for the floor puzzles); the game's own solved flag is the verdict. Nothing is ever forced during play.
--start SPEC begins from a prepared state instead of a new game (the mod's start_progress: "preset:three-lasers", "area:Desert;lasers:2", ...):
a slice. Slices of the game run at once on different slots test the later areas without waiting for the early ones; that set-up is forced (the
mod marks those panels solved), so a slice's records say where it started, and only a run from a new game proves the whole route.

Co-op is tested on every solve: the panel must turn solved in every other world, and the time it takes is recorded per peer. Every
few rounds all worlds are compared (solved panels and open doors) and any difference is a recorded divergence.

Output, in <run folder>/playthrough/<start time>/: events.jsonl (one line per solve, failure, propagation, divergence, blocker),
summary.json (totals, per-bot and per-area counts, propagation percentiles, what blocked), shots/ (the game's picture after a failure)."""
import concurrent.futures as cf, json, os, shutil, statistics, subprocess, sys, threading, time
from pathlib import Path
HERE = os.path.dirname(os.path.abspath(__file__)); TOOLS = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, TOOLS)
from logic import Logic, CREDITS
from planner import Planner
from witness import ROOT, RUN, PYW, win, real_saves
import solver
from group_config import configs
from group_suite import replace_copy, digest, EXE_HASH, DLL_HASH

RES = os.environ.get('WITNESS_RESOURCES', '')


def sh(args, timeout=120): return subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=timeout).stdout.strip()
def pyw(script, *args, timeout=120, env=None, both=False):
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', script)), *map(str, args)], capture_output=True, text=True, timeout=timeout, env=env)
    return (r.stdout, r.stderr) if both else r.stdout


class Bot:
    """One game and the bot playing it."""
    def __init__(self, index):
        self.id = index; self.dir = Path(os.environ['WITNESS_DIR%d' % index]); self.port = int(os.environ['WITNESS_PORT%d' % index]); self.pid = None
        self.slot = os.environ.get('WITNESS_SLOTS', '').split(',')[index - 1] if os.environ.get('WITNESS_SLOTS') else str(index)

    def send(self, text):
        dest = self.dir / 'witness_cmd.txt'; deadline = time.monotonic() + 15
        while dest.exists():
            if time.monotonic() > deadline: raise RuntimeError('bot %d: command not consumed' % self.id)
            time.sleep(.05)
        tmp = dest.with_suffix('.tmp'); tmp.write_text(text + '\n'); tmp.replace(dest)

    def state(self):
        """Solved panels and open doors as the game holds them (memory read: tools/win/wstatedump.py)."""
        for attempt in range(3):      # a read can come back empty (the game was busy, or it is gone: the caller checks alive())
            try:
                out = pyw('wstatedump.py', self.pid, timeout=60).strip().splitlines()
                s = json.loads(out[-1]); return {int(k, 16) for k in s['panels']}, {int(k, 16) for k in s['doors']}, {int(k, 16) for k in s['powered_panels']}
            except (IndexError, ValueError, KeyError, subprocess.SubprocessError): time.sleep(1)
        raise RuntimeError('bot %d: no state' % self.id)

    def net(self):
        dest = self.dir / 'witness_net.json'; dest.unlink(missing_ok=True); self.send('netstate'); deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try: return json.loads(dest.read_text())
            except (OSError, ValueError): time.sleep(.05)
        raise RuntimeError('bot %d: no netstate' % self.id)

    def prepare(self, cfg_lines, fps, drop, start=''):
        d = self.dir
        assert digest(d / 'witness64_d3d11.exe') == EXE_HASH, 'unsupported game build'
        if not (d / 'AnselSDK64_real.dll').exists():
            assert digest(d / 'AnselSDK64.dll') == DLL_HASH, 'unrecognized original DLL'
            (d / 'AnselSDK64.dll').rename(d / 'AnselSDK64_real.dll')
        replace_copy(Path(ROOT) / 'proxy/AnselSDK64.dll', d / 'AnselSDK64.dll')
        models = d / 'witness_models'; models.mkdir(exist_ok=True)
        for asset in (Path(ROOT) / 'assets/models').iterdir():
            if asset.suffix in ('.glb', '.txt'): replace_copy(asset, models / asset.name)
        cfg = cfg_lines + 'force_active=1\ncap_fps=%d\ndev_menu=1\nvoice_mode=off\nvoice_mute_output=1\nno_partner_video=1\n' % fps
        if drop: cfg += 'drop_pct=%d\ndrop_seed=%d\n' % (drop, self.id)
        if start: cfg += 'start_progress=%s\n' % start
        (d / 'witness_coop.cfg').unlink(missing_ok=True); (d / 'witness_coop.cfg').write_text(cfg)
        for name in ('witness_cmd.txt', 'witness_proxy.log', 'witness_net.json'): (d / name).unlink(missing_ok=True)
        saves = d / 'appdata/The Witness'
        if saves.exists():
            for f in saves.iterdir():
                if f.suffix in ('.witness_campaign', '.png'): f.unlink()

    def start(self, new_game=True):
        if self.pid: sh(['taskkill.exe', '/PID', self.pid, '/F']); time.sleep(2)
        args = ['-skip_config_dialog', '-windowed'] + (['-new_game'] if new_game else [])      # without -new_game the game continues its newest save
        self.pid = pyw('launch.py', win(str(self.dir)), *args, timeout=60).strip().splitlines()[-1]
        sh([RES, 'register-pid', self.pid])
        rect = sh([RES, 'rect', self.slot]).split()
        wait(lambda: 'placed True' in pyw('wplace.py', self.pid, *rect), 'bot %d window placed' % self.id, 120)

    def alive(self):
        return self.pid and self.pid in sh(['tasklist.exe', '/FI', 'PID eq %s' % self.pid, '/NH'], 30)


def wait(predicate, what, timeout=180):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if predicate(): return
        except (ValueError, IndexError, RuntimeError, OSError, subprocess.SubprocessError): pass
        time.sleep(2)
    raise RuntimeError('timed out: ' + what)


class Recorder:
    def __init__(self, folder):
        self.folder = folder; folder.mkdir(parents=True, exist_ok=True); (folder / 'shots').mkdir(exist_ok=True)
        self.lock = threading.Lock(); self.f = open(folder / 'events.jsonl', 'a'); self.t0 = time.time()
    def __call__(self, kind, **data):
        rec = dict(t=round(time.time() - self.t0, 2), kind=kind, **data)
        with self.lock: self.f.write(json.dumps(rec) + '\n'); self.f.flush()
        if kind in ('solved', 'failed', 'blocked', 'divergence', 'goal', 'no solver', 'resolve another way', 'unpowered', 'crash', 'reroute', 'candidate lines'): print(json.dumps(rec), flush=True)
        return rec


class Playthrough:
    def __init__(self, n, goal, minutes, fps, drop, start='', resume=''):
        self.logic = Logic(); self.n, self.goal_name, self.deadline = n, goal, time.time() + minutes * 60
        self.fps, self.drop, self.start, self.resume = fps, drop, start, resume
        self.bots = [Bot(i) for i in range(1, n + 1)]
        self.out = Path(RUN) / 'playthrough' / time.strftime('%Y%m%d-%H%M%S'); self.rec = Recorder(self.out)
        self.manifest = {}; self.paths = {}; self.no_solver = set()
        self.planner = None; self.solved = set(); self.lock = threading.Lock()
        self.prop = []      # (panel, from bot, to bot, seconds)
        self.powered = set(); self.variant = {}; self.stalled_since = {}; self.fixups = []      # see stalls()
        self.last_why = {}; self.restore = set(); self.candidates_only = set(); self.avoid = set(); self.attempts = {}      # avoid: panels a detour must go round      # panels drawn from relaxed-rule candidates (the game judges)      # why a panel's last attempt failed; panels whose redraw was refused (drawn back the first way)

    # ---- set-up
    def launch(self):
        if self.n > 1:
            roster = [dict(id=b.id, host='127.0.0.1', port=b.port) for b in self.bots]; cfgs = configs(roster)
        else:
            cfgs = {1: 'id=1\nlisten=%d\nsave_dir=instance\nshare_zip=1\n' % self.bots[0].port}
        for b in self.bots: b.prepare(cfgs[b.id] if cfgs[b.id].endswith('\n') else cfgs[b.id] + '\n', self.fps, self.drop, self.start)
        if self.resume:      # every world continues the same save (a checkpoint of an earlier run: the bots' own play, not a forced state)
            for b in self.bots:
                saves = b.dir / 'appdata/The Witness'; saves.mkdir(parents=True, exist_ok=True)
                replace_copy(self.resume, saves / Path(self.resume).name)
        for b in self.bots: b.start(new_game=not self.resume)
        if self.n > 1: wait(lambda: all(b.net()['ready'] for b in self.bots), 'every world stable', 300)
        else:      # one game has no co-op core (no peer configured): wait for its picture and a readable world, then let it settle
            b = self.bots[0]
            wait(lambda: 'resources ready' in (b.dir / 'witness_proxy.log').read_text(errors='ignore') and len(b.state()[0]) >= 0, 'the world loaded', 300); time.sleep(20)
        self.rec('started', bots=self.n, pids=[b.pid for b in self.bots], fps=self.fps, drop=self.drop, goal=self.goal_name, start=self.start or ('resumed from %s' % self.resume if self.resume else 'new game'))

    def load_manifest(self):
        """The panels' puzzles, from a game before any line is drawn (a drawn line overwrites a stored solution): the master copy made
        by the sweep if there is one, else taken now from bot 1's fresh game."""
        master = Path(RUN) / 'panels.json'
        if not master.exists():
            import glob
            found = sorted(glob.glob(os.path.join(os.path.dirname(RUN), '*', 'panels.json')))
            if found: master = Path(found[0])
            else: master.write_text(pyw('wpanels.py', self.bots[0].pid, '--full', timeout=300).strip().splitlines()[-1])
        self.manifest = {q['id']: q for q in json.loads(master.read_text())}
        # what earlier sweeps learned about drawing each panel (its size in metres, the start offset): saves the scan on every panel it knows
        import glob, sweep
        if not os.path.exists(sweep.KCACHE):
            merged = {}
            for fn in sorted(glob.glob(os.path.join(os.path.dirname(RUN), '*', 'panel_k.json'))):
                try: merged.update(json.load(open(fn)))
                except (OSError, ValueError): pass
            if merged: json.dump(merged, open(sweep.KCACHE, 'w')); self.rec('kcache', panels=len(merged))
        self.rec('manifest', source=str(master), panels=len(self.manifest))

    def path_for(self, h):
        if h not in self.paths:
            if not self.manifest[h].get('nodes'):      # some panels get their puzzle only later in the game (the mountain's pillar room entry): read it now
                try:
                    live = json.loads(pyw('wpanels.py', self.bots[0].pid, '--full', '--ids', '%x' % h, timeout=120).strip().splitlines()[-1])
                    if live and live[0].get('nodes'): self.manifest[h] = live[0]; self.rec('puzzle read live', panel='%#x' % h, nodes=live[0]['nodes'])
                    else: return None      # not there yet: asked again next time (no path is cached)
                except (ValueError, IndexError, subprocess.SubprocessError): return None
            q = dict(self.manifest[h]); import sweep
            q['only_edges'] = sweep.floor_edges(h)
            try: v = solver.variants(q); self.variant[h] = (0, v); self.paths[h] = v[0] if v else None
            except Exception as e: self.paths[h] = None; self.rec('solver error', panel='%#x' % h, error=repr(e))
            if self.paths[h] is None and not solver.unsupported(q):
                # No line satisfies every rule as the solver models them (Mountain Near Row 4): the solver may miss a detail, so ask it again with
                # one rule switched off at a time (psolve's own switch) and let the game judge each candidate; a wrong line on an unsolved panel is
                # harmless (the game resets it).
                cands = []
                for rule in ('stars', 'squares', 'poly', 'triangles', 'dots'):
                    os.environ['PSOLVE_MUTANT'] = rule
                    try: c_ = solver.solve_rules(q, timeout=120)
                    except Exception: c_ = None
                    finally: os.environ.pop('PSOLVE_MUTANT', None)
                    if c_ and c_ not in cands: cands.append(c_)
                if cands:
                    self.variant[h] = (0, cands); self.paths[h] = cands[0]; self.candidates_only.add(h)
                    self.rec('candidate lines', panel='%#x' % h, name=self.logic.entities[h].name, n=len(cands), why='no line meets every modelled rule')
            if self.paths[h] is None:
                self.no_solver.add(h); self.rec('no solver', panel='%#x' % h, name=self.logic.entities[h].name, area=self.logic.entities[h].area, why=', '.join(solver.unsupported(q)) or 'no path found')
        return self.paths[h]

    # ---- one solve by one bot
    def solve(self, bot, h, redraw=False):
        import sweep
        e = self.logic.entities[h]; path = self.paths[h]; t0 = time.time()
        script = 'wwalk.py' if str(h) in sweep.floor_panels() else 'wdraw.py'
        # the drawing script's own account (timings and each segment) goes to stderr: kept with a failure, and the time split recorded for every solve
        env = {**os.environ, 'WDRAW_KCACHE': win(sweep.KCACHE), 'WDRAW_TIMING': '1', 'WDRAW_SHOTS': '1', 'WSLENV': os.environ.get('WSLENV', '') + ':WDRAW_KCACHE:WDRAW_TIMING:WDRAW_SHOTS:WDRAW_REDRAW'}
        if redraw: env['WDRAW_REDRAW'] = '1'
        # A first attempt gets a short search for the start circle, so a hard panel does not hold a bot while easy work waits; later attempts the full one.
        env['WDRAW_START_BUDGET'] = '75' if self.planner.tries.get(h, 0) == 0 else '230'; env['WSLENV'] += ':WDRAW_START_BUDGET:WDRAW_FLIP_SIDE'
        # The start was not found from the front last time: this time stand on the other side (a panel seen from behind or through a cage).
        if 'did not start' in self.last_why.get(h, '') and self.planner.tries.get(h, 0) % 2 == 1: env['WDRAW_FLIP_SIDE'] = '1'
        # The in-game tracer (proxy/tracer.cpp) draws the edges after the first at one frame a step, about twice as fast; a panel whose last
        # attempt failed in the tracer is drawn the old way.
        env['WSLENV'] += ':WDRAW_TRACER:WDRAW_STAND'
        # A line that got stuck may have run into something hiding part of the panel (the cursor only reaches what the camera sees): the next
        # attempts stand elsewhere, 1.2 m to one side then the other, nearer or further.
        if self.last_why.get(h, '').startswith('stuck'):
            env['WDRAW_STAND'] = ('1.2,2.0', '-1.2,2.0', '0,4.0', '1.5,3.5', '-1.5,3.5')[self.planner.tries.get(h, 0) % 5]
        if not self.last_why.get(h, '').startswith(('tracer', 'the in-game tracer')): env['WDRAW_TRACER'] = '1'
        err = ''
        try:
            out, err = pyw(script, bot.pid, win(str(bot.dir)), '%x' % h, ','.join(map(str, path)), timeout=420, env=env, both=True)
            lines = [l for l in out.splitlines() if l.startswith('{')]; r = json.loads(lines[-1]) if lines else {'solved': False, 'why': 'no result: ' + out[-160:]}
        except subprocess.TimeoutExpired: r = {'solved': False, 'why': 'the drawing script timed out'}
        dt = round(time.time() - t0, 1)
        if not bot.alive():
            self.rec('crash', bot=bot.id, panel='%#x' % h); bot.start(); wait(lambda: bot.net()['ready'], 'bot %d back' % bot.id, 300)
            return False
        if r.get('solved') and r.get('why') == 'already solved':      # its own world already had it: nothing was drawn
            self.rec('already solved', bot=bot.id, panel='%#x' % h, name=e.name); return True
        if r.get('solved'):
            self.rec('solved', bot=bot.id, panel='%#x' % h, name=e.name, area=e.area, seconds=dt, method=script, timing=[l.strip() for l in err.splitlines() if ' s  ' in l][-12:])
            threading.Thread(target=self.propagation, args=(bot, h), daemon=True).start()
            return True
        self.last_why[h] = r.get('why', '')
        shot = 'fail_%x_b%d' % (h, bot.id); bot.send('shot ' + shot)
        self.rec('failed', bot=bot.id, panel='%#x' % h, name=e.name, area=e.area, seconds=dt, why=r.get('why', ''), shot=str(bot.dir / 'shots' / (shot + '.bmp')), trace=err.splitlines()[-40:])
        return False

    def event_time(self, bot, text):
        """Milliseconds (the machine's performance counter, shared by every game here) of the last line of bot's witness_events.log containing `text`."""
        try:
            with open(bot.dir / 'witness_events.log', 'rb') as f:
                f.seek(0, 2); f.seek(max(0, f.tell() - 200000)); lines = f.read().decode(errors='ignore').splitlines()
        except OSError: return None
        for l in reversed(lines):
            if text in l:
                try: return float(l.split(' ', 1)[0])
                except ValueError: return None
        return None

    def propagation(self, origin, h, limit=60):
        """The co-op check: every other world must show the panel solved; how long each took."""
        t0 = time.time(); waiting = {b.id: b for b in self.bots if b is not origin}
        while waiting and time.time() - t0 < limit:
            for i, b in list(waiting.items()):
                try:
                    if h in b.state()[0]:
                        # exact: the sender's event line against the receiver's "applied" line (both games log with the same clock); else the polled time
                        t_sent = self.event_time(origin, 'local panel %#x solved' % h); t_got = self.event_time(b, 'remote panel %#x applied as solved' % h)
                        exact = round((t_got - t_sent) / 1000, 3) if t_sent and t_got and t_got >= t_sent else None
                        self.prop.append((h, origin.id, i, exact if exact is not None else time.time() - t0))
                        self.rec('propagated', panel='%#x' % h, origin=origin.id, to=i, seconds=round(time.time() - t0, 2), exact_s=exact); del waiting[i]
                except Exception: pass
            time.sleep(.5)
        for i in waiting: self.rec('divergence', panel='%#x' % h, origin=origin.id, missing_on=i, after=limit)

    def stalls(self, solved, busy, grace=8):
        """A panel the logic says is available but the game has not powered: something before it was solved the wrong way (a panel with
        two exits where only one lights the cable: the tutorial gate's latches). After a grace period (cables take a moment), each solved
        prerequisite that has another solution is queued to be drawn again the next way."""
        now = time.time(); avail = [h for h in self.logic.available(solved) if h in self.planner.targets and h not in busy]
        for h in list(self.stalled_since):
            if h not in avail or h in self.powered: del self.stalled_since[h]
        for h in avail:
            if h in self.powered: continue
            t0 = self.stalled_since.setdefault(h, now)
            if now - t0 < grace: continue
            pre = self.prerequisites(h, solved); moved = self.other_ways(h, solved, busy)
            if moved: self.stalled_since[h] = now      # give the new attempt its own grace period
            elif now - t0 > 120 and not getattr(self, '_unpowered_logged', {}).get(h):
                self.__dict__.setdefault('_unpowered_logged', {})[h] = True
                self.rec('unpowered', panel='%#x' % h, name=self.logic.entities[h].name, seconds=round(now - t0), prerequisites=sorted('%#x' % q for q in pre))

    def state_or_recover(self, b):
        """A bot's world, or None when it cannot be read: a game that is gone is started again (it rejoins the others as a late join); one bad read
        never stops the run."""
        try: return b.state()
        except RuntimeError:
            if b.alive(): self.rec('state unreadable', bot=b.id); return None
            self.rec('crash', bot=b.id, panel=None)
            try: b.start(); wait(lambda: b.net()['ready'] if self.n > 1 else True, 'bot %d back' % b.id, 300)
            except RuntimeError as e: self.rec('restart failed', bot=b.id, why=str(e))
            return None

    def prerequisites(self, h, solved):
        return {int(t, 16) for alt in self.logic.entities[h].rule for t in alt if t.startswith('0x') and int(t, 16) in solved
                and self.logic.entities.get(int(t, 16)) and self.logic.entities[int(t, 16)].kind == 'Panel'}

    def other_ways(self, h, solved, busy=()):
        """Queue every solved prerequisite of `h` that has a solution not tried yet, drawn the next way. True when one was queued."""
        moved = False
        for q in sorted(self.prerequisites(h, solved)):
            self.path_for(q); k, v = self.variant.get(q, (0, []))
            if k + 1 < len(v) and q not in self.fixups and q not in busy:
                self.variant[q] = (k + 1, v); self.paths[q] = v[k + 1]; self.fixups.append(q); moved = True
                self.rec('resolve another way', panel='%#x' % q, because='%#x' % h, variant=k + 1, of=len(v), start=v[k + 1][0], end=v[k + 1][-1])
        return moved

    def compare_worlds(self, settle=5):
        """Compare every world with bot 1's, twice `settle` seconds apart: what differs both times is recorded as persistent (a real
        divergence); what differed only the first time was in flight (a door still moving, a solve still travelling) and is recorded as such."""
        def snap():
            with cf.ThreadPoolExecutor(max_workers=self.n) as rp: got = dict(zip([b.id for b in self.bots], rp.map(self.state_or_recover, self.bots)))
            return {i: st[:2] for i, st in got.items() if st} if all(got.values()) else None
        def diff(st):
            base = st[self.bots[0].id]; out = {}
            for i, (panels, doors) in st.items():
                d = (frozenset(panels - base[0]), frozenset(base[0] - panels), frozenset(doors ^ base[1]))
                if any(d): out[i] = d
            return out
        a = snap()
        if not a: return      # a world could not be read: compare next time
        first = diff(a)
        if not first: return
        time.sleep(settle); b2 = snap()
        if not b2: return
        second = diff(b2)
        for i, (extra, missing, doors) in first.items():
            e2, m2, d2 = second.get(i, (frozenset(),) * 3)
            for persistent, (x, m, d) in ((True, (extra & e2, missing & m2, doors & d2)), (False, (extra - e2, missing - m2, doors - d2))):
                if x or m or d:
                    self.rec('divergence', persistent=persistent, between=[self.bots[0].id, i], panels_only_here=sorted('%#x' % p for p in x)[:20],
                             panels_missing=sorted('%#x' % p for p in m)[:20], doors_differ=sorted('%#x' % q for q in d)[:20])

    # ---- the loop
    def run(self):
        try: self.play()
        except KeyboardInterrupt: self.rec('stopped', why='interrupted'); self.checkpoint(); self.summary()

    def play(self):
        self.launch(); self.load_manifest()
        # A bot only starts a panel the game has powered: the logic says when it should be, the game says when it is (see stalls()).
        self.planner = Planner(self.logic, self.goal_name, can_solve=lambda h: h in self.manifest and h in self.powered and self.path_for(h) is not None)
        if self.start:      # a slice: the route from the state it starts in (seven lasers lit: straight to the mountain, not round the island again)
            from planner import route
            done0 = set().union(*(st[0] for st in [b.state() for b in self.bots]))
            r_ = route(self.logic, solved=frozenset(done0))
            if r_: self.planner.targets = r_; self.rec('route from start', solved=len(done0), route=len(r_), left=len(r_ - done0))
        busy = {}      # bot id -> (future, panel)
        rounds = 0
        with cf.ThreadPoolExecutor(max_workers=self.n) as pool:
            while time.time() < self.deadline:
                # Plan from every world at once: a panel solved in one world counts as done even if it never reached another (that is a
                # co-op failure, recorded by propagation() and compare_worlds(), not a reason to solve it again). Power likewise.
                with cf.ThreadPoolExecutor(max_workers=self.n) as rp: states = [st for st in rp.map(self.state_or_recover, self.bots) if st]
                if not states: continue
                solved = set().union(*(st[0] for st in states)); self.powered = set().union(*(st[2] for st in states)); self.solved = solved
                self.stalls(solved, [h for _, h in busy.values()])
                for i, (fut, h) in list(busy.items()):
                    if not fut.done(): continue
                    ok = fut.result(); why = self.last_why.get(h, '') if not ok else ''; was_redraw = h in self.restore
                    self.restore.discard(h); del busy[i]; self.attempts[h] = self.attempts.get(h, 0) + 1      # every attempt, whatever its tries count says
                    if ok: continue
                    self.planner.failed(h)
                    if was_redraw:
                        # A redraw the game refused leaves the panel unsolved in that world (the progress is lost there): draw the solution that
                        # worked back at once, and do not try this panel's other ways again.
                        k, v = self.variant[h]; self.variant[h] = (len(v), v); self.paths[h] = v[0]; self.fixups.insert(0, h)
                        self.rec('redraw refused: restoring', panel='%#x' % h, why=why)
                    elif 'did not accept' in why and h in self.variant and self.variant[h][0] + 1 < len(self.variant[h][1]) and self.attempts.get(h, 0) < len(self.variant[h][1]) + 3:
                        # The game refused this line: the panel's next solution (another exit, another start: Town's Triple Exit; or the next
                        # relaxed-rule candidate). A refused line on an unsolved panel costs nothing.
                        k, v = self.variant[h]; self.variant[h] = (k + 1, v); self.paths[h] = v[k + 1]; self.planner.tries[h] = 0      # the next candidate line
                        self.rec('next candidate', panel='%#x' % h, candidate=k + 1, of=len(v))
                    elif why.startswith('stuck') or 'did not accept' in why:
                        # The line got stuck (or was refused) on a panel the game has powered: what blocks it may be a prerequisite solved the
                        # wrong way (the tutorial gate's latches stay shut unless Back Left ends at its bottom exit). Redraw those another way and
                        # give this panel its tries back. A start that was not found is the drawing's own problem and changes nothing here.
                        if self.other_ways(h, self.solved, [x for _, x in busy.values()]): self.planner.tries[h] = 0
                if self.planner.done(solved): self.rec('goal', goal=self.goal_name, solved=len(solved)); break
                free = [b.id for b in self.bots if b.id not in busy]
                while free and self.fixups:      # re-solving a prerequisite another way comes before new work: it unblocks something
                    h = self.fixups.pop(0)
                    if h in [x for _, x in busy.values()]: continue
                    i = free.pop(0); k, v = self.variant.get(h, (0, []))
                    if k < len(v): self.restore.add(h)      # an experiment (another way): a refusal is undone; a restore (k == len) is not one
                    busy[i] = (pool.submit(self.solve, self.bots[i - 1], h, True), h)
                for i, h in self.planner.assign(solved, free, busy=[h for _, h in busy.values()]).items():
                    busy[i] = (pool.submit(self.solve, self.bots[i - 1], h), h)
                young = [h for h, t0 in self.stalled_since.items() if time.time() - t0 < 150]
                if not busy and not self.fixups and not young:
                    blockers = self.planner.blocked(solved) or [h for h in self.stalled_since]
                    # Before giving up: a detour round what the bots cannot do (another set of lasers, another way into an area).
                    self.avoid |= set(blockers)
                    added = self.planner.reroute(solved, self.avoid) if blockers else None
                    if added is not None:
                        self.rec('reroute', avoiding=sorted('%#x' % h for h in self.avoid), added=len(added), route=len(self.planner.targets)); continue
                    # No detour: before giving up, every blocker gets one more round of tries (what made it fail may have changed since:
                    # a prerequisite redrawn, another world's progress arrived, a game restarted).
                    if not getattr(self, '_second_round', False) and blockers:
                        self._second_round = True; self.avoid -= set(blockers)
                        for h in blockers: self.planner.tries[h] = 0
                        self.rec('second round', panels=sorted('%#x' % h for h in blockers)); continue
                    self.rec('blocked', panels=[dict(panel='%#x' % h, name=self.logic.entities[h].name, area=self.logic.entities[h].area,
                                                    tries=self.planner.tries.get(h, 0), no_solver=h in self.no_solver) for h in blockers])
                    break
                rounds += 1
                if rounds % 20 == 0: self.compare_worlds()
                # Do not wait for a full stop: when panels have used up their tries, look for a route round them while there is still other work.
                spent = {h for h, n in self.planner.tries.items() if n >= self.planner.max_tries and h not in solved} - self.avoid
                if rounds % 30 == 0 and spent:
                    added = self.planner.reroute(solved, self.avoid | spent)
                    if added is not None: self.avoid |= spent; self.rec('reroute', avoiding=sorted('%#x' % h for h in self.avoid), added=len(added), route=len(self.planner.targets), early=True)
                time.sleep(2)
        try: time.sleep(5); self.compare_worlds(); self.checkpoint()
        finally: self.summary()

    def checkpoint(self):
        """A save point of bot 1's world copied into the run folder (checkpoint.witness_campaign): the next run can --resume from it."""
        try:
            b = self.bots[0]; src = b.dir / 'savepoints' / 'checkpoint.witness_campaign'; t0 = src.stat().st_mtime if src.exists() else 0
            b.send('savepoint checkpoint'); deadline = time.time() + 30
            while time.time() < deadline and not (src.exists() and src.stat().st_mtime > t0): time.sleep(0.5)
            if src.exists() and src.stat().st_mtime > t0:
                time.sleep(1); shutil.copyfile(src, self.out / 'checkpoint.witness_campaign'); self.rec('checkpoint', file=str(self.out / 'checkpoint.witness_campaign'), solved=len(self.solved))
        except (OSError, RuntimeError) as e: self.rec('checkpoint failed', why=str(e))

    def summary(self):
        ev = [json.loads(l) for l in open(self.out / 'events.jsonl')]
        solves = [e for e in ev if e['kind'] == 'solved']; fails = [e for e in ev if e['kind'] == 'failed']
        props = [p[3] for p in self.prop]
        def pct(v, q): return round(statistics.quantiles(v, n=100)[q - 1], 2) if len(v) >= 2 else (round(v[0], 2) if v else None)
        by_area = {}
        for e in solves: by_area.setdefault(e['area'], 0); by_area[e['area']] += 1
        done, _ = self.logic.derive(self.solved)
        s = dict(start=self.start or 'new game', goal=self.goal_name, reached=self.planner.done(self.solved), bots=self.n, minutes=round((time.time() - self.rec.t0) / 60, 1),
                 solved_in_world=len(self.solved & set(self.logic.panels)), route=len(self.planner.targets), solves=len(solves), failures=len(fails),
                 lasers=sum(1 for l in self.logic.lasers if l in done), per_bot={b.id: sum(1 for e in solves if e['bot'] == b.id) for b in self.bots},
                 by_area=by_area, median_solve_s=statistics.median([e['seconds'] for e in solves]) if solves else None,
                 propagation_s=dict(p50=pct(props, 50), p95=pct(props, 95), max=round(max(props), 2) if props else None, n=len(props)),
                 divergences=sum(1 for e in ev if e['kind'] == 'divergence' and e.get('persistent', True)), transient_differences=sum(1 for e in ev if e['kind'] == 'divergence' and e.get('persistent') is False), crashes=sum(1 for e in ev if e['kind'] == 'crash'),
                 no_solver=sorted('%#x' % h for h in self.no_solver), blocked=[e for e in ev if e['kind'] == 'blocked'][-1:] or None)
        (self.out / 'summary.json').write_text(json.dumps(s, indent=1)); print('SUMMARY', json.dumps(s), flush=True)


def main():
    a = sys.argv[1:]
    def opt(name, default, cast=str): return cast(a[a.index(name) + 1]) if name in a else default
    n = int(a[0]) if a and a[0].isdigit() else 2
    before = real_saves()
    try: Playthrough(n, opt('--goal', 'credits'), opt('--minutes', 600, float), opt('--fps', 60, int), opt('--drop', 0, int), opt('--start', ''), opt('--resume', '')).run()
    finally: assert real_saves() == before, 'real saves changed'


if __name__ == '__main__': main()
