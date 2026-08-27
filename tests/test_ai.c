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

/* Round 7.14: given a choice between advancing a pawn already safely in
 * its home column and an ordinary ring move whose only merit is a minor
 * tactical bonus (here, leaving its own contested entry square), the AI
 * should prefer the risk-free home-column advance -- per explicit user
 * report that it wasn't visibly doing so before WEIGHT_HOME_COLUMN_ADVANCE_*
 * was added (see src/ai.c). */
static void test_prefers_home_column_advance_over_ring_tactic(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 0;
	g.last_roll = 2;

	/* Pawn 0: already in the home column (steps 40 -> 42), an ordinary
	 * safe advance with no capture/danger heuristic applicable. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 40;

	/* Pawn 1: sitting on its own entry square (steps 0) -- moving off it
	 * earns WEIGHT_ENTRY_SQUARE_LEAVE, the kind of minor ring tactic that
	 * used to outweigh a home-column advance's flat per-step progress
	 * alone. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 0;

	CHECK(ludo_ai_choose_pawn(&g, 0x3, LUDO_AI_NORMAL) == 0);
}

/* ...but an actual capture still outranks a home-column advance -- the
 * new bonus must not be so large it swamps a guaranteed tactical gain. */
static void test_capture_still_beats_home_column_advance(void)
{
	ludo_game g;

	ludo_init(&g);
	g.current_player = 0;
	g.last_roll = 1;

	/* Pawn 0: an ordinary home-column advance (steps 40 -> 41). */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 40;

	/* Pawn 1: ring square 2 -> 3, where an opponent (player 2, entry
	 * square 20) is sitting -- player 2 steps=23 puts them at square
	 * (20+23)%40=3 too. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 2;
	g.players[2].pawns[0].in_play = 1;
	g.players[2].pawns[0].steps = 23;

	CHECK(ludo_ai_choose_pawn(&g, 0x3, LUDO_AI_NORMAL) == 1);
}

/* Round 7.45: with g->rules.own_pawn_capture off, landing on the
 * player's own other pawn no longer sends it home -- so, unlike
 * test_avoids_own_collision_when_alternative_exists() above (the
 * default, own_pawn_capture-on behaviour this is the opposite of), the
 * AI should have no reason to avoid it, and should prefer it over a
 * plain, uncontested move whenever it's otherwise the stronger play
 * (here: it happens to also be a capture of an opponent sharing that
 * square, which the collision-avoidance bug would previously have
 * masked entirely). */
static void test_no_own_collision_avoidance_when_capture_off(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO); /* own_pawn_capture off */
	ludo_set_rules(&g, &r);
	g.current_player = 0;
	g.last_roll = 3;

	/* Pawn 0: ring square 15 -> 18, where pawn 1 (same player) sits --
	 * under own_pawn_capture off this is a harmless shared square, not a
	 * setback, so nothing should discourage this move; it also happens
	 * to be further along than pawn 2's alternative below, so a
	 * lingering collision penalty (the bug this guards against) would
	 * be the only thing that could make the AI prefer pawn 2 instead. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 15;
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 18;

	/* Pawn 2: an ordinary, uncontested move, less progress than pawn 0's. */
	g.players[0].pawns[2].in_play = 1;
	g.players[0].pawns[2].steps = 2;

	CHECK(ludo_ai_choose_pawn(&g, 0x5, LUDO_AI_NORMAL) == 0);
}

/* Round 7.45: with g->rules.mandatory_six_release off, releasing a home
 * pawn is a genuine scored choice (score_release()) rather than
 * happening automatically before the AI ever gets a say. Given a choice
 * between releasing a fresh pawn and an ordinary, uncontested ring move,
 * the AI should prefer releasing -- bringing a new pawn into play is
 * scored as clearly valuable (WEIGHT_RELEASE_BASE), and an uncontested
 * ring move earns nothing beyond a small per-step progress tie-breaker. */
static void test_ai_can_choose_optional_release(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_LUDO); /* optional six-release */
	ludo_set_rules(&g, &r);
	g.current_player = 0;

	/* Pawn 1: an ordinary, uncontested ring move -- nothing special. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 15;

	ludo_roll(&g, 6);
	/* Both pawn 0 (release) and pawn 1 (ordinary move) must be offered --
	 * confirms this is a genuine choice, not a trivial single option. */
	CHECK((ludo_movable_pawns(&g) & (1u << 0)) != 0);
	CHECK((ludo_movable_pawns(&g) & (1u << 1)) != 0);

	CHECK(ludo_ai_choose_pawn(&g, ludo_movable_pawns(&g), LUDO_AI_NORMAL) == 0);
}

/* Round 7.45: score_move() must use game_logic.c's own
 * ludo_resolve_move_destination() rather than a naive p->steps + roll --
 * otherwise, under g->rules.overshoot_bounce, a move that actually
 * bounces backward (nowhere near finishing) could be mis-scored as if it
 * finished the pawn outright, since the naive sum alone can exceed
 * LUDO_TOTAL_STEPS. Pawn 0's roll bounces it back to just inside the
 * home column (real score: an ordinary home-column-entry advance,
 * ~2200) rather than finishing it (a naive scorer would wrongly return
 * WEIGHT_FINISH, 6000); pawn 1 makes a genuine, real capture (~4040) --
 * a score between the two, so the AI's choice directly reveals which
 * scoring pawn 0 actually got: correct math prefers the real capture
 * (pawn 1), the old naive bug would have wrongly preferred pawn 0. */
static void test_ai_scores_bounced_destination_not_naive_overshoot(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_MEJN);
	r.overshoot_bounce = 1;
	ludo_set_rules(&g, &r);
	g.current_player = 0;
	g.last_roll = 6;

	/* Pawn 0: 2 short of finishing -- rolling 6 overshoots by 4, bounces
	 * to LUDO_TOTAL_STEPS - 4 == 39, clamped up to LUDO_RING_LENGTH (40,
	 * the home column entrance) since 39 is back on the shared ring. */
	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = LUDO_TOTAL_STEPS - 2;

	/* Pawn 1: ring square 2 -> 8, where an opponent (player 2, entry
	 * square 20) is sitting -- player 2 steps=28 puts them at square
	 * (20+28)%40=8 too. */
	g.players[0].pawns[1].in_play = 1;
	g.players[0].pawns[1].steps = 2;
	g.players[2].pawns[0].in_play = 1;
	g.players[2].pawns[0].steps = 28;

	CHECK(ludo_ai_choose_pawn(&g, 0x3, LUDO_AI_NORMAL) == 1);
}

/* A trivial sanity check for the backward-movement fallback API: with
 * only pawn 0 backward-movable, that's what gets chosen. Real backward-
 * movement strategy isn't scored deeply (see ai.c's top-of-file
 * comment) -- this only confirms the API picks a legal, non-crashing
 * choice. */
static void test_ai_backward_fallback_picks_legal_pawn(void)
{
	ludo_game g;
	ludo_rules r;

	ludo_init(&g);
	r = ludo_default_rules(LUDO_VARIANT_PACHISI);
	ludo_set_rules(&g, &r);
	g.current_player = 0;
	g.last_roll = 4;

	g.players[0].pawns[0].in_play = 1;
	g.players[0].pawns[0].steps = 10;

	CHECK(ludo_ai_choose_pawn_backward(&g, 0x1) == 0);
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

			/* See test_game_logic.c's test_headless_full_games_invariants()
			 * for the full reasoning (round 7.20) -- a player's finished
			 * pawns, as a *set*, must occupy exactly the topmost N
			 * distinct home-column squares; checking each pawn's steps
			 * against a threshold retroactively recomputed from the
			 * *current* finished count is wrong for a pawn that already
			 * finished earlier, before a sibling finished after it. */
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

				finished_count += nf;
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

/*
 * Function: test_headless_four_ai_games_pachisi_variant
 * Summary: The AI equivalent of test_game_logic.c's
 *          test_headless_full_games_pachisi_variant_invariants() -- all
 *          four seats AI-controlled under the full Pachisi-style preset
 *          (every rule toggle active at once), preferring
 *          ludo_ai_choose_pawn() and only falling back to
 *          ludo_ai_choose_pawn_backward() when the forward bitmask is
 *          empty -- exactly the pattern src/game_view.c's own
 *          advance_ai_turns() is expected to use once Phase 4 wires this
 *          up (see docs/ARCHITECTURE.md's Round 7.45). Checks only that
 *          nothing crashes, every AI choice is actually legal, and every
 *          game terminates -- the same looser invariant style as the
 *          engine-only Pachisi simulation, for the same reason (bounce/
 *          backward movement break the exact-arithmetic assumptions the
 *          original MEJN-only simulation relies on).
 */
static void test_headless_four_ai_games_pachisi_variant(void)
{
	int game_num;
	ludo_rules rules = ludo_default_rules(LUDO_VARIANT_PACHISI);

	srand(20260827u); /* same seed as test_game_logic.c's Pachisi simulation */

	for (game_num = 0; game_num < 20; game_num++) {
		ludo_game g;
		int roll_num;
		const int max_rolls = 5000;

		ludo_init(&g);
		ludo_set_rules(&g, &rules);

		for (roll_num = 0; roll_num < max_rolls && g.winner == -1; roll_num++) {
			int roller = g.current_player;
			int player, pawn;
			unsigned forward_mask, backward_mask;
			int chosen;

			ludo_roll(&g, 0);

			for (player = 0; player < LUDO_PLAYERS; player++)
				for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
					CHECK(g.players[player].pawns[pawn].steps >= 0
					   && g.players[player].pawns[pawn].steps <= LUDO_TOTAL_STEPS);

			if (g.current_player != roller)
				continue;

			forward_mask = ludo_movable_pawns(&g);
			backward_mask = ludo_movable_pawns_backward(&g);
			if (forward_mask == 0 && backward_mask == 0)
				continue;

			if (forward_mask != 0) {
				chosen = ludo_ai_choose_pawn(&g, forward_mask, LUDO_AI_NORMAL);
				CHECK(chosen >= 0 && chosen < LUDO_PAWNS);
				CHECK((forward_mask & (1u << chosen)) != 0);
				ludo_move_pawn(&g, chosen);
			} else {
				chosen = ludo_ai_choose_pawn_backward(&g, backward_mask);
				CHECK(chosen >= 0 && chosen < LUDO_PAWNS);
				CHECK((backward_mask & (1u << chosen)) != 0);
				ludo_move_pawn_backward(&g, chosen);
			}
		}

		CHECK(roll_num < max_rolls);
	}
}

int main(void)
{
	RUN(test_prefers_capture);
	RUN(test_prefers_finishing);
	RUN(test_prefers_winning_move);
	RUN(test_avoids_own_collision_when_alternative_exists);
	RUN(test_prefers_escaping_danger);
	RUN(test_prefers_home_column_advance_over_ring_tactic);
	RUN(test_capture_still_beats_home_column_advance);
	RUN(test_no_own_collision_avoidance_when_capture_off);
	RUN(test_ai_can_choose_optional_release);
	RUN(test_ai_scores_bounced_destination_not_naive_overshoot);
	RUN(test_ai_backward_fallback_picks_legal_pawn);
	RUN(test_only_one_choice);
	RUN(test_headless_four_ai_games);
	RUN(test_headless_four_ai_games_pachisi_variant);

	printf("\n%d/%d checks passed (%d test%s)\n",
	       checks_run - checks_failed, checks_run,
	       tests_run, tests_run == 1 ? "" : "s");

	return checks_failed ? 1 : 0;
}
