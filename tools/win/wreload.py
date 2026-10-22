# Reload a save inside the running game through its own menu (keyboard only, no focus or desktop input):
#   wreload.py <game folder> [backup folder]
# Esc opens the pause menu, the third entry is "Load a Game", the list is newest first with the newest selected. The game keeps
# one save file per session and rewrites it in place (opening the pause menu saves too), so the save in the backup folder, if given,
# is copied over it after Esc and before the list is read.
import os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wcontrol import Control
gdir = sys.argv[1]; backup = sys.argv[2] if len(sys.argv) > 2 else None
saves = [f for f in os.listdir(os.path.join(gdir, 'appdata', 'The Witness')) if f.endswith('.witness_campaign')]
if not saves: sys.exit('no saves to load')
c = Control(gdir)
def key(k): c.send(['key %s down' % k], 0.15); c.send(['key %s up' % k], 0.4)
key('ESC'); time.sleep(1.5)
if backup:
    for f in os.listdir(backup): shutil.copy(os.path.join(backup, f), os.path.join(gdir, 'appdata', 'The Witness', f))
key('S'); key('S'); key('ENTER'); time.sleep(1.5)          # Load a Game
key('ENTER')
print('reload requested (%d save(s) listed)' % len(saves), flush=True)
