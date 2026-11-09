# The Witness Random Puzzle Generator, built into the mod

The mod's built-in randomizers (the launcher's Randomizers tab: Sigma's Randomizer Normal and Expert, and the Variety Randomizer) are the puzzle
generator of [NewSoupVi's Witness Random Puzzle Generator for Archipelago](https://github.com/NewSoupVi/The-Witness-Randomizer-for-Archipelago),
which carries [sigma144's randomizer](https://github.com/sigma144/witness-randomizer) (Normal, Expert), [Penguin12335's Variety](https://github.com/Penguin12335/Variety-Randomiser)
and [jbzdarkid's original](https://github.com/jbzdarkid/witness-randomizer). MIT licence, copyright 2018 jbzdarkid ([LICENSE](LICENSE)); the
players' copy of the notices is `dist/THIRD-PARTY-NOTICES.txt`, which the launcher also shows.

- `UPSTREAM`: the upstream commit these files come from.
- `src/`, `App/Version.h`: upstream's files (`Source/` and `App/` there), with their line endings, changed only where a line says `witness-coop:`.
  `tools/randomizer_upstream.sh status` lists every change against `UPSTREAM`.
- `shim/`: what replaces upstream's files that reach the game or the desktop. `Memory.h` (upstream's `Memory.h`/`.cpp`: `ReadProcessMemory` from another
  process) sends every read and write to the calling thread's backend (`proxy/randomizer/backend.h`); `ClientWindow.h` (the randomizer's window)
  reports progress to the mod; `Utilities.h` keeps only `isAprilFools` (always false, so every PC makes the same puzzles); `compat.h` is
  force-included for the headers MSVC brings in by itself. Upstream's `Random.cpp` (seeded from the clock) is not used: the mod seeds from the
  host's choice (`proxy/randomizer/generate.cpp`).

The marked changes:

| File | Why |
|---|---|
| `src/Watchdog.cpp` | `Watchdog::start` and `run` left out: each watchdog ran on a thread of its own, writing the game's memory; the mod runs them on the render thread |
| `src/Watchdog.h`, `src/Watchdog.cpp` | the Jungle watchdog keeps two pointers in `long`, 32 bits on Windows: `intptr_t` |
| `src/Randomizer.cpp` | `std::exception("...")` is MSVC only: `std::runtime_error` |
| `src/Randomizer.cpp` | `RandomizeDesert` permuted the global `desertPanels`: a second run in one process (a save loaded again) made other puzzles than a partner's first run; it works on a copy |

How the generator runs in the mod, what is tested and how: `docs/DEVELOPMENT.md`, "Built-in randomizers".

## Updating to a newer upstream

1. `tools/randomizer_upstream.sh update <commit>`: a three-way merge per file; `UPSTREAM` moves on only without conflicts. It also names files
   upstream added (a new source file the generator may now need: add it to `proxy/Makefile` and `proxy/randomizer/Makefile`, `VSRC`).
2. `make -C proxy/randomizer test` (game-free: the memory layer and, over a recorded game, whole runs of every randomizer, twice in one process).
3. `tools/randomizer_solo.sh --record` records the game again with the new generator (the recorded fingerprints are what the game makes), then step 2 again.
4. `tools/qa.sh randomizer` (two games: the same puzzles, a solve shared, other puzzles told) and `tools/qa.sh randomizer --mutant`.
