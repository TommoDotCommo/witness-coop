#!/usr/bin/env python3
"""Game-free test of tools/oracle_merge.py, with mutants (a policy that replaces too much or too little must fail)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, 'win')); sys.path.insert(0, HERE)
import wnav, oracle_merge as om

def maps():
    o = wnav.Map(); d = wnav.Map()
    o.cells = {(0, 0): [-100.0, 1, wnav.ORACLE], (1, 0): [-100.0, 1, wnav.ORACLE], (2, 0): [4.0, 1, wnav.ORACLE], (3, 0): [7.0, 1, wnav.ORACLE]}
    d.cells = {(0, 0): [3.0, 1, 0], (1, 0): [3.5, 1, wnav.WALKED], (2, 0): [9.0, 1, 0], (4, 0): [1.0, 1, 0]}
    return o, d

def run():
    bad = 0; o, d = maps()
    def check(name, ok):
        nonlocal bad
        print(('ok     ' if ok else 'FAILED '), name); bad += (not ok)
    m, n = om.merge(o, d, 'yield')
    check('yield: both no-ground cells replaced', n == 2 and m.cells[(0, 0)][0] == 3.0 and m.cells[(1, 0)][0] == 3.5)
    check('yield: the oracle keeps a cell it has ground for', m.cells[(2, 0)][0] == 4.0 and m.cells[(2, 0)][2] & wnav.ORACLE)
    check('yield: oracle-only and depth-only cells are kept', m.cells[(3, 0)][0] == 7.0 and m.cells[(4, 0)][0] == 1.0)
    m, n = om.merge(o, d, 'walked')
    check('walked: only the walked cell is replaced', n == 1 and m.cells[(0, 0)][0] == -100.0 and m.cells[(1, 0)][0] == 3.5)
    # mutant: a merge that lets the depth map overwrite every shared cell
    mm = {c: list(r) for c, r in o.cells.items()}
    for c, r in d.cells.items(): mm[c] = list(r)
    check('mutant (depth overwrites all) is told apart: oracle ground cell lost', mm[(2, 0)][0] != 4.0)
    return bad

if __name__ == '__main__':
    sys.exit(1 if run() else 0)
