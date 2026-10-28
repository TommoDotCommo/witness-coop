# witness-coop showcase

These are historical captures made before the adventurer became the only character. They document the social features, not the current character roster. New runs of `tools/showcase.sh` use adventurers; fresh captures are pending.


Co-op for **The Witness**: you and your friends in the same world, seeing each other, talking, drawing, and now also racing. These are real pictures of the game with the
mod, taken by `tools/showcase.sh` from copies of the game running on one PC (the player names are the test harness's: the models' names). Everything on this page was seen in these
pictures; what is not in them is not claimed. [Player guide](../README.md) · [All features and how each is tested](../features.md)

No game files are in this repository: every player needs their own copy of The Witness. The pictures are of the game as it runs with the mod.

![Seven players in the garden, each a different character, with name tags and pings](showcase/lineup_a.jpg)

## The lobby

![The launcher's lobby: the invite, who is here, the chat, what to play](showcase/launcher_lobby.jpg)

Before the game starts everybody meets in the launcher. One player hosts and sends the invite (address, port and a code); the others paste it and join. The list shows who is
here: a lit dot beside the name of whoever is talking (the host, here), an amber one for a player who has said they are ready. Chat is by text and by voice (a microphone, push to
talk or open, with mute and deafen; the host relays it, so only the host's port needs to be reachable). The host chooses what to play, co-op or a race with its goal, a new game with
parts of the map already done, and starts it when everybody is ready: a countdown, then every launcher writes its game's settings from the same list of players and starts the game.
When the games end everybody is back in the lobby, with the result of the race.

## A race

![The countdown, both players at 0 of 3](showcase/race_countdown.jpg)
![The race: the clock, the goal and the standings](showcase/race_running.jpg)
![The winner](showcase/race_winner.jpg)

A game for two or more where nothing is shared: everybody plays their own copy of the world, sees the others run about, and chats and talks, but a panel one player solves is not
solved in anybody else's world and no door opens for them. Everybody waits until all the worlds are loaded, then a countdown ends in the same GO for all. The clock, the goal (here:
solve three panels, or a given panel such as the mountain's last box) and the standings are on screen; the first to the goal wins, and the banner shows the winner and the time while
the other player is still racing.

## See through players

![A player stands between the camera and the panel and is a ghost](showcase/ghost_in_way.jpg)
![The same player a step to the side](showcase/ghost_aside.jpg)

A player who stands between your camera and a puzzle panel you look towards (or right in your face) fades to a ghost, so the puzzle can be read through them: the line on the
panel shows through the head. A step to the side and they are drawn normally again.

## Voice channels

![Proximity: the label says so](showcase/voice.jpg)

Who hears you, and how, is the channel, shown in the microphone line at the bottom left. **H** steps through them. Proximity (the default) is heard in the world, louder when close, and
turns into a walkie-talkie far away. The bars beside a name are the players who are talking (here the even players: a test voice instead of a microphone).

| Radio | Open line | One on one |
|---|---|---|
| ![Radio](showcase/voice_radio.jpg) | ![Open line](showcase/voice_global.jpg) | ![Private voice](showcase/voice_direct.jpg) |

Radio: everybody hears you at any distance, with the walkie-talkie sound. Open line: everybody hears you as you sound, as loud far away as next to you. One on one: only that
player hears you (over the radio): the voice is not even sent to anybody else.

## Eight players, eight models

![The garden, seen by player 1](showcase/lineup_a.jpg)

Player 1 looks at the other seven: Xbot, Robot, Rigged Figure, Michelle, Magic Frog, Fox and Cesium Man, each a different skinned, animated
character, standing in the garden around the stone pillar. Every name tag has its own colour and shows a ping (round trip to that player).
The "[V] watch ... view" line appears while the cross-hair is on a player (see the screens section below).

![The same garden, seen by player 2](showcase/lineup_b.jpg)

Player 2's view of the same group: player 1, drawn as Brainstem, is in this picture. Together the two pictures show all eight models.

Current builds draw every player as the adventurer, identified by their name and player colour.

## Walking

![The seven others walking toward the camera](showcase/walk.jpg)

Players hold the forward key. Current builds play the adventurer walk or run clip at the measured speed.

## Watching another player's screen

![The player list](showcase/pov_roster.jpg)

F5 opens the player list (names in their colours, pings; Up / Down, Enter to watch, Backspace to clear). **V** watches the player under the cross-hair; with nobody there it watches the only other
player, or opens this list when there are several; `]` and `[` cycle. A reminder is at the bottom right of the screen.

![Watching another player's screen](showcase/pov_watch.jpg)

A backup view: the box is the other game's own picture, streamed only because this game asked for it (requests last 3 s and are renewed while you watch), with the keys under it.

## The dev menu

![Map](showcase/devmenu_map.jpg)

F8 opens the dev menu. The map page shows every panel of the island (green solved, amber powered, grey off), the doors, and all the players; the left list groups
the panels into spots; the selected panel's links and actions are below it.

![Nearby puzzles](showcase/devmenu_nearby.jpg)

The nearby page lists the puzzles within a radius with what they power (`-> cable`, `-> Pylon`, `-> Door`), with Teleport, Solve, Run its target and Light cable.

![Saves](showcase/devmenu_saves.jpg)

The saves page makes save points (a copy of the game's own save, restored through the game's pause menu) and presets (progress to start from, "complete the first
N of 31 spots"). `F3` toggles the panel mouse boost (the circle on a puzzle follows the real mouse):

![The boost message](showcase/boost_toast.jpg)

## The pause menu

![Playing](showcase/pause_0_playing.jpg)
![Paused](showcase/pause_1_menu.jpg)

While the game's pause menu is open the proxy draws nothing of its own over it: no puppets, no name tags, no lines. (The proxy reads two values the game keeps for the
menu's dimming; they are 0.0 everywhere else, in the dev menu, in a puzzle and after the model keys included. A first attempt used a value that flipped with the model keys
and hid the players every second press; the test now presses them.)

## Shared panels and lines

![The maze, unsolved](showcase/panel_0_before.jpg)
![Player 2 draws](showcase/panel_draw_2.jpg)
![Solved in both games](showcase/panel_2_solved.jpg)

Player 2 draws the cable-driving maze with real mouse input (through the game's own input path); player 1, standing in front of it, sees the blue line appear on the panel
while it is drawn. When the line is finished the panel is solved in both games and the cable lights up: panel and power state travel as reliable events, lines are drawn from
what the other game sends.

## Chat, typing dots, pings

![Chat, typing dots and pings](showcase/social.jpg)

Seven players speak at once: each line floats over its player and is in the log at the bottom left. The player with the three green dots over the head is typing. The poles
with a name and a distance are pings (the sender's colour); the arrow at the bottom is a ping behind the camera. The bars beside some names are the players who are talking
(here the even players, a test voice instead of a microphone), and the line at the bottom left is this player's own microphone (`hold N to talk`).

## Emotes

![Eight emotes at once](showcase/emotes.jpg)

Spin, bow, no, yes, cheer, dance, wave, each with its name over the head: a model's own clip when it has one, otherwise a procedural motion (a hop, a nod, a turn).

![The emote wheel](showcase/emote_wheel.jpg)

Hold G and move the mouse toward a slot (the up-right one, Dance, is lit); release plays it. The camera does not turn while the wheel is open.


## Drawing

![Three players drawing](showcase/drawing.jpg)

X draws under the cross-hair, Z turns the mouse into a pen; the ink is put on the surface under the pen (the game's own scene depth) and drawn as ribbons in the world, so a
player standing in front of a line hides it, as the red circle shows. Everyone sees it as it is drawn.

## The map picture

![The dev menu map with the island under it](showcase/map_picture.jpg)

The dev menu's map (F8) with a picture of the island under the panels and doors. It was made by the button at the top right: the player is flown over the island looking forward and
down (the game does not allow looking straight down: its pitch stops at 53 degrees), 103 frames are rectified onto the ground and stitched. Nothing of the mod is drawn
into those frames. The picture stays on the machine that made it (`witness_map\`).

## The launcher

![Play](showcase/launcher_play.jpg)
![Video](showcase/launcher_video.jpg)
![Sound and voice](showcase/launcher_sound.jpg)
![Game options](showcase/launcher_options.jpg)
![Saves](showcase/launcher_saves.jpg)

In the look of the game: a dark panel, the pale line of a puzzle and the cyan of one that has power, a start circle for a tick. Every option outside the game: the game's own video file,
the sound and voice settings (and the voice channel), the options that live inside a save (mouse speed, field of view, ...), and the saves, each with the players who were in the game and how
far it got. The first tab is the lobby, above.

![New game](showcase/launcher_newgame.jpg)

New game: tick the parts of the map that are done; the window says how many panels that is and what else the game needs first (the tutorial gate's panels, a panel from another area), and
the game starts with exactly those solved, in the order it needs them.

## Listen

The voice is not in a picture: `proxy/voice/voice_demo.cpp` writes WAV files of what the mixer makes of one (synthetic) voice at 3, 10, 22, 28 and 80 m and walking away (3 to 70 m):
`g++ -std=c++17 -O2 proxy/voice/voice_demo.cpp proxy/voice/voice.cpp -o voice-demo && ./voice-demo <folder>`.

## Models

Only the original [adventurer](../assets/models/CREDITS.md) is bundled now. The third-party characters visible in these historical pictures are no longer shipped or selectable. Historical image credits: Cesium Man and Rigged Figure by Cesium (CC BY 4.0); Fox by PixelMannen (mesh, CC0), tomkranis (rig/animation, CC BY 4.0), AsoboStudio and scurest (conversion, CC BY 4.0); RobotExpressive by Tomas Laulhe (CC0), modified by Don McCurdy; Xbot and Michelle from Mixamo; Brainstem by Smith Micro (Poser EULA); Magic Frog by David Glynch (Sketchfab Free Standard licence). These image attributions remain because the old screenshots remain; none of those model files is distributed.

## Run it

```
tools/showcase.sh 8               # all scenes except the race; tools/showcase.sh 5 lineup,walk for some
SHOWCASE_CFG_EXTRA='game_mode=race\nrace_goal=count:3\nrace_countdown_ms=60000' tools/showcase.sh 2 race
tools/showcase_publish.sh         # the JPEGs of docs/showcase/ from the PNGs of showcase/
```

It leases N game slots from the resource provider, builds N instance folders, starts the games, teleports them to the garden, and writes the pictures and `showcase.json` (the captions)
to `showcase/` (not in git). `tools/showcase_publish.sh` makes the small JPEGs that are in git, in `docs/showcase/`. Eight games need about 11 GB of free memory (about 1.3 GB each,
measured) plus the resource provider's 3 GB reserve; it refuses to start with less. The scenes live in `tools/showcase.py` and `tools/win/wshowcase.py`.
