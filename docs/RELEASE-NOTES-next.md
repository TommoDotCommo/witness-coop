# The Witness Co-op: the next release (draft)

(A draft for the release after v0.2.0-beta.1, with the built-in randomizers and the puzzle editor. Rename it to
`RELEASE-NOTES-<version>.md` when the version is chosen.)

## New: puzzles of your own, and randomizers

- **Randomizers, built in.** On the launcher's new **Randomizers** tab: the Witness Randomizer (Normal, Expert, Variety),
  the Symbol Shuffler's modes, the Star Randomizer, the Tutorializer and more, each with a seed you can share. In a lobby the
  host's choice is everyone's, so you all play the same puzzles. Each one's authors are named on the tab.
- **Make your own puzzles.** Choose **Your puzzles** on the Randomizers tab and press **Open the puzzle editor**. Draw a
  puzzle on a grid of up to 7 by 7 with the game's symbols: squares and stars in the game's colours, eliminators,
  triangles, shapes (turnable or negative), dots, gaps and missing lines, start and end points, and symmetry. Pick the
  panel it goes on, or let it take the next free one. The editor shows the puzzle in that panel's own colours, and tells
  you when a symbol would be hard to see there.
- **Try them before you play.** **Try it** lets you draw a line as on a panel; **Show a solution** and **Count solutions**
  tell you whether it can be solved and how many ways. The rules are those of witnesspuzzles.com, and in the game the
  panels judge your lines the same way.
- **Share them.** In a lobby the host's puzzles are sent to everyone before the game starts. **Import** takes a puzzle
  from [witnesspuzzles.com](https://witnesspuzzles.com), and **Copy for the web** gives you the text to put there.
- **Continue as you left it.** A continued game keeps the puzzles it was started with, even after you change the set in
  the editor.

## Good to know

- **Everyone in a lobby needs this version.** The lobby has changed to send puzzle sets, so this launcher cannot join a
  lobby of an older version, or the other way round. The launcher says so.
- Puzzles go on ordinary grid panels of the Tutorial, the area outside it, Symmetry Island, the Quarry, the Swamp and the
  Town; every other panel keeps the game's own puzzle.
- A start point must be on a corner of the grid, not in the middle of a line, and a gap must be on a line, not on a
  corner. The editor tells you.

## Verified

- In the game, with two games (2026-11-09 and -10): a set of puzzles of every kind was applied the same in both games,
  each panel held its puzzle, the game accepted the editor's solutions and refused lines its rules refuse (among them an
  eliminator with nothing to remove), and each solve reached the other game. The deliberately broken build (shapes upside
  down) was refused by the game.
- Through the launchers: the host's set reached the other player and both games started with it; a launcher that never
  sends the set holds the start.
- Without the game: the editor's rules gave the same solutions as witnesspuzzles.com's own on the 7454 published puzzles
  that fit a panel; every one of the 83 panels a puzzle may go on takes one and reads it back.
