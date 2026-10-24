"""Solve a floor puzzle (a panel whose line is drawn by walking over pressure plates) by walking the path.

Usage: Windows Python wwalk.py <pid> <game folder> <panel id hex> <node,node,...>
Measured (panel 0x1cd3, build 11370412): the panel has one pressure plate per node and one per edge of its graph (66 plates for 25 nodes and 41 edges); a plate
lists its panel at +0xe4 (an engine id) and what it stands for at +0xe8 (1 = an edge, 0 = a node), +0xf0 / +0xf4 (the node, or the two nodes of the edge). Standing on a plate
changes the panel (its traced-node count at +0x228 becomes 1, its line object appears). The player is held over each plate in the order of the path, with the same
hover the dev menu's teleport uses, and the panel's traced count and solved flags are read after every step. Prints one JSON line like wdraw.py."""
import os, sys, math, struct, time, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
pid, gdir, eid = int(sys.argv[1]), sys.argv[2], int(sys.argv[3], 16)
path = [int(v) for v in sys.argv[4].split(',')]
p = attach(pid); c = Control(gdir); e = p.entity(eid); base_id = p.u32(p.u64(p.base + GLOBALS) + 8)
DWELL = float(os.environ.get('WWALK_DWELL') or 0.8)
def traced(): return p.u32(e + 0x228)
def is_solved(): return p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1
def done(solved, why, steps=0):
    c.send(['hover off'], 0.3)
    print(json.dumps({'id': eid, 'solved': solved, 'why': why, 'steps': steps, 'traced': traced(), 'want': path}), flush=True); sys.exit(0 if solved else 1)
if is_solved(): done(True, 'already solved')
# the plates of this panel
tab = p.entity_table(); node_plate, edge_plate = {}, {}
for i, pe in enumerate(tab):
    if not pe: continue
    f = struct.unpack('<8I', p.rd(pe + 0xe4, 32))
    if f[0] - base_id != eid: continue
    x, y, z = struct.unpack('<3f', p.rd(pe + 0x24, 12))
    if f[1] == 1: edge_plate[frozenset((f[3], f[4]))] = (x, y, z)
    elif f[1] == 0: node_plate[f[3]] = (x, y, z)
print('plates: %d nodes, %d edges' % (len(node_plate), len(edge_plate)), file=sys.stderr)
if not node_plate: done(False, 'this panel has no pressure plates')
# nodes in the middle of the path that have no plate are markers on an edge (a dot or a gap): the plate of the edge between its neighbours is the one to step on
path = [nd for kk, nd in enumerate(path) if nd in node_plate or kk == 0 or kk == len(path) - 1]
steps = []      # (name, position, traced count expected after it, or None)
for kk, nd in enumerate(path):
    if nd in node_plate: steps.append(('node %d' % nd, node_plate[nd], kk + 1))
    if kk + 1 < len(path):
        ed = frozenset((nd, path[kk + 1]))
        if ed in edge_plate: steps.append(("edge %d-%d" % (nd, path[kk + 1]), edge_plate[ed], None))
print('walking %d plates' % len(steps), file=sys.stderr)
for n_, (name, (x, y, z), expect) in enumerate(steps):
    for attempt in range(4):      # a plate that did not take the step (the count did not move on) is walked on again, a little off-centre
        jx, jy = (0.0, 0.0) if attempt == 0 else ((0.12, 0.0), (-0.12, 0.0), (0.0, 0.12))[attempt - 1]
        c.send(['hover %.3f %.3f %.3f 0 0' % (x + jx, y + jy, z + 2.2)], DWELL)
        if expect is None or traced() >= expect: break
    if os.environ.get('WWALK_DEBUG'): print(n_, name, 'traced', traced(), 'expected', expect, file=sys.stderr)
    if is_solved(): break
t0 = time.time()
while time.time() - t0 < 6 and not is_solved(): time.sleep(0.3)
done(is_solved(), '' if is_solved() else 'walked the path but the game did not accept it', len(steps))
