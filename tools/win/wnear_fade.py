"""T2 near-camera opacity oracle: real poses, process camera reads and GPU fragment queries."""
import os, sys, time, math, struct, re, json
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
A, B = attach(pa), attach(pb)
checks = {}; samples = {}
def check(name, value, detail):
    checks[name] = bool(value)
    print('  %s: %s (%s)' % (name, 'ok' if value else 'FAILED', detail), flush=True)
def send(d, lines, settle=0):
    cmd = os.path.join(d, 'witness_cmd.txt'); deadline = time.time()+8
    while os.path.exists(cmd) and time.time()<deadline: time.sleep(.05)
    tmp=cmd+'.tmp'; open(tmp,'w').write('\n'.join(lines)+'\n'); os.replace(tmp,cmd)
    deadline=time.time()+8
    while os.path.exists(cmd) and time.time()<deadline: time.sleep(.05)
    if os.path.exists(cmd): raise RuntimeError('command was not consumed')
    time.sleep(settle)
def query():
    log=os.path.join(da,'witness_proxy.log'); offset=os.path.getsize(log)
    send(da,['modelstat','tagstat'])
    deadline=time.time()+8; next_request=time.time()+1
    while time.time()<deadline:
        with open(log,errors='ignore') as f: f.seek(offset); text=f.read()
        body=re.search(r'modelstat: visible_px (\d+) drawn (\d).*scene_depth (-?\d+)',text)
        tag=re.search(r'tagstat: id (\d+).*onscreen (\d).*visible_px (\d+) query_ready (\d) scene_depth (-?\d+)',text)
        if body and tag:
            return dict(body=int(body[1]), drawn=int(body[2]), scene=int(body[3]), tag=int(tag[3]), onscreen=int(tag[2]), ready=int(tag[4]))
        # Diagnostic log writes can lose a line when another thread holds the
        # append handle. Repeat the query at this unchanged pose; never accept
        # missing counters as a mutant catch or a zero-pixel result.
        if time.time() >= next_request:
            send(da,['modelstat','tagstat']); next_request=time.time()+1
        time.sleep(.1)
    raise RuntimeError('missing GPU query results')
def sample(label):
    time.sleep(1); query(); time.sleep(.5); s=query(); samples[label]=s; return s
px,py,pz=struct.unpack('<3f',A.rd(A.entity(0x64)+0x24,12))
sx,sy=px-.15,py+3.34; z=3.727; yaw=math.atan2(py-sy,px-sx)
def tp(d,x,y,pitch=.26): send(d,['tp %.5f %.5f %.5f %.5f %.5f'%(x,y,z-1.69,yaw,pitch)],2)
send(da,['fakepose off']); send(db,['fakepose off'])
tp(da,sx,sy)
check('viewer camera is at the tutorial viewpoint',math.dist(A.camera()[0],(sx,sy,z))<.2,A.camera())
# Start separated: proves that a stale zero pixel count cannot pass the overlap check.
for label,distance in [('far',3),('overlap',0),('close',.5),('restored',3)]:
    x,y=sx+distance*math.cos(yaw),sy+distance*math.sin(yaw)
    tp(db,x,y)
    check(label+' pose reached',math.dist(B.camera()[0],(x,y,z))<.2,B.camera())
    s=sample(label)
    check(label+' scene and body query valid',s['drawn'] and s['scene'] in (1,2),s)
    if distance==3:
        check(label+' body and tag visible',s['body']>3000 and s['tag']>100 and s['ready'],s)
    elif distance==0:
        check('overlapping body invisible',s['body']<=10,s)
        check('overlapping tag invisible',s['tag']<=10,s)
    else:
        check('close body invisible',s['body']<=10,s)
        check('close tag invisible',s['onscreen'] and s['ready'] and s['tag']<=10,s)
print('NEAR_FADE_RESULT '+json.dumps(dict(checks=checks,samples=samples)))
sys.exit(0 if checks and all(checks.values()) else 1)
