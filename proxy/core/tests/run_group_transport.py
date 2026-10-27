#!/usr/bin/env python3
"""Run eight tiny Windows clients against the production UDP layer; no game or resource provider slots."""
import os
from pathlib import Path
import subprocess
import sys
here=Path(__file__).resolve().parent
exe=here.parent/'build/group-transport.exe'
def win(p):return subprocess.check_output(['wslpath','-w',str(p)],text=True).strip()
result=subprocess.run([os.environ.get('PYW','python.exe'),'-I',win(here/'run_group_transport_win.py'),win(exe)])
sys.exit(result.returncode)
