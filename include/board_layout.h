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
 * Board shape: a square ring path (40 cells, 10 per side, matching
 * LUDO_RING_LENGTH) around a central home-column area, on a
 * BOARD_GRID_SIZE x BOARD_GRID_SIZE grid of cells:
 *
 *   - Ring cells sit on the frame at column/row 2 or 12 (an 11x11 core,
 *     columns/rows 2..12), giving exactly 4*(11-1) = 40 unique cells --
 *     see board_ring_cell().
 *   - Each player's home column runs diagonally from just inside their
 *     entry corner to the centre cell (7,7) -- see board_home_column_cell().
 *   - Each player's home base (for pawns still waiting) is a 2x2 block in
 *     one of the four outer corners (columns/rows 0-1 or 13-14) -- see
 *     board_home_base_cell().
 *   - Finished pawns are drawn stacked at the centre cell -- see
 *     board_finished_cell().
 *
 * This is a deliberately simple placeholder geometry for Phase 1 (see
 * docs/ARCHITECTURE.md's Roadmap) -- functional and symmetric, not a
 * pixel-accurate recreation of a physical Ludo board. Real board artwork
 * in Phase 2 can either keep this same cell grid (just prettier per-cell
 * sprites) or prompt a redesign; this module is where that would change.
 */

#define BOARD_GRID_SIZE 15

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
 * Function: board_finished_cell
 * Summary: Grid cell where finished pawns are drawn (stacked together at
 *          the centre of the board).
 * Syntax:  board_cell board_finished_cell(void);
 * Input:   none.
 * Output:  the centre cell's (col, row).
 */
board_cell board_finished_cell(void);

/*
 * Function: board_pawn_cell
 * Summary: Grid cell for one specific pawn, given the game's current
 *          state -- dispatches to board_home_base_cell(),
 *          board_ring_cell(), board_home_column_cell(), or
 *          board_finished_cell() as appropriate. This is the one function
 *          src/game_view.c actually needs to call per pawn each redraw.
 * Syntax:  board_cell board_pawn_cell(const ludo_game *g, int player, int pawn_index);
 * Input:   g          - the game in progress.
 *          player     - 0..LUDO_PLAYERS-1.
 *          pawn_index - 0..LUDO_PAWNS-1.
 * Output:  the cell's (col, row) on the board grid.
 */
board_cell board_pawn_cell(const ludo_game *g, int player, int pawn_index);

#endif
