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

def real_saves():
    """Hashes of the owner's real save folder (%APPDATA%\\The Witness), read through Windows Python. Instances must never touch it."""
    import json, hashlib
    code = ("import os,hashlib,json\nd=os.path.join(os.environ['APPDATA'],'The Witness');m={}\n"
            "for r,_,fs in os.walk(d):\n for f in fs:\n  p=os.path.join(r,f);m[os.path.relpath(p,d)]=hashlib.sha256(open(p,'rb').read()).hexdigest()\nprint(json.dumps(m))")
    return json.loads(subprocess.run([PYW, '-I', '-c', code], capture_output=True, text=True).stdout)
