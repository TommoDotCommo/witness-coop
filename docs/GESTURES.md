# Adventurer gestures

Hold the configured emote key (`G` by default), point at a slot, then release.
While held, `Tab` advances the page and resets the pointer to its dead zone.
Release in the centre or press `Esc` to cancel the selection. The wheel remembers
its page. `F1` toggles the guide; its single binding table also drives social key
dispatch, including configured keys. `emote 0` cancels the current animation.

| Page | IDs, clockwise from the top |
| --- | --- |
| 1 | 1 Wave, 2 Dance, 3 Cheer, 4 Yes, 5 No, 6 Bow, 7 Spin, 8 Shrug |
| 2 | 9 Point, 10 Clap, 11 Facepalm, 12 Thumbs up, 13 Thinking, 14 Sit, 15 Stretch, 16 Salute |
| 3 | 17 Beckon, 18 Celebrate, 19 Sad, 20 Laugh, 21 Panic, 22 Sway, 23 Puzzle spark, 24 Horizon |

Thinking, Sit and Sway loop until cancelled or interrupted. Other gestures finish
in 2.6–4.8 seconds. Wave, Yes, No, Shrug, Point, Clap, Thumbs up, Salute and Beckon
retain control of the upper body while walking. Movement above 0.35 m/s cancels
other gestures; they stay cancelled when the player stops. Starting another
emote replaces the previous one through the same pose filter. Player movement is
never locked by an emote. The procedural poses blend over the current idle,
walk or run. Run selection has 10% release hysteresis to avoid restarting a
walk/run fade on packet-timing jitter. The pose layer has quintic entry/exit envelopes and a 90 ms joint response.
Sit and Stretch enter more slowly. The seated pelvis follows the leg-chain
height. Spin turns about the feet; the other standing gestures keep the gait's
foot transforms. Torso counter-motion, wrist motion and offset arm rhythms add
motion beyond a rigid whole-character effect.

Proximity speech uses the existing microphone capture and VAD pipeline. The
animation gate additionally requires RMS of at least 700 PCM units for 60 ms,
uses 400 units to remain active, and holds activity for 300 ms through syllable
gaps. PTT with a silent microphone does not lift the hand. Stopping transmission
or changing to radio, global or direct clears activity immediately; the renderer
lowers the hand smoothly. Emotes own the arms while active, including Laugh;
speech returns after an emote ends if proximity activity continues. Remote
activity expires with presence after four seconds without updates.

`WTS6` presence flag bit 2 now carries proximity gesture activity separately
from the existing speaking bit. Its length and the first eight emote IDs stay
unchanged. Old v6 clients ignore this bit but cannot animate the extended catalog;
use matching builds for this feature. The receiver validates IDs, reserved flags
and padding before touching state. Per-peer emote changes require 400 ms spacing;
cancellation and unchanged heartbeats remain free. Appearance, typing and voice
updates continue during an emote flood. Existing checksums, session/sequence
ordering and the fixed roster apply. A heartbeat repairs a lost start or stop.
Animation time starts when a peer first observes the new counter; exact phase
alignment across a delayed or lossy link is not guaranteed.

## Game-free verification

```sh
python3 tools/test_gestures.py
make -C proxy/core test HUB=<playbook-checkout>
make -C proxy
python3 tools/preview_gestures.py <output.png>
```

The focused script loads the actual bundled GLB and samples each emote at
120 Hz. It checks durations, finite transforms, anatomical angle bounds, entry
and exit continuity, wrist displacement, foot heights, standing foot placement,
looping, cancellation, pose switching and speech conflict behavior. Activity
streams cover isolated spikes, flicker, long speech, hysteresis, silence and
channel switching. The wire tests exercise every ID and hostile byte value,
atomic malformed-packet rejection, flood spacing, immediate cancellation,
reordering, counter wrap and expiry. An eight-player simulator runs 32 loss seeds
with duplicate and reversed delivery, then verifies convergence after recovery.
Eighteen deliberately broken variants must fail: blending, movement cancellation,
channel gating, hysteresis, conflict priority, envelope, limits, loops, receive
validation, rate limiting, missing skeletal application, seated height, bind-axis
sign, speech expiry, loop expiry, speech encoding, local API validation and walk/run threshold hysteresis.

The preview uses the production joint palette and CPU skinning, rasterized with
a depth buffer and the bundled atlas. The 5×5 sheet shows IDs 1–24 at 1.5 seconds,
left to right, then proximity speech in cell 25. It is a pose preview, not evidence
of live rendering or complete animation quality.

## Ordered live verification (not run in the game-free environment)

1. Run the four game-free commands above, substituting the playbook checkout and
   preview destination. All tests must pass and every mutant must be rejected.
2. Run `tools/qa.sh emote`. This builds and installs into disposable test clients
   through the existing harness. The clean scenario must print `ALL OK`; each
   of `no_emote_send`, `no_emote_anim`, `no_key_swallow`, `no_speaking_flag` and
   `no_voice_channel` must be caught. The Windows probe reads `witness_net.json`,
   `witness_models.json` and camera memory; it never asserts on logs or images.
   It checks all 24 received/rendered IDs, smooth release, one-shot expiry,
   looping, movement cancellation, all three wheel pages, mouse-look capture,
   guide toggling and Escape capture. It then checks the proximity activity flag,
   hand height, silence, Laugh priority and exclusion of all other channels.
3. Run `tools/qa.sh voice channels` to check existing channel audio routing and
   its mutants alongside the new visual activity flag.
4. Run `tools/qa.sh` for the full regression suite. Preserve its cleanup and save
   hygiene checks. For a single-instance startup check, the probe can read
   `netstate`/`modelstate` after the standard harness starts that instance; the
   two-client scenario above additionally proves startup on both processes.

Before accepting visual polish, inspect live motion from front, side and rear
while idle, walking and running. Use screenshots only to discover defects; encode
any discovered defect as a pose or process-state assertion before accepting a fix.

## Limits requiring review

The base asset has only rigid hand bones: its authored fingers share the palm's
skin influence. Wrist and arm poses are animated, but independent pointing,
thumbs-up and cupping finger shapes are unavailable. Cloth, hair and scarf also
have no secondary-motion joints. This change does not alter the mesh or add a
finger/cloth rig. The tests check joint/foot trajectories and numerical continuity;
they do not prove absence of all mesh intersections or ground clearance on uneven
terrain. Live appearance, whole-body weight transfer, contact quality, frame cost
and the requested level of animation polish remain unverified. A rig extension
and animation review are needed before calling the result world-class.
