# board_layout manual

`include/board_layout.h` / `src/board_layout.c` -- maps `game_logic.c`'s
abstract pawn state onto the real Mens Erger Je Niet board's cells. Pure
C, no OSLib/WIMP dependency (like `game_logic.c`), so it's host-testable
-- see `tests/test_board_layout.c` (3 tests, 169 checks).

## Why this is a separate module from `game_logic.c`

`game_logic.c` knows about steps and rules, not squares and pixels.
`src/game_view.c` (the WIMP shell) needs to turn a pawn's `steps`/`in_play`/
`finished` state into an actual place on screen. `board_layout.c` is the
translation layer between the two -- kept separate so `game_logic.c` stays
free of any notion of "board shape" (a future alternate board skin, or a
different board entirely for one of the future rule variants noted in
`docs/ARCHITECTURE.md`'s Roadmap, would only touch this module and
`game_view.c`, never the rules engine).

## Geometry: ported from the GEOS edition, not invented

The first Phase 1 draft of this module used an invented square-ring
layout. After seeing it running in Arculator, the user pointed out it
didn't look like Mens Erger Je Niet at all, and asked to use exactly the
same board as this game's own GEOS port instead -- so the geometry here
is now a direct conversion of
`/home/xahmol/git/ludo/GEOS/src/main.c`'s `fieldcoords[40][2]` (the ring)
and `homedestcoords[4][8][2]` (each player's home base + home column),
not a new design.

**Conversion**: GEOS's coordinates use 2-unit steps on a wider native
grid -- x is always even (0..20), y is always odd (3..23). Converting with
`col = raw_x/2`, `row = (raw_y-3)/2` lands every one of them exactly on an
11x11 grid (`BOARD_GRID_SIZE`, 0..10 both axes) with no remainder. This
was verified two ways before writing `board_layout.c`: (1) by checking
that every player's home-column cells connect to the ring cell
immediately before their own entry point (`ring_cell(player*10 - 1)`),
matching `game_logic.c`'s `player*10` entry convention exactly -- now also
a permanent regression test
(`test_player_entry_and_home_column_connectivity`); (2) by rendering the
converted coordinates as an image and visually confirming the classic
cross ("plus sign") shape came out right, with green/red/blue/yellow home
bases in the four corners.

**Player order/colours** also come from the GEOS source, specifically its
`startfieldgraphics` array comments ("Player 1 - Green", "Player 2 - Red",
"Player 3 - Blue", "Player 4 - Yellow" for 0-indexed array entries 0-3):
player 0 = green, 1 = red, 2 = blue, 3 = yellow. This must stay in sync
with `player_rgb`/`player_name` in `src/game_view.c` and
`PLAYER_COLOURS` in `assets/generate_placeholder_art.py`.

- **Ring** (40 cells, `LUDO_RING_LENGTH`): the cross's outer track --
  `board_ring_cell()` is now a plain lookup into a fixed 40-entry table
  (`ring_cells[]`), not a computed formula.
- **Home column** (4 cells per player): runs from just inside each
  player's ring entry corner along the middle of "their" arm of the cross
  toward the centre -- `board_home_column_cell()`, lookup into
  `home_column_cells[player][index]`.
- **Home base** (4 slots per player): a 2x2 block in one of the four
  outer corners -- `board_home_base_cell()`, lookup into
  `home_base_cells[player][slot]`.
- **Finished**: pawns that have completed their home column are drawn
  stacked at the centre cell (5,5) -- see `board_finished_cell()`. This is
  the point all four home columns converge on but that none of them
  actually stores as one of their 4 cells, so it doesn't collide with
  anything.

`board_pawn_cell(g, player, pawn_index)` is the one function
`src/game_view.c` actually calls per pawn each redraw -- it dispatches to
whichever of the above matches that pawn's current state. This function's
signature and behaviour are unchanged from the original invented layout,
which is exactly why swapping the geometry underneath it didn't require
any changes to `game_view.c`'s rendering code.

## How it's rendered (in `src/game_view.c`)

`game_view.c` builds a one-time lookup table (`build_cell_kinds()`) by
calling `board_ring_cell()`/`board_home_column_cell()`/
`board_home_base_cell()`/`board_finished_cell()` for every index, marking
each of the 121 grid cells as ring/home-column/home-base/centre/empty
(plus owning player, for the coloured ones). The Redraw_Window handler
then just walks that table each time, filling each non-empty cell with a
flat colour, and plots each pawn's sprite at its `board_pawn_cell()`
position on top. See `docs/GRAPHICS_TOOLING.md` for the pawn sprites
themselves.

## Updating this file

Add a note here if the grid size, ring shape, or home-column path changes
-- and update the corresponding tests in `tests/test_board_layout.c` at
the same time.
