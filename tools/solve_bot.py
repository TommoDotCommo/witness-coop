#!/usr/bin/env python3
"""Bot: player A walks to the first panel (entity 0x64, the door at the end of the start tunnel) and solves it with real input.
usage: solve_bot.py <pidA>   (Windows window must be visible; the bot takes the keyboard and mouse focus)"""
import sys, time, math
from witness import pyw, camera_and_entity, dist2d
pid = sys.argv[1]
def keys(*a): return pyw('wkeys.py', pid, *a)

# 1. walk to about 3.4 m from the panel (3 s key presses; a long press right after a focus change was ignored once)
for _ in range(30):
    cam, ang, pan = camera_and_entity(pid); d = dist2d(cam, pan)
    print('walk: dist %.1f' % d, flush=True)
    if d < 3.6: break
    keys('key', 'W', '3.0' if d > 8 else '0.6')
# 2. aim at the panel by feedback: +mouse x turns right (theta down) 0.00255 rad per count, +y looks down 0.00237 rad per count
for _ in range(6):
    cam, ang, pan = camera_and_entity(pid)
    yaw = math.atan2(pan[1] - cam[1], pan[0] - cam[0]); pitch = math.atan2(pan[2] - cam[2], dist2d(cam, pan))
    dyaw = (yaw - ang[0] + math.pi) % (2 * math.pi) - math.pi; dpitch = pitch - ang[1]
    dx, dy = int(round(-dyaw / 0.00255)), int(round(-dpitch / 0.00237))
    print('aim: yaw err %.4f pitch err %.4f -> mouse %d %d' % (dyaw, dpitch, dx, dy), flush=True)
    if abs(dx) < 2 and abs(dy) < 2: break
    keys('seq', 'mouse', str(dx), str(dy), '4'); time.sleep(0.4)
cam, ang, pan = camera_and_entity(pid); d = math.dist(cam, pan)
# 3. click to enter the puzzle (the cursor starts at the screen centre); the start circle is 0.218 m left of the panel
#    centre, 624 px per radian of view, and the puzzle cursor moves 1 px per 3.6 mouse counts
keys('click', 'L', '0.1'); time.sleep(2.5)
px = 0.218 * 624 / d
print('cursor to circle: %.1f px left (distance %.2f m)' % (px, d), flush=True)
keys('seq', 'mouse', str(-int(round(px * 3.6))), '0', '8'); time.sleep(0.5)
# 4. press on the circle, drag along the line to the end bump, then click to finish
keys('seq', 'down', 'L;', 'wait', '0.15;', 'up', 'L;', 'wait', '0.3;', 'mouse', '450', '0', '30;', 'wait', '0.5')
keys('click', 'L', '0.12'); time.sleep(3)
print('done. panel 0x64 pos now', camera_and_entity(pid)[2], flush=True)
