# witness-coop

Co-op for The Witness (Steam app 210970, build 11370412). **Dev preview, not playable with friends yet.** Two copies of the
game on one PC can see each other as puppets, and when one player solves a panel the other game marks it solved and opens the
door it gates. Only the first puzzle has been played through and tested; cables, panels powered by other panels, lasers, plates
and bridges are not synced, and there is no installer or release.
Built with the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook; research notes live in
`games/the-witness/NOTES.md` there.

No game files are in this repo. You need your own copy of The Witness, Steam build 11370412
(`witness64_d3d11.exe` SHA-256 `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5`).

## What works

- **Puppet.** Each player sees the other as a green body with a yellow facing dot, drawn into the game's frame by a hook on the
  Direct3D 11 `Present` call (camera model checked against screenshots). It is a flat rectangle and shows through walls.
- **Shared panels and doors.** A detector watches all 594 `Machine_Panel` and 327 `Door` entities. A solved panel is sent to the
  other game and written there as solved; an opened door is opened there by calling the game's own door function, so the
  door animates and collides as in single-player. Events are numbered, acknowledged, resent until acknowledged and applied once
  in order. A restarted game gets the whole state again.
- **Safe start.** Nothing is detected, resent or applied until the world is stable (camera valid and identical panel and door
  counts on three scans three seconds apart); after applying a remote change the entity is suppressed for 5 s so it is not
  sent back.

Not done: cables and panels powered by panels, plates, lasers, bridges, a real network (localhost only), late join into a game
with progress (the full resend exists but is not in the suite), saves, a second puzzle, a player model.

## Try it (two copies on one PC)

Requirements: Windows with the Steam build above; WSL2 with `make` and MinGW-w64 (`x86_64-w64-mingw32-g++`); Windows Python 3
(default `/mnt/c/Python311/python.exe`, override with `PYW`); Python 3 in WSL. Nothing here installs anything for you.

**With the shared lab** (`~/source/coop-lab`, several agents on one PC): `tools/qa.sh` does everything below for you: it leases two
game slots, builds hard-linked instance folders on the NTFS pool (`C:\coop\lab\witness\s<slot>`: one shared base copy, so a slot
costs about 40 MB; the game opens `data-pc.zip` exclusively, so the mod adds `FILE_SHARE_READ` to that open, `share_zip=1`), runs
with each instance's saves redirected into its own folder and a 30 fps cap, and kills only its own games at the end. Run `lab firewall` once (one UAC click) so the copies start without a Windows Firewall prompt.

**Test copies, without the lab.** The proxy goes into two disposable copies of the install, never the real one:

```
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-1
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-2     # (then set WITNESS_DIR1/2, or use the lab)
tools/install.sh       # builds the DLL, checks both exe hashes, renames AnselSDK64.dll to AnselSDK64_real.dll, installs the proxy
tools/restart.sh       # starts both with -skip_config_dialog -windowed -new_game, side by side
```

Each copy is 3.9 GB. **The game keeps its saves in `%APPDATA%\The Witness` whatever folder the exe runs from**, so a copy does not
isolate them: starting a New Game from a copy rewrote an existing save there. Back that folder up first.
A hand test: walk player A (left window) to the first panel at the end of the start tunnel and solve it; the door opens in B
as well (B's `local/witness-2/witness_proxy.log` says `remote panel 0x64 applied as solved`, `remote door 0x34eda opened`).

## How it works

The exe loads `AnselSDK64.dll` from its own folder. `proxy/` builds a replacement whose nine exports are stubs that load the
renamed original (`AnselSDK64_real.dll`) by full path on first use and jump to the real function (export forwarders in a `.def`
file made the loader fail with "AnselSDK64_real.dll not found"). `DllMain` only starts threads.

| Part | File | What it does |
|---|---|---|
| Overlay | `overlay.cpp` | Patches slot 8 (`Present`) of the swap-chain vtable (found through a dummy device), draws the puppet; also runs queued game commands and remote events between frames |
| Network | `coop.cpp` | UDP: pose at 30 Hz (`WTCP`, 36 bytes), reliable events (`WTCE`, 24 bytes: epoch, sequence, kind, entity), cumulative acks (`WTCA`); a peer session change starts a new epoch and a full state resend |
| State | `state.cpp` | Detector thread (10 Hz) and the applier; echo guard and stable-world gate |
| Input | `input.cpp`, `game.cpp` | Injects keys, mouse and clicks into the game's own input pump, teleports the player, takes screenshots, parses `witness_cmd.txt` |
| Hooks | `hooks.cpp`, `config.cpp` | Import-table hooks installed from `DllMain` (memory patching only): `SHGetFolderPathW` (save folder), `CreateFileA/W` (`data-pc.zip` share mode); settings read lazily from the cfg |
| Game calls | `game.cpp` | `DoorOpenStart(door, amount)` at `base + 0x8E4B0`, type-checked; `witness_cmd.txt` (`door <hexid>`) for development |

Build 11370412 facts the code relies on (module-relative): entity table `[[base+0x62D0A0]+0x18]` indexed by entity id; entity
`+0x08` points to a type descriptor (`Machine_Panel` `0x62E670`, `Door` `0x62DE70`); camera position `0x6304C0`, angles `0x6303EC`;
a panel is solved when the float at `+0x294` is `1.0` and the int at `+0x298` is `1` (a solve also sets `+0x278`, `+0x280`, `+0x290`);
a door is open when the word at `+0x1D0` is `1.0`. Writing those fields into another game does not start a door: its opening is
started by the function call. Sources and the evidence for each are in the hub notes.

`witness_coop.cfg` (next to the DLL): `id`, `listen`, `peer` (UDP ports on localhost), optional `peer_host`. Instance isolation (all
off by default, so a player's own game is unchanged): `save_dir=instance` (the game's save folder, `%APPDATA%\The Witness`, becomes
`<game folder>\appdata`, or give a path; the owner's real saves are then never touched), `cap_fps=N` (frame cap), `share_zip=1`
(see above), `force_active=1` (the game believes its window is always active: full speed, and it accepts injected input, without taking the focus or the mouse; the real cursor is replaced by a virtual one). Test-only switches: `drop_first_event=1`, `no_resend=1`, `no_echo_guard=1`. The log is `witness_proxy.log` next to the DLL.

Without `force_active` the game runs unthrottled only while its window is the active one (otherwise about 10 fps) and ignores keys and
mouse buttons. It also takes the foreground by itself while loading (`tools/restart.sh` runs a guard that hands the focus back).

**In-process control** (what the bots use; no window needs the focus and the desktop is never touched). The proxy polls
`witness_cmd.txt` (next to the DLL, one command per line, the whole file is taken at once, one input event is executed per frame):

| Command | Effect |
|---|---|
| `key W|A|S|D|SPACE|ENTER|ESC down|up` | a key press as the game's own key messages (holding `W` for 3 s walks the player about 9 m) |
| `mouse <dx> <dy>` | relative mouse motion as raw input (0.0025 rad per count when looking; 3.6 counts per pixel on a puzzle) |
| `button L|R down|up` | a mouse click as `WM_LBUTTONDOWN`-style messages at the window centre (the virtual cursor) |
| `tp <x> <y> <z> <yaw> <pitch>` | put the player there and face that way (the camera is 1.69 above the player entity) |
| `shot <name>` | back buffer to `shots\<name>.bmp`, also while the window is unfocused or covered (`tools/bmp2png.py` converts) |
| `door <hexid>` | open that Door entity with the game's own function |
| `stats` | log the input counters |

## Automated checks

**T0, no game needed:** the sync logic lives in `proxy/core/` (`sync_core.{h,cpp}`, pure C++17, talks to the game only through a `World` interface; `state.cpp` is the game adapter). `make -C proxy/core test` (needs a clone of the Co-op Anything hub for its fuzzer; `HUB=<dir>`, default `~/source/coop-anything`) fuzzes two cores over a lossy link with a fake world: 500 sessions must pass, and each of 10 bug twins (no resend, no echo guard, no dedupe, ...) must be caught. About 3 seconds.

**T2, two real games:**

`tools/qa.sh` restarts both games for each scenario, and a bot plays player A through the proxy's in-process control (teleports to the
first panel, aims, clicks to enter the puzzle, traces the line, clicks to finish), with no window focused and no desktop input, and `tools/state_diff.py` compares the
solved panels and open doors read from both games' memory. Scenarios marked MUTANT are deliberately broken and must fail.

Last full run from this repo (2026-10-22, one machine, localhost, build 11370412, about 8 minutes now that the bot teleports instead of
walking 46 m). Every scenario also checks that the owner's real save folder is byte-identical, that the instance's saves were
redirected, that the real mouse is not grabbed (system clip rectangle is the whole screen) and that the frame cap holds:

```
baseline                                 PASS   identical (1 panel, 10 doors); B sent nothing back; no resend needed
lost first send of every event           PASS   identical; A resent all 11 events
MUTANT: no resend + lost first send      PASS   games diverged (panel 0x64 and door 0x34eda only on A), as required
MUTANT: frame cap off                    PASS   game A read 1123 fps (uncapped), as required
MUTANT: echo guard off                   PASS   B echoed its 2 events back, as required
ALL OK
```

Two Witness agents (separate worktrees, so separate slots, ports and folders) ran the baseline **at the same time** beside an MDK2
agent's own QA (34 of 34): all passed, and a monitor sampling the foreground window every 0.2 s (1,648 samples) never saw a game own it.
An earlier run of the in-process suite passed while the bot had done nothing (two untouched games are "identical"); the suite now
requires the bot to report the panel solved.

The suite found one real bug in an earlier run: a default-open door (`0x338d5`) powered up on player B a few seconds *after* the
"world stable" gate, so B sent an event with nobody having solved anything and the baseline check ("B sends nothing") would have
failed. The gate now also needs the same set of solved panels and open doors on three scans, and changes during the first 15 s after
the gate are folded into the baseline (the log says `changed during start-up, folded into the baseline`). It happened once in about nine
starts and did not recur in the final run, so the fix is in place but not shown to trigger.

Not covered by the suite: a real network, random loss (only a deterministic "first send lost"), more than two players, any puzzle
besides the first, cables and powered panels, a late join into a game that already has progress, saves, fullscreen, non-16:9 windows.

The probes in `tools/win/` (`wread.py`, `wsnap.py` + `wdiff.py` for before/after memory diffs, `wnames.py`, `wstatedump.py`, `wfps.py`) are
read-only; `wkeys.py`, `wshot.py`, `wplace.py`, `wfocus.py` and `launch.py` drive the windows.

## License

MIT, see LICENSE.
