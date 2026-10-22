import subprocess, sys, os
d = sys.argv[1]
p = subprocess.Popen([os.path.join(d, 'witness64_d3d11.exe')] + sys.argv[2:], cwd=d)
print(p.pid)
