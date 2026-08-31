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
#include <stdlib.h>

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

/* Landing on a square already occupied by one of the SAME player's own
 * pawns (on the shared ring, not the home column) sends that earlier
 * pawn home rather than letting both stack on one square -- this
 * project's house rule, distinct from the home column's own blocking
 * rule below (which prevents landing there at all rather than bumping). */
static void test_own_pawn_sent_home_on_ring_collision(void)
{
	ludo_game g;

	ludo_init(&g);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 5; /* ring square 5, sitting there already */

	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 2; /* ring square 2 */

	ludo_roll(&g, 3); /* pawn 1: 2 + 3 = 5, lands on pawn 0's square */
	CHECK(ludo_move_pawn(&g, 1) == 1); /* reported same as an opponent capture */

	CHECK(g.players[0].pawns[0].in_play == 0);
	CHECK(g.players[0].pawns[0].steps == 0);
	CHECK(g.players[0].pawns[1].in_play == 1);
	CHECK(g.players[0].pawns[1].steps == 5);
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

/* Once one pawn has finished (permanently occupying LUDO_TOTAL_STEPS),
 * a second pawn of the same player must NOT be able to stack on that
 * same square -- its
 * own effective end becomes one square short (LUDO_TOTAL_STEPS - 1),
 * blocked from the true end exactly like ordinary home-column blocking,
 * and *that* square is what finishes it. */
static void test_second_finishing_pawn_lands_one_square_short(void)
{
	ludo_game g;

	ludo_init(&g);
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].finished = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS; /* already finished, parked at the true end */

	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = LUDO_TOTAL_STEPS - 1 - 3; /* 3 short of LUDO_TOTAL_STEPS-1 */

	ludo_roll(&g, 4); /* would land exactly on the true end -- occupied, blocked */
	CHECK((ludo_movable_pawns(&g) & (1u << 1)) == 0);

	ludo_roll(&g, 3); /* exact distance to this pawn's own (one-shorter) effective end */
	CHECK((ludo_movable_pawns(&g) & (1u << 1)) != 0);
	ludo_move_pawn(&g, 1);

	CHECK(g.players[0].pawns[1].steps == LUDO_TOTAL_STEPS - 1);
	CHECK(g.players[0].pawns[1].finished == 1);
	/* The two finished pawns must occupy two DIFFERENT squares. */
	CHECK(g.players[0].pawns[0].steps != g.players[0].pawns[1].steps);
}

/* The player who finishes all four pawns first is recorded as the winner. */
static void test_winner_detected_when_all_pawns_finish(void)
{
	ludo_game g;
	int i;

	ludo_init(&g);
	/* Three pawns already finished, each at its own dynamic threshold
	 * (see game_logic.h) -- LUDO_TOTAL_STEPS,
	 * LUDO_TOTAL_STEPS-1, LUDO_TOTAL_STEPS-2, matching the order they'd
	 * actually queue into the home column in real play, not all three
	 * stacked on the same square (no longer a reachable state at all). */
	for (i = 0; i < 3; i++) {
		g.players[0].pawns[i].in_play = 1;
		g.players[0].pawns[i].finished = 1;
		g.players[0].pawns[i].steps = LUDO_TOTAL_STEPS - i;
	}
	g.players[0].pawns[3].in_play = 1;
	/* The 4th pawn's own threshold is LUDO_TOTAL_STEPS-3 (three others
	 * already finished) -- needs exactly 2 more to reach it. */
	g.players[0].pawns[3].steps = LUDO_TOTAL_STEPS - 3 - 2;

	ludo_roll(&g, 2);
	CHECK((ludo_movable_pawns(&g) & (1u << 3)) != 0);
	ludo_move_pawn(&g, 3);

	CHECK(g.players[0].pawns[3].steps == LUDO_TOTAL_STEPS - 3);
	CHECK(g.winner == 0);
	CHECK(g.finish_order[0] == 0);
	CHECK(g.finish_order[1] == -1);
}

/* finish_order records every player as they finish, not just the first
 * (winner) -- game_view.c's win dialogue shows a distinct message per
 * place (1st through 4th), so all four need to be tracked, in the order
 * they actually finish, not player index order. */
static void test_finish_order_tracks_all_four_places(void)
{
	static const int order[LUDO_PLAYERS] = { 2, 0, 3, 1 }; /* finish order */
	ludo_game g;
	int i, j, place;

	ludo_init(&g);

	for (place = 0; place < LUDO_PLAYERS; place++) {
		int player = order[place];

		/* Same dynamic-threshold pattern as
		 * test_winner_detected_when_all_pawns_finish() above: 3 pawns
		 * already finished, the 4th moved into place to trigger the
		 * finish. */
		for (i = 0; i < 3; i++) {
			g.players[player].pawns[i].in_play = 1;
			g.players[player].pawns[i].finished = 1;
			g.players[player].pawns[i].steps = LUDO_TOTAL_STEPS - i;
		}
		g.players[player].pawns[3].in_play = 1;
		g.players[player].pawns[3].steps = LUDO_TOTAL_STEPS - 3 - 1;

		g.current_player = player;
		ludo_roll(&g, 1);
		CHECK((ludo_movable_pawns(&g) & (1u << 3)) != 0);
		ludo_move_pawn(&g, 3);

		CHECK(g.finish_order[place] == player);
		for (j = place + 1; j < LUDO_PLAYERS; j++)
			CHECK(g.finish_order[j] == -1);
	}

	CHECK(g.winner == order[0]); /* the first to finish */
	CHECK(g.finish_order[0] == order[0]);
	CHECK(g.finish_order[1] == order[1]);
	CHECK(g.finish_order[2] == order[2]);
	CHECK(g.finish_order[3] == order[3]);
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

/*
 * Once ANY player has won (g.winner != -1), every OTHER player who
 * hasn't finished yet must still keep their own normal six-goes-again
 * bonus -- per this project's "continue playing after the first winner"
 * house-rule mode (see docs/RULES.md). The turn-advance logic must check
 * this specific player's own all_pawns_finished(), not the global
 * g.winner == -1, or every remaining player would permanently lose their
 * bonus turn for the rest of the game the moment anyone won.
 */
static void test_six_bonus_survives_another_players_win(void)
{
	ludo_game g;
	int i;

	ludo_init(&g);
	/* Player 0 wins outright. */
	for (i = 0; i < LUDO_PAWNS; i++) {
		g.players[0].pawns[i].in_play = 1;
		g.players[0].pawns[i].finished = 1;
		g.players[0].pawns[i].steps = LUDO_TOTAL_STEPS - i;
	}
	g.winner = 0;
	/* Player 1 (still racing) is up next, with a pawn ready to move. */
	g.current_player = 1;
	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 0;

	ludo_roll(&g, 6);
	ludo_move_pawn(&g, 0);

	CHECK(g.winner == 0); /* the win itself is untouched */
	CHECK(g.current_player == 1); /* still player 1's turn, not advanced */
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

/* A pawn one step short of finishing (needs exactly 1) must never be
 * reported movable, or moved at all, on a roll of 2.
 * test_overshoot_not_movable() above already covers "2 short, roll 3" --
 * this is the "1 short" off-by-one neighbour, worth its own explicit
 * check. */
static void test_one_short_overshoot_not_movable(void)
{
	ludo_game g;

	ludo_init(&g);
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS - 1; /* needs exactly 1 to finish */

	ludo_roll(&g, 2);
	CHECK((ludo_movable_pawns(&g) & 1u) == 0);

	ludo_roll(&g, 1);
	CHECK((ludo_movable_pawns(&g) & 1u) != 0);
	ludo_move_pawn(&g, 0);
	CHECK(g.players[0].pawns[0].steps == LUDO_TOTAL_STEPS);
	CHECK(g.players[0].pawns[0].finished == 1);
}

/*
 * Multi-rule-set toggles (see include/game_logic.h's ludo_rules and
 * ludo_default_rules()) -- each test below starts from ludo_init()'s MEJN
 * defaults, then flips exactly one toggle via ludo_set_rules() so any
 * unrelated regression would show up as a failure in one of the many
 * MEJN-default tests above instead of being masked here.
 */

/* own_pawn_capture off: landing on one of your own pawns shares the
 * square instead of sending it home (contrast with
 * test_own_pawn_sent_home_on_ring_collision() above, the MEJN-default
 * behaviour this is the opposite of). */
static void test_own_pawn_capture_off_shares_square(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_MEJN);
	r.own_pawn_capture = 0;
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 5; /* ring square 5, sitting there already */

	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 2; /* ring square 2 */

	ludo_roll(&g, 3); /* pawn 1: 2 + 3 = 5, lands on pawn 0's square */
	CHECK(ludo_move_pawn(&g, 1) == 0); /* no capture reported */

	CHECK(g.players[0].pawns[0].in_play == 1); /* still there, untouched */
	CHECK(g.players[0].pawns[0].steps == 5);
	CHECK(g.players[0].pawns[1].steps == 5); /* sharing the square */
}

/* mandatory_six_release off: a six no longer auto-releases a home pawn
 * via ludo_roll() -- instead, releasing that pawn becomes one of the
 * choices ludo_movable_pawns() reports, alongside any other legal move,
 * and picking it via ludo_move_pawn() performs the release (with the
 * usual six-grants-a-bonus-roll behaviour, but no "must move this pawn
 * next" obligation, since the player chose it rather than having it
 * forced on them). */
static void test_optional_six_release_offers_choice(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO); /* optional release, no own-pawn capture */
	ludo_set_rules(&g, &r);

	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 10; /* another pawn also has a legal move */

	ludo_roll(&g, 6);

	CHECK(g.players[0].pawns[0].in_play == 0); /* NOT auto-released */
	CHECK(g.just_released == 0);
	/* Both the home pawn (0) and the already-in-play pawn (1) are offered. */
	CHECK((ludo_movable_pawns(&g) & (1u << 0)) != 0);
	CHECK((ludo_movable_pawns(&g) & (1u << 1)) != 0);

	ludo_move_pawn(&g, 0); /* player chooses to release pawn 0 */

	CHECK(g.players[0].pawns[0].in_play == 1);
	CHECK(g.players[0].pawns[0].steps == 0);
	CHECK(g.forced_pawn == -1); /* no follow-up obligation, unlike the mandatory path */
	CHECK(g.pending_forced_pawn == -1);
	CHECK(g.current_player == 0); /* six still grants its usual bonus roll */
	CHECK(g.last_roll == 0); /* bonus roll pending, same as any other six */
}

/* overshoot_bounce on: a roll that would carry a pawn past the very end
 * of its home column instead bounces it backward by the remainder. */
static void test_overshoot_bounce_moves_backward(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_MEJN);
	r.overshoot_bounce = 1;
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS - 2; /* needs exactly 2 to finish */

	ludo_roll(&g, 5); /* 2 forward to the end, 3 pips left over -> bounce back 3 */
	CHECK((ludo_movable_pawns(&g) & 1u) != 0); /* now legal, unlike the bounce-off default */
	ludo_move_pawn(&g, 0);

	CHECK(g.players[0].pawns[0].steps == LUDO_TOTAL_STEPS - 3);
	CHECK(g.players[0].pawns[0].finished == 0);
}

/* overshoot_bounce on, extreme case: a bounce large enough to overshoot
 * back past the home column's own entrance is clamped there rather than
 * continuing onto the shared ring (see resolve_move_destination()'s own
 * doc comment in game_logic.c for why -- this project's 4-square home
 * column is shorter than a die's max value, unlike classic Ludo's
 * 6-square stretch which never hits this case). */
static void test_overshoot_bounce_clamped_at_home_column_entrance(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_MEJN);
	r.overshoot_bounce = 1;
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_RING_LENGTH + 1; /* 1 square into the home column */
	/* No home pawn left for MEJN's own mandatory_six_release to auto-place
	 * on this six -- unrelated to the bounce toggle under test, but a six
	 * would otherwise release one of these instead of leaving pawn 0 up
	 * for a plain move. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 5;
	g.players[0].pawns[2].in_play = 1;
	g.players[0].pawns[2].steps = 6;
	g.players[0].pawns[3].in_play = 1;
	g.players[0].pawns[3].steps = 7;

	ludo_roll(&g, 6); /* needs 2 to finish -- overshoot by 4, would bounce to entrance-1 */
	CHECK((ludo_movable_pawns(&g) & 1u) != 0);
	ludo_move_pawn(&g, 0);

	CHECK(g.players[0].pawns[0].steps == LUDO_RING_LENGTH); /* clamped, not onto the ring */
}

/* no_six_needed_last_pawn on: a player's own LAST pawn still at home may
 * be released on any roll, not just a six -- but only once it really is
 * the last one (with others still home, only a six qualifies, same as
 * ever). */
static void test_no_six_needed_for_last_home_pawn(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO);
	r.no_six_needed_last_pawn = 1;
	ludo_set_rules(&g, &r);

	/* Three of the four pawns already in play -- pawn 3 is the last one home. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[2].in_play = 1;

	ludo_roll(&g, 4); /* not a six, but the last-home-pawn exception applies */
	CHECK((ludo_movable_pawns(&g) & (1u << 3)) != 0);

	ludo_move_pawn(&g, 3);
	CHECK(g.players[0].pawns[3].in_play == 1);
	CHECK(g.players[0].pawns[3].steps == 0);
	CHECK(g.current_player == 1); /* non-six release ends the turn normally */
}

/* free_home_column on: a player's own pawns may pass or land on each
 * other in the home column (contrast with test_home_column_blocking()
 * above, the off-by-default behaviour this is the opposite of). */
static void test_free_home_column_allows_passing(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_MEJN);
	r.free_home_column = 1;
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 41; /* 1 square into the home column */

	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 38; /* still on the ring, about to enter it */

	ludo_roll(&g, 4); /* pawn 1 would land on 42, passing pawn 0 at 41 */
	CHECK((ludo_movable_pawns(&g) & (1u << 1)) != 0); /* no longer blocked */

	ludo_move_pawn(&g, 1);
	CHECK(g.players[0].pawns[1].steps == 42);
}

/* blockade on: 2+ of one player's pawns stacked on a ring square block
 * every other player from landing on OR passing through that square
 * (only reachable in the first place with own_pawn_capture off, since
 * capture would otherwise prevent the stack from forming). */
static void test_blockade_blocks_landing_and_passing_through(void)
{
	ludo_game g;
	ludo_rules r;

	/* Sub-case 1: landing exactly on the blockaded square. */
	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO); /* own_pawn_capture off; blockade explicitly forced on below regardless of the preset's own default */
	r.blockade = 1;
	ludo_set_rules(&g, &r);

	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 5; /* ring_square(1, 5) == 15 */
	g.players[1].pawns[1].in_play = 1;
	g.players[1].pawns[1].steps = 5; /* stacked on the same square -- a blockade */

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 10; /* ring_square(0, 10) == 10 */

	ludo_roll(&g, 5); /* would land exactly on square 15 -- blockaded */
	CHECK((ludo_movable_pawns(&g) & 1u) == 0);

	/* Sub-case 2: passing through (not landing on) the blockaded square. */
	ludo_init(&g);
	ludo_set_rules(&g, &r);

	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 5;
	g.players[1].pawns[1].in_play = 1;
	g.players[1].pawns[1].steps = 5;

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 10;

	ludo_roll(&g, 6); /* would land on square 16, but must cross blockaded 15 first */
	CHECK((ludo_movable_pawns(&g) & 1u) == 0);
}

/* The same stacked-pawns setup with blockade explicitly off (the Ludo
 * preset's own default is ON, so this test forces it off rather than
 * relying on any preset -- see the CHECK below): landing on or passing
 * through the square is fine when the rule genuinely isn't active. */
static void test_blockade_off_allows_landing_and_passing(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO);
	/* The Ludo preset's own blockade default is 1 (see
	 * ludo_default_rules()'s own doc comment) -- this test's whole point
	 * is the blockade=0 behaviour specifically, so it must set that
	 * explicitly rather than lean on the preset's default. */
	r.blockade = 0;
	ludo_set_rules(&g, &r);

	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 5;
	g.players[1].pawns[1].in_play = 1;
	g.players[1].pawns[1].steps = 5;

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 10;

	ludo_roll(&g, 6); /* would cross square 15 and land on 16 */
	CHECK((ludo_movable_pawns(&g) & 1u) != 0);
}

/* blockade on: a barricade sitting on a player's own start square also
 * refuses releasing a new pawn there -- both under mandatory six-release
 * (falls through to the "no legal action" bookkeeping, using up one of
 * the three tries) and under optional release (simply not offered as a
 * movable choice). */
static void test_blockade_blocks_release_at_own_start_square(void)
{
	ludo_game g;
	ludo_rules r;

	/* Mandatory six-release (MEJN + blockade). */
	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_MEJN);
	r.blockade = 1;
	ludo_set_rules(&g, &r);

	/* Player 1's two pawns stacked exactly on player 0's own start
	 * square: ring_square(1, 30) == (10 + 30) % 40 == 0. */
	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 30;
	g.players[1].pawns[1].in_play = 1;
	g.players[1].pawns[1].steps = 30;

	ludo_roll(&g, 6);
	CHECK(g.players[0].pawns[0].in_play == 0); /* release refused */
	CHECK(g.just_released == 0);
	CHECK(g.tries_remaining == 2); /* counted as a wasted attempt, like no six at all */

	/* Optional release (Ludo + blockade): same barricade, but release
	 * should simply not be offered as a movable choice. */
	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO);
	r.blockade = 1;
	ludo_set_rules(&g, &r);

	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 30;
	g.players[1].pawns[1].in_play = 1;
	g.players[1].pawns[1].steps = 30;

	ludo_roll(&g, 6);
	CHECK((ludo_movable_pawns(&g) & 1u) == 0);
}

/* backward_movement on: a pawn already on the ring may move backward by
 * the current roll as an alternative to its ordinary forward move. */
static void test_backward_movement_moves_pawn_back(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO);
	r.backward_movement = 1;
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 10;

	ludo_roll(&g, 4);
	CHECK((ludo_movable_pawns_backward(&g) & 1u) != 0);
	ludo_move_pawn_backward(&g, 0);

	CHECK(g.players[0].pawns[0].steps == 6);
}

/* backward_movement on: a pawn cannot move backward past its own start
 * square (it can't "un-release" itself off the ring). */
static void test_backward_movement_cannot_pass_start(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO);
	r.backward_movement = 1;
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 2;

	ludo_roll(&g, 5); /* 2 - 5 = -3, past the start square */
	CHECK((ludo_movable_pawns_backward(&g) & 1u) == 0);
}

/* backward_movement + blockade both on: a backward move is blocked by a
 * barricade on its path exactly like a forward one. */
static void test_backward_movement_blocked_by_blockade(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_PACHISI); /* backward_movement + blockade both on */
	ludo_set_rules(&g, &r);

	/* Player 1's two pawns stacked at absolute ring square 8:
	 * ring_square(1, 38) == (10 + 38) % 40 == 8. */
	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 38;
	g.players[1].pawns[1].in_play = 1;
	g.players[1].pawns[1].steps = 38;

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 10; /* ring_square(0, 10) == 10 */

	ludo_roll(&g, 4); /* backward path: 9, 8, 7, 6 -- crosses the blockaded square 8 */
	CHECK((ludo_movable_pawns_backward(&g) & 1u) == 0);

	ludo_roll(&g, 1); /* backward path: 9 only -- clear */
	CHECK((ludo_movable_pawns_backward(&g) & 1u) != 0);
}

/* three_sixes_forfeit_turn: a player's first two sixes in a row grant the
 * usual bonus roll (current_player unchanged); the THIRD forfeits the
 * whole turn immediately, inside ludo_roll() itself, before any release
 * or move -- current_player moves on right away, with no move needed in
 * between to observe it (nothing except ludo_end_turn() ever changes
 * current_player, and this rule is the only thing that can call it from
 * inside ludo_roll() itself for an otherwise ordinary roll). */
static void test_three_sixes_forfeit_turn(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO); /* three_sixes_forfeit_turn on by default */
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 5;

	ludo_roll(&g, 6);
	CHECK(g.current_player == 0); /* 1st six -- ordinary bonus roll */

	ludo_roll(&g, 6);
	CHECK(g.current_player == 0); /* 2nd six -- still an ordinary bonus roll */

	ludo_roll(&g, 6);
	CHECK(g.current_player == 1); /* 3rd six -- forfeited, turn passed */
}

/* Same three-sixes-in-a-row sequence and setup as
 * test_three_sixes_forfeit_turn() above, but with the rule explicitly
 * OFF (this project's original, traditional behaviour) -- sixes keep
 * chaining indefinitely with no forfeit. */
static void test_three_sixes_no_forfeit_when_rule_off(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO);
	r.three_sixes_forfeit_turn = 0;
	ludo_set_rules(&g, &r);

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 5;

	ludo_roll(&g, 6);
	ludo_roll(&g, 6);
	ludo_roll(&g, 6);
	CHECK(g.current_player == 0); /* still the same player -- no cap */
}

/*
 * Function: test_headless_full_games_invariants
 * Summary: Play out many complete, real, randomly-rolled games start to
 *          finish through nothing but the public API (exactly what
 *          game_view.c itself does), asserting a handful of invariants
 *          after every single roll and move -- a broad, automated
 *          equivalent of manually playing hundreds of games in Arculator
 *          looking for a rules bug, but deterministic (a fixed RNG seed)
 *          and runnable in well under a second -- catches a live-reported
 *          rules bug here first, and gives any *other* latent rules bug
 *          a much better chance of surfacing than a specific manual
 *          playthrough would.
 *
 *          The one invariant this is really built around: before every
 *          ludo_move_pawn() call, the pawn being moved must be in
 *          ludo_movable_pawns()'s mask, and its steps count afterwards
 *          must be exactly steps-before + the roll (or clamped to
 *          LUDO_TOTAL_STEPS if and only if that sum reaches or passes
 *          it) -- if compute_movable_pawns() ever let an overshooting
 *          move through, this is exactly the check that would catch it.
 *
 *          "finished iff steps == LUDO_TOTAL_STEPS" is not a valid
 *          invariant, since each pawn has its own dynamic finish
 *          threshold (game_logic.c's finish_threshold_for()) -- see
 *          expected_finish_threshold() below, a test-side
 *          reimplementation of that same logic against only the public
 *          ludo_pawn/ludo_game fields, used to keep asserting the
 *          equivalent per-pawn invariant.
 */

/*
 * Function: expected_finish_threshold (test helper)
 * Summary: Test-side mirror of game_logic.c's internal, non-exported
 *          finish_threshold_for() -- the steps value a specific pawn
 *          must reach to be finished right now, which is LUDO_TOTAL_STEPS
 *          minus however many of that player's *other* pawns have
 *          already finished (see include/game_logic.h's own note on
 *          this near LUDO_TOTAL_STEPS for the full reasoning).
 *          Deliberately reimplemented here rather than
 *          exposed from game_logic.c, since it's purely an internal
 *          rule detail -- callers only ever need ludo_movable_pawns()/
 *          ludo_move_pawn(), never this threshold directly.
 */
static int expected_finish_threshold(const ludo_game *g, int player, int pawn_index)
{
	int i, finished_ahead = 0;

	for (i = 0; i < LUDO_PAWNS; i++) {
		if (i != pawn_index && g->players[player].pawns[i].finished)
			finished_ahead++;
	}
	return LUDO_TOTAL_STEPS - finished_ahead;
}

static void test_headless_full_games_invariants(void)
{
	int game_num;

	srand(20260824u); /* fixed seed -- reproducible across runs */

	for (game_num = 0; game_num < 500; game_num++) {
		ludo_game g;
		int roll_num;
		int last_finished_count = 0;
		const int max_rolls = 5000; /* generous safety cap against an infinite game */

		ludo_init(&g);

		for (roll_num = 0; roll_num < max_rolls && g.winner == -1; roll_num++) {
			int roller, roll, player, pawn, finished_count = 0;
			unsigned movable;
			int candidates[LUDO_PAWNS], candidate_count = 0;
			int chosen, before_steps, after_steps, expected_after;

			/* Per game_logic.h's ludo_roll() doc comment, the caller
			 * must "keep rolling while ludo_movable_pawns() is 0 and
			 * the current_player has not changed" -- three consecutive
			 * failed tries makes ludo_roll() silently end the turn
			 * *internally* (see ludo_end_turn()), advancing
			 * current_player and resetting last_roll to 0 before
			 * returning. Capturing who was actually rolling first, and
			 * treating a changed current_player exactly like "nothing
			 * to resolve this roll" (skip straight to the next
			 * iteration, which will roll fresh for whoever it is now)
			 * is the same handling src/game_view.c's resolve_roll()
			 * uses. Getting this wrong here produces exactly the same
			 * class of bug:
			 * evaluating movability with a stale last_roll==0, under
			 * which every in-play pawn looks "movable" since adding
			 * zero can never overshoot. */
			roller = g.current_player;
			roll = ludo_roll(&g, 0);
			CHECK(roll >= 1 && roll <= 6);
			CHECK(g.current_player >= 0 && g.current_player < LUDO_PLAYERS);

			/* Every pawn's steps must be in range; a player's finished
			 * pawns, as a set, must occupy exactly the topmost N
			 * distinct home-column squares (LUDO_TOTAL_STEPS-N+1 ..
			 * LUDO_TOTAL_STEPS, one pawn each -- never two sharing a
			 * square, never a gap); an unfinished pawn's steps must
			 * stay strictly below its own current threshold; and the
			 * number of finished pawns overall must never go backwards.
			 *
			 * NOTE: expected_finish_threshold(), recomputed from the
			 * *current* snapshot of which siblings are finished, is only
			 * valid for a pawn that ISN'T finished yet -- an
			 * already-finished pawn's steps reflects whatever the
			 * threshold was at the moment *it* finished, which can be
			 * higher than what the same formula gives now if a sibling
			 * finished *after* it. (A first attempt at this test applied
			 * the retroactive formula to already-finished pawns too --
			 * wrong: pawn A finishing first at 43 doesn't retroactively
			 * become "should have been 42" just because pawn B finishes
			 * at 42 later. A debug harness run against real gameplay
			 * caught this immediately -- see docs/GAME_LOGIC.md's Round
			 * 7.20 note.) The set-based check below doesn't have this
			 * problem: it only cares about the *current* set of occupied
			 * squares, not any individual pawn's finishing history. */
			for (player = 0; player < LUDO_PLAYERS; player++) {
				int finished_steps[LUDO_PAWNS], nf = 0;
				int ii, jj;

				for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
					const ludo_pawn *p = &g.players[player].pawns[pawn];

					CHECK(p->steps >= 0 && p->steps <= LUDO_TOTAL_STEPS);
					if (p->finished)
						finished_steps[nf++] = p->steps;
					else
						CHECK(p->steps < expected_finish_threshold(&g, player, pawn));
				}

				for (ii = 0; ii < nf; ii++)
					for (jj = ii + 1; jj < nf; jj++)
						if (finished_steps[jj] < finished_steps[ii]) {
							int tmp = finished_steps[ii];

							finished_steps[ii] = finished_steps[jj];
							finished_steps[jj] = tmp;
						}
				for (ii = 0; ii < nf; ii++)
					CHECK(finished_steps[ii] == LUDO_TOTAL_STEPS - nf + 1 + ii);

				finished_count += nf;
			}
			CHECK(finished_count >= last_finished_count);
			last_finished_count = finished_count;

			if (g.current_player != roller)
				continue; /* turn passed automatically -- nothing rolled for whoever it is now */

			movable = ludo_movable_pawns(&g);
			if (movable == 0)
				continue; /* mandatory six-release, or genuinely stuck this roll */

			for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
				if (movable & (1u << pawn))
					candidates[candidate_count++] = pawn;
			chosen = candidates[rand() % candidate_count];

			before_steps = g.players[roller].pawns[chosen].steps;
			/* This is the exact check that would catch the originally
			 * reported bug: a movable pawn must never actually overshoot. */
			CHECK(before_steps + roll <= LUDO_TOTAL_STEPS);
			expected_after = before_steps + roll;

			ludo_move_pawn(&g, chosen);

			/* current_player may have changed again (a non-six ends
			 * the turn) -- re-read via roller/chosen, captured before
			 * the move, not g.current_player. */
			after_steps = g.players[roller].pawns[chosen].steps;
			CHECK(after_steps == expected_after);
		}

		CHECK(roll_num < max_rolls); /* every game actually finished within the cap */
	}
}

/*
 * Function: test_headless_full_games_pachisi_variant_invariants
 * Summary: A second, smaller-scale headless full-game simulation (see
 *          test_headless_full_games_invariants() above for the original,
 *          MEJN-only version and its own rationale) -- this one plays
 *          out many complete games under the full "Pachisi-style" preset
 *          (ludo_default_rules(LUDO_VARIANT_PACHISI): optional
 *          six-release, own-pawn capture off, overshoot bounce-back,
 *          blockade, backward movement, and free home-column
 *          manoeuvring all active together), exercising every Phase 1/2
 *          rule-toggle code path in combination -- something no
 *          individual per-toggle test above can do.
 *
 *          Move selection deliberately prefers any legal FORWARD move
 *          over a backward one whenever both exist (only resorting to
 *          ludo_move_pawn_backward() when it's the sole legal option --
 *          which does happen sometimes, e.g. when a blockade seals off
 *          every forward path) -- purely to keep games converging in a
 *          bounded number of rolls under fully random play; it isn't
 *          meant to be realistic strategy (that's ai.c's job).
 *
 *          Checks looser invariants than the MEJN version above: several
 *          of its exact-arithmetic assumptions (a movable pawn's steps
 *          always equal before + roll, never past LUDO_TOTAL_STEPS) are
 *          specifically NOT true once bounce-back and backward movement
 *          are active, so this only asserts what must still hold
 *          regardless -- every pawn's steps stays in its valid range,
 *          finished pawns still occupy distinct squares
 *          (finish_threshold_for()'s own invariant, untouched by any of
 *          the Phase 2 toggles), and the game terminates. Per the
 *          multi-rule-set plan's own testing section, this is meant to
 *          catch a gross regression (a crash, an infinite game, an
 *          out-of-range value) from the combination of rules working
 *          together -- not to pin down each toggle's exact behaviour,
 *          which the focused per-toggle tests above already do.
 */
static void test_headless_full_games_pachisi_variant_invariants(void)
{
	int game_num;
	ludo_rules rules = ludo_default_rules(LUDO_VARIANT_PACHISI);

	srand(20260827u); /* fixed seed -- reproducible across runs */

	for (game_num = 0; game_num < 200; game_num++) {
		ludo_game g;
		int roll_num;
		const int max_rolls = 5000;

		ludo_init(&g);
		ludo_set_rules(&g, &rules);

		for (roll_num = 0; roll_num < max_rolls && g.winner == -1; roll_num++) {
			int roller = g.current_player;
			int player, pawn;
			unsigned forward_mask, backward_mask;
			struct { int pawn; int backward; } candidates[LUDO_PAWNS];
			int candidate_count = 0, chosen;

			ludo_roll(&g, 0);

			for (player = 0; player < LUDO_PLAYERS; player++) {
				int finished_steps[LUDO_PAWNS], nf = 0;
				int ii, jj;

				for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
					const ludo_pawn *p = &g.players[player].pawns[pawn];

					CHECK(p->steps >= 0 && p->steps <= LUDO_TOTAL_STEPS);
					if (p->finished)
						finished_steps[nf++] = p->steps;
				}
				for (ii = 0; ii < nf; ii++)
					for (jj = ii + 1; jj < nf; jj++)
						if (finished_steps[jj] < finished_steps[ii]) {
							int tmp = finished_steps[ii];

							finished_steps[ii] = finished_steps[jj];
							finished_steps[jj] = tmp;
						}
				for (ii = 0; ii < nf; ii++)
					CHECK(finished_steps[ii] == LUDO_TOTAL_STEPS - nf + 1 + ii);
			}

			if (g.current_player != roller)
				continue; /* turn passed automatically */

			forward_mask = ludo_movable_pawns(&g);
			backward_mask = ludo_movable_pawns_backward(&g);
			if (forward_mask == 0 && backward_mask == 0)
				continue; /* genuinely stuck this roll */

			if (forward_mask != 0) {
				for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
					if (forward_mask & (1u << pawn)) {
						candidates[candidate_count].pawn = pawn;
						candidates[candidate_count].backward = 0;
						candidate_count++;
					}
			} else {
				for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
					if (backward_mask & (1u << pawn)) {
						candidates[candidate_count].pawn = pawn;
						candidates[candidate_count].backward = 1;
						candidate_count++;
					}
			}
			chosen = rand() % candidate_count;

			if (candidates[chosen].backward)
				ludo_move_pawn_backward(&g, candidates[chosen].pawn);
			else
				ludo_move_pawn(&g, candidates[chosen].pawn);
		}

		CHECK(roll_num < max_rolls); /* every game actually finished within the cap */
	}
}

/*
 * Function: test_headless_all_rule_combinations
 * Summary: Extends test_headless_full_games_pachisi_variant_invariants()'s
 *          own approach (loose, toggle-agnostic invariants; forward move
 *          preferred, backward as fallback) from that one hand-picked
 *          preset to EVERY one of the 2^7 = 128 possible combinations of
 *          this engine's 7 independent house-rule booleans -- not just
 *          the 3 curated variant presets (MEJN/Ludo/Pachisi-style)
 *          ludo_default_rules() offers.
 *
 *          Why this is worth having, and why it's tractable: the Rules
 *          dialogue (src/rules_view.c) lets a player flip any of the 7
 *          toggles individually on top of whichever variant preset they
 *          started from, so the real reachable configuration space is
 *          all 128 combinations, not just the 3 presets -- but hand-
 *          writing a dedicated test per combination clearly isn't
 *          practical. 128 is small enough to enumerate EXHAUSTIVELY,
 *          though (not randomly sampled/fuzzed -- every single one),
 *          each run cheaply (a handful of games, not the 200-500 the
 *          single-preset simulations above use) -- covering the full
 *          reachable space this way costs about as much runtime as one
 *          more chunk of the existing headless simulations, not 128x it.
 *
 *          `variant` itself is never read by any gameplay logic in
 *          game_logic.c (confirmed by inspection -- it's only ever SET,
 *          inside ludo_default_rules(), purely for the Rules dialogue/
 *          save-file's own bookkeeping), so it's fixed at
 *          LUDO_VARIANT_MEJN here throughout -- only the 7 booleans
 *          actually vary.
 *
 *          Same invariants as the Pachisi-only version above, for the
 *          same reason (several of the MEJN-only version's tighter
 *          arithmetic assumptions aren't true once bounce-back/backward
 *          movement are active, so this only asserts what must hold
 *          regardless of which toggles are on): every pawn's steps
 *          stays in its valid range, finished pawns occupy distinct
 *          correct squares, and every game actually terminates within
 *          the roll cap -- catching a gross regression (crash, infinite
 *          game, out-of-range value, illegal shared square) from any
 *          combination of toggles working together, not pinning down
 *          each one's exact behaviour (the focused per-toggle tests
 *          earlier in this file already do that).
 */
static void test_headless_all_rule_combinations(void)
{
	int combo;

	srand(20260829u); /* fixed seed -- reproducible across runs */

	for (combo = 0; combo < 128; combo++) {
		ludo_rules rules = {0};
		int game_num;

		rules.variant = LUDO_VARIANT_MEJN; /* never read by gameplay logic -- see doc comment */
		rules.mandatory_six_release   = (combo >> 0) & 1;
		rules.own_pawn_capture        = (combo >> 1) & 1;
		rules.overshoot_bounce        = (combo >> 2) & 1;
		rules.blockade                = (combo >> 3) & 1;
		rules.backward_movement       = (combo >> 4) & 1;
		rules.free_home_column        = (combo >> 5) & 1;
		rules.no_six_needed_last_pawn = (combo >> 6) & 1;

		for (game_num = 0; game_num < 5; game_num++) {
			ludo_game g;
			int roll_num;
			const int max_rolls = 5000;

			ludo_init(&g);
			ludo_set_rules(&g, &rules);

			for (roll_num = 0; roll_num < max_rolls && g.winner == -1; roll_num++) {
				int roller = g.current_player;
				int player, pawn;
				unsigned forward_mask, backward_mask;
				struct { int pawn; int backward; } candidates[LUDO_PAWNS];
				int candidate_count = 0, chosen;

				ludo_roll(&g, 0);

				for (player = 0; player < LUDO_PLAYERS; player++) {
					int finished_steps[LUDO_PAWNS], nf = 0;
					int ii, jj;

					for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
						const ludo_pawn *p = &g.players[player].pawns[pawn];

						CHECK(p->steps >= 0 && p->steps <= LUDO_TOTAL_STEPS);
						if (p->finished)
							finished_steps[nf++] = p->steps;
					}
					for (ii = 0; ii < nf; ii++)
						for (jj = ii + 1; jj < nf; jj++)
							if (finished_steps[jj] < finished_steps[ii]) {
								int tmp = finished_steps[ii];

								finished_steps[ii] = finished_steps[jj];
								finished_steps[jj] = tmp;
							}
					for (ii = 0; ii < nf; ii++)
						CHECK(finished_steps[ii] == LUDO_TOTAL_STEPS - nf + 1 + ii);
				}

				if (g.current_player != roller)
					continue; /* turn passed automatically */

				forward_mask = ludo_movable_pawns(&g);
				backward_mask = ludo_movable_pawns_backward(&g);
				if (forward_mask == 0 && backward_mask == 0)
					continue; /* genuinely stuck this roll */

				if (forward_mask != 0) {
					for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
						if (forward_mask & (1u << pawn)) {
							candidates[candidate_count].pawn = pawn;
							candidates[candidate_count].backward = 0;
							candidate_count++;
						}
				} else {
					for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
						if (backward_mask & (1u << pawn)) {
							candidates[candidate_count].pawn = pawn;
							candidates[candidate_count].backward = 1;
							candidate_count++;
						}
				}
				chosen = rand() % candidate_count;

				if (candidates[chosen].backward)
					ludo_move_pawn_backward(&g, candidates[chosen].pawn);
				else
					ludo_move_pawn(&g, candidates[chosen].pawn);
			}

			CHECK(roll_num < max_rolls); /* every game actually finished within the cap */
		}
	}
}

int main(void)
{
	RUN(test_new_game_starts_correctly);
	RUN(test_stuck_all_home_decrements_tries);
	RUN(test_three_failed_tries_passes_turn);
	RUN(test_six_releases_home_pawn);
	RUN(test_forced_pawn_next_roll);
	RUN(test_capture_sends_pawn_home);
	RUN(test_own_pawn_sent_home_on_ring_collision);
	RUN(test_home_column_blocking);
	RUN(test_overshoot_not_movable);
	RUN(test_one_short_overshoot_not_movable);
	RUN(test_pawn_finishes_exactly);
	RUN(test_second_finishing_pawn_lands_one_square_short);
	RUN(test_winner_detected_when_all_pawns_finish);
	RUN(test_finish_order_tracks_all_four_places);
	RUN(test_extra_roll_on_six_keeps_same_player);
	RUN(test_six_bonus_survives_another_players_win);
	RUN(test_non_six_move_ends_turn);
	RUN(test_own_pawn_capture_off_shares_square);
	RUN(test_optional_six_release_offers_choice);
	RUN(test_overshoot_bounce_moves_backward);
	RUN(test_overshoot_bounce_clamped_at_home_column_entrance);
	RUN(test_no_six_needed_for_last_home_pawn);
	RUN(test_free_home_column_allows_passing);
	RUN(test_blockade_blocks_landing_and_passing_through);
	RUN(test_blockade_off_allows_landing_and_passing);
	RUN(test_blockade_blocks_release_at_own_start_square);
	RUN(test_backward_movement_moves_pawn_back);
	RUN(test_backward_movement_cannot_pass_start);
	RUN(test_backward_movement_blocked_by_blockade);
	RUN(test_three_sixes_forfeit_turn);
	RUN(test_three_sixes_no_forfeit_when_rule_off);
	RUN(test_headless_full_games_invariants);
	RUN(test_headless_full_games_pachisi_variant_invariants);
	RUN(test_headless_all_rule_combinations);

	printf("\n%d/%d checks passed (%d test%s)\n",
	       checks_run - checks_failed, checks_run,
	       tests_run, tests_run == 1 ? "" : "s");

	return checks_failed ? 1 : 0;
}
