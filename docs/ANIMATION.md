# Animating the adventurer's emotes

The emotes are keyframed clips in [`assets/emotes/`](../assets/emotes), one text file per emote. The proxy
plays them through a layer that adds the animation principles a hand-tuned sine wave never had (overlap,
follow-through, weight shifts with planted feet), and a game-free quality gate checks every clip on the real
character before it can ship. This page covers the format, the tools and the work loop.

## The loop

1. Edit a clip in `assets/emotes/`. Its header comment says what the emote is meant to read as.
2. `make -C proxy/model emotes` runs the gate (about 7 seconds). Each failure names the check, the time and,
   for clipping, the point on the body it hit (`the hat brim`, a rest-pose coordinate).
3. `python3 tools/emote_review.py` renders every clip from the front, side and rear with the production pose
   code and writes `local/emote_review/index.html` (the dailies page): a player per emote, wrist spacing
   trails, a dope-sheet timeline with the keys and hits, the gate's numbers and the clip source.
4. `python3 tools/emote_embed.py` copies the clips into `proxy/model/emote_data.h`, which the proxy compiles
   in. The gate refuses an out-of-date header (`emote_embed.py --check`).
5. In a running test game, `tools/install.sh` puts the clips beside the proxy in `witness_models/emotes/`;
   after editing them there, the `emotes reload` command (in `witness_cmd.txt`) swaps them in at the next
   frame. A file that does not parse is refused and the old clips stay; the log says why.
6. Live checks: `tools/qa.sh emote` (two clients, every ID received and rendered).

Screenshots and the dailies are for finding problems. A problem found becomes a gate check or a bad clip in
`proxy/model/tests/emote_bad/` before it is fixed.

## The clip format

```text
# Salute: a crisp hat-brim salute. ...
emote 16 Salute          # wire id and name, as in proxy/core/emotes.h
duration 3.2             # must match proxy/core/emotes.h (both games end it at the same time)
drag 0.02                # overlap: seconds of lag per joint down a chain (default 0.03)
stiffness 1.4            # scales every joint spring (default 1; below 1 is heavier, slower)
touch hand.R head 0.03 0.6 2.3   # the gate: the hand comes within 3 cm of the head between 0.6 and 2.3 s
pose up arm.R=23,39,89 fore.R=104
key 0.00 rest
key 0.22 chest=-3 head=-3 arm.R=8,10,20 fore.R=40
key 0.48 hit @up hand.R=0,0,-8
key 3.18 rest
```

- `key <time> ...` is a key pose. It starts from the previous key, so a key lists only what changes. `rest`
  starts from the bind pose, `@name` from a `pose`. `hit` marks an impact: the curve arrives at speed and
  stops there, instead of easing in.
- Between keys every channel follows a monotone cubic: no overshoot between keys, flat at extremes, so a key
  that is a held extreme eases in and out by itself. Overshoot and settle come from the springs.
- Joint values are degrees: `bone=forward,twist,side`. Missing components keep their inherited value
  (`head=,10` turns the head and keeps its nod). Bones: `spine chest neck head pelvis`, `arm fore hand thigh
  knee foot` with `.L` or `.R`. Left bones take the same anatomical numbers as the right (the parser mirrors
  twist and side for the rig); `arm.LR=...` sets both.
- Directions: forward bends the torso or nods the head down, and swings a limb forward; twist turns the head
  or chest to the character's left; side tilts the torso or head toward the right and raises an arm out to
  the side. Overhead arms go up by `side` (abduction), as in Cheer; forward angles past about 120 degrees
  cross the arm over the face.
- `hips=x,y,z` moves the pelvis in centimetres (x to the character's left, y up, z forward). With planted
  feet the foot IK bends the knees, so `hips=0,-5,0` is a dip and `hips=3,-1,0` a weight shift. Straight legs
  cannot rise: keep `y` at or below zero, and lower it a little when moving the hips sideways or forward.
- `step.L=out,up,forward`, `step.R`, `step.LR` move a planted foot's target in centimetres: a toe tap, feet
  sliding forward to sit, a hop (feet up 15 cm with the hips up 10).
- `turn=degrees` turns the whole body about the feet (Spin). Planted feet pivot with it.
- `feet free` turns the foot IK off for the clip, and `plant=0..1` keys it; legs are then posed by their own
  channels. Prefer steps: blending between IK legs and keyed legs swings the feet through the floor.
- `loop <from> <to>`: after the first pass the clip repeats that span. Put keys exactly on both ends with
  the same values; the gate checks the seam.
- `symmetric` allows exactly mirrored arms; `eyedrop <min> <max>` asks the gate for a seated eye height.
- The proximity-speech hand is clip id 100 (`99_talk.emote`), looping while the player talks.

## What the runtime adds (proxy/model/gestures.h, model.cpp)

- **Overlap.** Each joint samples its curve `drag × depth` seconds late, depth counted from the core: chest 1,
  shoulder 2, forearm and neck 3, hand 4.5. The hand trails the arm into every pose and out of it.
- **Follow-through.** Each joint follows the clip through a damped spring (heavier and more damped in the
  torso, lighter in the hands), integrated in 240 Hz sub-steps so 10 and 60 fps look the same.
- **Weight.** Hips offset and pelvis rotation with two-bone foot IK: the soles stay where the gait or idle put
  them (or where `step` sends them), the knees bend, the feet keep their angle.
- **Sparse layering.** A clip drives only the bones it mentions. Wave leaves the legs and the left arm to the
  walk cycle.
- The 0.35 s entry and 0.45 s exit envelope (proxy/core/emotes.h), movement cancellation, Laugh-over-speech
  priority and the wire format are unchanged.

## The quality gate (proxy/model/tests/emote_quality.cpp)

Every clip plays at 120 Hz on the bundled character, over the idle, through the production layers. Body
shapes are the mesh's own: a posed point is carried back into the bind pose of the torso or head and measured
against the nearest surface vertex and its normal, so the hat brim is a thin disc and the coat is the coat.

| Check | Fails when |
|---|---|
| catalog | a wire id has no clip, or name, duration or loop flag differs from `proxy/core/emotes.h` |
| mobile | an emote that plays while walking drives the hips, pelvis, legs or turn |
| limits | a joint (springs included) leaves its anatomical range |
| finite | a joint matrix is not finite |
| pop | a hand or the face moves more than 9 m/s (7.5 cm in one 120 Hz frame) |
| jerk | a hand or the face accelerates over 260 m/s² |
| foot-slide | a planted foot is more than 1.2 cm from its target (gait position, turn and steps) |
| ground | a sole goes more than 1.2 cm into the ground |
| balance | the centre of mass leaves the outline of the planted feet by over 1 cm |
| penetration | a hand goes 1 cm, or a forearm 1.5 cm, into the torso or head (declared contacts allow 2.5-3 cm); the palms meet closer than 4.5 cm |
| contact | a declared `touch` is never reached |
| dead-hold | nothing moves for 0.6 s while the emote is fully in (holds must be moving holds) |
| readable | the largest hand move (or twice the face's) is under 12 cm: it will not read across a room |
| twinning | both arms mirror each other within 6 degrees the whole time, unless the clip is `symmetric` |
| distinct | two emotes move alike (mean hand/face difference under 2 cm) |
| loop-seam | a looped clip jumps across its loop |
| eye-drop | a seated pose misses its eye height (Sit: 0.44-0.50 m, as the old seat; proxy/tv/tv.h relies on it) |
| exit | a one-shot has not let go a second after its end |

`make -C proxy/model emotes` also runs every clip in `tests/emote_bad/` (its first line, `# expect: <check>`,
names the check it must trip) and four code mutants (no foot IK, no hips, no steps, no springs) that must fail.
`tools/test_gestures.py` covers the controller: the wheel, cancellation, speech, the parser's rejections, the
safe reload, the overlap lag and the spring's overshoot, each with a mutant.

## Authoring tools

```sh
g++ -std=c++17 -O2 tools/emote_render.cpp proxy/model/model.cpp -lz -ldl -o emote_render
./emote_render assets/models/adventurer.glb - --probe "arm.R=16,88,100 fore.R=70"         # where the hands and head land
./emote_render assets/models/adventurer.glb - --probe "<seed pose>" --reach tip.R=-0.15,1.70,0.21   # the arm that puts the hand there
```

`--reach hand.R=x,y,z` places the wrist, `tip.R=` the hand's end, in model space (metres: x to the
character's left, y up, z forward). Useful landmarks on this character: chin about (0, 1.55, 0.12), nose
(0, 1.64, 0.14), the hat brim a disc at y 1.71-1.75 reaching 0.25 m forward and 0.26 m to the sides, the
chest front z 0.13-0.14, the boots' toes z 0.22.

## Limits

The rig has rigid hands (no finger bones), no clavicles and no cloth joints in the sleeves, so pointing,
thumbs-up and fists are wrist poses and a shrug is the neck sinking rather than the shoulders rising. The
gate measures clipping against the torso and head; arm-against-arm and hand-against-leg clipping are only
checked where a clip declares a contact. Live appearance at game frame rates, from the other player's
camera, still needs a person to look (`tools/qa.sh emote` proves the clips play on both clients, not that
they look good).
