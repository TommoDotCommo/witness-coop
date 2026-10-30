"""Shut an open door and open it again a moment later, several times, with the game's own door call (the proxy's 'door <id> <amount>'
command), as the island sometimes does by itself (0x14724, a hedge maze shortcut: shut and open again within about 100 ms). The other game
must end with the door open. Usage: Windows Python wflicker.py <game folder> <door id hex> [gaps in ms, comma separated]"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wcontrol import Control
c = Control(sys.argv[1]); door = sys.argv[2]
gaps = [int(g) for g in (sys.argv[3] if len(sys.argv) > 3 else '60,80,100,120,60,80,100,120').split(',')]
for g in gaps:      # the gaps vary so that, whatever the phase of the 150 ms send round, the shut and the open often travel together
    c.send(['door %s 0' % door]); time.sleep(g / 1000); c.send(['door %s 1' % door]); time.sleep(2.5)
print('flickered door %s %d times' % (door, len(gaps)))
