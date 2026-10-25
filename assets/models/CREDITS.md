# Credits for the bundled character models

All three come from the Khronos glTF Sample Assets (https://github.com/KhronosGroup/glTF-Sample-Assets) and are unmodified.
They are used as stand-ins for the other player. Each one needs its credit kept wherever the file is redistributed.

| File | Model | Author | Licence |
|---|---|---|---|
| `cesium-man.glb` | Cesium Man | Cesium | Creative Commons Attribution 4.0 International (the Cesium logo on it is a trademark of Cesium) |
| `rigged-figure.glb` | Rigged Figure | Cesium | Creative Commons Attribution 4.0 International |
| `fox.glb` | Fox | PixelMannen (model, CC0 1.0), tomkranis (rigging and animation, CC BY 4.0), @AsoboStudio and @scurest (glTF conversion, CC BY 4.0) | CC0 1.0 for the mesh, CC BY 4.0 for the rest |

CC BY 4.0: https://creativecommons.org/licenses/by/4.0/legalcode  CC0 1.0: https://creativecommons.org/publicdomain/zero/1.0/legalcode

## Adding your own model

Any glTF binary (`.glb`) with embedded data works: skinned meshes (several meshes and several skins on one skeleton are fine) with or without
animations, one base-colour texture (PNG or JPEG) for the model plus flat colours (a material's base colour factor) for everything else.
Put it in `assets/models/`, optionally with a `<file>.glb.txt` next to it, and add its credit here. Only use models whose licence lets you
redistribute them; this repository is public. Models that may not be shared go into a folder of your own (`WITNESS_EXTRA_MODELS=<folder>`
when you run the tools; the showcase uses them too).

Keys of `<file>.glb.txt`: `height=<metres>`, `speed=<m/s the walk clip was made for>`, `run_speed=<m/s the run clip was made for>`,
`yaw=<degrees>`, and `eye_joint=<part of a joint name>` (with `eye=<metres>`, default the camera's 1.71): size the model so that those
joints (its eyes) are at that height above the feet instead of using `height=`.

Animation clips are picked by name: a clip containing `idle`, `stand` or `survey` is the idle, `walk` the walk, `run` or `sprint` the run;
a model with a single unnamed clip uses it as the walk and stands in its first frame. The proxy cross-fades between the clips (200 ms).
