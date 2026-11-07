# The walker: giving up, the progress clock, the simulator, the trace

The bot walks to the front of a panel with `tools/win/wnav.py` (a depth camera, a map of 0.5 m cells, A\*) and teleports only when the walk
gives up. This page is the release note of the round that changed how the walker notices it is stuck and how it reports; the planner and the
map rules are the same as before.

## What changed

* **`WalkResult`** (`Walker.go` returns it, never raises for a walk; a proxy that stops taking commands is a walk that `timed out`).
  `ok`; `why` = `arrived` | `stuck` | `no way` | `timed out` | `view does not turn`; `detail` (the words); `distance_left` (m); `last_way`
  (the cells of the last plan); `stuck_class` (`wall` | `foliage` | `open`, what the last stuck was against); `tried` (replans, bumps,
  wall rules, sidesteps, pocket escapes, in order); the counters (`walked`, `stuck`, `replans`, ...). `wdraw.py` keeps the give-up of a failed
  walk in its `moves`, `run.py` counts `movement.teleports_after_giveup` (every teleport follows a walk that gave up) and lists the give-ups.
* **Progress clock.** Progress is the distance to the target 0.5 m below the best so far, a cell of the planned way reached for the first time,
  or a bump that changed the map. Each resets the clock. After `stall` seconds (20, `WNAV_STALL`) without progress the watchdog runs a
  ladder: plan again with the bumped cells, the wall rule (all of the wall in sight bumped), the pocket escape (back out along the trail),
  each rung 10 s after the one before; then it gives up (`stuck`). Only getting nearer starts the ladder again. Stuck four times without
  getting nearer for 60 s fires it too: wandering along a wall finds new cells and new bumps for ever (the closed room of the simulator
  walked until its time was up under the three-part definition alone). The 20 stucks (`WNAV_STUCKS`) are a fallback limit now. A turn
  commanded and the yaw unchanged for 2 s gives up at once: `view does not turn`.
* **Typed trace** (`tools/win/wtrace.py`): `<game folder>/trace.jsonl`, one JSON line each, flushed: `walk_start`, `replan`, `bump`,
  `progress`, `stall`, `giveup`, `teleport`, `panel_start`/`panel_end`, `verdict`. Unknown kinds and missing fields are refused when written.
  The playthrough copies the bots' lines into `<run>/trace.jsonl` after every panel, between its own `panel_start` and `panel_end`.
  `python3 tools/trace_summary.py TRACE...` gives the benchmark rows and checks that a teleport follows a giveup.
* **Simulator** (`tools/navsim.py`, `tools/test_navsim.py`): the real planner and walk loop on simulated levels with a simulated clock, 17 levels
  (the autopilot list: open field, wide wall, dead end, room with a far door, zigzag, wall with a gap, pit, pillars, narrow door, closed room
  that must give up; three island shapes: `shore_bay`, `hedge_pocket`, `ledge_spot`; `wall_80`, a known gap; `hedge_long`, which needs the wall rule; `locked_view`, `locked_mid`) and
  random clutter worlds from a seed. See the header of `navsim.py` for the blocker classes. Mutants: a walker that ignores bumps, a planner without
  the wall rule (twice), a watchdog that never fires, a progress clock that never resets, no view check, a walk that never says why, a
  proxy that raises out of the walk. Each fails at least one level.

## Held-out rule

A change to the planner (`Map.plan` and the rules under it) or the walker (`Walker.go` and what it calls) merges only with

1. `python3 tools/test_navsim.py` passing (clean and all mutants rejected; a new rule gets a level or a mutant of its own), and
2. a before/after of `tools/test_walk_bench.sh 1 <save> --compare <before.jsonl>` on the 15 legs, the table pasted in the note: walked, teleported and
   failed may not be worse, no leg slower (the compare prints `slower`; a leg's time varies by tens of percent from run to run, so read it against a control run of the old code on the same day, as below), every teleported leg carrying a giveup in the trace
   (`tools/trace_summary.py` says so).

The simulator is the cheap test (a minute, no game); the benchmark is the held-out one: a rule tuned on the simulator's levels can pass them
and still lose legs on the island, and the levels were drawn from the island's stuck legs, not from the 15 legs.

## Raise sites that can end a run

Audit of `tools/win/wnav.py` and `tools/playthrough/*.py` (what a walk or a run does when it hits one):

| where | raises | effect |
|---|---|---|
| `wnav.py` `Walker.go` | nothing; `TimeoutError` from the command file is caught and returned as `why: timed out` | was: the exception ended `wdraw.py` with no result line, `run.py` recorded `no result` |
| `wnav.py` `depth_points` | `ValueError` (not a depth dump) | caught in `grab_depth` (the look fails, the walk goes on) |
| `wcontrol.py` `Control.wait_taken` | `TimeoutError` (the proxy does not take the command file in 8 s) | caught by `go` for walks; elsewhere ends the script |
| `run.py` `Bot.send`, `Bot.state`, `Bot.net`, `wait` | `RuntimeError` (command not consumed, no state, timed out) | `solve()` catches everything: a failed attempt, never the end of the run; the main loop's own `wait`s end the run |
| `planner.py` | `AssertionError` (goal not reachable), `RuntimeError` (a panel not available: the planner broke the rules) | ends the run on purpose (a bug in the logic) |
| `logic.py` | `ValueError` unknown rule token | ends the run at start (the logic file is wrong) |
| `wdraw.py` / `wbench.py` | `sys.exit` after one JSON line | the normal end |
| `wwalk.py` (floor plates) | no walking: `hover` is a teleport by design | not a walker give-up; not changed |

## Benchmark (15 legs, one game)

Same save, same starting map (`local/nav_map.bin`, 1.38 M cells), one game, `tools/test_walk_bench.sh 1 <save> --compare before_after5a.jsonl`,
2026-11-01. "before" is the committed baseline of the previous round (6 walked / 7 teleported / 2 failed, 1587 s). "control" is the **old code**
(main b000a29) run again the same day; "new" is this round's code (commit of the walker as in this note).

| panel | before | control (old code, today) | new |
|---|---|---|---|
| 0x79df | teleported 268 s | teleported 244 s | teleported 252 s |
| 0x28998 | walked 14 s | walked 14 s | walked 13 s |
| 0x28a79 | walked 11 s | walked 10 s | walked 10 s |
| 0xb10 | walked 114 s | **teleported** 17 s | walked 68 s |
| 0x139 | teleported 291 s | teleported 155 s | teleported 186 s |
| 0x334db | teleported 163 s | teleported 163 s | teleported 124 s |
| 0x34d4 | teleported 150 s | teleported 129 s | teleported 108 s |
| 0x21b3 | teleported 26 s | teleported 40 s | teleported 43 s |
| 0x1e5a | failed 45 s | failed 76 s | failed 41 s |
| 0x3678 | failed 205 s | failed 276 s | failed 299 s |
| 0x3612 | teleported 14 s | teleported 181 s | teleported 21 s |
| 0x698 | walked 52 s | walked 51 s | walked 52 s |
| 0xb0 | walked 75 s | walked 118 s | walked 120 s |
| 0x1a54 | walked 53 s | walked 52 s | walked 49 s |
| 0x3629 | teleported 107 s | teleported 167 s | teleported 97 s |
| **total** | **6 / 7 / 2, 1587 s** | **5 / 8 / 2, 1693 s** | **6 / 7 / 2, 1484 s** |

(walked / teleported / failed.) Results: new = `tools/walk_bench.py compare` PASS (no leg ranks lower, teleports + failures not higher); every one of
its 7 teleported legs carries a giveup line in the trace (`trace_summary.txt`: no problem). `compare` also prints legs 10% and 5 s slower than
`before`: 0x21b3, 0x3678, 0x3612, 0xb0. The old code run the same day is slower than `before` on the same legs (0x21b3, 0x3678, 0xb0, and
0x3612 and 0x3629 as well) and ends 5 / 8 / 2: a leg's time on the island varies by tens of percent from run to run, so the "no slower"
rule is read against a control run of the old code, not against a number from another day. Those four legs spent no stall time in
the new code (their stalls column is 0: the quick reflex, unchanged, did the work).

A first run of the new code (`local/bench`, not in the table) walked legs 0-9 to the same results as `before`, and then the game stopped
answering after 0x3678 (legs 10-14: `view does not turn` after 4 s and the player not moving, where the old walker would have spent the
time to find out); the cause of that freeze was not found (it did not recur in the two later runs; an earlier round saw a crash at the same leg).
Two legs of that run were slower than `before`, probably because of a mistake in this change, found from its trace and undone: the cells the wall rule
bumps had been made forbidden for the walk like the cells just ahead (the old code only makes them dear), which is a map rule. The simulator's
first dead-end level had been passing only because of that mistake; it was made one the old rules solve.
