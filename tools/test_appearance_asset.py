#!/usr/bin/env python3
"""Release-gate mutants, using synthetic labelled triangles and the real atlas."""
import copy
import contextlib
import io
from pathlib import Path
import struct
import tempfile
from glbtools import read_glb, write_glb, add_acc
from check_appearance_asset import check
ROOT=Path(__file__).resolve().parent.parent
src,raw=read_glb(ROOT/'assets/models/adventurer.glb')
iv=src['bufferViews'][src['images'][0]['bufferView']]
image=raw[iv.get('byteOffset',0):iv.get('byteOffset',0)+iv['byteLength']]
j={'asset':{'version':'2.0'},'buffers':[{}],'bufferViews':[],'accessors':[]}
b=bytearray(); pos=add_acc(j,b,[(0,0,0),(1,0,0),(0,1,0)]*9,'VEC3',True)
label=add_acc(j,b,[(g,) for g in range(9) for _ in range(3)],'SCALAR')
off=len(b); b.extend(struct.pack('<27I',*range(27))); j['bufferViews'].append({'buffer':0,'byteOffset':off,'byteLength':108})
j['accessors'].append({'bufferView':len(j['bufferViews'])-1,'componentType':5125,'count':27,'type':'SCALAR'}); indices=len(j['accessors'])-1
j['bufferViews'].append({'buffer':0,'byteOffset':len(b),'byteLength':len(image)}); b.extend(image)
j.update(images=[{'bufferView':len(j['bufferViews'])-1,'mimeType':'image/jpeg'}],meshes=[{'primitives':[{'attributes':{'POSITION':pos,'_APPEARANCE':label},'indices':indices}]}])
labeloffset=j['bufferViews'][j['accessors'][label]['bufferView']]['byteOffset']
def scalar(j,b,v): struct.pack_into('<f',b,labeloffset,v)
def missing(j,b): del j['meshes'][0]['primitives'][0]['attributes']['_APPEARANCE']
def missinggroup(j,b):
    for n in range(3): struct.pack_into('<f',b,labeloffset+4*n,1)
def count(j,b): j['accessors'][label]['count']=26
def vectype(j,b): j['accessors'][label]['type']='VEC2'
def images(j,b): j['images'].append(j['images'][0])
def huge(j,b): b.extend(bytes(6*1024*1024))
def triangles(j,b):
    off=len(b); rows=list(range(27))*11112; b.extend(struct.pack('<%dI'%len(rows),*rows))
    j['bufferViews'][j['accessors'][indices]['bufferView']].update(byteOffset=off,byteLength=len(rows)*4)
    j['accessors'][indices]['count']=len(rows)
def atlas(j,b):
    # Replace only the test fixture image with a PNG header declaring an oversized image.
    off=len(b); png=b'\x89PNG\r\n\x1a\n'+bytes(8)+struct.pack('>II',4096,2048); b.extend(png)
    j['bufferViews'][j['images'][0]['bufferView']].update(byteOffset=off,byteLength=len(png))
variants={'clean':None,'missing':missing,'missing-region':missinggroup,'count':count,'type':vectype,
          'nan':lambda j,b:scalar(j,b,float('nan')),'fraction':lambda j,b:scalar(j,b,.5),
          'range':lambda j,b:scalar(j,b,9),'triangle-boundary':lambda j,b:scalar(j,b,1),
          'images':images,'glb-budget':huge,'triangle-budget':triangles,'texture-budget':atlas}
with tempfile.TemporaryDirectory(prefix='appearance-asset-') as tmp:
    for name,mutate in variants.items():
        data=copy.deepcopy(j); binary=bytearray(b)
        if mutate: mutate(data,binary)
        path=Path(tmp)/'test.glb'; write_glb(path,data,binary)
        try:
            with contextlib.redirect_stdout(io.StringIO()): check(path)
        except AssertionError:
            assert mutate is not None, name
        else: assert mutate is None, name+' mutant survived'
        print(name+(': passed' if mutate is None else ': mutant rejected'))
