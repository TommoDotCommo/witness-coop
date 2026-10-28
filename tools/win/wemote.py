"""T2 emote and proximity gesture contract. Assertions use live JSON bridges and
camera memory, never log messages or screenshots. Config: A has the test tone in
PTT mode; B sees A. Run through tools/qa.sh emote for clean and broken variants.
"""
import json, math, os, sys, time
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
A=attach(pa)

def send(d, lines, settle=0):
    path=os.path.join(d,'witness_cmd.txt'); deadline=time.monotonic()+10
    while os.path.exists(path):
        assert time.monotonic()<deadline, 'command bridge stalled'
        time.sleep(.02)
    with open(path+'.tmp','w') as f: f.write('\n'.join(lines)+'\n')
    os.replace(path+'.tmp',path)
    while os.path.exists(path):
        assert time.monotonic()<deadline, 'command not consumed'
        time.sleep(.02)
    time.sleep(settle)

def snapshot(d):
    # Remove old files so a failed bridge cannot pass on stale evidence.
    for name in ('witness_net.json','witness_models.json'):
        try: os.remove(os.path.join(d,name))
        except FileNotFoundError: pass
    send(d,['netstate','modelstate'])
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        try:
            with open(os.path.join(d,'witness_net.json')) as f: net=json.load(f)
            with open(os.path.join(d,'witness_models.json')) as f: model=json.load(f)
            return net,model
        except (OSError,ValueError): time.sleep(.03)
    raise AssertionError('state bridges timed out')

def expect(label, test, directory=db, timeout=5):
    deadline=time.monotonic()+timeout; last=None
    while time.monotonic()<deadline:
        net,model=snapshot(directory)
        remote=next((p for p in net['remotes'] if p['id']==1),{})
        draw=next((p for p in model['players'] if p['id']==1),{})
        last=(net,remote,draw)
        if test(net,remote,draw): print('PASS '+label,flush=True); return last
        time.sleep(.05)
    raise AssertionError('%s: %r'%(label,last))

send(da,['menusel c335','menudo teleport'],4)
(cx,cy,cz),(heading,pitch)=A.camera()
send(db,['tp %.5f %.5f %.5f %.5f 0'%(cx-5*math.cos(heading),cy-5*math.sin(heading),cz-1.69,heading)],3)
# Every stable catalog ID must reach the sync core and the actual renderer.
for emote in range(1,25):
    send(da,['emote %d'%emote],.2)
    expect('catalog %d received and animated'%emote,
           lambda n,r,d: r.get('emote')==emote and d.get('emote')==emote and d.get('gesture',0)>.3)
    send(da,['emote 0'],.5)
expect('cancel relaxes the rig',lambda n,r,d:r.get('emote')==0 and d.get('gesture',1)<.02)
send(da,['emote 3'])
expect('one-shot starts',lambda n,r,d:d.get('emote')==3)
expect('one-shot expires and blends out',lambda n,r,d:d.get('emote')==0 and d.get('gesture',1)<.02,timeout=6)
# Loop survives its authored period; moving cancels and stopping never resumes.
send(da,['emote 22'],5)
expect('sway loops',lambda n,r,d:d.get('emote')==22 and d.get('gesture',0)>.9)
send(da,['key W down'],.8)
expect('movement cancels full-body gesture',lambda n,r,d:d.get('emote')==0)
send(da,['key W up'],1)
expect('stopping does not restart cancelled gesture',lambda n,r,d:d.get('emote')==0)
send(da,['emote 0'],.5)
# Each page uses the real input path. Right selects slot three (3, 11, 19).
for page in range(3):
    before=A.camera()[1][0]
    send(da,['key G down'],.1)
    if page: send(da,['key TAB down','key TAB up'],.1)
    send(da,['mouse 60 0','key G up'],.3)
    expect('wheel page %d selection'%page,lambda n,r,d:d.get('emote')==3+8*page)
    assert abs(A.camera()[1][0]-before)<.01, 'wheel leaked mouse-look'
    send(da,['emote 0'],.5)
send(da,['key F1 down','key F1 up'])
expect('F1 guide toggles',lambda n,r,d:n.get('guide') is True,directory=da)
send(da,['key F1 down','key F1 up','key T down','key T up','key ESC down','key ESC up'],.3)
expect('chat Escape swallowed without pausing',lambda n,r,d:n.get('paused') is False,directory=da)
# Silence in PTT cannot trigger; activity in proximity must lift the actual wrist.
expect('silent hand stays down',lambda n,r,d:not r.get('proximity_speaking') and d.get('talk',1)<.02)
send(da,['voicechannel proximity','key N down'])
expect('proximity speech raises hand',lambda n,r,d:r.get('proximity_speaking') and d.get('talk',0)>.9 and d.get('hand',[0,0])[1]>1.4)
send(da,['emote 20'])
expect('laugh has priority over speech',lambda n,r,d:d.get('emote')==20 and d.get('talk',1)<.02)
send(da,['emote 0'],.5)
expect('speech resumes after conflict',lambda n,r,d:d.get('talk',0)>.9)
for channel in ('radio','global','direct:2'):
    send(da,['voicechannel '+channel],.3)
    expect(channel+' cannot raise hand',lambda n,r,d:not r.get('proximity_speaking') and d.get('talk',1)<.02)
send(da,['key N up','voicechannel proximity'],.5)
expect('silence lowers hand',lambda n,r,d:d.get('talk',1)<.02)
print('ALL OK',flush=True)
