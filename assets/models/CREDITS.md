# Credit for the bundled character

| File | Model | Author | Licence |
|---|---|---|---|
| `adventurer.glb` | The adventurer: a traveller with a coat, scarf, hat and handmade leather gear | original work made for this mod: `models/src/adventurer.py` (Blender, `models/build.sh`), nothing taken from elsewhere | CC0 1.0 (public domain): use it for anything |

CC0 1.0: https://creativecommons.org/publicdomain/zero/1.0/legalcode

The former Cesium Man, Rigged Figure and Fox sample assets were removed; no other code or asset uses them. The proxy still uses the independently licensed cgltf and stb_image libraries, whose own notices remain in `proxy/third_party/`.

`adventurer.glb.txt` records the model's fit: `eye_joint=eye`, `speed=1.20` and `run_speed=2.80` metres per second. Animation clips are named idle, walk and run; the proxy cross-fades between them over 200 ms.

The palette definitions and original material-region mapping in `models/src/appearance.py` are also original work for this mod, CC0 1.0. The refined face, modeled hair locks, strap holes, boot welts and surface microtexture are original additions in the same generator and licence. No new external art or textures are used.
