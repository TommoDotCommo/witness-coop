"""Drive the in-game dev menu through the proxy's command file (no desktop input).
Usage: Windows Python wmenu.py <game folder> <step> [<step> ...] with steps: open | map | nearby | close | at:X,Y | click | rclick | key:NAME | shot:NAME | wait:SECONDS"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wcontrol import Control
c = Control(sys.argv[1])
for st in sys.argv[2:]:
    if st in ('open', 'map'): c.send(['menu map'], 1.0)
    elif st == 'nearby': c.send(['menu nearby'], 1.0)
    elif st == 'close': c.send(['menu toggle'], 1.0) if False else c.send(['key ESC down', 'key ESC up'], 1.0)
    elif st.startswith('at:'): x, y = st[3:].split(','); c.send(['menuat %s %s' % (x, y)], 0.5)
    elif st == 'click': c.click(hold=0.2, settle=0.6)
    elif st == 'rclick': c.send(['button R down'], 0.2); c.send(['button R up'], 0.6)
    elif st.startswith('key:'): c.send(['key %s down' % st[4:], 'key %s up' % st[4:]], 0.8)
    elif st.startswith('shot:'): c.send(['shot ' + st[5:]], 1.5)
    elif st.startswith('wait:'): time.sleep(float(st[5:]))
print('ok')
