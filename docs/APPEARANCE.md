# Adventurer appearance

Implementation status: the portable descriptor, sync core, palette parameters,
generator labels, renderer path and controls are implemented. The bundled GLB is
the regenerated labelled asset and passes the asset, geometry, material and gait
checks. Windows builds, two-client palette sync and session edits, legacy-option
migration, and launcher controls have also passed their live checks.

Only colours vary in this first descriptor. Height, body shape, face geometry,
hair geometry and outfit pieces remain shared. This keeps cloth clearance, eye
alignment, skeleton and gait identical for every palette combination. No model
per combination is shipped. Further geometry options need their own fit coverage.

The launcher's Appearance page saves `appearance_<feature>=0|1|2|3`. Missing values use the original palette; invalid entries are ignored. In game, open chat and enter
`/appearance coat 2` (or any feature below); this is a local command, not a chat
message sent to other players. Changes apply for this session; use the launcher
for persistent choices. Existing game saves and historical character metadata
retain the migration behavior documented in the README.

| Feature | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| skin | Original | Tan | Deep | Light |
| hair | Original | Black | Auburn | Silver |
| eyes | Original | Blue | Green | Amber |
| coat | Original | Rust | Blue | Olive |
| scarf | Original | Blue | Red | Green |
| trousers | Original | Charcoal | Blue | Olive |
| boots | Original | Black | Tan | Olive |
| gear | Original | Charcoal | Tan | Olive |

Tints preserve the baked surface detail and relative shading of companion
materials (for example coat and collar). Eye whites, pupils, metal and other
unmapped materials retain their authored appearance. Palette zero uses an exact
identity multiplier. Runtime operations convert the baked sRGB colour to linear
RGB for tinting and back before the existing lighting path.

## Wire contract

Presence uses protocol v6, with distinct v7 tags for group pose/events/ACKs. All
clients must update together; v5/v6-group clients cannot join this vocabulary.
Presence carries magic, sender, session and sequence as four little-endian u32s;
emote, emote counter, typing/speaking bits and padding as four bytes; then this
appearance record; then the existing u32 checksum (36 bytes total).

| Byte | Meaning |
|---|---|
| 0 | Descriptor version, exactly 1 |
| 1–8 | skin, hair, eyes, coat, scarf, trousers, boots, gear; each 0–3 |
| 9–11 | Reserved; must all be zero |

The record is explicitly encoded; native struct layout is never transmitted.
The stable default is version 1, eight zero choices and three zero reserved bytes.
Local and remote choices above 3 clamp to 3. Wrong lengths, versions or reserved
bytes reject the whole presence before advancing its sequence. Checksums, roster,
session and sequence protections continue to apply. Presence changes send at once
and repeat every second, restoring appearance after loss or a late join. Appearance
persists when typing/speaking expire; a new peer session has its own sequence.

The simulator changes appearance alongside movement, including raw byte values
at its local API, and checks convergence after loss, corruption, replay and restart.
Dedicated native contracts exercise all byte values for every feature, malformed
records and strict config text, with eleven broken twins. The asset-label loader and
asset release gate have independent malformed-data tests and mutants.

## Asset pipeline and budget

`models/src/adventurer.py` accepts `--appearance skin=1,hair=2,coat=3` for preview
variants. Production assets must be generated with the default appearance to avoid
applying a tint twice. `models/src/appearance.py` supplies material regions and
palettes, checked against the runtime palette by `tools/test_appearance.py`.
The generator records a scalar `_APPEARANCE` vertex attribute (0: unchanged;
1–8: the feature order above) before joining parts and baking one shared atlas.
This avoids guessing region identity from texture colours. The loader validates
labels; the renderer uses per-draw constants so players share one mesh and texture.

The budget gate requires all nine regions, whole triangles within one region,
matching scalar/position counts, a colour atlas at most 2048×2048 (with at most a
normal map and a material map beside it, each at most 2048×2048), at most 100,000
triangles and at most 9 MiB per GLB. `models/build.sh adventurer` checks this before
replacing the bundled asset. The old asset remains loadable for the default look.

| Measured asset | Vertices | Triangles | Texture | GLB bytes |
|---|---:|---:|---|---:|
| Seed before appearance labels | 57,615 | 80,668 | one 2048×2048 JPEG | 4,503,988 |
| Regenerated labelled baseline | 57,615 | 80,668 | one 2048×2048 JPEG | 4,739,328 |
| Refined asset | 59,750 | 82,780 | one 2048×2048 JPEG | 4,903,964 |
| Living asset (eyelids, eye pivots, scarf and gear bones, fidgets, normal and material maps) | 59,740 | 82,356 | 2048 JPEG colour + 1024 PNG normal + 1024 PNG material | 7,894,920 |

The refinement adds modeled cheek/nose detail and nostrils, six hair locks,
satchel strap holes and boot welts, with finer original hair and leather surface
detail. It stays within the loader's 60,000-vertex limit and adds 2,112 triangles
and 164,636 GLB bytes over the labelled baseline, without another atlas or draw.
No frame-rate improvement is claimed. The native vertex structure grows from 56
to 60 bytes, and per-frame constants from 192 to 432 bytes (the camera, the
lighting and the shadow plane); no extra animation state or texture is required for
palettes. The palette tints are applied in linear light before the lit shading
([docs/LIFE.md](LIFE.md)). Before/after front, three-quarter, face, hand, back and
walking previews have been inspected.

## Verification

Game-free checks:

```sh
make -C proxy/core test HUB=<hub checkout>
make -C proxy/progress test
python3 tools/test_check_model.py
python3 tools/test_idle_seam.py assets/models/adventurer.glb
python3 tools/test_run_pose.py assets/models/adventurer.glb
python3 tools/check_appearance_asset.py assets/models/adventurer.glb
```

With Blender available, also run `models/build.sh adventurer`,
`tools/test_face_fit.sh`, `tools/test_cloth_fit.sh`, `tools/test_strap_fit.sh`,
`tools/test_material_bake.sh` and `tools/test_gait_contact.sh`. Inspect the default
and each of the 24 nondefault feature choices in previews. Compare default/variant
positions, normals, weights and animation accessors; only colours may change.

A verifier with Windows and isolated game clients can use:

```sh
make -C proxy
make -C launcher test lobby-test launcher
make -C proxy/core transport-test
tools/qa.sh model adventurer
tools/qa.sh emote
tools/qa.sh social
tools/qa.sh pause
tools/qa.sh launcher
tools/qa_group.sh 4
tools/qa_group_render.sh
tools/showcase.sh 2 hero
```

Also exercise every Appearance dropdown, Apply/reopen and launch; invalid/missing
config values; session commands for all features; two players with different
palettes; late join, reconnect and packet loss; and three players with independent
palettes. `netstate` writes received choices to `witness_net.json`, and the render
bridge writes submitted choices to `witness_models.json`. Assert both records agree
per player, inspect actual renderer screenshots of all features (especially eyes
and skin), and verify real save-folder hashes remain identical. The bridge records
CPU inputs; screenshots are still required to verify shader output.

The two-client appearance scenario and its launcher checks passed with distinct
palettes, live coat/scarf changes, invalid-input rejection, Apply/reopen and
legacy fallbacks; the in-game and launcher captures were inspected and real save
hashes were unchanged. The model/adventurer scenario also passed, including
legacy model-name migration and its network-protection mutants. Not every broader
regression or optional live exercise listed above was run.
