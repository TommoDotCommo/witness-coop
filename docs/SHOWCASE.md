# witness-coop showcase

Co-op for **The Witness**: you and your friends in the same world, seeing each other, talking, drawing and racing. These are real pictures of the game with the mod, taken by
`tools/showcase.sh` from copies of the game running on one PC: three games at a time, so the groups in the pictures have three players. The player names are the test
harness's (Fox, Owl, Heron), the games use the adventurer, the only character, and the voice bars come from a test tone instead of a microphone. Everything on this page was
seen in these pictures; what is not in them is not claimed. [Player guide](../README.md) · [All features and how each is tested](../features.md)

No game files are in this repository: every player needs their own copy of The Witness. The pictures are of the game as it runs with the mod.

![Two players in the garden: name tags with the ping, a voice bar beside the one who is talking](showcase/lineup_a.jpg)

## The launcher

The launcher has four tabs: Play, Saves, Settings and About. It is drawn in the look of the game: the newest save's picture as the banner (a drawn puzzle panel when there is no
save yet), the game's typeface and its yellow for the main button and the selection.

![Play: your name, Host a game, Join a friend, Play solo, and the button Open lobby](showcase/launcher_play.jpg)

**Play** has your name, three tiles (Host a game, Join a friend, Play solo) and one button that says what it does. Here Host a game is chosen, through Steam or direct, and the
button reads *Open lobby*.

![The lobby: players, chat, the invite, what to play](showcase/launcher_lobby.jpg)

Hosting or joining turns Play into the **lobby** (the tab is called Lobby). Three columns: who is here (a lit dot beside the name of whoever is talking, an amber one for a player who has
said they are ready, *Mute me* and *Deafen* below), the chat, and the invite (*Copy invite*; the address in the picture is the test harness's loopback address) above *What to play*,
which the host chooses: co-op or a race, a new game, parts of the map already done. The host's *Start game* starts it when everybody is ready; when the games end everybody is back in
the lobby.

![Saves: each save with its players, progress, lasers and time, a picture, New game](showcase/launcher_saves.jpg)

**Saves** lists each save with the players who were in the game and how far it got, with the picture of the save, and has *New game*, *Back up*, *Move to backup folder*, *Open folder*
and *Refresh*.

![New game: tick the parts of the map that are done](showcase/launcher_newgame.jpg)

*New game*: tick the parts of the map that are done; the window says how many panels that is and what else the game needs first, and the game starts with exactly those solved.

**Settings** has the sections Co-op rules, Players and interface, Voice and sound, Controls, Display, Network and Launcher. A setting is saved when it changes, and *Reset section*
puts a section back.

| | |
|---|---|
| ![Settings, Players and interface](showcase/launcher_players.jpg) | ![Settings, Voice and sound](showcase/launcher_sound.jpg) |
| Players and interface: your name, the adventurer's colours (skin, hair, eyes, coat, scarf, trousers, boots, gear), see-through players, the third-person view when emoting, the developer menu. | Voice and sound: voice chat (here Push to talk), the push-to-talk key by pressing a key, volumes, hearing range. |
| ![Settings, Controls](showcase/launcher_keys.jpg) | ![Settings, Display](showcase/launcher_display.jpg) |
| Controls: every co-op key by pressing a key, and the pad's View-button layer on or off, with what each press does. | Display: the game's own Launcher.variables (full screen, resolution, frame limit, quality) and the options that live inside a save (brightness, field of view, reticle, vignetting). |
| ![Settings, Network](showcase/launcher_network.jpg) | ![About](showcase/launcher_about.jpg) |
| Network: the lobby and game ports and the address friends reach this PC at (the addresses are the harness's loopback addresses). | About: version, *Check for updates*, the quick guide, *Send diagnostics*, project page, release notes, the game folder (a generic path replaces this PC's in the picture) and credits. |

## In the world

![The garden, seen by player 1](showcase/lineup_a.jpg)

Every player is drawn as the adventurer, with a name tag in the player's own colour and the ping (round trip to that player). The bars beside a name show who is talking.
The text of the co-op overlay is in the game's own typeface.

### See-through players

| Another player, three metres away | The same player in the camera player's body |
|---|---|
| ![Solid](showcase/ghost_solid.jpg) | ![A ghost](showcase/ghost_overlap.jpg) |

A player who overlaps your body fades to a ghost, so the view is not blocked; a step away and it is drawn normally again.

### Chat, typing dots, pings

![Chat, typing dots and pings](showcase/social.jpg)

Players speak: each line floats over its player and is in the log at the bottom left (`T` opens the chat, Enter sends). The player with the three dots over the head is typing.
The poles with a name and a distance are pings, in the sender's colour; the arrow at the bottom is a ping behind the camera. The line at the bottom left is this player's own
microphone (`hold B to talk`).

![Notice, chat lines and name tags](showcase/ui_hud.jpg)
![Typing a chat line](showcase/ui_chat.jpg)

A notice (here a test notice, "Fox joined"), the chat lines and a chat line being typed, in the game's typeface.

### The player panel

![The player panel](showcase/ui_panel.jpg)

Holding Tab opens the player panel: one row per player with the colour, name, ping, speaking light and distance, and the buttons Watch, Mute and `-` `+` for the player's voice volume;
below, voice volume, name tags, partner picture and Leave. Releasing Tab closes it. The panel is for the mouse; its footer lists the keys (arrows, Enter, M, L, Esc).

![Watching another player's screen](showcase/pov_watch.jpg)

*Watch* shows the other player's own screen in a box (streamed only because this game asked for it). The line under the box in this picture is the key reminder as the build
draws it today; it still names the old keys (V clear, F5 list), which the quick guide corrects: the watch keys are `[` and `]`, and Watch is a button in the player panel.

### Emotes

![Emotes at the same moment](showcase/emotes.jpg)

Two players play an emote at the same moment, each with its name over the head (here Dance and Wave).

![The emote wheel](showcase/emote_wheel.jpg)

Hold `H` and move the mouse toward a slot (Dance is lit); release plays it. The wheel shows eight slots here: Wave, Shrug, Dance, Cheer, Yes, No, Bow, Spin.

![Playing an emote yourself: the view behind the adventurer](showcase/emote_self_1.jpg)

When you play an emote yourself, the view moves back behind you and a little above, so you see your own adventurer; the other players see nothing different.

### Voice

![Proximity: the label says so](showcase/voice.jpg)

Who hears you is the voice channel, shown in the microphone line at the bottom left. `Y` steps through the channels. Proximity (the default) is heard in the world, louder when close,
and turns into a radio sound far away. The bars beside a name are the players who are talking (here a test tone instead of a microphone).

| Open line | One on one |
|---|---|
| ![Open line](showcase/voice_global.jpg) | ![One on one](showcase/voice_direct.jpg) |

Open line: everybody hears you as you sound, as loud far away as next to you. One on one: only the named player hears you (the label says *Private: Owl*); the voice is not sent to anybody
else.

### Drawing

![Two kinds of ink on the ground](showcase/drawing.jpg)

`X` draws under the cross-hair, `Z` turns the mouse into a pen; the ink is put on the surface under the pen and drawn as ribbons in the world, in the player's colour. Everyone sees it
as it is drawn.

### The quick guide

![The quick guide](showcase/ui_guide.jpg)

Hold `F1` for the quick guide, in the game and in the launcher: your current bindings, the fixed keys, the pad layer, how it works, troubleshooting.

### The pause menu

| Playing | Paused |
|---|---|
| ![Playing](showcase/pause_0_playing.jpg) | ![Paused](showcase/pause_1_menu.jpg) |

While the game's pause menu is open the mod draws nothing over it: no player, name tag or line.

## The shared keys

The keys are the same in every co-op mod, and every one can be changed in the launcher (Settings, Controls; the guide above shows the bindings in use). In these pictures: `T` text chat,
`V` or the middle mouse button ping, `B` (or the mouse side button) push to talk, hold `H` the emote wheel, hold `Tab` the player panel, `[` and `]` watch the previous or next player,
`Y` the next voice channel, hold `F1` the quick guide.

## On a gamepad

With the pad layer on, the View button is the co-op button: tap to ping, hold for the player panel, and while it is held the D-pad opens the emote wheel (up), chat (down) and watches the
previous or next player (left, right). The pictures were made with a simulated pad (a test command, not a real controller), so the key reminder at the bottom right still shows
keyboard text.

| Hold View: the player panel | View + D-pad up: the emote wheel | View + RB: quick chat |
|---|---|---|
| ![Player panel](showcase/pad_panel.jpg) | ![Emote wheel](showcase/pad_emote.jpg) | ![Quick chat wheel](showcase/pad_quick.jpg) |

The panel's footer reads *Right stick: point, RT or A: click, Let go of View: close*. The wheels say *right stick picks, let go of View plays* (emotes) and *let go of View sends*
(quick chat: Ready, Wait for me, Come look at this, Over here, Thanks).

## A race

![The countdown](showcase/race_countdown.jpg)
![The race: the clock, the goal and the standings](showcase/race_running.jpg)
![The winner](showcase/race_winner.jpg)

A game for two or more where nothing is shared: everybody plays their own copy of the world, sees the others, and chats and talks, but a panel one player solves is not solved in
anybody else's world. Everybody waits until all the worlds are loaded, then a countdown ends in the same GO for all. The clock, the goal (here: solve three panels) and the standings
are on screen; the first to the goal wins, and the banner shows the winner and the time while the other player is still racing. The host chooses the race in the lobby (*Game*,
*Race goal*).

## The developer menu

The developer menu is for people who work on the mod, and only exists when it is switched on in the settings file (`dev_menu=1`). `F8` opens it.

![Map](showcase/devmenu_map.jpg)

The map page shows every panel of the island with a picture of the island under it, and the doors and the players; the left list groups the panels into spots; the selected panel's links
and actions are below it. The picture of the island is made by the button at the top right and stays on the machine that made it.

![Nearby puzzles](showcase/devmenu_nearby.jpg)

The nearby page lists the puzzles within a radius with what they power, with *Teleport to it*, *Solve (and run target)*, *Run its target* and *Open target door* for the selected one.

![Saves](showcase/devmenu_saves.jpg)

The saves page makes save points (a copy of the game's own save, restored through the game's pause menu) and presets (progress to start from). `F3` toggles the panel mouse boost
(the circle on a puzzle follows the real mouse):

![The boost message](showcase/boost_toast.jpg)

## Experimental (off by default)

Two feature families are behind options that are off by default and apply at the next start: the back TV family (TV, sky TV, audience seats) and the handheld radio with its voice channel.
They are not shown in pictures here. The options are in the launcher under Settings, Launcher:

![Settings, Launcher: the experimental options are off](showcase/launcher_launcher.jpg)

The same page has *Show the launcher at start*, *Check for updates when the launcher starts* and *Offer pre-releases*. The two keys of the experimental families (R and J, K)
are listed in Controls with "only with Experimental" under them.

## Listen

The voice is not in a picture: `proxy/voice/voice_demo.cpp` writes WAV files of what the mixer makes of one (synthetic) voice at 3, 10, 22, 28 and 80 m and walking away (3 to 70 m):
`g++ -std=c++17 -O2 proxy/voice/voice_demo.cpp proxy/voice/voice.cpp -o voice-demo && ./voice-demo <folder>`.

## The character

Only the original [adventurer](../assets/models/CREDITS.md) is bundled. The adventurer as it looks now (its life layer, lit shading and shadow) is pictured in [docs/LIFE.md](LIFE.md).

## Run it

```
tools/showcase.sh 3                          # every scene except the race; tools/showcase.sh 3 lineup,social for some
SHOWCASE_CFG_EXTRA='game_mode=race\nrace_goal=count:3\nrace_countdown_ms=60000' tools/showcase.sh 2 race
tools/showcase_publish.sh                    # the JPEGs of docs/showcase/ from the PNGs of showcase/ (scaled to 1366 px wide)
```

It leases N game slots from the resource provider, builds N instance folders, starts the games, teleports them to the garden, and writes the pictures and `showcase.json` (the captions)
to `showcase/` (not in git). `tools/showcase_publish.sh` makes the small JPEGs that are in git, in `docs/showcase/`. A game needs about 1.3 to 3.8 GB of free memory (measured) plus the
resource provider's reserve; the script refuses to start with less. The scenes live in `tools/showcase.py` and `tools/win/wshowcase.py`.
