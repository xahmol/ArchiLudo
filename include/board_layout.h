#ifndef BOARD_LAYOUT_H
#define BOARD_LAYOUT_H

#include "game_logic.h"

/*
 * ArchiLudo board layout
 * =======================
 *
 * Maps game_logic.c's abstract pawn state (in_play/finished/steps) onto a
 * concrete board geometry, expressed as integer grid cells rather than
 * screen pixels -- so this module stays platform-independent (no OSLib
 * dependency) and host-testable, exactly like game_logic.c itself. The
 * WIMP shell (src/game_view.c) multiplies these grid cells by its own
 * chosen cell size to get actual window/screen coordinates.
 *
 * Board shape: the classic Mens Erger Je Niet / Ludo cross ("plus sign"),
 * on an 11x11 grid -- ported directly from the coordinate tables in this
 * game's own prior GEOS port (`/home/xahmol/git/ludo/GEOS/src/main.c`'s
 * `fieldcoords[40][2]` and `homedestcoords[4][8][2]`), not invented from
 * scratch, per the user's explicit request to match that version's board.
 * Those coordinates use 2-unit steps on a wider native grid (x even
 * 0..20, y odd 3..23); converting with col = raw_x/2, row = (raw_y-3)/2
 * lands them exactly on an 11x11 grid (0..10 both axes) with no
 * remainder and no collisions -- confirmed both by inspection (every
 * player's home-column cells connect to the ring cell immediately before
 * their own entry point, matching game_logic.c's `player*10` entry
 * convention exactly) and by rendering the converted grid as an image
 * during development to visually confirm the classic cross shape came out
 * right.
 *
 * Player order/colours match the GEOS source's `startfieldgraphics`
 * comments exactly: player 0 = green, 1 = red, 2 = blue, 3 = yellow (see
 * player_rgb/player_name in game_view.c).
 *
 * - Ring: 40 cells (`LUDO_RING_LENGTH`), forming the cross's outer track
 *   -- see `board_ring_cell()`.
 * - Home column: 4 cells per player, running from just inside their ring
 *   entry corner along the middle of "their" arm of the cross toward the
 *   centre -- see `board_home_column_cell()`.
 * - Home base: 4 slots per player, a 2x2 block in one of the four outer
 *   corners -- see `board_home_base_cell()`.
 * - Finished pawns stay on their own home column's last square (index
 *   `LUDO_HOME_COLUMN_LENGTH - 1`) -- there is no separate shared "finish"
 *   cell. An earlier version of this module invented one at the board's
 *   centre (5,5), which turned out not to match the GEOS source at all:
 *   `homedestcoords[player][0..7]` there has exactly 8 slots per player
 *   (0-3 home base, 4-7 home column) and nothing beyond index 7 -- a
 *   finished pawn simply occupies that last slot, staying put rather than
 *   moving anywhere new. Fixed in `board_pawn_cell()` -- see
 *   docs/BOARD_LAYOUT.md's "Round 6.7 correction".
 */

#define BOARD_GRID_SIZE 11

/*
 * Type: board_cell
 * Summary: A position expressed in board grid cells (0..BOARD_GRID_SIZE-1
 *          on each axis), not screen pixels.
 */
typedef struct {
	int col;
	int row;
} board_cell;

/*
 * Function: board_ring_cell
 * Summary: Grid cell for a position on the shared 40-square ring.
 * Syntax:  board_cell board_ring_cell(int ring_index);
 * Input:   ring_index - 0..LUDO_RING_LENGTH-1 (matches a pawn's `steps`
 *                        while steps < LUDO_RING_LENGTH; also matches the
 *                        `player * (LUDO_RING_LENGTH/LUDO_PLAYERS)` entry
 *                        square convention from game_logic.c).
 * Output:  the cell's (col, row) on the board grid.
 */
board_cell board_ring_cell(int ring_index);

/*
 * Function: board_home_column_cell
 * Summary: Grid cell for a position in one player's home column.
 * Syntax:  board_cell board_home_column_cell(int player, int index);
 * Input:   player - 0..LUDO_PLAYERS-1.
 *          index  - 0..LUDO_HOME_COLUMN_LENGTH-1 (matches
 *                   steps - LUDO_RING_LENGTH for a pawn in its home column).
 * Output:  the cell's (col, row) on the board grid.
 */
board_cell board_home_column_cell(int player, int index);

/*
 * Function: board_home_base_cell
 * Summary: Grid cell for one of the (up to 4) pawns still waiting in a
 *          player's home base.
 * Syntax:  board_cell board_home_base_cell(int player, int slot);
 * Input:   player - 0..LUDO_PLAYERS-1.
 *          slot   - 0..LUDO_PAWNS-1, an arbitrary but stable position
 *                   within that player's 2x2 home base block (typically
 *                   the pawn's own index, so a given pawn always draws in
 *                   the same slot while waiting).
 * Output:  the cell's (col, row) on the board grid.
 */
board_cell board_home_base_cell(int player, int slot);

/*
 * Function: board_pawn_cell
 * Summary: Grid cell for one specific pawn, given the game's current
 *          state -- dispatches to board_home_base_cell(),
 *          board_ring_cell(), or board_home_column_cell() as appropriate
 *          (a finished pawn resolves to its home column's last cell, not
 *          a separate case). This is the one function src/game_view.c
 *          actually needs to call per pawn each redraw.
 * Syntax:  board_cell board_pawn_cell(const ludo_game *g, int player, int pawn_index);
 * Input:   g          - the game in progress.
 *          player     - 0..LUDO_PLAYERS-1.
 *          pawn_index - 0..LUDO_PAWNS-1.
 * Output:  the cell's (col, row) on the board grid.
 */
board_cell board_pawn_cell(const ludo_game *g, int player, int pawn_index);

#endif
