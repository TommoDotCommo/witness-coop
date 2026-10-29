# Witness co-op: agent instructions

This file is the shared source of truth. Read optional ignored `local/AGENTS.md`
before work. Read the Co-op Anything playbook for research and testing methods.
Check Git status and worktrees; preserve unfinished work and use a separate
checkout for concurrent tasks. Record evidence and report only tests actually run.
Keep personal paths, tools, accounts, machine configuration and session permissions
out of tracked files. Follow explicit private publishing permissions; otherwise
prepare a reviewable diff before asking to publish. Never force-push.

## Repository map

- `README.md`: what works, how to run the dev setup, architecture, QA results.
- `proxy/`: C++ source of the 64-bit `AnselSDK64.dll` proxy that the game loads from its own folder.
  `ansel_proxy.cpp` (export stubs, `DllMain`, log, startup of the other parts), `overlay.cpp` (Direct3D 11
  `Present` hook and puppet drawing), `coop.cpp` (UDP pose exchange and the reliable event layer), `state.cpp`
  (detector for solved panels and opened doors, and the applier), `game.cpp` (calls into the game's own
  functions and the `witness_cmd.txt` channel), `input.cpp` (in-process keys, mouse and clicks, so no window needs the focus),
  `hooks.cpp` and `config.cpp` (import-table hooks for the save folder and the
  archive's share mode, and the lazily read `witness_coop.cfg`). Build offsets are at the top of those files.
- `tools/`: WSL scripts (`config.sh`, `install.sh`, `restart.sh`, `qa.sh`, `qa_par.sh`, `suite.py`, `shard.py`, `solve_bot.py`,
  `state_diff.py`) and `tools/win/` (Windows Python probes and input helpers; `wlib.py` is shared).
- Puzzle sweep: `tools/sweep.sh` / `tools/sweep.py` (every panel), `tools/puzzle.sh` / `puzzle.py` (one panel), `tools/solver.py` (which line to draw),
  `tools/psolve/` (the rule solver, C++, `make -C tools/psolve test`), `tools/win/wdraw.py` (teleport, enter, find the start, draw), `tools/win/wpanels.py` (manifest).
- `local/`: ignored private configuration, disposable test copies and QA evidence.

## Build and validation

- Build the DLL: `make -C proxy` (needs `x86_64-w64-mingw32-g++`).
- Put it into the test copies: `tools/install.sh` (checks both exe hashes first and refuses any other build).
- Restart both games with fresh new games: `tools/restart.sh` (with a resource provider: registers and places your allocated windows). Close them: `tools/stop.sh`.
- Dev menu (F8/F4/F9), partner view and saves: `proxy/ui.cpp` (2D drawing and text atlas), `devmenu.cpp` (menu, map, actions, test commands), `video.cpp` (partner pictures), `savepoints.cpp` (save points and presets); see the README.
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
- For documentation changes, check links, paths and `git diff --check`; game runs are unnecessary.

## Compatibility constraints

- Addresses, offsets and signatures target Steam build 11370412 (`witness64_d3d11.exe`, SHA-256 in the README).
  `install.sh` refuses any other build. A new build needs its offsets re-derived and the checks re-run.
- Do no file or network I/O in `DllMain` (loader lock: a log write there hung the game). Work goes to threads.
- Call game functions and write game memory only from the render thread (the `Present` hook), between frames, and
  check the entity's type descriptor before calling a type-specific function.
- Never sync or baseline before the world is stable; after applying a remote change, suppress local detection for
  that entity (the echo guard). Both were real bugs, see the README.
- Keep the real install untouched: the proxy is installed into disposable copies only.
