# Agent instructions

Read and follow [CLAUDE.md](CLAUDE.md) before starting work in this repository,
including the Co-op Anything `AGENTS.md` it references. `CLAUDE.md` is the shared
source of truth for session setup, research notes, testing, privacy, commit identity,
and publishing permissions. Keep shared workflow changes there so agents stay in sync.

## Repository map

- `README.md`: what works, how to run the dev setup, architecture, QA results.
- `proxy/`: C++ source of the 64-bit `AnselSDK64.dll` proxy that the game loads from its own folder.
  `ansel_proxy.cpp` (export stubs, `DllMain`, log, startup of the other parts), `overlay.cpp` (Direct3D 11
  `Present` hook and puppet drawing), `coop.cpp` (UDP pose exchange and the reliable event layer), `state.cpp`
  (detector for solved panels and opened doors, and the applier), `game.cpp` (calls into the game's own
  functions and the `witness_cmd.txt` channel), `input.cpp` (in-process keys, mouse and clicks, so no window needs the focus),
  `hooks.cpp` and `config.cpp` (import-table hooks for the save folder and the
  archive's share mode, and the lazily read `witness_coop.cfg`). Build offsets are at the top of those files.
- `tools/`: WSL scripts (`config.sh`, `install.sh`, `restart.sh`, `qa.sh`, `suite.py`, `solve_bot.py`,
  `state_diff.py`) and `tools/win/` (Windows Python probes and input helpers; `wlib.py` is shared).
- `local/`: ignored. Only used without the lab (two game copies `witness-1`, `witness-2`, `run/`); in lab mode the game folders,
  pid files and logs live under `C:\coop\lab\` (see `~/source/coop-lab/LAB.md`).

## Build and validation

- Build the DLL: `make -C proxy` (needs `x86_64-w64-mingw32-g++`).
- Put it into the test copies: `tools/install.sh` (checks both exe hashes first and refuses any other build).
- Restart both games with fresh new games: `tools/restart.sh` (lab mode: your slots; registers and tiles the windows). Close them: `tools/stop.sh`.
- Manual testing: `WITNESS_HUMAN=1 WITNESS_FPS=60 tools/restart.sh` starts both games for a person (real mouse and keyboard, A on the first monitor, B on the second); `tools/tour.sh list|go N|next|prev|here [A|B|both]` teleports the player to numbered spots across the island (31 map cells, each in front of a real panel).
- Full regression: `tools/qa.sh` (leases two slots, runs every scenario one after the other with its mutants, always cleans up; 28 scenarios took 48 minutes on 2026-10-24).
  Faster: `tools/qa_par.sh` splits the same suite over 3 shards, each its own lab owner with two slots (needs 6 free slots and about 11 GB of free RAM): 23 scenarios in about 13.5 minutes. Of five parallel runs on 2026-10-24 the last one passed everything; the first four exposed load flakes (a hygiene check that counted on slow scenarios, a late door, a game not yet in the world, a missed click), fixed as listed below.
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
