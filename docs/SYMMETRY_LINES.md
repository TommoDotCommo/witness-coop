# Release note: the second line of a symmetry panel

**Problem (owner playtest, 2026-10-31).** When another player draws on a symmetry panel (Symmetry Island, the
glass-factory and other mirrored panels) the watching player saw only one line, not the pair the drawing player sees.
Single-line panels were fine.

## What the game does (read from a live game, Steam build 11370412)

* A panel keeps **one** traced path: the count at `+0x228` and the records at the pointer in `+0x230` (13 dwords, 52 bytes
  each: node a, node b, two ids, the fraction drawn, the world positions of both ends). While a mirrored panel was being drawn
  the array held only the player's own edges. A scan of the whole process for the reflected edges (the same record with both
  node numbers replaced by their mirror nodes) found none, and the other fields near the trace are name strings.
* The reflection is **implicit**: the panel keeps a table that names, for every node, its mirror node (`+0x438`, one int per
  node, the same table the sweep manifest calls `mirror`), the grid place of every node (`+0x3c0`, u and v per node, `+0x3b4`
  nodes) and the node flags (`+0x3c8`; `0x100` / `0x200` mark the coloured dots). The game draws the one path and its reflection
  through that table.
* The shared line is built from the one path (`proxy/state.cpp`, `scanLines`) and drawn by the other game as ribbons in the
  panel's surface (`proxy/overlay.cpp`). Nothing there knew about the table, so the sender sent the one path and the receiver
  drew the one path: **the reflection was never drawn**. The sender and the wire format were not at fault.
* Where a node is in the panel's frame is an affine map `p = o + u*a + v*b`; on panel 0x22 it fits all eight nodes of a real
  trace to 0.4 mm. The surface is tilted about 4 degrees out of the rotation the panel reports (`b` has a component along the
  surface normal), so a flat guess would put the reflection 4 cm off the panel.

## The fix

`proxy/frame/mirror.h` (game-free, `make -C proxy/frame test`): the table is turned into a flip of the grid (u, v or both
about the middle of the mirror pairs; it refuses a table no flip explains), the surface map is fitted from the nodes of the
line itself (least squares; with the nodes on one grid line, which is the start of a line, it uses the fit remembered from an
earlier line on that panel, else a flat guess), and each point of the line is turned into (u, v), flipped and put back, so the
tip of a line still being drawn, and a solved line, are reflected the same way. `overlay.cpp` draws the reflection as a second set of
ribbons from the receiver's own copy of the table, in the panel's frame (it follows a door that swings). Single-line panels
have no table and take the old code path unchanged; the sender and the wire format did not change.

Colours: the panel data has no colour fields for the two lines. On panels with coloured dots the player's line is drawn cyan
and its reflection yellow, on the other symmetry panels both lines keep the player's colour. Which of the two real lines is
cyan is **not established**: see the open question below.

## Gate

* Game-free: `make -C proxy/frame test` (10 checks; mutants: no flip, wrong axis, flat surface, all killed).
* Two games: `tools/qa.sh symmetry` (`tools/qa_symmetry.py`): a bot draws panel 0x22 in one game; the other game must report
  the line as mirrored (`linestat ... mirrored 1`), about twice the ribbon vertices (1.8 to 2.2) and at least 1.5 times the
  visible pixels (GPU occlusion query) of the same line with the reflection switched off (`linemirror 0`). Both directions run;
  the mutant is `no_line_mirror=1` on the watcher and must fail. `tools/qa.sh lines` is unchanged.

## Open question for the owner

Which line is cyan and which yellow on the coloured symmetry panels (Symmetry Island Back, Left, and so on)? One screenshot of a
half-drawn line in the real game settles it; the constants are `cyan` / `yellow` in `BuildLineVerts`.
