"""Shared helpers for the WSL-side scripts: paths from the repo location and calls to the Windows-side probes."""
import os, re, subprocess, math

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
TOOLS = os.path.join(ROOT, 'tools')
DIR1 = os.environ.get('WITNESS_DIR1', os.path.join(ROOT, 'local', 'witness-1'))
DIR2 = os.environ.get('WITNESS_DIR2', os.path.join(ROOT, 'local', 'witness-2'))
PYW = os.environ.get('PYW', '/mnt/c/Python311/python.exe')
RUN = os.environ.get('WITNESS_RUN', os.path.join(ROOT, 'local', 'run'))

def win(path): return subprocess.check_output(['wslpath', '-w', path]).decode().strip()
def pid(which): return open(os.path.join(RUN, 'pid%s.txt' % which)).read().strip()

def pyw(script, *args, timeout=90):
    """Run a tools/win script with Windows Python; returns stdout."""
    return subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', script)), *args], capture_output=True, text=True, timeout=timeout).stdout

def camera_and_entity(p, eid='0x64'):
    out = pyw('wread.py', p, eid)
    cam = re.search(r'camera pos \(([-\d.e]+), ([-\d.e]+), ([-\d.e]+)\)', out)
    ang = re.search(r'camera ang \(([-\d.e]+), ([-\d.e]+)\)', out)
    ent = re.search(r'entity %s pos \(([-\d.e]+), ([-\d.e]+), ([-\d.e]+)\)' % eid, out)
    return [float(x) for x in cam.groups()], [float(x) for x in ang.groups()], [float(x) for x in ent.groups()]

def dist2d(a, b): return math.dist(a[:2], b[:2])
