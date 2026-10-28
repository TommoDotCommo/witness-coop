#!/usr/bin/env python3
"""Self-test the screen-space alignment oracle with clean and broken fixtures."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parent.parent
(root/'local').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=root/'local',prefix='line-pixels-') as directory:
    for name,red_y,expected in [('aligned',327,0),('compressed',333,1),('missing',None,1)]:
        w,h=1366,768;data=bytearray(bytes([25,178,235,255])*(w*h))
        def rect(x0,x1,y0,y1,color):
            for y in range(y0,y1):
                for x in range(x0,x1):data[(y*w+x)*4:(y*w+x+1)*4]=bytes(color)
        rect(625,739,324,332,[23,107,142,255])
        if red_y is not None:rect(633,733,red_y,red_y+3,[89,89,255,255])
        bmp=Path(directory)/(name+'.bmp')
        header=b'BM'+struct.pack('<IHHI',54+len(data),0,0,54)+struct.pack('<IiiHHIIiiII',40,w,-h,1,32,0,len(data),0,0,0,0)
        bmp.write_bytes(header+data)
        r=subprocess.run([sys.executable,str(root/'tools/check_line_capture_pixels.py'),str(bmp)],capture_output=True,text=True)
        assert r.returncode==expected,(name,r.stdout,r.stderr)
        print('PASS pixel oracle '+name)
