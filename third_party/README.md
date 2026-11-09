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

Every change to an upstream file is on a line that says `witness-coop:`. `tools/randomizer_upstream.sh all status` lists them against each
folder's `UPSTREAM` commit.

| Where | Why |
|---|---|
| every folder's `Watchdog.cpp` | `Watchdog::start` and `run` left out: each watchdog ran on a thread of its own, writing the game's memory; the mod runs them on the render thread |
| every folder's `Randomizer.cpp` (Tutorializer's too) | `std::exception("...")` is MSVC only: `std::runtime_error` |
| every folder's `Randomizer.cpp` but the Tutorializer's | `RandomizeDesert` permuted the global `desertPanels`: a second run in one process (a save loaded again) made other puzzles than a partner's first run; it works on a copy |
| `Watchdog.h`/`.cpp` of `witness-randomizer`, `variety-randomizer`, `despair-randomizer`, `star-randomizer` | the Jungle watchdog keeps two pointers in `long`, 32 bits on Windows: `intptr_t` |
| `symbol-shuffler/src/PuzzleList.cpp` | `void::PuzzleList::` (MSVC) is not valid inside a namespace: `void PuzzleList::` |
| `star-randomizer/App/Version.h` | `L#s` is MSVC only: `L ## #s` |

Not carried over: the Tutorializer's two patches to the game's code that stop the endings from ending the game (in the co-op mod the endings
work as in the game). How the generators run in the game, what is tested and how: `docs/DEVELOPMENT.md`, "Built-in randomizers".

## Updating to a newer upstream

1. `tools/randomizer_upstream.sh <folder> update <commit>`: a three-way merge per file; `UPSTREAM` moves on only without conflicts. It also names
   files upstream added (a new source file the generator may now need: add it to that variant's `_SRC` in `proxy/randomizer/variants.mk`).
2. `make -C proxy/randomizer test` (game-free: the memory layer and, over a recorded game, whole runs of every randomizer, twice in one process).
3. `tools/randomizer_solo.sh --record` records the game again with the new generator (the recorded fingerprints are what the game makes), then
   step 2 again.
4. `tools/qa.sh randomizer --spec <mode;seed:N>` for the modes that changed (two games: the same puzzles, a solve shared, other puzzles told),
   and `tools/qa.sh randomizer --mutant`.

## Adding another fork

Vendor its `Source/*.cpp`/`.h` (not `Memory.*`, `Random.cpp`, `pch.*`) into `third_party/<folder>/src` with its `LICENSE` and an `UPSTREAM`; mark
the changes it needs as above; add it to `proxy/randomizer/variants.mk`, write `proxy/randomizer/variants/<v>.cpp` from another variant's glue
(its statics to reset come from a search for statics in its sources), declare `Run<V>` in `variant.h` and route its modes in `generate.cpp`'s
`FindVariant`; add its modes and credits to `common/randomizers.h` and its notice to `dist/THIRD-PARTY-NOTICES.txt`.
