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

**Test copies.** The proxy goes into two disposable copies of the install, never the real one:

```
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-1
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-2
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
| Game calls | `game.cpp` | `DoorOpenStart(door, amount)` at `base + 0x8E4B0`, type-checked; `witness_cmd.txt` (`door <hexid>`) for development |

Build 11370412 facts the code relies on (module-relative): entity table `[[base+0x62D0A0]+0x18]` indexed by entity id; entity
`+0x08` points to a type descriptor (`Machine_Panel` `0x62E670`, `Door` `0x62DE70`); camera position `0x6304C0`, angles `0x6303EC`;
a panel is solved when the float at `+0x294` is `1.0` and the int at `+0x298` is `1` (a solve also sets `+0x278`, `+0x280`, `+0x290`);
a door is open when the word at `+0x1D0` is `1.0`. Writing those fields into another game does not start a door: its opening is
started by the function call. Sources and the evidence for each are in the hub notes.

`witness_coop.cfg` (next to the DLL): `id`, `listen`, `peer` (UDP ports on localhost), optional `peer_host`; test-only switches
`drop_first_event=1`, `no_resend=1`, `no_echo_guard=1` (all off by default). The log is `witness_proxy.log` next to the DLL.

## Automated checks

`tools/qa.sh` restarts both games for each scenario, a bot plays player A with real keyboard and mouse input (walks 46 m to the
first panel, aims by feedback from the camera angles, enters the puzzle, traces the line), and `tools/state_diff.py` compares the
solved panels and open doors read from both games' memory. Scenarios marked MUTANT are deliberately broken and must fail.

Last full run from this repo (2026-10-24, one machine, localhost, build 11370412, about 15 minutes):

```
baseline                                 PASS   identical (1 panel, 10 doors); B sent nothing back; no resend needed
lost first send of every event           PASS   identical; A resent all 11 events
MUTANT: no resend + lost first send      PASS   games diverged (panel 0x64 and door 0x34eda only on A), as required
MUTANT: echo guard off                   PASS   B echoed its 2 events back, as required
ALL OK
```

The suite found one real bug in an earlier run: a default-open door (`0x338d5`) powered up on player B a few seconds *after* the
"world stable" gate, so B sent an event with nobody having solved anything and the baseline check ("B sends nothing") would have
failed. The gate now also needs the same set of solved panels and open doors on three scans, and changes during the first 15 s after
the gate are folded into the baseline (the log says `changed during start-up, folded into the baseline`). It happened once in about nine
starts and did not recur in the final run, so the fix is in place but not shown to trigger.

Not covered by the suite: a real network, random loss (only a deterministic "first send lost"), more than two players, any puzzle
besides the first, cables and powered panels, a late join into a game that already has progress, saves, fullscreen, non-16:9 windows.

The probes in `tools/win/` (`wread.py`, `wsnap.py` + `wdiff.py` for before/after memory diffs, `wnames.py`, `wstatedump.py`) are
read-only; `wkeys.py`, `wshot.py`, `wplace.py` and `launch.py` drive the windows.

## License

MIT, see LICENSE.
