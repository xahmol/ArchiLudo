/*
 * ArchiLudo board layout -- implementation.
 * See include/board_layout.h for the geometry writeup and API docs.
 *
 * The ring/home-column/home-base tables below are converted directly from
 * /home/xahmol/git/ludo/GEOS/src/main.c's fieldcoords[40][2] and
 * homedestcoords[4][8][2] (col = raw_x/2, row = (raw_y-3)/2) -- see the
 * header comment for why that conversion is exact.
 */

#include "board_layout.h"

static const board_cell ring_cells[LUDO_RING_LENGTH] = {
	{ 0, 4}, { 1, 4}, { 2, 4}, { 3, 4}, { 4, 4},
	{ 4, 3}, { 4, 2}, { 4, 1}, { 4, 0}, { 5, 0},
	{ 6, 0}, { 6, 1}, { 6, 2}, { 6, 3}, { 6, 4},
	{ 7, 4}, { 8, 4}, { 9, 4}, {10, 4}, {10, 5},
	{10, 6}, { 9, 6}, { 8, 6}, { 7, 6}, { 6, 6},
	{ 6, 7}, { 6, 8}, { 6, 9}, { 6,10}, { 5,10},
	{ 4,10}, { 4, 9}, { 4, 8}, { 4, 7}, { 4, 6},
	{ 3, 6}, { 2, 6}, { 1, 6}, { 0, 6}, { 0, 5},
};

static const board_cell home_column_cells[LUDO_PLAYERS][LUDO_HOME_COLUMN_LENGTH] = {
	{ {1, 5}, {2, 5}, {3, 5}, {4, 5} },   /* 0: green */
	{ {5, 1}, {5, 2}, {5, 3}, {5, 4} },   /* 1: red */
	{ {9, 5}, {8, 5}, {7, 5}, {6, 5} },   /* 2: blue */
	{ {5, 9}, {5, 8}, {5, 7}, {5, 6} },   /* 3: yellow */
};

static const board_cell home_base_cells[LUDO_PLAYERS][LUDO_PAWNS] = {
	{ { 0, 0}, { 1, 0}, { 0, 1}, { 1, 1} },   /* 0: green */
	{ { 9, 0}, {10, 0}, { 9, 1}, {10, 1} },   /* 1: red */
	{ { 9, 9}, {10, 9}, { 9,10}, {10,10} },   /* 2: blue */
	{ { 0, 9}, { 1, 9}, { 0,10}, { 1,10} },   /* 3: yellow */
};

board_cell board_ring_cell(int ring_index)
{
	return ring_cells[ring_index];
}

board_cell board_home_column_cell(int player, int index)
{
	return home_column_cells[player][index];
}

board_cell board_home_base_cell(int player, int slot)
{
	return home_base_cells[player][slot];
}

board_cell board_finished_cell(void)
{
	board_cell c;

	c.col = 5;
	c.row = 5;
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
		int entry = player * (LUDO_RING_LENGTH / LUDO_PLAYERS);
		return board_ring_cell((entry + p->steps) % LUDO_RING_LENGTH);
	}
	return board_home_column_cell(player, p->steps - LUDO_RING_LENGTH);
}
