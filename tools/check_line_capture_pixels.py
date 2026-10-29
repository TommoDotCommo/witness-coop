#!/usr/bin/env python3
"""Check a receiver's native maze groove against its remote red trace in a BMP.
Fixture: panel 0x293, dev-menu teleport, default red player 1, in-game capture.
This independently checks screen space: matching world vertices is insufficient
when the overlay and native camera projections differ. Exit 1 exposes a residual
alignment bug; it does not change geometry or assume a replacement field of view.
"""
import json
import statistics
import struct
import sys


def measure(path):
    with open(path,'rb') as f:data=f.read()
    off=struct.unpack_from('<I',data,10)[0];w,h=struct.unpack_from('<ii',data,18);top=h<0;h=abs(h)
    assert struct.unpack_from('<H',data,28)[0]==32,'expected in-game 32-bit BMP'
    def pixel(x,y):
        at=off+((y if top else h-1-y)*w+x)*4
        b,g,r=data[at:at+3];return r,g,b
    def red(x,y):
        r,g,b=pixel(x,y);return r>230 and 50<g<130 and 50<b<130
    redrows=[]
    # The puzzle is around the screen center; exclude HUD and other players.
    for y in range(h//4,3*h//4):
        xs=[x for x in range(w//3,2*w//3) if red(x,y)]
        if len(xs)>=30:redrows.append((y,xs))
    assert redrows,'no long red maze edge in capture'
    top_y=redrows[0][0]
    y,xs=max((r for r in redrows if r[0]<top_y+8),key=lambda r:len(r[1]))
    # Sample the middle of the top horizontal edge, away from corners/stubs.
    lo,hi=min(xs),max(xs);assert hi-lo>40,'maze edge too small'
    offsets=[];native=[];remote=[]
    # The groove search window must stay inside the groove's own row: the next horizontal groove is about a quarter of the maze width away.
    half=int(min(18,max(5,.17*(hi-lo))))
    for x in range(lo+(hi-lo)//4,hi-(hi-lo)//4):
        ry=[yy for yy in range(y-4,y+5) if red(x,yy)]
        gy=[]
        for yy in range(y-half,y+half+1):
            r,g,b=pixel(x,yy)
            if 70<r<190 and 50<g<145 and b<65:gy.append(yy)
        if ry and gy:
            rc=(min(ry)+max(ry))/2;gc=(min(gy)+max(gy))/2
            remote.append(rc);native.append(gc);offsets.append(rc-gc)
    assert len(offsets)>=10,'native groove not measurable'
    return dict(samples=len(offsets),native_y=statistics.median(native),remote_y=statistics.median(remote),offset_px=statistics.median(offsets))


if __name__=='__main__':
    result=measure(sys.argv[1]);print(json.dumps(result))
    if abs(result['offset_px'])>1:
        print('FAIL remote trace misses native groove by more than one pixel')
        sys.exit(1)
    print('PASS remote trace within one pixel of native groove')
