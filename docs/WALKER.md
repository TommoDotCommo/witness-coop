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
* **Simulator** (`tools/navsim.py`, `tools/test_navsim.py`): the real planner and walk loop on simulated levels with a simulated clock, 16 levels
  (the autopilot list: open field, wide wall, dead end, room with a far door, zigzag, wall with a gap, pit, pillars, narrow door, closed room
  that must give up; three island shapes: `shore_bay`, `hedge_pocket`, `ledge_spot`; `wall_80`, a known gap; `locked_view`, `locked_mid`) and
  random clutter worlds from a seed. See the header of `navsim.py` for the blocker classes. Mutants: a walker that ignores bumps, a planner without
  the wall rule (twice), a watchdog that never fires, a progress clock that never resets, no view check, a walk that never says why, a
  proxy that raises out of the walk. Each fails at least one level.

## Held-out rule

A change to the planner (`Map.plan` and the rules under it) or the walker (`Walker.go` and what it calls) merges only with

1. `python3 tools/test_navsim.py` passing (clean and all mutants rejected; a new rule gets a level or a mutant of its own), and
2. a before/after of `tools/test_walk_bench.sh 1 <save> --compare <before.jsonl>` on the 15 legs, the table pasted in the note: walked, teleported and
   failed may not be worse, no leg slower (the compare prints `slower`), every teleported leg carrying a giveup in the trace
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

