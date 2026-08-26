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

/* Round 7.20, exactly the scenario reported live: once one pawn has
 * finished (permanently occupying LUDO_TOTAL_STEPS), a second pawn of
 * the same player must NOT be able to stack on that same square -- its
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
	 * (round 7.20 -- see game_logic.h) -- LUDO_TOTAL_STEPS,
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
 * Round 7.35 regression: once ANY player has won (g.winner != -1), every
 * OTHER player who hasn't finished yet must still keep their own normal
 * six-goes-again bonus -- per this project's "continue playing after the
 * first winner" house-rule mode (see docs/ARCHITECTURE.md's Round 7.35).
 * The bug this guards against checked the global g.winner == -1 instead
 * of this specific player's own all_pawns_finished(), which meant every
 * remaining player permanently lost their bonus turn for the rest of the
 * game the moment anyone won -- never noticed while the game simply ended
 * at the first winner, since nothing kept playing afterwards to exercise
 * it.
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

/* Exactly the scenario reported live in Arculator: a pawn one step short
 * of finishing (needs exactly 1) must never be reported movable, or
 * moved at all, on a roll of 2 -- see docs/ARCHITECTURE.md's Round 7.8
 * for the investigation this came out of. test_overshoot_not_movable()
 * above already covers "2 short, roll 3" -- this is the "1 short"
 * off-by-one neighbour, worth its own explicit check. */
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
 * Function: test_headless_full_games_invariants
 * Summary: Play out many complete, real, randomly-rolled games start to
 *          finish through nothing but the public API (exactly what
 *          game_view.c itself does), asserting a handful of invariants
 *          after every single roll and move -- a broad, automated
 *          equivalent of manually playing hundreds of games in Arculator
 *          looking for a rules bug, but deterministic (a fixed RNG seed)
 *          and runnable in well under a second. Written in direct
 *          response to a live-reported bug (a pawn one step short of
 *          finishing appeared to move on an overshooting roll) that
 *          turned out not to reproduce through this API at all (see
 *          docs/ARCHITECTURE.md's Round 7.8) -- this exists so the next
 *          such report gets checked here first, and so any *other*
 *          latent rules bug this specific manual playthrough didn't
 *          happen to trigger gets a much better chance of surfacing.
 *
 *          The one invariant this is really built around: before every
 *          ludo_move_pawn() call, the pawn being moved must be in
 *          ludo_movable_pawns()'s mask, and its steps count afterwards
 *          must be exactly steps-before + the roll (or clamped to
 *          LUDO_TOTAL_STEPS if and only if that sum reaches or passes
 *          it) -- if compute_movable_pawns() ever let an overshooting
 *          move through, this is exactly the check that would catch it.
 *
 *          Round 7.20 update: "finished iff steps == LUDO_TOTAL_STEPS"
 *          stopped being a valid invariant once each pawn got its own
 *          dynamic finish threshold (game_logic.c's
 *          finish_threshold_for()) -- see expected_finish_threshold()
 *          below, a test-side reimplementation of that same logic
 *          against only the public ludo_pawn/ludo_game fields, used to
 *          keep asserting the equivalent per-pawn invariant.
 */

/*
 * Function: expected_finish_threshold (test helper)
 * Summary: Test-side mirror of game_logic.c's internal, non-exported
 *          finish_threshold_for() -- the steps value a specific pawn
 *          must reach to be finished right now, which is LUDO_TOTAL_STEPS
 *          minus however many of that player's *other* pawns have
 *          already finished (see game_logic.h's Round 7.20 note for the
 *          full reasoning). Deliberately reimplemented here rather than
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
			 * is the same fix applied to src/game_view.c's
			 * resolve_roll() -- see docs/ARCHITECTURE.md's Round 7.8.
			 * Getting this wrong here (as an earlier version of this
			 * very test did) produces exactly the same class of bug:
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
	RUN(test_extra_roll_on_six_keeps_same_player);
	RUN(test_six_bonus_survives_another_players_win);
	RUN(test_non_six_move_ends_turn);
	RUN(test_headless_full_games_invariants);

	printf("\n%d/%d checks passed (%d test%s)\n",
	       checks_run - checks_failed, checks_run,
	       tests_run, tests_run == 1 ? "" : "s");

	return checks_failed ? 1 : 0;
}
