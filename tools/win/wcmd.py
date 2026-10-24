"""Send one command line to a game's proxy and wait until it was taken. Usage: Windows Python wcmd.py <game folder> <command words...>"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wcontrol import Control
Control(sys.argv[1]).send([' '.join(sys.argv[2:])], 3)
print('sent', ' '.join(sys.argv[2:]))
