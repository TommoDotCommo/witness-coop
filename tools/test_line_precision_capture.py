#!/usr/bin/env python3
"""Prove the native-process/renderer capture oracle rejects alignment regressions."""
import copy
from qa_line_precision import measure
native=dict(anchor=[0,0,0,0,0,0,1],records=[dict(a=[.012473,-.231237,.1],b=[.012473,.411237,.1],t=.371)])
a=native['records'][0]['a'];r=native['records'][0]
b=[x+(y-x)*r['t'] for x,y in zip(a,r['b'])]
row=dict(segment=0,fine=True,pose=native['anchor'],ends=[a,b],quad=[[p[0],p[1],p[2]+z] for p in (a,b) for z in (-.006,.006)])
measure(native,[row])
for mutant in ('millimetres','overhang','whole-edge','view-facing'):
    broken=copy.deepcopy(row)
    if mutant=='millimetres':
        for p in broken['ends']:
            for k in range(3):p[k]=round(p[k]*1000)/1000
    elif mutant=='overhang':
        for p in broken['quad'][2:]:p[1]+=.003
    elif mutant=='whole-edge':broken['ends'][1]=r['b'][:]
    else:broken['quad'][0][0]+=.006
    try:measure(native,[broken])
    except AssertionError:print('PASS capture oracle rejected '+mutant)
    else:raise AssertionError('capture oracle accepted '+mutant)
print('PASS capture oracle clean partial native edge')
