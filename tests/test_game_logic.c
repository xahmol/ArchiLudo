/*
 * ArchiLudo game logic test suite
 * ================================
 *
 * Host-side automated test harness for src/game_logic.c. Built and run
 * with the *host's* native compiler (see the `test` target in the
 * project Makefile) -- not cross-compiled with ArchieSDK -- since the
 * whole point of separating the rules engine from the WIMP shell is that
 * it needs no RISC OS hardware or emulator to verify.
 *
 * Dependency-free by design: a tiny CHECK()-based harness rather than
 * pulling in an external unit test framework. Each test function directly
 * manipulates the (deliberately transparent) ludo_game struct to set up
 * whatever board position it needs to verify, then asserts on the result.
 *
 * Exit code is 0 if every check passed, 1 otherwise, so this doubles as
 * a CI-able pass/fail gate (see the `test` Makefile target).
 */

#include <stdio.h>

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

/* A freshly initialised game has every pawn at home and player 0 to move. */
static void test_new_game_starts_correctly(void)
{
	ludo_game g;
	int p, i;

	ludo_init(&g);

	CHECK(g.current_player == 0);
	CHECK(g.tries_remaining == 3);
	CHECK(g.forced_pawn == -1);
	CHECK(g.winner == -1);

	for (p = 0; p < LUDO_PLAYERS; p++) {
		for (i = 0; i < LUDO_PAWNS; i++) {
			CHECK(g.players[p].pawns[i].in_play == 0);
			CHECK(g.players[p].pawns[i].finished == 0);
		}
	}
}

/* With every pawn at home, a non-six roll has no legal move and costs one
 * of the three attempts, without ending the turn yet. */
static void test_stuck_all_home_decrements_tries(void)
{
	ludo_game g;

	ludo_init(&g);
	ludo_roll(&g, 3);

	CHECK(ludo_movable_pawns(&g) == 0);
	CHECK(ludo_no_move_possible(&g));
	CHECK(g.tries_remaining == 2);
	CHECK(g.current_player == 0);
}

/* Three non-six rolls in a row while stuck at home exhausts all three
 * attempts and passes the turn to the next player. */
static void test_three_failed_tries_passes_turn(void)
{
	ludo_game g;

	ludo_init(&g);
	ludo_roll(&g, 1);
	ludo_roll(&g, 2);
	ludo_roll(&g, 3);

	CHECK(g.current_player == 1);
	CHECK(g.tries_remaining == 3); /* reset for the new player */
}

/* Rolling a six with a pawn still at home mandatorily releases it (this
 * variant's house rule) -- and that release *is* the move for this roll,
 * so nothing is reported movable until the next roll. */
static void test_six_releases_home_pawn(void)
{
	ludo_game g;

	ludo_init(&g);
	ludo_roll(&g, 6);

	CHECK(g.players[0].pawns[0].in_play == 1);
	CHECK(g.players[0].pawns[0].steps == 0);
	CHECK(ludo_movable_pawns(&g) == 0);
	CHECK(g.current_player == 0); /* six always grants another roll */
}

/* The pawn released by a six must be the one moved on the very next
 * roll, even if another already-released pawn could otherwise move too. */
static void test_forced_pawn_next_roll(void)
{
	ludo_game g;

	ludo_init(&g);
	ludo_roll(&g, 6); /* releases pawn 0 */

	/* Give the player a second pawn already in play, which would
	 * otherwise be a perfectly legal choice for the next roll. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 5;

	ludo_roll(&g, 3);

	CHECK(ludo_movable_pawns(&g) == 1u); /* only pawn 0's bit, not pawn 1's */

	ludo_move_pawn(&g, 0);

	CHECK(g.players[0].pawns[0].steps == 3);
	CHECK(g.forced_pawn == -1); /* obligation fulfilled */
	CHECK(g.current_player == 1); /* roll of 3 was not a six: turn ends */
}

/* Landing exactly on an opponent's pawn sends it back to their home base. */
static void test_capture_sends_pawn_home(void)
{
	ludo_game g;

	ludo_init(&g);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 2; /* ring square 2 */

	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 35; /* player 1 starts at square 10: ring square (10+35)%40 = 5 */

	ludo_roll(&g, 3); /* player 0's pawn: 2 + 3 = 5, lands on player 1's pawn */
	CHECK(ludo_move_pawn(&g, 0) == 1); /* capture reported */

	CHECK(g.players[1].pawns[0].in_play == 0);
	CHECK(g.players[1].pawns[0].steps == 0);
}

/* A pawn cannot pass, or land on, one of its own player's pawns that is
 * already further along in the home column. */
static void test_home_column_blocking(void)
{
	ludo_game g;

	ludo_init(&g);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 41; /* 1 square into the home column */

	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 38; /* still on the ring, about to enter it */

	ludo_roll(&g, 4); /* pawn 1 would land on 42, passing pawn 0 at 41 */

	CHECK((ludo_movable_pawns(&g) & (1u << 1)) == 0);
}

/* A pawn must reach the very end of the home column on an exact roll --
 * overshooting past it is not a legal move. */
static void test_overshoot_not_movable(void)
{
	ludo_game g;

	ludo_init(&g);
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS - 2; /* needs exactly 2 to finish */

	ludo_roll(&g, 3);
	CHECK((ludo_movable_pawns(&g) & 1u) == 0);

	ludo_roll(&g, 2);
	CHECK((ludo_movable_pawns(&g) & 1u) != 0);
}

/* Moving a pawn the exact remaining distance finishes it. */
static void test_pawn_finishes_exactly(void)
{
	ludo_game g;

	ludo_init(&g);
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS - 2;

	ludo_roll(&g, 2);
	ludo_move_pawn(&g, 0);

	CHECK(g.players[0].pawns[0].steps == LUDO_TOTAL_STEPS);
	CHECK(g.players[0].pawns[0].finished == 1);
}

/* The player who finishes all four pawns first is recorded as the winner. */
static void test_winner_detected_when_all_pawns_finish(void)
{
	ludo_game g;
	int i;

	ludo_init(&g);
	for (i = 0; i < 3; i++) {
		g.players[0].pawns[i].in_play = 1;
		g.players[0].pawns[i].finished = 1;
		g.players[0].pawns[i].steps = LUDO_TOTAL_STEPS;
	}
	g.players[0].pawns[3].in_play = 1;
	g.players[0].pawns[3].steps = LUDO_TOTAL_STEPS - 2;

	ludo_roll(&g, 2);
	ludo_move_pawn(&g, 3);

	CHECK(g.winner == 0);
}

/* A six rolled for a pawn move (not a home release) grants another roll
 * for the same player, without creating a forced-pawn obligation. */
static void test_extra_roll_on_six_keeps_same_player(void)
{
	ludo_game g;
	int i;

	ludo_init(&g);
	for (i = 0; i < LUDO_PAWNS; i++) {
		g.players[0].pawns[i].in_play = 1;
		g.players[0].pawns[i].steps = i;
	}

	ludo_roll(&g, 6);
	ludo_move_pawn(&g, 0);

	CHECK(g.current_player == 0);
	CHECK(g.forced_pawn == -1);
}

/* A non-six move ends the turn and advances to the next player. */
static void test_non_six_move_ends_turn(void)
{
	ludo_game g;

	ludo_init(&g);
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 5;

	ludo_roll(&g, 3);
	ludo_move_pawn(&g, 0);

	CHECK(g.current_player == 1);
}

int main(void)
{
	RUN(test_new_game_starts_correctly);
	RUN(test_stuck_all_home_decrements_tries);
	RUN(test_three_failed_tries_passes_turn);
	RUN(test_six_releases_home_pawn);
	RUN(test_forced_pawn_next_roll);
	RUN(test_capture_sends_pawn_home);
	RUN(test_home_column_blocking);
	RUN(test_overshoot_not_movable);
	RUN(test_pawn_finishes_exactly);
	RUN(test_winner_detected_when_all_pawns_finish);
	RUN(test_extra_roll_on_six_keeps_same_player);
	RUN(test_non_six_move_ends_turn);

	printf("\n%d/%d checks passed (%d test%s)\n",
	       checks_run - checks_failed, checks_run,
	       tests_run, tests_run == 1 ? "" : "s");

	return checks_failed ? 1 : 0;
}
