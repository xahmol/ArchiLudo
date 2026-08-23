/*
 * ArchiLudo AI test suite.
 * Host-side, dependency-free -- same CHECK()/RUN() harness style as
 * tests/test_game_logic.c and tests/test_board_layout.c. See
 * include/ai.h / src/ai.c for what's being verified: given a set of
 * legal moves (from ludo_movable_pawns()), does ludo_ai_choose_pawn()
 * pick the one the scoring weights say it should?
 */

#include <stdio.h>
#include <stdlib.h>

#include "ai.h"
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

/* Given a choice between a move that captures an opponent and one that
 * doesn't, the AI should capture. */
static void test_prefers_capture(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 0;
	g.last_roll = 3;

	/* Pawn 0: ring square 2 -> 5, empty square, unremarkable move. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 2;

	/* Pawn 1: ring square 20 -> 23, where an opponent (player 2, whose
	 * own ring entry is square 20) is sitting -- player 2 steps=3 puts
	 * them at square 23 too. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 20;
	g.players[2].pawns[0].in_play = 1;
	g.players[2].pawns[0].steps = 3;

	CHECK(ludo_ai_choose_pawn(&g, 0x3, LUDO_AI_NORMAL) == 1);
}

/* Given a choice between finishing a pawn and an ordinary move, the AI
 * should finish it. */
static void test_prefers_finishing(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 0;
	g.last_roll = 2;

	/* Pawn 0: one square short of the end of the home column -- this
	 * roll finishes it exactly. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS - 2;

	/* Pawn 1: an ordinary mid-ring move, nothing special. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 5;

	CHECK(ludo_ai_choose_pawn(&g, 0x3, LUDO_AI_NORMAL) == 0);
}

/* Finishing this player's very last unfinished pawn (the winning move)
 * outweighs finishing a different pawn that leaves others still in
 * play, even though both moves complete a pawn. */
static void test_prefers_winning_move(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 0;
	g.last_roll = 1;

	/* Pawns 1 and 2 already finished. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].finished = 1;
	g.players[0].pawns[1].steps = LUDO_TOTAL_STEPS;
	g.players[0].pawns[2].in_play = 1;
	g.players[0].pawns[2].finished = 1;
	g.players[0].pawns[2].steps = LUDO_TOTAL_STEPS;

	/* Pawn 0: one short of finishing -- moving it wins the game outright. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS - 1;

	/* Pawn 3: also one short of finishing, but pawn 0 stays unfinished,
	 * so this move does NOT win the game even though it also finishes a
	 * pawn -- the AI should still prefer pawn 0's move over this one. */
	g.players[0].pawns[3].in_play = 1;
	g.players[0].pawns[3].steps = LUDO_TOTAL_STEPS - 1;

	CHECK(ludo_ai_choose_pawn(&g, 0x9, LUDO_AI_NORMAL) == 0);
}

/* Given a choice between a move that lands on (and sends home) the
 * player's own other pawn, and an ordinary move, the AI should avoid
 * the collision when a reasonable alternative exists. */
static void test_avoids_own_collision_when_alternative_exists(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 0;
	g.last_roll = 3;

	/* Pawn 0: ring square 2 -> 5, where pawn 1 (same player) already
	 * sits -- landing here sends pawn 1 home. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 2;
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 5;

	/* Pawn 2: an ordinary, uncontested move. */
	g.players[0].pawns[2].in_play = 1;
	g.players[0].pawns[2].steps = 15;

	CHECK(ludo_ai_choose_pawn(&g, 0x5, LUDO_AI_NORMAL) == 2);
}

/* A pawn currently within reach of an opponent's next throw (real board
 * distance, not GEOS's same-lap-position shortcut) should be preferred
 * for movement over one that isn't threatened, all else being similar. */
static void test_prefers_escaping_danger(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 0;
	g.last_roll = 3;

	/* Pawn 0: ring square 8, with an opponent (player 1, entry square
	 * 10) sitting 6 squares behind at ring square 2 (player 1 steps 32,
	 * i.e. (10+32)%40=2) -- exactly reachable next turn. Moving to
	 * square 11 escapes that threat (and isn't itself an entry square,
	 * which would confound the test with a separate penalty). */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 8;
	g.players[1].pawns[0].in_play = 1;
	g.players[1].pawns[0].steps = 32;

	/* Pawn 1: ring square 25, no opponent anywhere nearby. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 25;

	CHECK(ludo_ai_choose_pawn(&g, 0x3, LUDO_AI_NORMAL) == 0);
}

/* With only one legal move, that's what gets chosen -- the trivial case,
 * but worth a direct check since every other test always offers a
 * choice. */
static void test_only_one_choice(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 1;
	g.last_roll = 4;
	g.players[1].pawns[2].in_play = 1;
	g.players[1].pawns[2].steps = 0;

	CHECK(ludo_ai_choose_pawn(&g, 0x4, LUDO_AI_NORMAL) == 2);
}

/*
 * Function: test_headless_four_ai_games
 * Summary: Play out several complete games with all four seats AI-
 *          controlled (ludo_ai_choose_pawn() choosing every single
 *          move) start to finish, purely through the public API --
 *          exactly the "four AI players" scenario the actual save file
 *          that prompted this investigation was in (see
 *          docs/ARCHITECTURE.md's Round 7.8), and exactly what
 *          src/game_view.c's own resolve_roll() does turn after turn in
 *          a real all-AI game. Same invariant style as
 *          tests/test_game_logic.c's own headless simulation (this
 *          project's engine-only equivalent, using a random legal pawn
 *          instead of the AI) -- this one additionally checks that
 *          ludo_ai_choose_pawn() always returns a pawn actually present
 *          in the movable mask it was given, which the scoring-weights
 *          tests elsewhere in this file don't exercise across a real,
 *          evolving board.
 */
static void test_headless_four_ai_games(void)
{
	int game_num;

	srand(20260824u); /* same fixed seed as test_game_logic.c's simulation */

	for (game_num = 0; game_num < 20; game_num++) {
		ludo_game g;
		int roll_num;
		int last_finished_count = 0;
		const int max_rolls = 5000;

		ludo_init(&g);

		for (roll_num = 0; roll_num < max_rolls && g.winner == -1; roll_num++) {
			int roller, roll, player, pawn, finished_count = 0;
			unsigned movable;
			int chosen, before_steps, after_steps, expected_after;

			/* See test_game_logic.c's test_headless_full_games_invariants()
			 * for why the roller must be captured before the roll and
			 * checked against current_player afterwards -- ludo_roll()
			 * can silently pass the turn (three failed tries), and
			 * resolving a "move" against the new player's fresh,
			 * not-yet-thrown state is exactly the bug this whole
			 * investigation started from. */
			roller = g.current_player;
			roll = ludo_roll(&g, 0);
			CHECK(roll >= 1 && roll <= 6);

			for (player = 0; player < LUDO_PLAYERS; player++) {
				for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
					const ludo_pawn *p = &g.players[player].pawns[pawn];

					CHECK(p->steps >= 0 && p->steps <= LUDO_TOTAL_STEPS);
					CHECK((p->steps == LUDO_TOTAL_STEPS) == (p->finished != 0));
					if (p->finished)
						finished_count++;
				}
			}
			CHECK(finished_count >= last_finished_count);
			last_finished_count = finished_count;

			if (g.current_player != roller)
				continue;

			movable = ludo_movable_pawns(&g);
			if (movable == 0)
				continue;

			chosen = ludo_ai_choose_pawn(&g, movable, LUDO_AI_NORMAL);
			CHECK(chosen >= 0 && chosen < LUDO_PAWNS);
			CHECK((movable & (1u << chosen)) != 0);

			before_steps = g.players[roller].pawns[chosen].steps;
			CHECK(before_steps + roll <= LUDO_TOTAL_STEPS);
			expected_after = before_steps + roll;

			ludo_move_pawn(&g, chosen);

			after_steps = g.players[roller].pawns[chosen].steps;
			CHECK(after_steps == expected_after);
		}

		CHECK(roll_num < max_rolls);
		CHECK(g.winner >= 0 && g.winner < LUDO_PLAYERS);
	}
}

int main(void)
{
	RUN(test_prefers_capture);
	RUN(test_prefers_finishing);
	RUN(test_prefers_winning_move);
	RUN(test_avoids_own_collision_when_alternative_exists);
	RUN(test_prefers_escaping_danger);
	RUN(test_only_one_choice);
	RUN(test_headless_four_ai_games);

	printf("\n%d/%d checks passed (%d test%s)\n",
	       checks_run - checks_failed, checks_run,
	       tests_run, tests_run == 1 ? "" : "s");

	return checks_failed ? 1 : 0;
}
