# The adventurer: art direction and the character pipeline

The bundled character is generated entirely from code (`models/src/adventurer.py`, Blender, `models/build.sh`). This page is
its art bible: the direction, the rules that keep it out of the uncanny valley, how the parts are built, and the checks that
hold every build to the design. The numbers live in [`models/src/character_spec.py`](../models/src/character_spec.py); the
generator builds from them and the gate measures against them.

## Direction: stylized appeal

A face built procedurally at realistic proportions and modest detail lands in the uncanny valley: close enough to human that
every missing cue (wet eyes, lid thickness, skin planes) reads as wrong. The adventurer is therefore **stylized**: simplified,
confident forms with deliberate proportions, in the spirit of high-end animated film rather than photoreal games. The identity
stays (a traveller with a coat, scarf, hat and handmade gear); the drawing changes.

| Rule | Why |
|---|---|
| About 6.7 heads tall, not 7.5 | A slightly large head reads as designed and carries expression at a distance. |
| Real eyeballs in sockets, under lids with thickness | Eyes painted on the face's surface read as dead. Depth, a lid rim and a catchlight read as alive. |
| An open almond: visible eye height about half its width | The old 0.26 aspect read as a squint. The gate holds 0.36 to 0.60. |
| The upper lid touches the top of the iris | White above the iris reads as alarm. White all round reads as a stare. |
| Iris about 0.4 to 0.5 of the opening's width | Smaller loses the gaze; larger becomes a doll's eye. |
| About one eye's width between the eyes | Wider spacing reads as vacant, narrower as cross. |
| Widest at the cheekbones, tapering to the chin | A bell-shaped lower face reads as jowly and old. |
| A dark lash line on the upper lid, a lid crease, soft arched brows | These are the cues that make stylized eyes read. |
| No ambient occlusion on the eyes | A ball in a socket bakes nearly black occlusion, which greys the whites. |
| Hair as a few soft clumps with a designed hairline | Thin spikes and helmet caps read as cheap. |
| Hands slightly large, with chunky tapered fingers in a relaxed curl | Thin realistic fingers are the second most uncanny thing on a figure. |
| The hat sits above the brows and tips back | Players look from eye height; a low brim hides the face. |

## How the organic parts are built

The head (skull, face, ears, neck), the moving upper lids, the brows, the hair and the hands are **signed distance fields**
(`models/src/sdf.py`): primitives blended with smooth unions and carved with smooth subtractions, like clay. `head.py`
lists the head as a sequence of strokes (the cranium, the face's mass, the cheeks, the jaw, the brow ridge, the nose's bridge,
tip and wings, the muzzle, the lips, the chin pad, the ears' rim and bowl, the neck), then cuts each eye socket and a tunnel
through the lid opening. `hands.py` does the same for the palm, fingers, thumb and glove.

A field becomes a mesh with surface nets (one vertex per grid cell that the surface crosses, quads between them). Every vertex
is then pulled onto the exact surface along the field's gradient, relaxed, decimated to its triangle budget (symmetrically on
a mirror-symmetric grid for the head), and shaded with the field's own normals. The eyes are spherical caps (white, iris ring,
iris, pupil, catchlight). The life layer blinks by turning the upper lid about the eyeball's centre (`lid_rotate=` in the
model's `.glb.txt`; older models keep `lid_close=`, the stretch). The lower lip is weighted into the face skin, so speech
deforms the skin instead of moving a separate strip.

The hat is scaled in plan to the head that was built (measured where its opening sits) and still has to pass its fit check.

## The loop

1. Shape in the lab: `blender -b -P models/src/headlab.py -- <out.glb> [--hands]` meshes only the head (and hands) in
   seconds; `tools/char_lookdev <out.glb> <out.png> --sheet face --time -1` shades it.
2. Build: `models/build.sh` (about 1.5 minutes at a 2048 atlas). It runs the model, idle seam, run pose, appearance and
   **character** checks; a build that misses the spec is not copied into `assets/models/`.
3. Look: `tools/char_lookdev assets/models/adventurer.glb sheet.png` writes the body turntable, five portraits and the
   hands, with smooth skinned normals, the normal and material maps and key, fill and rim lights.
4. Review: `python3 tools/char_review.py` writes a before/after page (`local/char_review/index.html`) with the gate's numbers.
5. Gate mutants: `tools/test_character.sh` (a few minutes; builds broken characters that the gate must reject).
6. The emote quality gate (`make -C proxy/model emotes`) re-checks every emote on the rebuilt rig.

Use screenshots and the review page to find problems. Fix a problem in the gate or the spec as well as in the model.

## The character gate (`tools/check_character.py`, `tools/char_measure.cpp`)

The measure looks at the built GLB the way a viewer does. It renders the face from the front at 0.1 mm per pixel with labels
(visible eye white, visible iris, skin) and measures the openings. It blinks the eyes through the proxy's own life layer and
measures again. It measures proportions, the face's mirror symmetry (point to surface) and budgets.

| Target | Range | The old model |
|---|---|---|
| heads tall | 6.4 – 7.1 | 7.55 (miss) |
| eye line above the chin / head | 0.45 – 0.53 | 0.52 |
| visible eye height / width | 0.36 – 0.60 | 0.26 (miss) |
| gap between the eyes / eye width | 0.95 – 1.40 | 1.53 (miss) |
| visible iris width / eye width | 0.38 – 0.52 | 0.32 (miss) |
| eye line to nose tip / head | 0.14 – 0.22 | 0.13 (miss) |
| face symmetry, 99th percentile | ≤ 0.8 mm | 0.13 mm |
| hand length / chin to brow | 0.75 – 1.00 | 0.99 |
| head skin triangles | 8 000 – 30 000 | 11 888 |
| visible eye after a full blink / open | ≤ 0.08 | 0.08 |
| brightness of the visible eye white | ≥ 0.70 | 0.60 (miss) |
| zero-area triangles | ≤ 400 | 122 |

`tools/test_character.sh` proves each protection can fail: an oversized iris, a lid that turns the wrong way, an eyeball
standing out of its socket, eyes darkened by the occlusion bake, and the blink code turned off must each be rejected on
their own target.

## Budgets

The proxy skins on the GPU with 32-bit indices and draws at most sixteen characters, so geometry is cheap. The old caps
(60 000 vertices, 100 000 triangles, 2048 textures, 9 MiB) were arbitrary and the model had already reached them. They are now
ceilings that catch an accident (250 000 vertices, 400 000 triangles, 4096 textures, 64 MiB), not art budgets. Spend vertices
where they read: the face, the lids, the hands, the silhouette.

## What is not done yet

- The coat, sleeves, trousers and boots are still the lofted shapes of the first version: tubes without folds or weight.
  They are the next pass, with the same method (fields for the soft forms, folds from the joints).
- The atlas is still 2048 for the whole body; a 4096 build works but its normal-map flatness check needs re-tuning.
- No finger or clavicle bones yet, so hand acting and shrugs are limited (docs/ANIMATION.md).
- Skin shading is the game's standard material: no subsurface tint at the ears and nose beyond what is painted.
