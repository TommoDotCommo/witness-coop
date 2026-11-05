# Adventurer gestures

Hold the configured emote key (`H` by default), point at a slot, then release.
While held, `Tab` advances the page and resets the pointer to its dead zone.
Release in the centre or press `Esc` to cancel the selection. The wheel remembers
its page. `F1` is a hold: the guide is shown while it is down. Its single binding table also drives social key
dispatch, including configured keys. `emote 0` cancels the current animation.
The press keys act once per press: the autorepeat of a held key (about 30 presses a second) changes nothing for `F2`,
`F3`, `F4`, `F8`, `F9`, `[`, `]`, ping (`V`), the drawing canvas (`Z`), the voice channel (`Y`), a keyboard ink colour key
and, with the experimental TV, `F10`, `F11` and its seat and deploy keys. The hold keys (`F1`, `Tab`, `H`, `X`, `B`, `R`)
act on the press and the release; the launcher's `F1` guide window is shown once when the hold starts and hidden on the release, never re-shown or repainted by the autorepeat (`tools/qa.sh ui lguide`). In the chat box, the developer menu and the player panel a held key still repeats.
The rule is `proxy/ui/key_repeat.h` (game-free test in `make -C proxy/ui test`); `tools/qa.sh ui keys` holds F8, Tab, Z
and V in a game, with the twin `key_repeat_toggle=1`.

| Page | IDs, clockwise from the top |
| --- | --- |
| 1 | 1 Wave, 2 Dance, 3 Cheer, 4 Yes, 5 No, 6 Bow, 7 Spin, 8 Shrug |
| 2 | 9 Point, 10 Clap, 11 Facepalm, 12 Thumbs up, 13 Thinking, 14 Sit, 15 Stretch, 16 Salute |
| 3 | 17 Beckon, 18 Celebrate, 19 Sad, 20 Laugh, 21 Panic, 22 Sway, 23 Puzzle spark, 24 Horizon |

The brows act with the emotes (Sad, Thinking, Laugh, Celebrate, Cheer, Panic, Facepalm, No and Shrug move them; see [the life layer](LIFE.md)).
Thinking, Sit and Sway loop until cancelled or interrupted. Other gestures finish
in 2.6–4.8 seconds. Wave, Yes, No, Shrug, Point, Clap, Thumbs up, Salute and Beckon
play while walking and leave the hips and legs to the gait. Movement above 0.35 m/s cancels
other gestures; they stay cancelled when the player stops. Starting another
emote replaces the previous one through the same pose filter. Player movement is
never locked by an emote. Run selection has 10% release hysteresis to avoid restarting a
walk/run fade on packet-timing jitter.

Each emote is a keyframed clip in `assets/emotes/`, played with overlap down the
joint chains, a spring per joint for follow-through, hips and foot IK for weight
shifts with planted feet, and a quintic entry/exit envelope. [ANIMATION.md](ANIMATION.md)
describes the format, the runtime layer, the quality gate and the work loop.

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
make -C proxy/model emotes            # the animation quality gate, its bad clips and mutants
python3 tools/test_gestures.py        # wheel, cancellation, speech, parser, overlap, springs, wire
make -C proxy/core test HUB=<playbook-checkout>
make -C proxy
python3 tools/emote_review.py         # dailies: local/emote_review/index.html
```

The gate (listed in [ANIMATION.md](ANIMATION.md)) plays every clip on the bundled
character at 120 Hz and checks limits, pops, planted feet, ground contact, balance,
clipping into the torso and head, declared contacts, moving holds, twinning,
readability, distinctness and loop seams. The controller tests cover the wheel,
movement cancellation, speech gating and conflicts, the clip parser's rejections,
the safe reload, the overlap lag and the springs. The wire tests exercise every ID
and hostile byte value, atomic malformed-packet rejection, flood spacing, immediate
cancellation, reordering, counter wrap and expiry; an eight-player simulator runs 32
loss seeds with duplicate and reversed delivery. Every protection has a mutant or a
bad clip that must fail.

The dailies render uses the production joint palette and CPU skinning from three
views. It is a review aid, not evidence of live rendering.

## Ordered live verification (not run in the game-free environment)

1. Run the game-free commands above, substituting the playbook checkout. All tests must pass and every mutant must be rejected.
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

The rig has rigid hands (no finger bones), no clavicles and no cloth joints in the
sleeves: pointing, thumbs-up and fists are wrist poses, and a shrug sinks the neck.
The gate checks clipping against the torso and head, not arm against arm. Live
appearance at game frame rates from the other player's camera still needs a person
to look; a finger and clavicle rig extension is the next step for hand acting.
