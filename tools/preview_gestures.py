#!/usr/bin/env python3
"""Render 24 emotes and proximity speech through the real CPU skinning path.
Usage: python3 tools/preview_gestures.py <output.png>
Rows contain IDs 1..25, left to right; 25 is speech. Flat material preview.
"""
from pathlib import Path
import struct, subprocess, tempfile, sys, zlib
root=Path(__file__).resolve().parent.parent
out=Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='witness-preview-') as tmp:
    exe=Path(tmp)/'preview'; ppm=Path(tmp)/'preview.ppm'
    subprocess.run(['g++','-std=c++17','-O2',str(root/'tools/gesture_preview.cpp'),str(root/'proxy/model/model.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(root/'assets/models/adventurer.glb'),str(ppm)],check=True)
    magic,size,maximum,data=ppm.read_bytes().split(b'\n',3); w,h=map(int,size.split())
    assert magic==b'P6' and maximum==b'255' and len(data)==w*h*3
    def chunk(tag,data): return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    scan=b''.join(b'\0'+data[y*w*3:(y+1)*w*3] for y in range(h))
    out.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
print(out)
