# board_layout manual

`include/board_layout.h` / `src/board_layout.c` -- maps `game_logic.c`'s
abstract pawn state onto a concrete, placeholder board geometry. Pure C,
no OSLib/WIMP dependency (like `game_logic.c`), so it's host-testable --
see `tests/test_board_layout.c` (5 tests, 47 checks).

## Why this is a separate module from `game_logic.c`

`game_logic.c` knows about steps and rules, not squares and pixels.
`src/game_view.c` (the WIMP shell) needs to turn a pawn's `steps`/`in_play`/
`finished` state into an actual place on screen. `board_layout.c` is the
translation layer between the two -- kept separate so `game_logic.c` stays
free of any notion of "board shape" (a future alternate board skin, or a
completely different visual layout, would only touch this module and
`game_view.c`, never the rules engine).

## Geometry

A `BOARD_GRID_SIZE x BOARD_GRID_SIZE` (15x15) grid of cells (see the
diagram in `include/board_layout.h`'s header comment for the full
reasoning). In summary:

- **Ring** (40 cells, `LUDO_RING_LENGTH`): a square frame at grid
  column/row 2 or 12, giving exactly 40 unique cells (4 sides of 10) --
  see `board_ring_cell()`.
- **Home column** (4 cells per player): runs diagonally from just inside
  each player's ring entry corner to the centre cell (7,7) -- see
  `board_home_column_cell()`.
- **Home base** (4 slots per player): a 2x2 block in one of the four
  outer corners (columns/rows 0-1 or 13-14) -- see `board_home_base_cell()`.
- **Finished**: pawns that have completed their home column are drawn
  stacked at the centre cell -- see `board_finished_cell()`.

`board_pawn_cell(g, player, pawn_index)` is the one function
`src/game_view.c` actually calls per pawn each redraw -- it dispatches to
whichever of the above matches that pawn's current state.

This is a deliberately simple, symmetric placeholder for Phase 1 (see
`docs/ARCHITECTURE.md`'s Roadmap), not a pixel-accurate recreation of a
physical board -- confirmed against the traditional layout described at
[Mens erger je niet!](https://nl.wikipedia.org/wiki/Mens_erger_je_niet!)
(4 pawn colours, a private "eindcirkel"/home-run per player) as a sanity
check that the *structure* matches, even though Phase 1's actual on-screen
look is flat colour rectangles rather than real board art.

## How it's rendered (in `src/game_view.c`)

`game_view.c` builds a one-time lookup table (`build_cell_kinds()`) by
calling `board_ring_cell()`/`board_home_column_cell()`/
`board_home_base_cell()`/`board_finished_cell()` for every index, marking
each of the 225 grid cells as ring/home-column/home-base/centre/empty
(plus owning player, for the coloured ones). The Redraw_Window handler
then just walks that table each time, filling each non-empty cell with a
flat colour, and plots each pawn's sprite at its `board_pawn_cell()`
position on top. See `docs/GRAPHICS_TOOLING.md` for the pawn sprites
themselves.

## Updating this file

Add a note here if the grid size, ring shape, or home-column path changes
-- and update the corresponding tests in `tests/test_board_layout.c` at
the same time.
