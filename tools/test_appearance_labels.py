#!/usr/bin/env python3
"""The native loader must preserve region IDs and reject unsafe attributes.
Synthetic mesh fixtures exercise the same decoder as the actual renderer.
"""
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from glbtools import add_acc, write_glb
ROOT = Path(__file__).resolve().parent.parent
CPP = r'''
#include "model.h"
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdlib>
int main(int argc,char**argv) {
    std::ifstream f(argv[1],std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
    wm::Model m; std::string e; if (!wm::LoadGlb(b.data(),b.size(),&m,&e)) return 1;
    if (m.verts.size()!=3) return 2;
    float want = std::atof(argv[2]);
    for (auto& v : m.verts) if (!std::isfinite(v.appearanceGroup) || v.appearanceGroup != want) return 3;
    return 0;
}'''
with tempfile.TemporaryDirectory(prefix='appearance-labels-') as tmp:
    root = Path(tmp); (root/'test.cpp').write_text(CPP)
    for mutant in (False,True):
        exe = root/('mutant' if mutant else 'clean')
        cmd=['g++','-std=c++17','-O2','-I',str(ROOT/'proxy/model'),str(ROOT/'proxy/model/model.cpp'),str(root/'test.cpp'),'-o',str(exe)]
        if mutant: cmd.insert(1,'-DWM_MUT_APPEARANCE_GROUP')
        subprocess.run(cmd,check=True)
        killed = False
        for group in (*range(9),-1,9,.5,float('nan'),float('inf')):
            j={'asset':{'version':'2.0'},'buffers':[{}],'bufferViews':[],'accessors':[]}
            b=bytearray(); pos=add_acc(j,b,[(0,0,0),(1,0,0),(0,1,0)],'VEC3',True)
            attr=add_acc(j,b,[(group,)]*3,'SCALAR')
            j.update(meshes=[{'primitives':[{'attributes':{'POSITION':pos,'_APPEARANCE':attr}}]}],nodes=[{'mesh':0}],scenes=[{'nodes':[0]}],scene=0)
            path=root/'test.glb'; write_glb(path,j,b)
            valid = group in range(9)
            want = group if valid else 0
            result=subprocess.run([str(exe),str(path),str(want)])
            if result.returncode:
                if not mutant: raise AssertionError(('label failed',group,result.returncode))
                killed=True
        assert not mutant or killed, 'unsafe label mutant survived'
        print('appearance labels: '+('unsafe-label mutant rejected' if mutant else 'all region IDs and invalid values passed'))
