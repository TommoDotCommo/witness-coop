# witness-coop: agent instructions

This mod is built with the Co-op Anything playbook, cloned at `~/source/coop-anything`
(upstream `cranebeaker/coop-anything`). Follow its `AGENTS.md`. These are the parts that
apply to every session here.

## Start of every session
1. `git -C ~/source/coop-anything pull --ff-only` (on a branch that has diverged, do not force it; fetch and look)
2. Check for others' work on this game:
   `gh issue list -R cranebeaker/coop-anything --search "Witness" --state all` and
   `gh pr list -R cranebeaker/coop-anything --search "Witness" --state all`. Read open Witness PRs.
3. Read `~/source/coop-lab/LAB.md` (the rules for every agent on this PC: testing policy, game slots, the shared desktop).
4. `git worktree list` and `ListAgents`: several agents work on this repo at once. Work in your own worktree
   (`git worktree add ~/source/witness-coop-worktrees/<topic> -b <topic>`), never in a shared tree. The worktree folder name is
   your identity in the lab, so your slots, ports, game folders and pid files never collide with another agent's.

## Testing: automated first
Automated testing is preferred over manual testing, always (LAB.md section 1). A change is done when a script proves it: write the
test, watch it fail, fix the code, watch it pass; every protection gets a mutant the test must fail. Manual play is for finding
problems, and each problem found becomes a script. Use the cheapest tier that can catch it: T0 game-free (none for this repo yet,
extracting the sync core so it runs without the game is on the roadmap), T2 `tools/qa.sh`. Assert on state read from both
games, not on logs or screenshots.

## While working
- Notes as you go: append to `~/source/coop-anything/games/the-witness/NOTES.md` in the same
  session as the work, as a dated section (what was tried, what happened, evidence such as a
  commit hash, address or screenshot). Keep observations, derived facts and guesses in
  separate labelled lists, and write `unknown` rather than guessing. Report only checks that ran.
- No private data in NOTES.md or anything that may become a PR: no game files, no real names,
  no emails, and no paths with a username (`<game dir>`, `%APPDATA%`).
- A lesson another co-op modder would want (a technique, a trap, a QA method) goes in the
  playbook page it belongs to (`playbook/methods.md` in the hub), with the game as evidence.
- Never touch the real install or the owner's saves. Work in the two copies under `local/`
  (see README, Test copies). The game writes saves to `%APPDATA%\The Witness` whatever folder
  the exe runs from, so a copy does not isolate them: back that folder up and hash it first.

## Giving back (end of a phase or a big fix, not only at the end)
- Prepare the contribution on branch `game/the-witness` of the coop-anything clone: NOTES.md,
  `games/the-witness/README.md` (from `templates/game.md`), the `data/directory.yaml` entry plus
  `python tools/build_directory.py`, and playbook lessons.
- Publishing goes out under the owner's GitHub account. Show the owner the diff and get their OK before
  `gh issue create`, `gh pr create --draft`, or anything new on GitHub. PRs are always drafts, one
  game per PR, with the owner-review box left for the owner.
- Not pre-approved: unlike a repo whose owner has granted standing approval, nothing here is. Show the owner
  what will be pushed (this repo or the PR branch) and wait for a yes each time. Never force-push, and never
  `push --all` / `--mirror`.
- Match the hub's conventions: look at recent `gh issue list` / `gh pr list` titles first. Claims are
  titled "Claim: <Game> (<year>) - <approach>", PRs "<Game>: <what it adds>".
- Already published: claim issue cranebeaker/coop-anything#117 and draft PR #116 (branch `game/the-witness`
  on the fork, remote `fork`). Add to that PR while it is open (commit + `git push fork game/the-witness`)
  instead of opening a new one.

## Public repo and identity
- This repo is public at github.com/TommoDotCommo/witness-coop (MIT; `origin`, branch `main`).
- Commits must carry only the GitHub identity: `TommoDotCommo <15873245+TommoDotCommo@users.noreply.github.com>`
  (set in this repo's and the coop-anything clone's local git config; check `git config user.email`
  in any new worktree or clone). No real names, emails or usernamed paths in files or commit messages.
  Before pushing: `git log --format='%an %ae %cn %ce' origin/main..HEAD` and read the diff.

## Running the games (several agents at once)
Game slots come from the shared lab; `tools/qa.sh` does all of this itself: it leases two slots without waiting (exit 3 = busy:
do game-free work and retry, do not block), builds your hard-linked instance folders on the NTFS pool, runs, and in a trap kills
only your own games and frees the slots. Your folders, UDP ports and run folder come from the lease (`tools/config.sh`).
`tools/stop.sh` closes your games. `lab status` shows who holds what.
The Witness bot uses `SendInput`, which takes the keyboard and mouse of the whole desktop, so the suite holds `lab input-lock`
while the bot runs (one bot on the PC at a time) and interrupts anyone typing; run it when the owner is away or has said it is
fine. Games sit on the second monitor, started without taking focus; do not minimize them (a minimized Witness runs at 10 fps).
`tools/qa.sh baseline` runs one scenario; the full suite is about 15 minutes.
Windows tools are started with Windows Python from WSL. After a failed launch a "System Error" box (owned by `csrss.exe`) stays
on screen and shows up in screenshots: close it by title (`<exe> - System Error`) with WM_CLOSE.
