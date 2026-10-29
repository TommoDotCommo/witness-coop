#!/usr/bin/env python3
"""Targeted native two-client precision check (run with tools/qa.sh probe ...).
A draws normally. Independently sample native trace memory with its write-time
frame; compare it with the exact surface ribbon submitted in B's render frame.
Includes held partial tip, final endpoint, moved door and a maze with corners.
Evidence and in-game captures go to the allocated run folder. No screen capture.
Screen space: each in-game capture of the receiver is also measured by the independent pixel oracle
(check_line_capture_pixels.py: native groove centre against the remote trace centre, within one pixel).
The receiver's field-of-view option is set to each value of LP_FOV (comma list, default 54,70: the game's
default and a changed option); LP_RES=WxH makes the receiver's window that size (default 1366x768, another aspect ratio);
LP_MUTANT=1 starts the receiver with old_projection=1 (the old hardcoded 60 degrees) and then REQUIRES the oracle to fail.
"""
import glob, json, math, os, re, struct, subprocess, threading, time
from witness import DIR1, DIR2, RUN, PYW, TOOLS, pyw, win, pid, real_saves
import qa_sweep, sweep, solver
import check_line_capture_pixels as pixels


def rotate(q,v):
    n=math.sqrt(sum(x*x for x in q)); x,y,z,w=(x/n for x in q)
    tx,ty,tz=2*(y*v[2]-z*v[1]),2*(z*v[0]-x*v[2]),2*(x*v[1]-y*v[0])
    return [v[0]+w*tx+y*tz-z*ty,v[1]+w*ty+z*tx-x*tz,v[2]+w*tz+x*ty-y*tx]


def measure(native, rows):
    anchor=native['anchor']; q=anchor[3:]; inv=[-q[0],-q[1],-q[2],q[3]]
    records=native['records']; points=[records[0]['a']]
    for i,r in enumerate(records):
        t=r['t'] if i==len(records)-1 and 0<=r['t']<.9999 else 1
        points.append([a+(b-a)*t for a,b in zip(r['a'],r['b'])])
    local=[rotate(inv,[a-b for a,b in zip(p,anchor[:3])]) for p in points]
    assert len(rows)==len(local)-1, (len(rows),len(local))
    corners=0
    for a,b,c in zip(local,local[1:],local[2:]):
        u=[y-x for x,y in zip(a,b)];v=[y-x for x,y in zip(b,c)]
        norm=math.sqrt(sum(x*x for x in u)*sum(x*x for x in v))
        if norm>1e-10 and abs(sum(x*y for x,y in zip(u,v))/norm)<.95:corners+=1
    assert [r['segment'] for r in rows]==list(range(len(local)-1))
    worst=0; center=0; surface=0
    for row in rows:
        assert row['fine'], 'ordinary panel fell back to millimetres'
        pose=row['pose']; i=row['segment']
        for end in (0,1):
            expected=[a+b for a,b in zip(pose[:3],rotate(pose[3:],local[i+end]))]
            actual=row['ends'][end]
            midpoint=[(a+b)/2 for a,b in zip(row['quad'][end*2],row['quad'][end*2+1])]
            worst=max(worst,math.dist(actual,expected)*1000)
            center=max(center,math.dist(midpoint,expected)*1000)
            normal=rotate(pose[3:],[1,0,0])
            for vertex in row['quad'][end*2:end*2+2]:
                surface=max(surface,abs(sum((a-b)*n for a,b,n in zip(vertex,actual,normal)))*1000)
    assert worst<.1 and center<.1 and surface<.05, (worst,center,surface)
    return dict(segments=len(rows),corners=corners,endpoint_max_mm=worst,ribbon_center_max_mm=center,surface_max_mm=surface)


MUTANT=os.environ.get('LP_MUTANT')=='1'
FOVS=[float(v) for v in os.environ.get('LP_FOV','54,70').split(',')]


def set_fov(deg):
    got=json.loads(pyw('wfovwrite.py',pid('B'),'%g'%deg,'keep').strip().splitlines()[-1])
    assert abs(got['vfov_matrix']-deg)<.05 and abs(got['vfov']-deg)<.01, ('the game did not take the field of view',deg,got)
    return got


def aspect():
    m=re.search(r'backbuffer (\d+)x(\d+)',open(os.path.join(DIR2,'witness_proxy.log'),errors='ignore').read());return '%sx%s'%m.groups() if m else None


def write_resolution(w,h):
    saves=os.path.join(DIR2,'appdata','The Witness'); os.makedirs(saves,exist_ok=True)
    open(os.path.join(saves,'Launcher.variables'),'w').write(':/launcher\nrender_profile 2\nfullscreen false\nvsync true\nhighdpi false\nresolution 4\nmultisampling 3\ncustom_width %d\ncustom_height %d\nshading_quality 0\nreflection_quality 1\ntexture_quality 2\ntexture_detail 1\ngeometry_detail 0\n'%(w,h))


def main():
    os.environ['WITNESS_KEEP_VIDEO']='1'      # the scenario chooses the window size itself
    os.makedirs(RUN,exist_ok=True); before=real_saves(); results=[]; oracle=[]
    manifest={q['id']:q for q in sweep.load(sweep.MANIFEST,[])}
    write_resolution(*map(int,os.environ.get('LP_RES','1366x768').lower().split('x')))      # always written: the instance folder remembers the last size
    qa_sweep.restart('name=Alpha','name=Beta'+('\nold_projection=1' if MUTANT else ''))
    print(json.dumps(dict(fov=FOVS[0],game=set_fov(FOVS[0]))),flush=True)
    for eid in (0x64,0x293):
        q=manifest[eid]; path=solver.solve(q)
        pyw('wcmd.py',win(DIR2),'menusel %x'%eid);pyw('wcmd.py',win(DIR2),'menudo teleport');time.sleep(3)
        native_path=os.path.join(RUN,'native_%x.json'%eid); stop=native_path+'.stop'
        for p in [native_path,stop]+glob.glob(native_path+'-*.json'):
            if os.path.exists(p):os.remove(p)
        watcher=subprocess.Popen([PYW,'-I',win(os.path.join(TOOLS,'win','wlinewatch.py')),pid('A'),'%x'%eid,win(native_path),win(stop)])
        try:
            held=os.path.join(DIR1,'wdraw_held.txt')
            if os.path.exists(held):os.remove(held)
            os.environ['WDRAW_HOLD_FRACTION']='0.5';os.environ['WDRAW_HOLD_SECS']='15'
            os.environ['WSLENV']=os.environ.get('WSLENV','')+':WDRAW_HOLD_FRACTION:WDRAW_HOLD_SECS'
            result={};th=threading.Thread(target=lambda:result.update(sweep.draw(q,path,'A')));th.start()
            deadline=time.monotonic()+220
            while th.is_alive() and not os.path.exists(held) and time.monotonic()<deadline:time.sleep(.1)
            assert os.path.exists(held),'bot never reached held tip'
            time.sleep(1)
            def latest_native():
                assert watcher.poll() is None,'native sampler stopped'
                files=sorted(glob.glob(native_path+'-*.json'))
                assert files,'no native process sample'
                with open(files[-1]) as f:return json.load(f)
            def shoot(name):      # the receiver's own picture, as PNG too; returns the BMP path
                bmp=os.path.join(DIR2,'shots',name+'.bmp')
                if os.path.exists(bmp):os.remove(bmp)
                pyw('wcmd.py',win(DIR2),'shot '+name)
                deadline=time.monotonic()+8
                complete=False
                while time.monotonic()<deadline:
                    try:
                        with open(bmp,'rb') as f:head=f.read(6)
                        complete=len(head)==6 and head[:2]==b'BM' and os.path.getsize(bmp)==struct.unpack_from('<I',head,2)[0]
                    except OSError:pass
                    if complete:break
                    time.sleep(.05)
                assert complete,'in-game screenshot did not finish'
                subprocess.run(['python3',os.path.join(TOOLS,'bmp2png.py'),bmp,os.path.join(RUN,name+'.png'),'1'],check=True)
                return bmp
            def judge(stage,fov,bmp):      # screen space: native groove against the remote trace in the receiver's own picture
                try:got=pixels.measure(bmp)
                except AssertionError as e:got=dict(error=str(e))
                got.update(panel=eid,stage=stage,fov=fov,aspect=aspect(),mutant=MUTANT);oracle.append(got);print('oracle',json.dumps(got),flush=True);return got
            def capture(stage):
                native=latest_native();target=os.path.join(DIR2,'witness_line_geometry.jsonl')
                if os.path.exists(target):os.remove(target)
                pyw('wcmd.py',win(DIR2),'linedump')
                deadline=time.monotonic()+8
                while time.monotonic()<deadline:
                    try:rows=[json.loads(l) for l in open(target)];rows=[r for r in rows if r['panel']==eid]
                    except (OSError,ValueError):rows=[]
                    if rows:break
                    time.sleep(.05)
                evidence=dict(native=native,geometry=rows)
                json.dump(evidence,open(os.path.join(RUN,'precision_%x_%s.raw.json'%(eid,stage)),'w'),indent=2)
                got=measure(native,rows);got.update(panel=eid,stage=stage)
                json.dump(dict(native=native,geometry=rows,result=got),open(os.path.join(RUN,'precision_%x_%s.json'%(eid,stage)),'w'),indent=2)
                name='lp%x_%s%d'%(eid,stage[0],FOVS[0]) # the command parser accepts at most 15 characters
                assert len(name)<=15
                judge(stage,FOVS[0],shoot(name))
                results.append(got);print(json.dumps(got),flush=True)
                return native,rows
            partial,_=capture('partial' if eid==0x64 else 'held')
            for fov in FOVS[1:]:      # still held: the same unfinished line seen with the other field of view
                state=set_fov(fov);print(json.dumps(dict(fov=fov,game=state)),flush=True);time.sleep(1)
                judge('partial' if eid==0x64 else 'held',fov,shoot('lp%x_p%d'%(eid,fov)))
            if len(FOVS)>1:set_fov(FOVS[0])
            if eid==0x64:assert 0<partial['records'][-1]['t']<.99,'no partial native edge'
            th.join(240);assert not th.is_alive() and result.get('solved'),result
            capture('final')
            for fov in FOVS[1:]:      # the finished line stays on the panel: only the option changes
                state=set_fov(fov);print(json.dumps(dict(fov=fov,game=state)),flush=True);time.sleep(1)
                judge('final',fov,shoot('lp%x_f%d'%(eid,fov)))
            if len(FOVS)>1:set_fov(FOVS[0])
            if eid==0x64:
                time.sleep(9);_,moved=capture('moved')
                assert math.dist(moved[0]['pose'][:3],partial['pose'][:3])>.3,'door did not move'
            else:assert results[-1]['corners']>=1,'maze has no corners'
        finally:
            open(stop,'w').write('stop');watcher.wait(timeout=10)
            os.environ.pop('WDRAW_HOLD_FRACTION',None);os.environ.pop('WDRAW_HOLD_SECS',None)
    assert before==real_saves(),'owner saves changed'
    json.dump(oracle,open(os.path.join(RUN,'line_pixel_oracle.json'),'w'),indent=2)
    measured=[o for o in oracle if 'offset_px' in o]
    for eid in (0x64,0x293):      # every panel at every field of view needs at least one measurable picture (a door that has opened takes its panel away)
        for fov in FOVS:assert any(o['panel']==eid and o['fov']==fov for o in measured),('no measurable picture',hex(eid),fov,oracle)
    if MUTANT:
        for fov in FOVS:assert any(o['panel']==0x293 and o['fov']==fov and abs(o['offset_px'])>1 for o in measured),('the old projection was not rejected at',fov,measured)      # the tutorial panel sits near the screen centre, where the old error is under a pixel
        print('PASS (mutant) the pixel oracle rejects the old projection',flush=True);return
    assert all(abs(o['offset_px'])<=1 for o in measured),('trace misses the groove by more than one pixel',measured)
    json.dump(results,open(os.path.join(RUN,'line_precision_results.json'),'w'),indent=2)
    print('PASS native two-client line precision and isolated saves',flush=True)

if __name__=='__main__':main()
