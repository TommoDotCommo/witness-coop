# Playing together: chat, pings, emotes, drawing, voice, the launcher

Everything here is in the proxy DLL (`witness_coop.cfg` settings in brackets) and the launcher (`launcher/`). Each feature has an automated check; the scenario that
proves it is named in the last column of the table at the end. Keys can be changed in the cfg or in the launcher.

## In the game

| What | How |
|---|---|
| **Chat** | `T` opens the box, Enter sends, Esc cancels. The log shows bottom left; a player's last line also floats over their head for a few seconds. |
| **Typing dots** | While someone has the chat box open, three animated dots show over their head for everyone. |
| **Ping** | `P` marks the point under the cross-hair (the distance comes from the game's own depth buffer). Everyone sees a pole with the sender's name and the distance; off screen, an arrow at the edge of the screen points the way. |
| **Emote wheel** | Hold `G`, move the mouse toward one of eight emotes (Wave, Dance, Cheer, Yes, No, Bow, Spin, Shrug), release to play it. The camera does not turn. Others see the motion on your figure (a model's own clip when it has one, else a procedural motion) and its name over your head. |
| **Character switcher** | `C` lists the models in `witness_models\`; Up / Down, Enter. Others draw you with it. `character=<name>` in the cfg sets the default. |
| **Free drawing** | Hold `X`: ink goes onto the surface the cross-hair is on (the game's scene depth at that pixel), in your colour, and everyone sees it, in puzzles too. `Z` toggles the canvas: the mouse then moves a pen over the screen (the camera stays), so a puzzle panel can be drawn on precisely. `B` changes ink colour, Backspace in the canvas undoes the last stroke, Shift+`X` clears yours. |
| **Voice chat** | Hold `N` to talk (`voice_mode=ptt`, the default), or `voice_mode=open` for an open microphone with a voice-activity gate. Players near you are heard in the world: louder when close, duller when farther, from the side they are on. Beyond `voice_range` (25 m) the voice turns into a walkie-talkie (band-limited, a little distorted, noise, squelch at the start and end) coming from the middle, fading over from the proximity voice; `voice_radio_range` limits even that, `voice_radio=0` turns it off. A speaking player shows moving bars beside their head; your own microphone state shows bottom left. |
| **Game volume** | `game_volume=<percent>` sets the game's entry in the Windows volume mixer. |
| **Map picture** | The dev menu's map (F8) can have a picture of the island under it. Its "Make a picture of the island" button flies the player over the island looking down and stitches the game's own frames (nothing of the mod is drawn); the picture stays on your machine in `witness_map\`. |
| **New game with progress** | `start_progress=<spec>` in the cfg (the launcher's New game window writes it) completes parts of the map when the next new world is up, in the order the game needs. See below. |

## New game with parts of the map completed

`progress/progression.txt` lists every puzzle panel with the game's own entity id, its area and the full set of panels the game needs solved before it: the panels in front of it in its
own area, the panel behind every door on the way in, the panels that open the way between areas. It is built by `tools/gen_progression.py` from the logic file of the
Archipelago randomizer for The Witness (MIT, https://github.com/ArchipelagoMW/Archipelago). Choosing "Symmetry Island" therefore also solves the tutorial gate and the Glass
Factory, because the game's door to the island is opened by a panel that needs the factory's last panel; choosing "Mountain (Outside)" lights the seven cheapest lasers; choosing
nothing else changes nothing. A spec reads `area:Desert;area:Symmetry Island;lasers:3;panel:0x293;preset:seven-lasers`. Doors the solved panels open are opened too (a few tutorial
doors are opened by the game's script, not by a panel's target, and are opened by hand). The list is kept next to the saves (`coop_progress.txt`), because the game's own save has no
solution line for a panel solved this way, and is put back when a later session loads a campaign. A save made while the mod runs gets `<save>.coop.txt` beside it: when it was made, the
players who were in the game with their characters and times (earlier sessions of the campaign add up), panels solved in all and per area, lasers lit, doors open.

## The launcher (`launcher/witness_launcher.exe`, next to the game's exe)

* **Play**: Continue (the latest save) or New game, which asks what is already completed (areas, lasers, presets, with what each needs first) and starts a fresh world with `-new_game`; saves stay where they are.
* **Video**: the game's own `Launcher.variables` (render profile, full screen, vsync, resolution or a custom size, anti-aliasing, the quality settings), written in the game's format; the launcher starts the game with `-skip_config_dialog`.
* **Sound and voice**: game volume, game sound off, voice mode, push-to-talk key, voice and microphone volume, proximity and radio ranges.
* **Controls**: the mod's keys.
* **Game options**: the options that live inside each save (mouse speed, invert, smoothing, brightness, field of view, reticle, vignetting, look magnetism), patched in place; a copy of the save as it was is kept once (`.before-launcher`).
* **Co-op**: player number, name, port, the other players, character, models, dev menu. Lines of the cfg the launcher does not know are kept.
* **Saves**: each save with who was in the game and how far it got; copy or move saves to a backup folder.

## Checks

| Feature | Game-free (T0) | Two real games (T2, `tools/qa.sh <scenario>`) |
|---|---|---|
| Chat, typing dots, bubbles, pings | `make -C proxy/core test` (social layer: loss, jitter, restart, corruption; 8 twins) | `social` |
| Emotes, characters | `make -C proxy/model test` (emote clips) | `emote` |
| Free drawing | core stroke messages | `ink` |
| Progression and new game | `make -C proxy/progress test` (closure, order, lasers, text, sidecar; 6 mutants) | `progress` (solved panels and open doors read from both games' memory; the save's sidecar) |
| Launcher | `make -C launcher test` (cfg, video file, options in a save, saves; 5 mutants) | `launcher` (the window driven with window messages, the files on disk, the game it starts: its own log and the save it writes) |
| Voice | `make -C proxy/voice test` (codec, packets, mix, radio filter, jitter; 7 mutants) | `voice` (proximity gain, pan, fade and radio read from the listener's mixer with real positions; push to talk; speaking flag; game volume read back from the system) |
| Map picture | - | `map` (seams of neighbouring tiles agree, content, nothing of the mod drawn, the player returns) |
