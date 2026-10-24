"""Print the camera position and angles of a game: x y z yaw pitch. Usage: Windows Python wcam.py <pid>"""
import os, sys
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); pos, ang = p.camera(); print('%.3f %.3f %.3f %.4f %.4f' % (*pos, *ang))
