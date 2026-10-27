# witness-coop

Co-op for The Witness (Steam app 210970, build 11370412). **Dev preview, not playable with friends yet.** Two copies of the
game on one PC can see each other as puppets, and when one player solves a panel the other game marks it solved and opens the
door it gates. The first two tutorial door puzzles and a panel-to-panel power-on chain have automated bots; power-off switches, lasers, plates
and bridges are not synced, and there is no installer or release.
Built with the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook; research notes live in
`games/the-witness/NOTES.md` there.

No game files are in this repo. You need your own copy of The Witness, Steam build 11370412
(`witness64_d3d11.exe` SHA-256 `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5`).

## What works

- **Player model.** Each player sees the other as an animated, skinned character (glTF `.glb`), drawn into the game's frame by a
  hook on the Direct3D 11 `Present` call. It stands at the other player's feet, faces where they look, plays a walk clip at a rate
  matched to their measured speed (and an idle or run clip when the model has one), and is hidden by walls and doors: the proxy reads
  the game's own depth buffer (4x MSAA, reversed depth stored as `0.0597 / distance`, found by watching which depth view the game
  binds) and discards the model's pixels that are behind it. Three open-licence models are bundled (`assets/models/`, credits in
  `CREDITS.md`) and `model=<name or .glb path>` in the cfg picks one; `model=none` brings back the old flat puppet. In game, **F7 / F6** switch to the next / previous model (every `.glb` in
  `witness_models\`) and a small panel lists them for four seconds; `model <name|next|prev>` does the same from `witness_cmd.txt`. The choice is
  local: it is what *you* see for the other player.
- **Shared panels and doors.** A detector watches all 594 `Machine_Panel` and 327 `Door` entities. A solved panel is sent to the
  other game and written there as solved; an opened door is opened there by calling the game's own door function, so the
  door animates and collides as in single-player. Events are numbered, received in sequence and resent until acknowledged.
  If an entity is temporarily missing or a door call is refused, its acknowledged event stays queued and retries on the render
  thread; other entities can still progress. Successful changes are applied once. A restarted game gets the whole state again.
  The restart catch-up scenario checks this after the original panel/door events have already been acknowledged.
- **Panel power-on progression.** Activating a panel is a separate reliable event from solving it. The peer calls the game's
  panel power-on function on the render thread; its normal frame updates animate the brightness. A powered target remains
  unsolved. Power state participates in startup stability, has its own echo guard, and is included in restart catch-up.
  This currently shares power-on progression; power-off switching is not replicated.
- **Safe start.** Nothing is detected, resent or applied until the world is stable (camera valid and identical panel and door
  counts on three scans three seconds apart); after applying a remote change the entity is suppressed for 5 s so it is not
  sent back.

Not done: power-off switching, plates, lasers, bridges, a real network (localhost only), late join into a game
with progress (restart catch-up, a first-time late join and a same-process save reload are in the suite), puzzles beyond the first two tutorial doors, the first black/white tutorial panel and one cable-driving maze (`0x293`), announcing a player's own model choice to the peer (the selection is local), crouch/prone poses (the game has none), lighting that matches the game's, models with several textures or materials, and shadows.

## Try it (two copies on one PC)

Requirements: Windows with the Steam build above; WSL2 with `make` and MinGW-w64 (`x86_64-w64-mingw32-g++`); Windows Python 3
(default `python.exe`, override with `PYW`); Python 3 in WSL. Nothing here installs anything for you.

**Test copies.** Optional machine configuration belongs in ignored `local/env.sh`.
Set `PYW` to Windows Python. For concurrent QA, configure `WITNESS_RESOURCES` to
an external provider implementing the interface in [docs/RESOURCES.md](docs/RESOURCES.md).
It supplies isolated copies, ports, process registration and cleanup. The public
repository has no required workstation service or private checkout path.

The proxy goes into two disposable copies of the install, never the real one:

```
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-1
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-2     # (then set WITNESS_DIR1/2, configure an external resource provider)
tools/install.sh       # builds the DLL, checks both exe hashes, renames AnselSDK64.dll to AnselSDK64_real.dll, installs the proxy
tools/restart.sh       # starts both with -skip_config_dialog -windowed -new_game, side by side
tools/restart.sh B     # restart only B with a fresh world, keeping A and its progress running (also accepts A)
```

Each copy is 3.9 GB. **The game keeps its saves in `%APPDATA%\The Witness` whatever folder the exe runs from**, so a copy does not
isolate them: starting a New Game from a copy rewrote an existing save there. Back that folder up first.
A hand test: walk player A (left window) to the first panel at the end of the start tunnel and solve it; the door opens in B
as well (B's `local/witness-2/witness_proxy.log` says `remote panel 0x64 applied as solved`, `remote door 0x34eda opened`).

## Experimental groups (2–16 players)

The `coop16` branch adds a configured full mesh of up to 16 players: independent reliable streams and ACKs,
all remote poses and avatars, player selection on the map, and shared panel, door, panel-power and cable state.
Group cable mutations carry logical versions so concurrent changes and old snapshots converge consistently.
Detection follows the requested power level while the cable glow fades; a genuine local reversal is still sent during
a remote echo-guard window. New commands are detected before restart snapshots are built.
The original two-player `peer=<port>` configuration still uses protocol v4. Group mode uses v5; every member must
use the group DLL and list every other member, with distinct player IDs and UDP endpoints.

Generate configurations from a JSON roster, for example:

```json
[{"id":1,"host":"127.0.0.1","port":48001},
 {"id":2,"host":"127.0.0.1","port":48002},
 {"id":3,"host":"127.0.0.1","port":48003}]
```

```sh
python3 tools/group_config.py roster.json local/group-configs
```

Copy each generated `player-<id>.cfg` to `witness_coop.cfg` beside that player's DLL in a disposable game copy.
Each config has `id`, `listen`, and one `peer=<id>,<IPv4 address>,<port>` line per other member (15 lines for a
16-player group). Keep `save_dir=instance` to isolate saves. Launch and install the copies as described above;
`tools/restart.sh` generates two-player configs, so it does not preserve group configs.
Group partner video displays the lowest-ID remote player's stream. Avatar choice is still local.

Validation so far: the eight-player game-free test covers lossy links, reordering, duplicates, independent ACKs,
a temporarily unreachable receiver, progression from every player, all remote poses, cable conflicts and a peer restart.
Five group mutants must fail, including recovery after 4,200 changes while one receiver is unreachable. `python3 tools/test_group_config.py` checks the 16-entry roster capacity and invalid rosters.
The production Windows UDP layer also passed an eight-client fake-world integration test with 25% loss,
cable on/off and a restarted client (`make -C proxy/core transport-test`; no game or resource provider slots).
A two-real-game group run passed a normal input-driven bot solve, door sync, progress originating on both clients,
pose/avatar submission, packet loss, restart catch-up, empty queues and byte-identical real saves.
`tools/qa_group_render.sh` also passed with one real game and seven lightweight protocol clients: seven actual D3D
avatar submissions with distinct positions, one moving gait and six stationary gaits, bidirectional panel progression,
a normal local bot solve and drained queues under 25% packet loss. Its shared-avatar-state mutant was caught.
This uses one game slot; it does not count as eight real game processes. The `modelstate` command writes the latest
render frame's submitted IDs, positions and gait speeds to `witness_models.json` for these checks.
Eight-real-game QA is **pending** while other resource provider tests occupy the available RAM: `tools/qa_group.sh 8` first checks resource provider activity and RAM, then leases only this
worktree's slots, runs a normal bot solve, checks every world's process memory, originates progress on every client,
and verifies restart recovery and drained queues. It refuses insufficient RAM rather than displacing another test. Groups of three or more also defer while another sweep or suite is live, so a client restart cannot make occupied test capacity look temporarily free.
Sixteen live clients and remote-PC networking have not been tested.

## How it works

The exe loads `AnselSDK64.dll` from its own folder. `proxy/` builds a replacement whose nine exports are stubs that load the
renamed original (`AnselSDK64_real.dll`) by full path on first use and jump to the real function (export forwarders in a `.def`
file made the loader fail with "AnselSDK64_real.dll not found"). `DllMain` only starts threads.

| Part | File | What it does |
|---|---|---|
| Network | `coop.cpp` | Protocol v4 UDP: pose at 30 Hz (`WTP4`, 40 bytes), reliable events (`WTE4`, 32 bytes: epoch, sequence, kind with bit 8 set for snapshot events, entity), cumulative acks (`WTA4`, 24 bytes); lengths include checksums. A peer session change starts a new epoch and full state resend |
| Overlay | `overlay.cpp` | Patches slot 8 (`Present`) of the swap-chain vtable (found through a dummy device), draws the player model (`model_gpu.cpp`, `model/`) or the flat puppet; also runs queued game commands and remote events between frames |
| Model | `model/model.{h,cpp}`, `model_gpu.cpp`, `depth.cpp` | `model/` is game-free C++ (glTF loading with cgltf and stb_image, skeletal animation, CPU skinning, camera maths, the remote player's walk state) with its own T0 test; `model_gpu.cpp` is the D3D11 skinned-mesh renderer with a GPU occlusion query; `depth.cpp` patches the context vtable (`OMSetRenderTargets`) to find the game's scene depth buffer |
| State | `state.cpp` | Detector thread (10 Hz) and the applier; echo guard and stable-world gate |
| Input | `input.cpp`, `game.cpp` | Injects keys, mouse and clicks into the game's own input pump, teleports the player, takes screenshots, parses `witness_cmd.txt` |
| Hooks | `hooks.cpp`, `config.cpp` | Import-table hooks installed from `DllMain` (memory patching only): `SHGetFolderPathW` (save folder), `CreateFileA/W` (`data-pc.zip` share mode); settings read lazily from the cfg |
| Game calls | `game.cpp` | Door opening at `base + 0x8E4B0`; panel power-on at `base + 0xC2890`, both type-checked and called on the render thread; `witness_cmd.txt` for development |

Build 11370412 facts the code relies on (module-relative): entity table `[[base+0x62D0A0]+0x18]` indexed by the mod's entity IDs
(table indices, including zero); entity
`+0x08` points to a type descriptor (`Machine_Panel` `0x62E670`, `Door` `0x62DE70`); camera position `0x6304C0`, angles `0x6303EC`;
a panel is solved when the float at `+0x294` is `1.0` and the int at `+0x298` is `1` (a solve also sets `+0x278`, `+0x280`, `+0x290`);
a door is open when the word at `+0x1D0` is `1.0`. The game's stored target IDs use a separate namespace: subtract the table
base ID at `[[base+0x62D0A0]+0x08]` (observed `1`) to get a table index. Writing those fields into another game does not start a door: its opening is
started by the function call. Panel requested power is at `+0x2A4`; the visible level at `+0x2A0` follows it in the engine. Sources and the evidence for each are in the hub notes.

`witness_coop.cfg` (next to the DLL): `id`, `listen`, `peer` (UDP ports on localhost), optional `peer_host`. Instance isolation (all
off by default, so a player's own game is unchanged): `save_dir=instance` (the game's save folder, `%APPDATA%\The Witness`, becomes
`<game folder>\appdata`, or give a path; the owner's real saves are then never touched), `cap_fps=N` (frame cap), `share_zip=1`
(see above), `force_active=1` (the game believes its window is always active: full speed, and it accepts injected input, without taking the focus or the mouse; the real cursor is replaced by a virtual one). Test-only switches: `drop_first_event=1`, `no_resend=1`, `no_echo_guard=1`, `no_resync_on_restart=1`, `skip_entity_zero=1`, `no_panel_power=1`, `no_cable_sync=1`, `no_cable_off=1`, `no_off_snapshot=1`, `no_snap_reassert=1`, `no_cable_arbiter=1`, `no_power_link=1`, `no_target_dispatch=1`. The log is `witness_proxy.log` next to the DLL.

Player model keys (also optional): `model=<name or path>` (default `cesium-man`; a bundled name is looked up in `witness_models\` next to the DLL, anything else is a `.glb` path; `model=none` draws the flat puppet), `model_height=<m>`, `model_speed=<m/s the walk clip was made for>`, `model_yaw=<degrees>` (each model's own defaults come from its `<file>.glb.txt`), `depth_bias=<fraction>` and `depth_near=<m>` (how the game's depth is compared; `depth_near=0.0597` was measured), and the test mutants `no_model_depth=1`, `no_model_gait=1` and `no_model_select=1`. `tools/install.sh` copies `assets/models/` next to the DLL.

Both peers must use the updated DLL. Protocol v2 added panel power-on and v3 adds cable lighting (event kind 4: a cable is lit when `+0xC4` and `+0xC8` are 1.0; applied through the game's own `exe+0x175EE0`, the call its target dispatcher at `exe+0xC24F0` makes for a cable target); each version uses new packet magics, so older peers are ignored.
Protocol v4 adds cable power-off (event kind 5, the game's own `exe+0x175E50`, which sets the requested level `+0xC8` to 0 and lets the glow fade). A cable that was lit and goes dark is announced; a snapshot also lists default-lit cables that were switched off, so a fresh peer learns them; a peer's snapshot never relights a cable this player switched off (this player answers with the off state); and when both players switch the same cable at the same moment the lower player id wins, so both worlds end the same.
Applying a remote panel solve also does what the game's target dispatcher (`exe+0xC24F0`) does for targets that have no event of their own (lasers, pressure plates, gauges and the like), and a panel powered by a remote event gets its source back-link at `+0x2C8` like a native solve.

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
| `modelstat` | log the model's state: pixels that passed the depth test (GPU occlusion query), animation clip and time, measured speed, pose |
| `fakepose <x> <y> <z> <yaw>` / `fakepose off` | test hook: draw the peer at this eye position instead of where it is (a player cannot be teleported behind a closed door, but the depth test must be checked there) |
| `probe` / `depthview on|off` | diagnostics: log the game's depth at a few pixels; draw the depth buffer instead of the model |

## Automated checks

**T0, no game needed:** the sync logic lives in `proxy/core/` (`sync_core.{h,cpp}`, pure C++17, talks to the game only through a `World` interface; `state.cpp` is the game adapter). `make -C proxy/core test` (needs a clone of the Co-op Anything hub for its fuzzer; `HUB=<dir>`, defaults to a sibling `coop-anything` checkout; override with `HUB`) fuzzes two cores over a lossy link with a fake world: 500 sessions must pass, and each of 10 bug twins (no resend, no echo guard, no dedupe, ...) must be caught. About 3 seconds.

The same command also runs deterministic application regressions: acknowledged events survive a missing panel, a missing door,
or repeated refused door calls, then apply without another network packet and without echoing. An unrelated door must still open,
wrong entity types must be rejected, and each retry case must catch the `noApplyRetry` bug twin. These simulate adapter failures;
they do not demonstrate a late-loading entity or a refused call in the real game.

Power-on regressions also run in T0: dropped first sends, refused power-on calls, no echo, keeping the target unsolved,
and restoring power to a restarted receiver after the original events were acknowledged. Disabled power sync, no retry,
no echo guard and no restart resync each have a twin the regression must catch.

**Model code, no game needed:** `make -C proxy/model test` (also run by `make -C proxy/core test`) loads the bundled models and checks skinning against the bind pose, that the walk loops, quaternion interpolation, that the camera matrix agrees with the projection the puppet always used, the placement of the figure (feet, height, facing, left/right), and the walk state (speed, idle, teleports, clip choice by name); six mutants (matrix order, no loop, no quaternion renormalising, mirrored model, no teleport guard, no run clip) must each fail it.

**T2, two real games:**

`tools/qa.sh` restarts both games for each scenario, and a bot plays player A through the proxy's in-process control (teleports to the
first panel, aims, clicks to enter the puzzle, traces the line, clicks to finish), with no window focused and no desktop input, and `tools/state_diff.py` compares the
solved panels and open doors read from both games' memory. Scenarios marked MUTANT are deliberately broken and must fail.

`tools/qa.sh model` runs the character-model suite (`tools/suite_model.py`, `tools/win/wmodel.py`): the viewer's proxy reports how many pixels of the model passed the depth test, and the check asserts that a player 5 m ahead is visible, a standing one is idle, a walking one has a measured speed of 1.5 to 5 m/s, plays the walk clip and animates, goes idle and still when stopping, is visible in front of the closed first door and has under 2% of those pixels behind it. Mutants: model drawn over walls, model never walks, model off, and a second bundled model selected by name. A selector scenario presses F7 / F6 (injected as the game's own key messages) and `model fox`, and asserts which model `modelstat` reports and that it is drawn; its mutant (`no_model_select=1`) must fail exactly the switching checks.

Last character-model run (2026-10-24, one machine, localhost, build 11370412, `tools/qa.sh model`, about 15 minutes), all five scenarios PASS, and the owner's real save folder was byte-identical after each:

```
model: visible, animated, hidden by walls        PASS   9 checks: visible 5 m ahead (27571 px), idle at rest, walking at 3.6 m/s plays the walk clip and animates,
                                                        idle and still after stopping, visible in front of the closed door (138589 px), 0 px behind it
model: another bundled model selected by name    PASS   fox: walk clip is index 1, idle clip (Survey) animates, hidden behind the door
MUTANT: model drawn over walls                   PASS   only "hidden behind the door" failed (16193 px visible)
MUTANT: model never walks                        PASS   only "animates" failed (clip time stayed 0)
MUTANT: model off (flat puppet)                  PASS   the six checks that need a drawn model failed
```

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

`tools/qa.sh 'restart catch-up'` runs two additional scenarios: B is restarted into a fresh new game after both copies have the
first panel solved and A's original events are acknowledged. The survivor must retain its PID and progress; the new process
must recover all solved panels and opened doors with no echo. The mutant disables the survivor's full-state resend and must
leave the first panel and its door missing on B. Assertions read both processes' state and require a complete entity census.
Targeted run on 2026-10-22: both scenarios PASS; clean state identical (1 panel, 10 doors), mutant missing exactly panel
`0x64` and door `0x34eda`. Real saves unchanged, instance save redirect and cursor checks passed, and A ran at 30.0 fps.

`tools/qa.sh 'second tutorial door'` runs the first-panel bot then `wbot2.py`, which naturally solves Tutorial Bend (`0x182`)
by tracing up and left. Both games must contain both solved panels and open doors `0x34eda` and `0x0`; the mutant skips entity
zero during A's scan and must leave exactly that door missing on B. This caught a real adapter bug: scanning from ID 1
excluded the second tutorial door even though table index zero is valid. The second bot uses a calibrated standing point and
cursor path for the resource provider's 1920x1080 back buffer; other resolutions are untested. `tools/win/wpanel.py <pid> <hex id>` reads a
panel's graph for extending the bot to more puzzles.
Targeted run on 2026-10-22: clean and mutant both PASS (command exit 0). Clean has 2 solved panels and 11 open doors on both
sides; mutant lacks only door `0x0` on B. Save isolation, free cursor and 30.0 fps checks passed. The clean check failed before
the scanner fix, with that same missing door.

`tools/qa.sh 'panel power chain'` solves black/white tutorial panel `0x18af`, which powers panel `0x1b`. The target must
become powered in both games while remaining unsolved. The clean scenario then restarts only B and checks solved panels,
doors and powered panels again after catch-up. The mutant disables A's power events and must leave only `0x1b` unpowered
on B. `state_diff.py` now compares powered panels as well as solved panels and open doors; a panel counts as powered only
when both requested power and visible level are 1. The bot is calibrated for a 1920x1080 back buffer.
Targeted run on 2026-10-22: both scenarios PASS (exit 0). Clean, including fresh-B catch-up, matched 2 solved panels,
10 open doors and 238 powered panels; the mutant left only `0x1b` unpowered on B (237 there). Both kept the target unsolved.
Save hashes, instance saves, free cursor and 30.0 fps checks passed.

`tools/qa.sh 'cable chain'` solves maze `0x293`, whose lit cable powers maze `0x295` in the same garden, then solves `0x295` and lights its cable; both cables, both panels and the powered panels must match on B, a fresh B must recover them, and the mutant without cable sync leaves both cables dark on B. That is a second cable-driving panel (the chain scenario also covers the single maze `0x293`, whose own scenario was folded into it; no walking bot was needed: its neighbours are reachable by teleport; panels such as `0x3505` and `0x290` are not, a teleport to them is refused and the player returns to the spawn point).

`tools/qa.sh 'bot-free effects'` needs no bot. It makes panel `0x32f5` solved on A with the test command `solve` (its fields plus the game's target dispatcher, as a native solve does), so its laser `0x32f9` switches on (`+0x12c` goes from 0 to -1.0). B must end with the same laser state and so must a fresh B; the mutant without the dispatcher call leaves the laser off on B. `tools/qa.sh 'panel power chain'` now also compares the target's source back-link (`+0x2C8`) and has a mutant that leaves it unset. The same scenario switches the default-lit cable `0x18458` off on A with the test command `cableoff`; it must go dark on B and stay dark on a fresh B. One mutant removes the dispatcher call and the power-off sync on B (the laser stays off and the cable stays lit there), another removes the off snapshot on A (a fresh B shows the cable lit again).

**Dev menu, partner view, saves** (`dev_menu=1` in `witness_coop.cfg`; the resource provider tools turn it on; a normal install has it off):
- `F8` opens the dev menu: a map of the island with every panel (green solved, amber powered, grey off), the doors, and both players (you as a cyan pointer, the other player in orange); a list of 31 spots to teleport to; wheel to zoom, right-drag to pan, click a dot to select it. A selected panel shows what it targets, which panels target it, the chain upstream and downstream, and buttons: teleport in front of it, solve it (and run its target), power it, run its target, open its door, light or darken its cable. `F4` opens the nearby-puzzles page (the puzzles within a radius of you, nearest first, same details and buttons). `Tab` switches pages, `Esc` or `F8` closes. While it is open the game gets no keyboard or mouse input (key-ups still pass so nothing sticks). A teleport is checked a moment later and retried at other heights and distances when the game puts the player back at its spawn point.
- `F9` shows the other player's screen in a corner (small, large, off). Each game sends a 480x270 JPEG of its own screen about 5 times a second in UDP chunks (a lost chunk drops that picture); the other game decodes it and draws it. It sees what the other player is looking at or tracing, including panels.
- The Saves page makes a save point (the game's save file, copied after the pause menu opens so the game writes it) and restores one through the pause menu. The game lists saves by the time stored inside them, newest first, so the restore removes the stale copies in the instance save folder and picks the second entry (the first is the save the game just wrote of the running world). The game's save only keeps progress made through its own solve code (each solved panel has a traced solution line), so progress made by the dev tools is kept in a list of solved panels next to the save and put back after the load. A preset is that list alone: apply it to a new game to start from a given amount of progress ("complete the first N spots" makes one). Loading needs the player to be out of the intro tunnel.
- Test commands in `witness_cmd.txt`: `menu map|nearby|saves|toggle`, `menuat X Y`, `menusel HEXID`, `menudo teleport|solve|power|run|door|cableon|cableoff`, `partner on|large|off|stats`, `partnershot NAME`, `savepoint NAME`, `savepreset NAME`, `restore NAME`, `complete N`. Scenarios: `dev menu` (teleport, input taken while open, solve and cable actions reach B), `partner view` (B's received picture matches A's screenshot: mean difference 5.3 against 83.8 for the control), `preset apply` and `save point restore` (a fresh pair restored to 16 solved panels and 18 open doors, with a mutant that skips the save file).

Not covered by the suite: a real network, more than two players, puzzles beyond the first two tutorial doors, the first black/white tutorial panel and the two chained mazes, the other target types (plates, gauges, bridges, record players: only the laser is checked, and its panel is made solved by command rather than played), the `:slider_cable` cables, a switch-off caused by a real in-game lever (the off call is made by command), loading a different save slot, saves, fullscreen, non-16:9 windows.

The probes in `tools/win/` (`wread.py`, `wsnap.py` + `wdiff.py` for before/after memory diffs, `wnames.py`, `wstatedump.py`, `wfps.py`) are
read-only; `wkeys.py`, `wshot.py`, `wplace.py`, `wfocus.py` and `launch.py` drive the windows.

## License

MIT, see LICENSE.
