/*
 * ArchiLudo board layout test suite.
 * Host-side, dependency-free -- same CHECK()/RUN() harness style as
 * tests/test_game_logic.c. See include/board_layout.h for the geometry
 * this is verifying.
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

/* The 40 ring cells must all be distinct -- otherwise two different board
 * positions would draw on top of each other. */
static void test_ring_cells_all_distinct(void)
{
	board_cell cells[LUDO_RING_LENGTH];
	int i, j, duplicates = 0;

	for (i = 0; i < LUDO_RING_LENGTH; i++)
		cells[i] = board_ring_cell(i);

	for (i = 0; i < LUDO_RING_LENGTH; i++)
		for (j = i + 1; j < LUDO_RING_LENGTH; j++)
			if (cells_equal(cells[i], cells[j]))
				duplicates++;

	CHECK(duplicates == 0);
}

/* Each player's ring entry square should sit at a distinct corner of the
 * frame, 90 degrees apart, matching game_logic.c's player*10 convention. */
static void test_player_entry_corners(void)
{
	board_cell p0 = board_ring_cell(0 * 10);
	board_cell p1 = board_ring_cell(1 * 10);
	board_cell p2 = board_ring_cell(2 * 10);
	board_cell p3 = board_ring_cell(3 * 10);

	CHECK(p0.col == 2 && p0.row == 2);
	CHECK(p1.col == 12 && p1.row == 2);
	CHECK(p2.col == 12 && p2.row == 12);
	CHECK(p3.col == 2 && p3.row == 12);
}

/* Home column cells must lie strictly inside the ring frame (not on it,
 * not outside it), and be distinct from each other within a player. */
static void test_home_column_cells_inside_frame_and_distinct(void)
{
	int player;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		board_cell cells[LUDO_HOME_COLUMN_LENGTH];
		int i, j, duplicates = 0;

		for (i = 0; i < LUDO_HOME_COLUMN_LENGTH; i++) {
			cells[i] = board_home_column_cell(player, i);
			CHECK(cells[i].col > 2 && cells[i].col < 12);
			CHECK(cells[i].row > 2 && cells[i].row < 12);
		}

		for (i = 0; i < LUDO_HOME_COLUMN_LENGTH; i++)
			for (j = i + 1; j < LUDO_HOME_COLUMN_LENGTH; j++)
				if (cells_equal(cells[i], cells[j]))
					duplicates++;
		CHECK(duplicates == 0);
	}
}

/* Home base slots must be distinct per player, and different players'
 * home bases must not overlap each other. */
static void test_home_base_cells_distinct(void)
{
	board_cell all[LUDO_PLAYERS][LUDO_PAWNS];
	int player, slot, i, j, duplicates = 0;

	for (player = 0; player < LUDO_PLAYERS; player++)
		for (slot = 0; slot < LUDO_PAWNS; slot++)
			all[player][slot] = board_home_base_cell(player, slot);

	for (player = 0; player < LUDO_PLAYERS; player++)
		for (i = 0; i < LUDO_PAWNS; i++)
			for (j = i + 1; j < LUDO_PAWNS; j++)
				if (cells_equal(all[player][i], all[player][j]))
					duplicates++;
	CHECK(duplicates == 0);

	for (player = 0; player < LUDO_PLAYERS; player++)
		for (i = 0; i < LUDO_PAWNS; i++)
			for (int other = player + 1; other < LUDO_PLAYERS; other++)
				for (j = 0; j < LUDO_PAWNS; j++)
					if (cells_equal(all[player][i], all[other][j]))
						duplicates++;
	CHECK(duplicates == 0);
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
	RUN(test_ring_cells_all_distinct);
	RUN(test_player_entry_corners);
	RUN(test_home_column_cells_inside_frame_and_distinct);
	RUN(test_home_base_cells_distinct);
	RUN(test_pawn_cell_dispatch);

	printf("\n%d/%d checks passed (%d test%s)\n",
	       checks_run - checks_failed, checks_run,
	       tests_run, tests_run == 1 ? "" : "s");

	return checks_failed ? 1 : 0;
}
