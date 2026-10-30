#!/usr/bin/env python3
"""T0 production pose and presence contracts; each broken protection must fail."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='witness-gestures-') as tmp:
    tmp=Path(tmp)
    # Model implementation is immutable across header-only controller mutants.
    obj=tmp/'model.o'
    subprocess.run(['g++','-std=c++17','-O2','-c',str(root/'proxy/model/model.cpp'),'-o',str(obj)],check=True)
    for variant in ('clean','BLEND','MOVE','CHANNEL','HYSTERESIS','CONFLICT','ENVELOPE','LIMIT','LOOP'):
        exe=tmp/variant
        cmd=['g++','-std=c++17','-O2',str(root/'proxy/model/tests/gestures.cpp'),str(obj),'-o',str(exe)]
        if variant!='clean': cmd.insert(1,'-DWE_MUT_'+variant)
        subprocess.run(cmd,check=True)
        result=subprocess.run([str(exe),str(root/'assets/models/adventurer.glb')],capture_output=True,text=True)
        assert (result.returncode==0)==(variant=='clean'),variant+': '+result.stdout+result.stderr
        print(variant+': '+('passed' if variant=='clean' else 'mutant rejected'),flush=True)
        if variant=='clean': print(result.stdout,end='')
    for variant in ('clean','RATE','VALIDATION'):
        exe=tmp/('sync-'+variant)
        cmd=['g++','-std=c++17','-O2',str(root/'proxy/core/tests/emotes.cpp'),str(root/'proxy/core/sync_core.cpp'),'-o',str(exe)]
        if variant!='clean': cmd.insert(1,'-DWE_MUT_'+variant)
        subprocess.run(cmd,check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        assert (result.returncode==0)==(variant=='clean'),variant+': '+result.stdout+result.stderr
        print('sync '+variant+': '+('passed' if variant=='clean' else 'mutant rejected'),flush=True)

    source=(root/'proxy/model/model.cpp').read_text()
    for name,old,new in (
        ('no-layer','if (gesture) {','if (false && gesture) {'),
        ('no-seat-drop','T[i*3+1]-=drop','T[i*3+1]-=0*drop'),
        ('wrong-axis','float fwd=m.gestureForward[i]','float fwd=-m.gestureForward[i]'),
        ('gait-hysteresis','(last_.kind == 2 ? .9f : 1.f)','1.f')):
        assert old in source
        # The implementation's relative includes resolve inside a temporary mirror.
        model=tmp/'proxy/model'; model.mkdir(parents=True,exist_ok=True)
        import shutil
        for folder in ('third_party','core'):
            target=tmp/'proxy'/folder
            if not target.exists(): shutil.copytree(root/'proxy'/folder,target,ignore=shutil.ignore_patterns('build'))
        for header in ('model.h','gestures.h','life.h'): shutil.copyfile(root/'proxy/model'/header,model/header)
        (model/'model.cpp').write_text(source.replace(old,new))
        exe=tmp/name
        subprocess.run(['g++','-std=c++17','-O2',str(root/'proxy/model/tests/gestures.cpp'),str(model/'model.cpp'),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe),str(root/'assets/models/adventurer.glb')],capture_output=True,text=True)
        assert result.returncode!=0,name+': mutant survived'
        print(name+': mutant rejected',flush=True)

    core_source=(root/'proxy/core/sync_core.cpp').read_text()
    for name,old,new in (
        ('speech-expiry','pose.proximitySpeaking = presFresh &&','pose.proximitySpeaking ='),
        ('loop-expiry','(!presFresh && ValidEmote','(false && ValidEmote'),
        ('speech-wire','(pres_.proximitySpeaking ? 4 : 0)','0'),
        ('local-validation','if (!ValidEmote(clean.emote)) clean.emote = 0;','')):
        assert old in core_source
        core=tmp/'proxy/core'
        (core/'sync_core.cpp').write_text(core_source.replace(old,new))
        exe=tmp/name
        subprocess.run(['g++','-std=c++17','-O2',str(root/'proxy/core/tests/emotes.cpp'),str(core/'sync_core.cpp'),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        assert result.returncode!=0,name+': mutant survived'
        print(name+': mutant rejected',flush=True)
