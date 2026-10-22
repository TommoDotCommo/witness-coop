# witness-coop: agent instructions

This mod is built with the Co-op Anything playbook, cloned at `~/source/coop-anything`
(upstream `cranebeaker/coop-anything`). Follow its `AGENTS.md`. These are the parts that
apply to every session here.

## Start of every session
1. `git -C ~/source/coop-anything pull --ff-only` (on a branch that has diverged, do not force it; fetch and look)
2. Check for others' work on this game:
   `gh issue list -R cranebeaker/coop-anything --search "Witness" --state all` and
   `gh pr list -R cranebeaker/coop-anything --search "Witness" --state all`. Read open Witness PRs.
3. `git worktree list` and `ListAgents`: if another session is working on this repo, work in
   your own worktree (`git worktree add ../witness-coop-<topic> -b <topic>`), never in a shared tree.
   Use separate game copies and ports for parallel game runs.

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

## Testing
The games are real windows and the bot uses real keyboard and mouse input, so the machine cannot be used while a
scenario runs (about 4 minutes each, 15 for the whole suite). `tools/qa.sh` is the full suite; `tools/qa.sh baseline`
runs one scenario by name. `tools/restart.sh` closes only the two copies it started (pid files in `local/run/`).
Windows tools are started with Windows Python from WSL; `powershell.exe` hangs under this setup.
After a failed launch a "System Error" box (owned by `csrss.exe`) stays on screen and shows up in screenshots:
close it by title (`<exe> - System Error`) with WM_CLOSE.
