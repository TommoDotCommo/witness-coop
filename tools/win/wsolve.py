"""Test command: make a panel solved on that game as a player's solve would (fields plus the game's target dispatcher).
Usage: Windows Python wsolve.py <game folder> <panel id hex>   (used for panels the bot cannot reach, e.g. laser panels)"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wcontrol import Control
Control(sys.argv[1]).send(['solve %x' % int(sys.argv[2], 16)], 3)
print('solve sent', sys.argv[2])
