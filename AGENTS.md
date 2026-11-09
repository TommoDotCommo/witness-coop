# Witness co-op: agent instructions

This file is the shared source of truth. Read optional ignored `local/AGENTS.md`
before work. Read the Co-op Anything playbook for research and testing methods.
Check Git status and worktrees; preserve unfinished work and use a separate
checkout for concurrent tasks. Record evidence and report only tests actually run.
Keep personal paths, tools, accounts, machine configuration and session permissions
out of tracked files. Follow explicit private publishing permissions; otherwise
prepare a reviewable diff before asking to publish. Never force-push.

## Repository map

- `README.md`, `docs/SHOWCASE.md`, `dist/README.txt`: the player's documents (install, play, keys, what is shared). Write them for players:
  no test names, file paths or internals, and no statements about what is or is not tested.
- `docs/DEVELOPMENT.md`: how the mod works, the dev setup, architecture, offsets, QA results and feature checks (the README until the beta).
- `proxy/`: C++ source of the 64-bit `dsound.dll` proxy that the game loads from its own folder (it forwards to the system `dsound.dll`).
  `dsound_proxy.cpp` (export stubs, chain load, `DllMain`, log, startup of the other parts), `overlay.cpp` (Direct3D 11
  `Present` hook and puppet drawing), `coop.cpp` (UDP pose exchange and the reliable event layer), `state.cpp`
  (detector for solved panels and opened doors, and the applier), `game.cpp` (calls into the game's own
  functions and the `witness_cmd.txt` channel), `input.cpp` (in-process keys, mouse and clicks, so no window needs the focus),
  `thirdperson.cpp` (the third-person view while emoting: routes the game's world drawing, exe+0x1C8970, through the mod; camera in `core/thirdperson.h`),
  `hooks.cpp` and `config.cpp` (import-table hooks for the save folder and the
  archive's share mode, and the lazily read `witness_coop.cfg`). Build offsets are at the top of those files.
- `tools/`: WSL scripts (`config.sh`, `install.sh`, `restart.sh`, `qa.sh`, `qa_par.sh`, `suite.py`, `shard.py`, `solve_bot.py`,
  `state_diff.py`) and `tools/win/` (Windows Python probes and input helpers; `wlib.py` is shared).
- Puzzle sweep: `tools/sweep.sh` / `tools/sweep.py` (every panel), `tools/puzzle.sh` / `puzzle.py` (one panel), `tools/solver.py` (which line to draw),
  `tools/psolve/` (the rule solver, C++, `make -C tools/psolve test`), `tools/win/wdraw.py` (walk there, enter, find the start, draw), `tools/win/wpanels.py` (manifest).
- Walking: `tools/win/wnav.py` (the player walks with mouse and keys; it maps the island from the proxy's `depthdump` and plans with A*;
  teleport only when a walk fails, and every move is recorded), `tools/navmap.py` (picture of a map), `python3 tools/test_wnav.py` (game-free, with mutants),
  `python3 tools/test_navsim.py` (the real planner and walk loop on simulated levels; a planner or walker change merges only with it passing and a
  before/after of `tools/test_walk_bench.sh`: `docs/WALKER.md`).
- Collision oracle (the engine's own ray cast and ground query, `proxy/oracle.cpp`; how it was found and what it cannot see: `docs/COLLISION-ORACLE.md`):
  commands `ray`, `ground`, `where`, `oracle` (batch), `scan` (map cells); `tools/oracle_live.py` (one game from a save, batch queries, `check`),
  `tools/oracle_map.py` (map a rectangle: the player is held at each tile so its collision is loaded), `proxy/core/oracle_cell.h` (cells from queries,
  T0 test `oracle-cell` in `make -C proxy/core test`), `python3 tools/test_oracle.py` (recorded geometry, game-free, with mutants),
  `tools/oracle_probe_eval.py` and `tools/oracle_eval.py` (score a map against probes the game's physics blocked or let through, and against walker traces).
- What a solve does: `proxy/state.cpp` `applyPanelSolvedLine` (the solver's line into the panel, the game's reactions exe+0xB7F80, target, Bridge) and
  `proxy/game.cpp` (`GameReactNow`, `GameBridgeAdvanceNow`, test-only `nativesolve`); tests `make -C proxy/core test` (solve-react), `tools/qa.sh react`, `tools/qa.sh bridge`;
  the sweep `tools/qa.sh probe tools/react_sweep.py` lists what a remote solve still misses (docs/DEVELOPMENT.md).
  The teleport hold (`GameHover`) lets go when the game resets the player: `tools/qa.sh hover`; `hover ... firm` holds through resets, for a test far from any ground (voice, radio).
- Teaching by a person: `tools/record.sh` (one game, a person solves the panels the bot cannot; `tools/win/wrecord.py` records),
  `tools/playthrough/teach.py` (recordings -> `data/taught.json`: line, node world positions, where they stood), used by `run.py` and
  `wdraw.py` (`WDRAW_TAUGHT`); `python3 tools/playthrough/test_teach.py` (game-free, with mutants).
- `dist/`: the player installer (`installer.c` stub -> `Witness-Coop-Setup.exe`, `installer-gui.ps1`, `witness-coop.ps1`, the
  ownership journal `install-state.ps1` with the pure decisions in `install-policy.h`), and the player's `README.txt`.
  `tools/release.sh <version>` builds the zip and prints the publish command with every asset (`tools/test_release_assets.sh`), `tools/check_release_package.py` checks it, `tools/qa_installer.sh <game folder>`
  runs `tools/test_installer.ps1` on disposable Windows fixtures. Release notes: `docs/RELEASE-NOTES-<version>.md`.
- One-file setup, build check, signing: `dist/installer.c` + `dist/setup-core.h` (the payload container, its hash and names; `tools/test_setup_core.sh`), `tools/pack_payload.py`, `tools/build_setup.sh`,
  `tools/release.sh` (`SIGN_CMD`, see `docs/SIGNING.md`), `dist/install-linux.sh` (`tools/test_install_linux.sh`), `common/buildid.h` (the exe fingerprint the proxy and launcher check; `tools/buildid_of.py`
  derives its constants for a new game build), `tools/test_build_gate.sh` (the real `DllMain` with a host that is not the known build), `tools/unknown_build_slot.sh` (one slot), `tools/qa_installer.sh` (everything game-free).
- Diagnostics: `common/diag_log.h` (per-session hash-chained logs, cap), `common/crash_handler.h` (unhandled-exception filter, minidump), `proxy/diag.cpp`
  (flight recorder, F2 marker, desync dump, the commands `diagmark`, `diagdesync`, `testcrash`), `dist/diag.ps1` (the Send diagnostics bundle, embedded in the launcher),
  `tools/diag_report.py`, `tools/minidump.py`, `tools/map_symbols.py`; tests `make -C launcher test` (diag-test, crash-test), `python3 -B tools/test_diag.py`, `tools/qa_diag.sh` (live).
- Keys and the player panel: `common/bindings.def` is the one binding table (guide, launcher Controls page and cfg parser read it); `common/guide.h` holds key codes (mouse buttons and the wheel are codes below 8),
  the shared co-op keys, the migration of moved defaults and the shared-key rules; `common/movedkeys.h` writes the one-time moved-keys list; `common/notices.h` is the one table of join/leave/lost/reconnect/version wording;
  `proxy/ui/player_panel.h` is the hold-Tab panel's model (rows, layout, hit test, keys), drawn by `proxy/overlay.cpp`. Tests: `make -C common test`, `make -C proxy/ui test`, `tools/qa.sh shared`.
- Where run-time files live (J4): `common/userdata.h` decides it for the DLL and the launcher (`%LOCALAPPDATA%\witness-coop\`, or the game folder when its `witness_coop.cfg`
  carries an instance key: `save_dir`, `force_active`, `run_unfocused`, `diag_dir`; test tools write such a file, the mod never does); `make -C common userdata-test`.
- Package for mod managers (J5-J9): `tools/mod_manifest.py` (coop-mod.json, the Nexus copy note), `tools/versioninfo.py` (VERSIONINFO of every binary), checked by
  `tools/check_release_package.py` and `tools/test_mod_manifest.py`; Setup's owner detection (Setup, a mod manager's hard links, a hand copy that gets adopted), install cards,
  options (shortcut, firewall rule, `witness-coop://` handler) and verbs are in `dist/install-state.ps1`, `dist/witness-coop.ps1`, `dist/installer-gui.ps1`, `dist/installer.c`
  and `dist/install-linux.sh`; tests `tools/test_setup_options.ps1`, `tools/test_setup_verbs.ps1`, `tools/test_install_linux.sh`, live `tools/userdata_live.sh`.
- Finding each other and the invite (standard B, C): `common/coop_invite.h`, `common/coop_discover.h`, `common/coop_discover_win.h`, `common/launcher_join.h`, `common/launcher_host_core.h`,
  `common/launcher_host_win.h` (vendored from MDK2 unchanged apart from `#ifndef` constants: `common/VENDORED.md`; the Witness's constants and the only include path are `common/coop_witness.h`),
  `launcher/joinglue.h` (paste box, probe answer, the copied invite), `launcher/netfind.c` (Join list, recents, invite routes at once, Friends can join by, router mapping, firewall check);
  tests `python3 tools/test_coop_discovery.py`, `tools/test_coop_join.py`, `tools/test_coop_host.py`, `tools/test_coop_witness.py`, `make -C launcher joinglue-test`, live `tools/qa.sh join` (no game).
- Built-in randomizers (the launcher's Randomizers tab): `third_party/` (six vendored randomizers, MIT; `third_party/README.md`: each folder, the marked changes,
  `tools/randomizer_upstream.sh <folder>|all status|update`), `common/randomizers.h` (the modes, credits, the spec), `proxy/randomizer/` (`backend.h` the overlay and
  fingerprint, `variant.h`/`variants.mk`/`variants/<v>.cpp` each vendored randomizer in a namespace of its own, `generate.cpp` one run, `game_randomizer.cpp` in the game:
  per world, applied in one frame, watchdogs, `World::worldReady`), `proxy/trainer.cpp` (the Witness Trainer's co-op options), `dist/THIRD-PARTY-NOTICES.txt`;
  tests `make -C proxy/randomizer test` (with a world recorded by `tools/randomizer_solo.sh --record`), `tools/qa.sh randomizer` (docs/DEVELOPMENT.md).
- `local/`: ignored private configuration, disposable test copies, release zips (`local/release/`) and QA evidence.

## Build and validation

- Build the DLL: `make -C proxy` (needs `x86_64-w64-mingw32-g++`).
- Put it into the test copies: `tools/install.sh` (checks both exe hashes first and refuses any other build).
- Restart both games with fresh new games: `tools/restart.sh` (with a resource provider: registers and places your allocated windows). Close them: `tools/stop.sh`.
- Dev menu (F8/F4/F9), partner view and saves: `proxy/ui.cpp` (2D drawing and text atlas), `devmenu.cpp` (menu, map, actions, test commands), `video.cpp` (partner pictures), `savepoints.cpp` (save points and presets); see docs/DEVELOPMENT.md.
- Manual testing: `WITNESS_HUMAN=1 WITNESS_FPS=60 tools/restart.sh` starts both games for a person (real mouse and keyboard, A on the first monitor, B on the second); `tools/tour.sh list|go N|next|prev|here [A|B|both]` teleports the player to numbered spots across the island (31 map cells, each in front of a real panel).
- Full regression: `tools/qa.sh` (or `tools/qa.sh --all [filter ...]`) runs every scenario with its mutants in parallel shards
  (`tools/qa_par.sh`): each shard is its own resource owner with two slots, and new shards start while the provider status shows
  2 free slots and about 4 GB of free RAM above the provider's floor (90 s apart, `QA_STAGGER`), up to `QA_SHARDS_MAX` (default 4); with no room it waits (`QA_WAIT`, default 1800 s)
  instead of failing. Scenarios are queued longest first from `tools/qa_durations.json`; the run ends with a table of scenario,
  shard, seconds and result plus the wall time, and leaves `durations.json` in its `qa_par.*` run folder (refresh the committed table
  from it when scenarios change). `QA_SHARDS=N` forces N shards. `tools/qa.sh --serial [filter ...]` keeps the old one-pair run
  (28 scenarios took 48 minutes on 2026-10-24); `tools/qa.sh <filter>` runs matching scenarios serially. Shard planning is game-free:
  `python3 tools/test_shard.py`.
  A scenario whose bot never reached its precondition (game not in the world yet, a click missed under load) is run once more automatically; that is logged as `RETRY`. Report only what you ran.
- One game without a partner: `tools/solo.sh` (one slot; a start whose cfg has no roster, as from Steam, must scan the world and stay alive;
  `--mutant` sets `late_voice_lock=1`, the lock as before 2026-10-31, and the game must die).
- For documentation changes, check links, paths and `git diff --check`; game runs are unnecessary.

## Compatibility constraints

- Addresses, offsets and signatures target Steam build 11370412 (`witness64_d3d11.exe`, SHA-256 in docs/DEVELOPMENT.md).
  `install.sh` refuses any other build. A new build needs its offsets re-derived and the checks re-run.
- Do no file or network I/O in `DllMain` (loader lock: a log write there hung the game). Work goes to threads.
- Call game functions and write game memory only from the render thread (the `Present` hook), between frames, and
  check the entity's type descriptor before calling a type-specific function.
- Never sync or baseline before the world is stable; after applying a remote change, suppress local detection for
  that entity (the echo guard). Both were real bugs, see docs/DEVELOPMENT.md.
- Changes in the first 15 s after the stable-world gate are start-up noise and are folded into the baseline, except
  after progress the mod applies itself: anything that solves, powers, opens or lights on its own (a progress list,
  a preset, the dev tools, doors opened by hand) calls `SyncCore::modProgressApplied`, which ends that window, so
  what the game's scripts do in response reaches the other players. A new tool that changes progress must call it too.
- Keep the real install untouched: the proxy is installed into disposable copies only.
