#!/usr/bin/env python3
"""T0 production pose and presence contracts; each broken protection must fail.
The emotes' animation quality (feet, contacts, clipping, balance, holds) is the gate in `make -C proxy/model emotes`.
Every variant (clean and mutants) is built and run side by side, each in a folder of its own (one after another they took about 90 s)."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import os, shutil, subprocess, tempfile
root=Path(__file__).resolve().parent.parent
glb=str(root/'assets/models/adventurer.glb')
CXX=['g++','-std=c++17','-O2']

def mirror(base,core_source=None,model_source=None):
    """A copy of proxy/core, proxy/third_party and proxy/model's headers under `base`, so a mutated source's relative includes resolve there."""
    for folder in ('third_party','core'):
        shutil.copytree(root/'proxy'/folder,base/'proxy'/folder,ignore=shutil.ignore_patterns('build'))
    model=base/'proxy/model'; model.mkdir(parents=True)
    for header in ('model.h','gestures.h','life.h','emote_clip.h','emote_data.h'): shutil.copyfile(root/'proxy/model'/header,model/header)
    if model_source is not None: (model/'model.cpp').write_text(model_source)
    if core_source is not None: (base/'proxy/core/sync_core.cpp').write_text(core_source)
    return base/'proxy'

with tempfile.TemporaryDirectory(prefix='witness-gestures-') as tmp:
    tmp=Path(tmp)
    # Model implementation is immutable across header-only controller mutants: built once.
    obj=tmp/'model.o'
    model_build=subprocess.Popen(CXX+['-c',str(root/'proxy/model/model.cpp'),'-o',str(obj)])
    jobs=[]      # (label, compile command, run command, must pass)
    for variant in ('clean','BLEND','MOVE','CHANNEL','HYSTERESIS','CONFLICT','ENVELOPE','LIMIT','LOOP','NO_SPRING','NO_DRAG'):
        exe=tmp/variant
        jobs.append((variant,CXX+([] if variant=='clean' else ['-DWE_MUT_'+variant])+[str(root/'proxy/model/tests/gestures.cpp'),str(obj),'-o',str(exe)],[str(exe),glb],variant=='clean'))
    for variant in ('clean','RATE','VALIDATION'):
        exe=tmp/('sync-'+variant)
        jobs.append(('sync '+variant,CXX+([] if variant=='clean' else ['-DWE_MUT_'+variant])+[str(root/'proxy/core/tests/emotes.cpp'),str(root/'proxy/core/sync_core.cpp'),'-o',str(exe)],[str(exe)],variant=='clean'))
    source=(root/'proxy/model/model.cpp').read_text()
    for name,old,new in (
        ('no-layer','if (gesture) {','if (false && gesture) {'),
        ('wrong-axis','float fwd=m.gestureForward[i]','float fwd=-m.gestureForward[i]'),
        ('gait-hysteresis','(last_.kind == 2 ? .9f : 1.f)','1.f')):
        assert old in source,name
        proxy=mirror(tmp/('m-'+name),model_source=source.replace(old,new)); exe=tmp/name
        jobs.append((name,CXX+[str(root/'proxy/model/tests/gestures.cpp'),str(proxy/'model/model.cpp'),'-o',str(exe)],[str(exe),glb],False))
    core_source=(root/'proxy/core/sync_core.cpp').read_text()
    for name,old,new in (
        ('speech-expiry','pose.proximitySpeaking = presFresh &&','pose.proximitySpeaking ='),
        ('loop-expiry','(!presFresh && ValidEmote','(false && ValidEmote'),
        ('speech-wire','(pres_.proximitySpeaking ? 4 : 0)','0'),
        ('local-validation','if (!ValidEmote(clean.emote)) clean.emote = 0;','')):
        assert old in core_source,name
        proxy=mirror(tmp/('c-'+name),core_source=core_source.replace(old,new)); exe=tmp/name
        jobs.append((name,CXX+[str(root/'proxy/core/tests/emotes.cpp'),str(proxy/'core/sync_core.cpp'),'-o',str(exe)],[str(exe)],False))
    assert model_build.wait()==0,'model.o did not build'

    def build_and_run(job):
        label,build,run,must_pass=job
        b=subprocess.run(build,capture_output=True,text=True)
        if b.returncode: return 'build failed: '+b.stderr[-2000:]
        return subprocess.run(run,capture_output=True,text=True)
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results=list(pool.map(build_and_run,jobs))
    bad=[]
    for (label,build,run,must_pass),result in zip(jobs,results):
        if isinstance(result,str): bad.append(label+': '+result); continue
        if (result.returncode==0)!=must_pass: bad.append(label+(': mutant survived' if not must_pass else ': '+result.stdout+result.stderr)); continue
        print(label+': '+('passed' if must_pass else 'mutant rejected'),flush=True)
        if label=='clean': print(result.stdout,end='')
    assert not bad,'\n'.join(bad)
