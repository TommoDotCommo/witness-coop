"""Read native panel trace records and remember the pose when they change.
Usage: <pid> <panel hex> <output json> <stop file>. Read-only process access.
The output is evidence from memory, independent of the mod's quantizer.
"""
import json, os, struct, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); eid = int(sys.argv[2],16)
last = None; anchor = None; sequence = 0; deadline = time.monotonic()+300
while time.monotonic() < deadline and not os.path.exists(sys.argv[4]):
    e = p.entity(eid)
    if e and p.type_of(e) == TI_PANEL:
        n = p.u32(e+0x228); ptr=p.u64(e+0x230)
        raw=p.rd(ptr,n*52) if ptr and n and n<600 else None
        pose=struct.unpack('<3f',p.rd(e+0x24,12))+struct.unpack('<4f',p.rd(e+0x34,16))
        if raw and raw != last:
            anchor=pose; last=raw; sequence+=1
            records=[]
            for i in range(n):
                r=raw[i*52:(i+1)*52]
                records.append(dict(nodes=struct.unpack_from('<2I',r),t=struct.unpack_from('<f',r,16)[0],a=struct.unpack_from('<3f',r,24),b=struct.unpack_from('<3f',r,36)))
            data=dict(panel=eid,anchor=anchor,pose=pose,records=records)
            target=sys.argv[3]+'-%08d.json'%sequence
            with open(target+'.tmp','w') as f: json.dump(data,f)
            os.replace(target+'.tmp',target)
    time.sleep(.01)
