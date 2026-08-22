/*
 * ArchiLudo board layout -- implementation.
 * See include/board_layout.h for the geometry writeup and API docs.
 */

#include "board_layout.h"

#define RING_OFFSET 2   /* ring frame sits at column/row RING_OFFSET or (BOARD_GRID_SIZE-1-RING_OFFSET) */
#define RING_SIDE_LEN (LUDO_RING_LENGTH / 4)   /* 10 cells per side */
#define BOARD_CENTRE ((BOARD_GRID_SIZE - 1) / 2)   /* cell 7, for a 15-cell grid */

board_cell board_ring_cell(int ring_index)
{
	int side = ring_index / RING_SIDE_LEN;      /* 0=top, 1=right, 2=bottom, 3=left */
	int pos = ring_index % RING_SIDE_LEN;       /* 0..9 along that side */
	int lo = RING_OFFSET;
	int hi = BOARD_GRID_SIZE - 1 - RING_OFFSET;
	board_cell c;

	switch (side) {
	case 0: c.col = lo + pos; c.row = lo; break;               /* top: left to right */
	case 1: c.col = hi;       c.row = lo + pos; break;         /* right: top to bottom */
	case 2: c.col = hi - pos; c.row = hi; break;               /* bottom: right to left */
	default: c.col = lo;      c.row = hi - pos; break;         /* left: bottom to top */
	}
	return c;
}

board_cell board_home_column_cell(int player, int index)
{
	/* Each player's home column runs diagonally from just inside their
	 * ring entry corner to the board centre, one cell per index. */
	static const int dir_col[LUDO_PLAYERS] = { 1, -1, -1, 1 };
	static const int dir_row[LUDO_PLAYERS] = { 1, 1, -1, -1 };
	board_cell entry = board_ring_cell(player * RING_SIDE_LEN);
	board_cell c;

	c.col = entry.col + dir_col[player] * (index + 1);
	c.row = entry.row + dir_row[player] * (index + 1);
	return c;
}

board_cell board_home_base_cell(int player, int slot)
{
	/* A 2x2 block in the outer margin at each corner; slot 0..3 maps
	 * onto the 4 positions in reading order (top-left, top-right,
	 * bottom-left, bottom-right of that player's own block). */
	static const int block_col[LUDO_PLAYERS] = { 0, BOARD_GRID_SIZE - 2, BOARD_GRID_SIZE - 2, 0 };
	static const int block_row[LUDO_PLAYERS] = { 0, 0, BOARD_GRID_SIZE - 2, BOARD_GRID_SIZE - 2 };
	board_cell c;

	c.col = block_col[player] + (slot & 1);
	c.row = block_row[player] + ((slot >> 1) & 1);
	return c;
}

board_cell board_finished_cell(void)
{
	board_cell c;

	c.col = BOARD_CENTRE;
	c.row = BOARD_CENTRE;
	return c;
}

board_cell board_pawn_cell(const ludo_game *g, int player, int pawn_index)
{
	const ludo_pawn *p = &g->players[player].pawns[pawn_index];

	if (!p->in_play)
		return board_home_base_cell(player, pawn_index);
	if (p->finished)
		return board_finished_cell();
	if (p->steps < LUDO_RING_LENGTH) {
		int entry = player * RING_SIDE_LEN;
		return board_ring_cell((entry + p->steps) % LUDO_RING_LENGTH);
	}
	return board_home_column_cell(player, p->steps - LUDO_RING_LENGTH);
}
