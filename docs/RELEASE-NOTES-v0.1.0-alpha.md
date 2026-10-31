# The Witness Co-op v0.1.0-alpha

First packaged build, for a first play test between two PCs. Extract the whole ZIP, open
`Witness-Coop-Setup.exe`, click Install, then start the game from `witness_launcher.exe`
(host a lobby and send the invite, or paste one and join). Both players need Steam build
11370412 of The Witness and this same version.

## What is in it

- **Shared world.** Solved panels, opened doors, powered panels and lit cables are applied in the
  other game through the game's own functions, with reliable, resent events and a full resync
  when a game restarts. Nothing is sent until each world is stable.
- **The other player.** The adventurer, skinned and animated (idle, walk, run, fidgets, emotes),
  hidden by walls and doors through the game's depth buffer, with a name tag.
- **Talking and pointing.** Text chat, pings with distance, free drawing on the world and on panels,
  a shared drawing pen, voice with proximity, radio, open-line and one-to-one channels, a view of the
  other player's screen, see-through when bodies overlap.
- **The launcher.** A lobby (host, invite `address:port/CODE`, join, ready, chat and voice before the
  game), co-op or race mode, a new game with chosen parts of the island done, the game's video
  settings, the mod's keys and sound settings, saves with who played.
- **Setup.** `Witness-Coop-Setup.exe` finds the game in the Steam libraries, verifies the exe's
  SHA-256 (any other build is refused, nothing changed), swaps `AnselSDK64.dll` for the proxy while
  keeping the original as `AnselSDK64_real.dll`, adds the launcher and the model, and uninstalls
  by receipts (only files it installed are touched; originals are restored).

## Fixed while packaging

- A start without the launcher (from Steam, or with a `witness_coop.cfg` that has no player number and
  ports) crashed a few seconds after the UI was ready: the overlay asked the voice module for its channel
  text every frame and took a critical section that only the networking start had initialised. The voice
  and race locks now exist from their first use. Found with the installed copy and a Windows crash dump
  (an access violation writing address 0x24 inside `ntdll`: `EnterCriticalSection` on a zeroed section). `tools/solo.sh` is the regression check, with `late_voice_lock=1` as its mutant.

## Not yet

- Lasers, bridges, pressure plates and a few special puzzles are not shared.
- Joining a game that already has progress: start together.
- Internet play is what this build is for testing; the automated suite runs on one PC.

## Verified

Game-free: `make -C proxy/core test`, `make -C launcher test`, `tools/check_release_package.py`
on the zip. Installer: `tools/qa_installer.sh` (disposable fixtures with a real exe; install,
update, repair, modified-file refusal, uninstall restoring the original, wrong build refused,
unowned files kept, setup window geometry). Live, on 2026-10-31 with this build: `tools/qa.sh lobby`
(two real launchers and two real games: host, invite, join, chat, voice, start; ALL OK, its mutants caught),
`tools/solo.sh` (a start without a roster scans the world and stays alive) and its mutant (the game dies with
the old lock), and the proxy loaded from a second copy of the supported build installed by this setup
(hooks, Present, 594 panels and 327 doors, settled). The full two-game suite was last run on 2026-10-22.
