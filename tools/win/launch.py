# usage: launch.py <game dir> [game args...]   starts witness64_d3d11.exe there and prints its pid.
# The window is shown without being activated (SW_SHOWNOACTIVATE), so a game starting in the background does not take the
# keyboard focus from whoever is typing in a terminal.
import subprocess, sys, os
d = sys.argv[1]
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4      # SW_SHOWNOACTIVATE
p = subprocess.Popen([os.path.join(d, 'witness64_d3d11.exe')] + sys.argv[2:], cwd=d, startupinfo=si)
print(p.pid)
