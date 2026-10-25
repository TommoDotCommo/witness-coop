# witness-coop showcase

Pictures of the current features, taken by `tools/showcase.sh 8`: eight real copies of the game in one full-mesh group on one PC, each player drawn with
a different character model, driven through every scene by the proxy's own commands, the pictures taken with the game's own screenshot command
(`shot`, the back buffer: no desktop capture). Everything on this page was seen in those pictures; what is not in them is not claimed.

The pictures are not part of the repository (they are pictures of the game): run the script to make them, they land in `showcase/` next to this file.

## Eight players, eight models

![The garden, seen by player 1](showcase/lineup_a.png)

Player 1 looks at the other seven: Xbot, Robot, Rigged Figure, Michelle, Magic Frog, Fox and Cesium Man, each a different skinned, animated
character, standing in the garden around the stone pillar. Every name tag has its own colour and shows a ping (round trip to that player).
The "[V] watch ... view" line appears while the cross-hair is on a player (see the screens section below).

![The same garden, seen by player 2](showcase/lineup_b.png)

Player 2's view of the same group: player 1, drawn as Brainstem, is in this picture. Together the two pictures show all eight models.

`model_by_player=1` in each game's `witness_coop.cfg` does this: player *i* is drawn with the *i*-th model of `witness_models\` (alphabetical), the same
in every game. Without it every game draws every other player with one model of its own choosing (F6 / F7 switch it).

## Walking

![The seven others walking toward the camera](showcase/walk.png)

All seven hold the forward key. Each model plays its own walk or run clip at the speed the other game measured; Michelle (whose file has a dance and a T-pose clip, no walk) dances,
the Fox trots, Cesium Man strides, the Magic Frog walks. A model without a clip for a state stands in its first frame.

## Watching another player's screen

![The player list](showcase/pov_roster.png)

F5 opens the player list (names in their colours, pings; Up / Down, Enter to watch, Backspace to clear). V on the player you look at, `]` and `[` cycle.

![Watching Fox's screen](showcase/pov_watch.png)

A backup view: the inset is the other game's own picture, streamed only because this game asked for it (requests last 3 s and are renewed while you watch).

## The dev menu

![Map](showcase/devmenu_map.png)

F8 opens the dev menu. The map page shows every panel of the island (green solved, amber powered, grey off), the doors, and all the players; the left list groups
the panels into spots; the selected panel's links and actions are below it.

![Nearby puzzles](showcase/devmenu_nearby.png)

The nearby page lists the puzzles within a radius with what they power (`-> cable`, `-> Pylon`, `-> Door`), with Teleport, Solve, Run its target and Light cable.

![Saves](showcase/devmenu_saves.png)

The saves page makes save points (a copy of the game's own save, restored through the game's pause menu) and presets (progress to start from, "complete the first
N of 31 spots"). `F3` toggles the panel mouse boost (the circle on a puzzle follows the real mouse):

![The boost message](showcase/boost_toast.png)

## The pause menu

![Playing](showcase/pause_0_playing.png)
![Paused](showcase/pause_1_menu.png)

While the game's pause menu is open the proxy draws nothing of its own over it: no puppets, no name tags, no lines. (The proxy reads two values the game keeps for the
menu's dimming; they are 0.0 everywhere else, in the dev menu, in a puzzle and after the model keys included. A first attempt used a value that flipped with the model keys
and hid the players every second press; the test now presses them.)

## Shared panels and lines

![The maze, unsolved](showcase/panel_0_before.png)
![Player 2 draws](showcase/panel_draw_2.png)
![Solved in both games](showcase/panel_2_solved.png)

Player 2 draws the cable-driving maze with real mouse input (through the game's own input path); player 1, standing in front of it, sees the blue line appear on the panel
while it is drawn. When the line is finished the panel is solved in both games and the cable lights up: panel and power state travel as reliable events, lines are drawn from
what the other game sends.

## Chat, typing dots, pings, voice

![Chat, typing dots and pings](showcase/social.png)

Seven players speak at once: each line floats over its player and is in the log at the bottom left. The player with the three green dots over the head is typing. The poles
with a name and a distance are pings (the sender's colour); the arrow at the bottom is a ping behind the camera. The bars beside some names are the players who are talking
(here the even players, a test voice instead of a microphone), and the line at the bottom left is this player's own microphone (`hold N to talk`).

![Voice](showcase/voice.png)

## Emotes and characters

![Eight emotes at once](showcase/emotes.png)

Spin, bow, no, yes, cheer, dance, wave, each with its name over the head: a model's own clip when it has one, otherwise a procedural motion (a hop, a nod, a turn).

![The emote wheel](showcase/emote_wheel.png)

Hold G and move the mouse toward a slot (the up-right one, Dance, is lit); release plays it. The camera does not turn while the wheel is open.

![The character list](showcase/character_list.png)

C opens the list of models; the one you choose is what everyone else sees you as.

## Drawing

![Three players drawing](showcase/drawing.png)

X draws under the cross-hair, Z turns the mouse into a pen; the ink is put on the surface under the pen (the game's own scene depth) and drawn as ribbons in the world, so a
player standing in front of a line hides it, as the red circle shows. Everyone sees it as it is drawn.

## The map picture

![The dev menu map with the island under it](showcase/map_picture.png)

The dev menu's map (F8) with a picture of the island under the panels and doors. It was made by the button at the top right: the player is flown over the island looking forward and
down (the game does not allow looking straight down: its pitch stops at 53 degrees), 103 frames are rectified onto the ground and stitched. Nothing of the mod is drawn
into those frames. The picture stays on the machine that made it (`witness_map\`).

## The launcher

![Play](showcase/launcher_play.png)
![Video](showcase/launcher_video.png)
![Sound and voice](showcase/launcher_sound.png)
![Game options](showcase/launcher_options.png)
![Saves](showcase/launcher_saves.png)

Every option outside the game: the game's own video file, the sound and voice settings, the options that live inside a save (mouse speed, field of view, ...), and the saves, each with
the players who were in the game and how far it got.

![New game](showcase/launcher_newgame.png)

New game: tick the parts of the map that are done; the window says how many panels that is (103 here: Desert, Keep and Town) and what else the game needs first (the tutorial gate's
8 panels and one from the jungle), and the game starts with exactly those solved, in the order it needs them.

## Listen

The voice is not in a picture: `proxy/voice/voice_demo.cpp` writes WAV files of what the mixer makes of one (synthetic) voice at 3, 10, 22, 28 and 80 m and walking away (3 to 70 m):
`g++ -std=c++17 -O2 proxy/voice/voice_demo.cpp proxy/voice/voice.cpp -o voice-demo && ./voice-demo <folder>`.

## Models

Three open-licence models are bundled (`assets/models/CREDITS.md`). The others in these pictures are not part of the repository:

| Model | Source and licence |
|---|---|
| Cesium Man, Fox, Rigged Figure | Khronos glTF sample assets, bundled, credits in `assets/models/CREDITS.md` |
| Robot (RobotExpressive) | Tomas Laulhe, CC0 1.0, modified by Don McCurdy; `tools/get_showcase_models.sh` downloads it |
| Xbot, Michelle | Mixamo characters as they come with the three.js examples; Mixamo's terms apply and I have not checked them for redistribution, so they stay out of the repository |
| Brainstem | (c) Smith Micro, Poser EULA (the Khronos sample asset): not redistributable here |
| Magic Frog | Sketchfab, by David Glynch ("Free Standard" licence): for your own use; `tools/frog_gait.py` adds walk and run clips |

Put the files you may use in `$WITNESS_EXTRA_MODELS` (default `/mnt/c/coop/lab/run/showcase-models`) with a `<name>.glb.txt` (`height=1.8`). With fewer than eight models the
script reuses them and says so.

## Run it

```
tools/get_showcase_models.sh      # the one extra model that may be shared
tools/showcase.sh 8               # all scenes; tools/showcase.sh 5 lineup,walk for some
```

It leases N game slots from the lab, builds N instance folders, starts the games, teleports them to the garden, and writes the pictures and `showcase.json` (the
captions) to `showcase/`. Eight games need about 11 GB of free memory (about 1.3 GB each, measured) plus the lab's 3 GB reserve; it refuses to start with less.
`showcase.md` and the scenes live in `tools/showcase.py` and `tools/win/wshowcase.py`.
