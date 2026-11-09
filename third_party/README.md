# The randomizers built into the mod

The launcher's Randomizers tab offers randomizers that other people wrote for The Witness. Each is vendored here nearly as its author published
it, one folder per project, and built into the mod's DLL inside a C++ namespace of its own (they are forks of one randomizer, so their classes
share names). All are under the MIT licence, copyright 2018 jbzdarkid (each folder's `LICENSE`); the players' copy of the notices is
`dist/THIRD-PARTY-NOTICES.txt`, which the launcher also shows.

| Folder | Upstream (`UPSTREAM`: commit, repository, branch) | Modes in the mod |
|---|---|---|
| `witness-randomizer` | [NewSoupVi's Witness Random Puzzle Generator for Archipelago](https://github.com/NewSoupVi/The-Witness-Randomizer-for-Archipelago) (sigma144's generator, jbzdarkid's original, Penguin12335's Variety) | Sigma's Normal, Sigma's Expert, Variety |
| `variety-randomizer` | [Penguin12335's Variety Randomizer](https://github.com/Penguin12335/Variety-Randomiser) | Ascension |
| `despair-randomizer` | [chuukunn's Despair Randomizer](https://github.com/chuukunn/witness-randomizer), branch `chuukunn_randomizer` | Despair, Symbols |
| `symbol-shuffler` | [Dawgup's (KemSpeedruns) Symbol Shuffler](https://github.com/KemSpeedruns/witness-randomizer), branch `main` | Symbol Shuffler, Emptiness, Puzzle Pack |
| `star-randomizer` | [Parker's (MrP4rK3r2) Star Randomizer](https://github.com/MrP4rK3r2/witness-randomizer), release 1.0.1 (the later 1.0STAR commit has unresolved merge conflicts in its sources) | Star |
| `tutorializer` | [hatkirby's Tutorializer](https://code.fourisland.com/witness-tutorializer) | Tutorializer |

The Witness Trainer's co-op options (walking speed, field of view, noclip, no challenge time limit, the environmental-puzzle overlay) are not
vendored: `proxy/trainer.cpp` ports the parts of [jbzdarkid's Witness Trainer](https://github.com/jbzdarkid/witness-trainer) they need (MIT,
copyright 2026 jbzdarkid).

The puzzle editor's model, rules, solver and puzzle text (`common/wpuzzle.h`, `common/wpuzzle_json.h`) are not vendored either: they port
[jbzdarkid's witnesspuzzles.com engine](https://github.com/jbzdarkid/jbzdarkid.github.io) (`engine/puzzle.js`, `validate.js`, `polyominos.js`,
`solve.js`; BSD 3-Clause, copyright 2017 jbzdarkid; the notice is in `dist/THIRD-PARTY-NOTICES.txt`), keeping his structure and names.
`tools/wpuzzle_diff.py` checks the port against his JavaScript on the puzzles published there (a clone of his repository, anywhere; it is not
part of the mod).

## What is ours

- `witness-randomizer/shim/`: what replaces upstream's files that reach the game or the desktop, shared by every folder. `Memory.h` (upstream's
  `Memory.h`/`.cpp`, `ReadProcessMemory` from another process) sends every read and write to the calling thread's backend
  (`proxy/randomizer/backend.h`), in both of upstream's flavours: the current singleton (`Memory::get()`) and the older forks' object per use,
  whose `WritePanelData` also forgets what it knew of that field's array. `ClientWindow.h` (the randomizer's window) reports progress to the mod;
  `Utilities.h` keeps only `isAprilFools` (always false, so every PC makes the same puzzles); `compat.h` is force-included for the headers MSVC
  brings in by itself. Upstream's `Random.cpp` (seeded from the clock) is not used: the mod seeds from the host's choice.
- `proxy/randomizer/`: the wrappers and glue (`variant.h`, `variant_prelude.h`, `variants.mk`, `variants/<v>.cpp`): each vendored `.cpp` is
  compiled as `namespace v_<v> { #include "<file>" }` after the prelude; the glue resets what upstream keeps between runs, seeds it, runs a mode and
  hands back its watchdogs. The older forks are built with MSVC's `min`/`max` macros (their code calls them bare; `<v>_FLAGS`). The Tutorializer
  has its own memory layer: `variants/tutorializer_inside.h` gives it one on the same backend, with the panel offsets its own `Memory.cpp` uses
  for this build.

## The marked changes

Every change to an upstream file is on a line that says `witness-coop:`, or, for a whole upstream pull request applied, under one such line naming it. `tools/randomizer_upstream.sh all status` lists them against each
folder's `UPSTREAM` commit.

| Where | Why |
|---|---|
| every folder's `Watchdog.cpp` | `Watchdog::start` and `run` left out: each watchdog ran on a thread of its own, writing the game's memory; the mod runs them on the render thread |
| every folder's `Randomizer.cpp` (Tutorializer's too) | `std::exception("...")` is MSVC only: `std::runtime_error` |
| every folder's `Randomizer.cpp` but the Tutorializer's | `RandomizeDesert` permuted the global `desertPanels`: a second run in one process (a save loaded again) made other puzzles than a partner's first run; it works on a copy |
| `Watchdog.h`/`.cpp` of `witness-randomizer`, `variety-randomizer`, `despair-randomizer`, `star-randomizer` | the Jungle watchdog keeps two pointers in `long`, 32 bits on Windows: `intptr_t` |
| `symbol-shuffler/src/PuzzleList.cpp` | `void::PuzzleList::` (MSVC) is not valid inside a namespace: `void PuzzleList::` |
| `star-randomizer/App/Version.h` | `L#s` is MSVC only: `L ## #s` |
| `symbol-shuffler/src/Panel.cpp` (`SavePanels`) | each run wrote `puzzledata<difficulty><seed>.dat` into the working folder (the game's folder in the game), for upstream's own reload of a seed: not written |
| `despair-randomizer/src/PuzzleList.cpp` (`GenerateSymmetryH`) | the symmetry lists hold repeats and a repeat is popped again, so for some seeds (4242) a list ran empty and the next pop divided by zero: the game died. Refilled when empty; every seed that worked makes the same puzzles |
| `variety-randomizer/src/Special.cpp` (`generateKeepLaserPuzzle`) | NewSoupVi's fix, [Penguin12335/Variety-Randomiser PR #1](https://github.com/Penguin12335/Variety-Randomiser/pull/1) (open; his own Variety in `witness-randomizer` ships it): the Keep laser puzzle could retry for ever; after 10000 failures one square colour loses a square, again until it fits |
| `Generate.cpp`/`.h` of `variety-randomizer`, `despair-randomizer`, `star-randomizer` (`place_all_symbols`, `place_shapes`) | NewSoupVi's shape-rotation fix, [sigma144/witness-randomizer PR #104](https://github.com/sigma144/witness-randomizer/pull/104) (open; `witness-randomizer` ships it): rotated and rotated negative shapes counted apart, so a puzzle gets the shapes it asks for. The PR's `std::min`/`std::max` are written `(std::min)`/`(std::max)`: these forks build with MSVC's `min`/`max` macros. Not in `symbol-shuffler`: with it, Puzzle Pack never finished for 16 of 30 seeds (its random puzzles ask for shape mixes the stricter counting cannot fit) |
| `variety-randomizer/src/Generate.cpp` (`Generate::generate`) | a panel was retried for ever: in Ascension, Mountain panel 0x09FD6 (ghosts, bars and antitriangles on a 6x6 grid) never fitted for any seed tried (15 minutes natively). After `WC_MAX_TRIES` (2000, `variants.mk`) failures in a row a panel keeps the game's own puzzle; the glue resets the count before a run |

Not carried over: the Tutorializer's two patches to the game's code that stop the endings from ending the game (in the co-op mod the endings
work as in the game). How the generators run in the game, what is tested and how: `docs/DEVELOPMENT.md`, "Built-in randomizers".

## Upstream pull requests

Checked 2026-11-09 (open pull requests of every upstream): NewSoupVi's Keep laser fix (Penguin12335/Variety-Randomiser #1) is applied above; his
shape-rotation fix (sigma144/witness-randomizer #104) is applied to Ascension, Despair and Star (above; not to the Symbol Shuffler, where it made
Puzzle Pack hang); both are already in `witness-randomizer`. Not applied: the stone-region change
(NewSoupVi #35, sigma144 #109, by lkaesberg: a change to how stone puzzles are made, not a crash or hang fix, unmerged by either maintainer), the
Mac port (sigma144 #78), and KemSpeedruns #1 (Parker's work into the Symbol Shuffler; the Star Randomizer is vendored from Parker's own repository).
When one of these is merged upstream, `tools/randomizer_upstream.sh <folder> update` brings it in.

## Updating to a newer upstream

1. `tools/randomizer_upstream.sh <folder> update <commit>`: a three-way merge per file; `UPSTREAM` moves on only without conflicts. It also names
   files upstream added (a new source file the generator may now need: add it to that variant's `_SRC` in `proxy/randomizer/variants.mk`).
2. `make -C proxy/randomizer test` (game-free: the memory layer and, over a recorded game, whole runs of every randomizer, twice in one process),
   and `tools/randomizer_sweep.py` (every mode over 30 seeds, each run in a process of its own: a crash or a hang in a generator would kill or
   stall the game; the two upstream bugs above were found this way).
3. `tools/randomizer_solo.sh --record` records the game again with the new generator (the recorded fingerprints are what the game makes), then
   step 2 again.
4. `tools/qa.sh randomizer --spec <mode;seed:N>` for the modes that changed (two games: the same puzzles, a solve shared, other puzzles told),
   and `tools/qa.sh randomizer --mutant`.

## Adding another fork

Vendor its `Source/*.cpp`/`.h` (not `Memory.*`, `Random.cpp`, `pch.*`) into `third_party/<folder>/src` with its `LICENSE` and an `UPSTREAM`; mark
the changes it needs as above; add it to `proxy/randomizer/variants.mk`, write `proxy/randomizer/variants/<v>.cpp` from another variant's glue
(its statics to reset come from a search for statics in its sources), declare `Run<V>` in `variant.h` and route its modes in `generate.cpp`'s
`FindVariant`; add its modes and credits to `common/randomizers.h` and its notice to `dist/THIRD-PARTY-NOTICES.txt`.
