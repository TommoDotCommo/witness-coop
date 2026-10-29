"""What each bot should do next, for N bots playing one co-op world.

A goal is a set of panels: `credits` (the panels the ending needs: found once by pruning, see route()) or `all` (every panel).
Each free bot gets a different panel that is available now (logic.available). Among those, panels that open the most new work come first
(doors, lasers, region access), so more panels become available for the other bots: with N bots the world opens up N ways at once.
A panel no bot can solve (no solver for it, or failed too often) is skipped while there is other work; when it blocks the goal the
planner says so (blocked()) instead of guessing."""
import json, os
from logic import Logic, CREDITS

ROUTE_CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'credits_route.json')


def finishes(logic, allowed, goal, start=frozenset()):
    """Does solving only `allowed` panels (whenever they become available), from the `start` state, reach every panel of `goal`?"""
    solved = set(start)
    while True:
        new = [h for h in logic.available(solved) if h in allowed]
        if not new: return goal <= solved or goal <= logic.derive(solved)[0]      # (a laser or door goal is lit or open, not solved)
        solved.update(new)


def route(logic, goal=frozenset([CREDITS]), cache=ROUTE_CACHE, avoid=frozenset(), solved=frozenset()):
    """A set of panels that reaches `goal`, pruned: a panel is dropped when the goal can still be reached without it. Greedy (not the
    smallest set in general, but every panel left in it is needed given the others). Cached in data/credits_route.json for the ending."""
    if avoid or solved: cache = None      # a detour, or a route from a state already part played, is worked out afresh
    if cache and goal == frozenset([CREDITS]) and os.path.exists(cache):
        return set(int(h, 16) for h in json.load(open(cache))['panels'])
    keep = set(logic.panels) - set(avoid)
    if not finishes(logic, keep, set(goal), solved):
        if avoid: return None      # no way round the avoided panels
        raise AssertionError('the goal cannot be reached at all')
    for h in sorted(logic.panels, key=lambda h: logic.entities[h].location or 0, reverse=True):      # later panels first: they are the likeliest extras
        if h in goal or h in solved: continue      # (what is solved already stays: pruning it would only reshuffle the route)
        keep.discard(h)
        if not finishes(logic, keep, set(goal), solved): keep.add(h)
    if cache and goal == frozenset([CREDITS]):
        json.dump({'goal': '%#x' % CREDITS, 'panels': sorted('%#07x' % h for h in keep)}, open(cache, 'w'), indent=0)
    return keep


class Planner:
    def __init__(self, logic, goal='credits', can_solve=lambda h: True, max_tries=3):
        self.logic = logic
        self.targets = route(logic) if goal == 'credits' else set(logic.panels)
        self.goal = {CREDITS} if goal == 'credits' else set(logic.panels)
        self.can_solve, self.max_tries = can_solve, max_tries
        self.tries = {}      # hex -> failed attempts

    def done(self, solved): return self.goal <= set(solved)

    def candidates(self, solved, busy=()):
        busy = set(busy)
        return [h for h in self.logic.available(solved) if h in self.targets and h not in busy
                and self.tries.get(h, 0) < self.max_tries and self.can_solve(h)]

    def gain(self, solved, h):
        """How much new work solving `h` opens: panels that become available (and lasers lit, which the ending counts)."""
        before = set(self.logic.available(solved)); after = set(self.logic.available(set(solved) | {h}))
        done_b, _ = self.logic.derive(solved); done_a, _ = self.logic.derive(set(solved) | {h})
        lasers = sum(1 for l in self.logic.lasers if l in done_a and l not in done_b)
        return len(after - before) + 10 * lasers

    def assign(self, solved, free_bots, busy=()):
        """{bot: panel} for as many free bots as there is work, each a different panel, the most opening ones first."""
        cands = self.candidates(solved, busy)
        cands.sort(key=lambda h: (-self.gain(solved, h), h))
        return dict(zip(free_bots, cands))

    def failed(self, h): self.tries[h] = self.tries.get(h, 0) + 1

    def reroute(self, solved, avoid):
        """A route to the goal round the panels in `avoid` (the ones the bots keep failing): the ending needs any 7 of 11 lasers, and some areas
        have two ways in, so a blocked panel may have a detour. Returns the panels it added to the route (None: no detour)."""
        if self.goal != {CREDITS}: return None
        new = route(self.logic, avoid=frozenset(avoid), solved=frozenset(solved))
        if new is None: return None
        added = new - self.targets; self.targets = new | (set(solved) & set(self.logic.panels)); return added

    def blocked(self, solved, busy=()):
        """When nothing is left to do but the goal is not reached: the available target panels the bots gave up on or cannot solve."""
        if self.done(solved) or self.candidates(solved, busy) or busy: return []
        return [h for h in self.logic.available(solved) if h in self.targets]


class SimGame:
    """A stand-in for the game in tests: it refuses a panel that is not available (as the real game does: unpowered, behind a closed door)."""
    def __init__(self, logic): self.logic, self.solved = logic, set()
    def solve(self, h):
        if h not in self.logic.available(self.solved): raise RuntimeError('%#x is not available: the planner broke the rules' % h)
        self.solved.add(h)


def simulate(logic, bots, goal='credits', seconds=lambda h: 60, can_solve=lambda h: True):
    """Discrete-event run of N bots on one shared world: returns (finished, makespan seconds, solves per bot, game)."""
    game, plan = SimGame(logic), Planner(logic, goal, can_solve)
    clock, running, per_bot = 0.0, {}, {b: 0 for b in range(bots)}      # running: bot -> (finish time, panel)
    while not plan.done(game.solved):
        free = [b for b in range(bots) if b not in running]
        for b, h in plan.assign(game.solved, free, busy=[p for _, p in running.values()]).items(): running[b] = (clock + seconds(h), h)
        if not running: return False, clock, per_bot, game
        b = min(running, key=lambda k: running[k][0]); clock, h = running.pop(b)
        game.solve(h); per_bot[b] += 1
    return True, clock, per_bot, game
