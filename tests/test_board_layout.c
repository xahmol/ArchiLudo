/*
 * ArchiLudo board layout test suite.
 * Host-side, dependency-free -- same CHECK()/RUN() harness style as
 * tests/test_game_logic.c. See include/board_layout.h for the geometry
 * this is verifying (the classic Mens Erger Je Niet cross, ported from
 * this game's GEOS edition).
 */

#include <stdio.h>

#include "board_layout.h"
#include "game_logic.h"

static int tests_run = 0;
static int checks_run = 0;
static int checks_failed = 0;

#define CHECK(cond) do { \
	checks_run++; \
	if (!(cond)) { \
		checks_failed++; \
		printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define RUN(test_fn) do { \
	tests_run++; \
	printf("- %s\n", #test_fn); \
	test_fn(); \
} while (0)

static int cells_equal(board_cell a, board_cell b)
{
	return a.col == b.col && a.row == b.row;
}

/* Every cell used anywhere on the board (ring, all 4 home columns, all 4
 * home bases, the centre) must be within the grid and must not collide
 * with any other cell used for a different purpose -- otherwise two
 * different board positions would draw on top of each other. */
static void test_all_used_cells_are_in_range_and_distinct(void)
{
	board_cell cells[LUDO_RING_LENGTH + LUDO_PLAYERS * LUDO_HOME_COLUMN_LENGTH
	                  + LUDO_PLAYERS * LUDO_PAWNS + 1];
	int n = 0, i, j, duplicates = 0;
	int player;

	for (i = 0; i < LUDO_RING_LENGTH; i++)
		cells[n++] = board_ring_cell(i);
	for (player = 0; player < LUDO_PLAYERS; player++)
		for (i = 0; i < LUDO_HOME_COLUMN_LENGTH; i++)
			cells[n++] = board_home_column_cell(player, i);
	for (player = 0; player < LUDO_PLAYERS; player++)
		for (i = 0; i < LUDO_PAWNS; i++)
			cells[n++] = board_home_base_cell(player, i);
	cells[n++] = board_finished_cell();

	for (i = 0; i < n; i++) {
		CHECK(cells[i].col >= 0 && cells[i].col < BOARD_GRID_SIZE);
		CHECK(cells[i].row >= 0 && cells[i].row < BOARD_GRID_SIZE);
	}

	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (cells_equal(cells[i], cells[j]))
				duplicates++;
	CHECK(duplicates == 0);
}

/* Each player's ring entry square (game_logic.c's player*10 convention)
 * should be a distinct point on the cross, and every player's home
 * column should connect to the ring cell immediately before their own
 * entry point -- the classic "peel off just before lapping your own
 * start" shape. */
static void test_player_entry_and_home_column_connectivity(void)
{
	int player;
	board_cell entries[LUDO_PLAYERS];

	for (player = 0; player < LUDO_PLAYERS; player++)
		entries[player] = board_ring_cell(player * 10);

	CHECK(!cells_equal(entries[0], entries[1]));
	CHECK(!cells_equal(entries[0], entries[2]));
	CHECK(!cells_equal(entries[0], entries[3]));
	CHECK(!cells_equal(entries[1], entries[2]));
	CHECK(!cells_equal(entries[1], entries[3]));
	CHECK(!cells_equal(entries[2], entries[3]));

	for (player = 0; player < LUDO_PLAYERS; player++) {
		board_cell last_ring_cell = board_ring_cell((player * 10 + LUDO_RING_LENGTH - 1) % LUDO_RING_LENGTH);
		board_cell first_home_column_cell = board_home_column_cell(player, 0);
		int dcol = first_home_column_cell.col - last_ring_cell.col;
		int drow = first_home_column_cell.row - last_ring_cell.row;

		/* Adjacent (one grid step away), not equal and not a diagonal jump. */
		CHECK((dcol == 0) != (drow == 0));
		CHECK((dcol == 1 || dcol == -1 || dcol == 0));
		CHECK((drow == 1 || drow == -1 || drow == 0));
	}
}

/* board_pawn_cell() must dispatch to the right helper for each pawn state. */
static void test_pawn_cell_dispatch(void)
{
	ludo_game g;

	ludo_init(&g);

	/* A pawn still at home -> its home base slot. */
	CHECK(cells_equal(board_pawn_cell(&g, 0, 2), board_home_base_cell(0, 2)));

	/* A pawn on the ring -> the matching ring cell (player 1 enters at
	 * ring square 10, so 5 steps puts it at ring square 15). */
	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 5;
	CHECK(cells_equal(board_pawn_cell(&g, 1, 0), board_ring_cell(15)));

	/* A pawn in its home column -> the matching home column cell. */
	g.players[2].pawns[1].in_play = 1;
	g.players[2].pawns[1].steps = LUDO_RING_LENGTH + 2;
	CHECK(cells_equal(board_pawn_cell(&g, 2, 1), board_home_column_cell(2, 2)));

	/* A finished pawn -> the centre cell. */
	g.players[3].pawns[3].in_play = 1;
	g.players[3].pawns[3].finished = 1;
	g.players[3].pawns[3].steps = LUDO_TOTAL_STEPS;
	CHECK(cells_equal(board_pawn_cell(&g, 3, 3), board_finished_cell()));
}

int main(void)
{
	RUN(test_all_used_cells_are_in_range_and_distinct);
	RUN(test_player_entry_and_home_column_connectivity);
	RUN(test_pawn_cell_dispatch);

	printf("\n%d/%d checks passed (%d test%s)\n",
	       checks_run - checks_failed, checks_run,
	       tests_run, tests_run == 1 ? "" : "s");

	return checks_failed ? 1 : 0;
}
