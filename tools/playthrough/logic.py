"""The game's progression as data: regions, the panels in them, the doors and lasers they open, and what each needs.

Read from data/WitnessLogicVanilla.txt (Archipelago's vanilla logic, see data/SOURCE.md). Its grammar:
  ==Area==                                              an area heading
  Region (Short) - Target - rule - Target - rule ...:   a region and its connections (two-way unless the option is TrueOneWay)
  <location id> - 0xHEX (Name) - rule - symbols         a panel, EP, obelisk or event in the current region
  Door - 0xHEX (Name) - rule                            a door: open once its rule holds
  Laser - 0xHEX (Name) - rule                           a laser: lit once its rule holds
A rule is in disjunctive normal form: `a & b | c`. A token is an entity id (that entity is done: a panel solved, a door open,
a laser lit), a region name (that region can be reached), `True`, `TrueOneWay`, `N Lasers`, `N Lasers + Redirect` (the Desert
laser redirected as well) or `Theater to Tunnels` (an EP that needs a quick run; treated as True here).

Game-free: the state is a set of solved entity ids; everything else (open doors, lit lasers, reachable regions, what can be
solved next) is derived from it the way the game derives it."""
import os, re
from collections import defaultdict

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'WitnessLogicVanilla.txt')
LASER_COUNT = re.compile(r'^(\d+) Lasers( \+ Redirect)?$')
REDIRECT = 0x09F98      # Desert Laser Redirect Control
CREDITS = 0x3D9A9       # Mountain bottom floor: Elevator Start (needs 7 lasers): the ending
BOX_LONG = 0xFFF00      # 11 lasers and the mountain entry panel: the long ending's box (an event, not a panel)


def parse_rule(text):
    """`a & b | c` -> [frozenset(a, b), frozenset(c)]; `True` -> [frozenset()]."""
    if text == 'True': return [frozenset()]
    return [frozenset(alt.split(' & ')) for alt in text.split(' | ')]


class Entity:
    __slots__ = ('hex', 'name', 'kind', 'region', 'area', 'rule', 'symbols', 'location')
    def __init__(self, hex_, name, kind, region, area, rule, symbols='', location=None):
        self.hex, self.name, self.kind, self.region, self.area, self.rule, self.symbols, self.location = hex_, name, kind, region, area, rule, symbols, location
    def __repr__(self): return '%s %#07x %s (%s)' % (self.kind, self.hex, self.name, self.region)


class Logic:
    def __init__(self, path=DATA):
        self.entities = {}                      # hex -> Entity
        self.regions = {}                       # name -> (short name, area)
        self.connections = defaultdict(list)    # region -> [(target, rule)]
        self.in_region = defaultdict(list)      # region -> [hex] of panels, EPs, events and obelisks placed there
        area = 'Misc'; region = None
        raw = []
        for line in open(path, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line[0] == '#': continue
            if line[0] == '=': area = line[2:-2]; continue
            if line.endswith(':'):
                parts = line[:-1].split(' - '); full = parts.pop(0); region, short = full.split(' (', 1)
                self.regions[region] = (short[:-1], area)
                for i in range(0, len(parts), 2): raw.append((region, parts[i], parse_rule(parts[i + 1])))
                continue
            parts = line.split(' - '); loc = parts[0]; hex_ = int(parts[1][:7], 16); name = parts[1][9:-1]; rule = parse_rule(parts[2])
            if loc in ('Door', 'Laser'):
                self.entities[hex_] = Entity(hex_, name, loc, region, area, rule)
                continue
            kind = ('Obelisk Side' if 'Obelisk Side' in name else 'Obelisk' if 'Obelisk' in name else 'EP' if 'EP' in name
                    else 'Event' if hex_ >= 0xFF000 or 'Pet the Dog' in name else 'Panel')
            self.entities[hex_] = Entity(hex_, name, kind, region, area, rule, parts[3] if len(parts) > 3 else '', int(loc))
            self.in_region[region].append(hex_)
        # Connections are two-way, except the options that contain TrueOneWay (Archipelago's reverse_connection).
        for src, dst, rule in raw:
            self.connections[src].append((dst, rule))
            back = [alt for alt in rule if 'TrueOneWay' not in alt]
            if back: self.connections[dst].append((src, back))
        self.lasers = [h for h, e in self.entities.items() if e.kind == 'Laser']
        self.panels = [h for h, e in self.entities.items() if e.kind == 'Panel']

    # ---- the state derived from what is solved
    def derive(self, solved):
        """From the solved panels (and events): open doors, lit lasers, reachable regions and done entities, to a fixed point."""
        solved = set(solved); done = set(solved); reach = {'Entry'}
        while True:
            changed = False
            for h, e in self.entities.items():      # doors and lasers have no place in the logic: they are done when their rule holds
                if h not in done and e.kind in ('Door', 'Laser') and self.holds(e.rule, done, reach): done.add(h); changed = True
            for h, e in self.entities.items():      # events (like the redirected laser) happen by themselves when they can
                if h not in done and e.kind == 'Event' and e.region in reach and self.holds(e.rule, done, reach): done.add(h); changed = True
            for src in list(reach):
                for dst, rule in self.connections[src]:
                    if dst not in reach and self.holds(rule, done, reach): reach.add(dst); changed = True
            if not changed: return done, reach

    def holds(self, rule, done, reach):
        return any(all(self.token(t, done, reach) for t in alt) for alt in rule)

    def token(self, t, done, reach):
        if t in ('True', 'TrueOneWay', 'Theater to Tunnels'): return True
        if t.startswith('0x'): return int(t, 16) in done
        m = LASER_COUNT.match(t)
        if m: return sum(1 for h in self.lasers if h in done) >= int(m.group(1)) and (not m.group(2) or REDIRECT in done)
        if t in self.regions: return t in reach
        raise ValueError('unknown rule token %r' % t)

    def available(self, solved, kinds=('Panel',)):
        """What can be done now: in a reachable region, its rule holds, not done yet."""
        done, reach = self.derive(solved)
        return [h for region in reach for h in self.in_region[region]
                if h not in done and self.entities[h].kind in kinds and self.holds(self.entities[h].rule, done, reach)]

    def needed_for(self, goal):
        """Every panel that some route to `goal` cannot skip... approximated as the panels on the requirement closure of the goal
        (rules followed through their first alternative, regions through every connection rule on a shortest region path)."""
        need = set(); stack = [goal]
        while stack:
            h = stack.pop()
            if h in need: continue
            need.add(h); e = self.entities[h]
            for t in e.rule[0]:
                if t.startswith('0x'): stack.append(int(t, 16))
        return need
