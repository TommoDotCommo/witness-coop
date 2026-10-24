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

Any glTF binary (`.glb`) with embedded data works: one skinned mesh with or without animations, a single base-colour texture (PNG or
JPEG). Put it in `assets/models/`, optionally with a `<file>.glb.txt` next to it (`height=<metres>`, `speed=<m/s the walk clip was made
for>`, `yaw=<degrees>`), and add its credit here. Only use models whose licence lets you redistribute them; this repository is public.
Animation clips are picked by name: a clip containing `idle`, `stand` or `survey` is the idle, `walk` the walk, `run` or `sprint` the run;
a model with a single unnamed clip uses it as the walk and stands in its first frame.
