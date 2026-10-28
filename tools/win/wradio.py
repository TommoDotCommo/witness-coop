"""T2: actual process snapshots, synthetic live audio, independent first/third person draws.
Usage: wradio.py <pidA> <dirA> <pidB> <dirB> [enabled|disabled]
The launcher supplies two isolated clients; this script never starts a process.
"""
import json, math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0,HERE)
from wcontrol import Control
exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
pa, da, pb, db = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4]
disabled = len(sys.argv) > 5 and sys.argv[5] == 'disabled'
a, b = Control(da), Control(db)
A, B = attach(pa), attach(pb)
token = int(time.time()) & 0x3fffffff

def snapshot(control, directory):
    global token
    token += 1
    control.send(['radiodump %d' % token])
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            with open(os.path.join(directory, 'witness_radio.json')) as f: state = json.load(f)
            if state['token'] == token: return state
        except (OSError, ValueError, KeyError): pass
        time.sleep(.04)
    raise AssertionError('no fresh process radio snapshot')

def entry(state, group, player):
    return next((x for x in state[group] if x['id'] == player), {})

def check(condition, text):
    if not condition: raise AssertionError(text)
    print('PASS ' + text, flush=True)

def wait_for(control, directory, condition):
    deadline = time.monotonic() + 10
    while True:
        state = snapshot(control, directory)
        if condition(state): return state
        if time.monotonic() > deadline: raise AssertionError('condition timed out: ' + repr(state))
        time.sleep(.08)

try:
    a.send(['menusel c335', 'menudo teleport'], 4)
    (x, y, z), (heading, _) = A.camera()
    b.send(['hover %.5f %.5f %.5f %.5f 0' % (x + 4*math.cos(heading), y + 4*math.sin(heading), z, heading + math.pi)], 2)
    a.send(['voicechannel proximity', 'key R up', 'key N up'], .5)
    state = snapshot(a, da)
    peer = wait_for(b, db, lambda s: entry(s, 'remotes', 1).get('age_ms', 10000) < 2000)
    check(bool(peer), 'two clients have fresh peer state')
    check(not state['tx'], 'A starts idle')
    a.send(['key R down'], 1.2)
    if disabled:
        state = snapshot(a, da)
        check(not state['enabled'] and not state['tx'] and not entry(state, 'draws', 0), 'disabled radio cannot transmit or submit a first-person prop')
    else:
        state = wait_for(a, da, lambda s: s['tx'] and entry(s, 'draws', 0).get('raise', 0) > .98)
        check(entry(state, 'draws', 0)['vertices'] > 0, 'A submits the raised first-person model')
        state = wait_for(b, db, lambda s: entry(s, 'remotes', 1).get('tx') and entry(s, 'draws', 1).get('raise', 0) > .98)
        remote, draw = entry(state, 'remotes', 1), entry(state, 'draws', 1)
        check(remote['channel'] == 1 and remote['equipped'] and remote['age_ms'] < 2000, 'B receives fresh validated A radio presence')
        check(draw['vertices'] > 0, 'B submits the independent raised wrist prop')
        # Captures are artifacts for visual review; all automated assertions use state.
        a.send(['shot radio_fp']); b.send(['shot radio_tp'])
        near = wait_for(b, db, lambda s: entry(s, 'speakers', 1).get('rms', 0) > .005)
        speaker = entry(near, 'speakers', 1)
        check(speaker['channel'] == 1 and speaker['radio'] > .8 and speaker['prox'] == 0, 'nearby B hears only radio processing')
        tones = speaker['tone']
        check(tones[1] > .02 and tones[0] < .25*tones[1] and tones[2] < .25*tones[1], 'live synthetic signal is band-limited at B')
        (bx, by, bz), _ = B.camera()
        a.send(['hover %.5f %.5f %.5f %.5f 0' % (bx+1000, by, bz, heading)], 2)
        (ax, ay, az), _ = A.camera(); (bx, by, bz), _ = B.camera()
        check(math.dist((ax, ay, az), (bx, by, bz)) > 900, 'process memory confirms large separation')
        far = snapshot(b, db); fs = entry(far, 'speakers', 1)
        check(fs['channel'] == 1 and fs['radio'] > .8 and fs['prox'] == 0 and fs['rms'] > .005, 'radio remains audible at session distance')
        b.send(['voiceplayer 1 0'], .5)
        check(entry(snapshot(b, db), 'speakers', 1)['rms'] < .00001, 'per-player mute silences A after the ramp')
        b.send(['voiceplayer 1 50'], .5)
        half = entry(snapshot(b, db), 'speakers', 1)
        check(abs(half['radio'] - .425) < .01 and half['rms'] > .001, 'per-player volume scales radio gain')
        b.send(['voiceplayer 1 100'])
    a.send(['key R up'], 1)
    state = snapshot(a, da)
    check(not state['tx'] and state['channel'] == 0, 'release stops radio and restores proximity')
    if not disabled:
        check(entry(state, 'draws', 0)['raise'] < .02, 'first-person prop lowers smoothly to stow')
        state = wait_for(b, db, lambda s: not entry(s, 'remotes', 1).get('tx', True) and entry(s, 'draws', 1).get('raise', 1) < .02)
        a.send(['shot radio_stowed'])
        a.send(['key R down'], .5)
        wait_for(a, da, lambda s: s['tx'])
        a.send(['typing on'], .5)
        check(not snapshot(a, da)['tx'], 'opening chat cancels a held transmission')
        a.send(['typing off'], .3)
        check(not snapshot(a, da)['tx'], 'closing chat does not resume a cancelled hold')
        a.send(['key R up', 'typing on', 'key R down'], .5)
        check(not snapshot(a, da)['tx'], 'chat captures radio presses')
        a.send(['key R up', 'typing off'])
    print('ALL OK', flush=True)
except AssertionError as error:
    print('RADIO_ASSERTION: ' + str(error), flush=True)
    sys.exit(1)
finally:
    a.send(['key R up', 'key N up', 'typing off'])
    b.send(['voiceplayer 1 100'])
