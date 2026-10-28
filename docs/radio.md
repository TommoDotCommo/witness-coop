# Handheld radio

Hold **R** to talk on the session radio. **N** retains the selected voice channel;
**H** cycles channels. Releasing R returns to that selection. **F1** shows the
configured bindings. The launcher Controls page, configuration parser and guide
share `proxy/bindings.h`. Set `key_radio=` to rebind, `radio_enabled=0` to disable
the prop and dedicated hold key, or `voice_mode=off` to disable capture. The
launcher Sound and voice page exposes the handheld option.

Radio transmissions use the existing 16 kHz / 20 ms ADPCM voice transport. The
radio channel is audible at any session distance, including beside its sender.
It uses a 350–3000 Hz filter, gentle saturation, seeded hiss and opening/closing
squelch. There is no additional DSP buffering; the existing jitter buffer waits
for three packets (60 ms). Device/network latency is additional and unmeasured.
No captured audio is recorded or stored. `voice_radio_range` continues to control
the automatic distant-radio fallback of proximity chat; the explicit radio
channel has unlimited range.

In the F5 player list, select a player and use Left/Right to adjust voice volume,
or M to mute/unmute. These controls affect all their voice channels for this
listener, for the current process lifetime. The command bridge equivalent is
`voiceplayer <id> <0..100>`; zero mutes. The microphone HUD identifies the channel,
and a transmitting player's speaker mark says RADIO.

The original procedural prop has an enamel bevelled shell, leather bumper,
brass trim/label plate, raised speaker bars, side PTT paddle, knob, antenna and
blinking red light. Budget: 732 vertices / 244 triangles per instance, zero texture
bytes, no external assets or additional licenses. It uses the existing triangle
renderer; the remote prop uses scene depth, while the camera prop has a separate
view-model pass. The raised view stays to the right of the central puzzle area;
the idle view lowers into the bottom edge. Rendering and visual quality still
require the live review below.

`ModelExtra::radioEquipped/radioTx` is the small third-person interface. The radio
uses a smooth, independent right-arm overlay after clip sampling and attaches
the prop to `hand.R`. Left-arm and gait poses remain sampled. When integrating a
proximity hands-to-mouth animation, arbitrate that arm slot with radio priority;
do not OR the proximity speaking flag into the radio pose. The finished
adventurer's bone names are required.

Presence retains its 36-byte record: flags bit 2 means radio transmission, bit 3
means equipped, and byte 19 carries the voice channel (0..3). A transmit flag
requires equipped, speaking and channel radio. Invalid combinations are rejected
atomically before sequence advancement. Newest-state ordering and heartbeat
expiry apply to transmission. An older peer ignores the new fields; older
senders leave them zero and therefore show no radio item.

## Verification

Game-free, from the repository root:

```sh
make -C proxy/radio test
make -C proxy/voice test
make -C proxy/core HUB=<playbook-checkout> test
make -C proxy
make -C launcher launcher
python3 -m py_compile tools/qa_radio.py tools/win/wradio.py
bash -n tools/qa.sh
git diff --check
```

The radio test runs the actual DSP, codec, mixer, sync encoder/decoder and
adventurer skeleton. It exhaustively checks presence flag/channel validity,
checks narrow through ultrawide projections at 40–110 degree vertical FOV,
checks finite bounded deterministic DSP, impulse onset and frequency response,
checks mute and channel gain, and checks heartbeat, stale updates and expiry.
Each added protection has a broken twin which the test must reject. Existing
voice and core tests exercise transport loss/reordering and their own mutants.

A Windows verifier with the supported build and two isolated clients must run,
in order:

```sh
make -C proxy
make -C launcher launcher
tools/install.sh
tools/qa.sh radio
tools/qa.sh voice channels
tools/qa.sh voice ptt
```

`qa.sh radio` sets a synthetic three-tone microphone on A and muted physical
output on B (the real mixer still runs). It asserts on fresh tokened process
snapshots written by `radiodump <token>`, never on log lines or image contents:

1. Idle A is silent; R raises A's first-person prop and B's independent remote
   wrist pose. B receives the equipped/transmitting flag and channel 1.
2. At both nearby and >900 m separation read from process memory, B's mixer uses
   radio gain with zero proximity gain. The 120 Hz and 5.5 kHz components are
   below one quarter of the 1 kHz component.
3. Per-player mute produces zero RMS; 50% volume yields 0.425 radio gain.
4. Release clears transmission, lowers both poses and restores proximity.
   Chat blocks new radio presses. A separate disabled run submits no camera prop.
5. Live mutants omit audio send, radio channel selection, speaking presence, camera drawing and the
   remote arm overlay. Each must reach the idle precondition and then fail a
   state assertion. A launch/precondition failure does not count as a kill.

Inspect the in-game `shots/radio_fp.bmp`, `radio_tp.bmp` and `radio_stowed.bmp`
copied from the clean run into `local/radio-captures/`; captures accompany
state assertions and are not automated evidence of appearance. Verify the light
blinks and antenna is visible, the held prop sits beside the puzzle, the remote
radio sits in the hand near the mouth, and release stows smoothly. Run an audible
human microphone check after removing the synthetic tone and output mute;
record intelligibility, actual latency and device behavior as unknown until
observed. T1–T3, actual GPU appearance and audible quality were not established
by game-free tests.
