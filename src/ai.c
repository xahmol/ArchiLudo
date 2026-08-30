/*
 * ArchiLudo AI -- implementation.
 * See include/ai.h for the module overview and API docs.
 *
 * Adapted from GeoLudo's `computerchoosepawn()`
 * (`/home/xahmol/git/ludo/GEOS/src/main.c`) -- assessed in detail before
 * writing this. Same overall shape carried over: score every currently
 * movable pawn and pick the highest score. What changed, and why:
 *
 * - GEOS tracks each pawn's position as (track, raw-position-within-that-
 *   player's-own-lap) and has to un-wrap that into an absolute board
 *   square by hand per player (four near-identical `if` blocks). This
 *   project's game_logic.c already has a single unified `steps` counter
 *   (0..LUDO_TOTAL_STEPS) and board_layout.c-style ring_square() maths
 *   (duplicated here as a small static helper, since game_logic.c
 *   doesn't expose it), so none of that unwrapping is needed.
 * - GEOS's "danger" heuristic (a pawn escaping/approaching an opponent)
 *   compares raw same-lap position numbers *across different players*
 *   directly (`vn - playerpos[y][z][1]`), which isn't actually a valid
 *   board distance except by coincidence (each player's lap starts at a
 *   different absolute square). Recomputed properly here using real
 *   ring-square distance via ring_distance_behind(), and the "reachable
 *   in one throw" range corrected to the actual 1-6 a die can roll (GEOS
 *   used "< 6", i.e. 1-5, missing the exact-6 case).
 * - GEOS scores landing on one of *its own* pawns at -8000 (strongly
 *   avoided, reflecting that game's rule that this is illegal/blocked).
 *   ArchiLudo's rule is different (see game_logic.c's capture_at()):
 *   landing on your own pawn can be legal and send the earlier one
 *   home. Scored here as a real but *scaled* penalty instead
 *   of a near-absolute one -- losing a pawn that had barely moved barely
 *   matters, losing one that was almost home matters a lot -- since it's
 *   sometimes the only legal (or the objectively best) move, not
 *   something to treat as if it never happens.
 * - The "is this the winning move" check is exact here (every other pawn
 *   actually finished), rather than GEOS's proxy
 *   (`playerdata[player][1]==1`, "only one pawn not yet at its
 *   destination" -- counted once per game, whose accuracy after
 *   corner-case sequences of moves wasn't independently re-verified here).
 *
 * Difficulty levels: only LUDO_AI_NORMAL (this weighted heuristic) is
 * implemented. LUDO_AI_EASY currently falls back to NORMAL too -- the
 * intended design (not yet built) is a much simpler rule (e.g. "prefer
 * releasing/advancing the frontmost pawn, ignore capture/danger
 * entirely") or randomised choice among legal moves, so a human opponent
 * can find it genuinely easier to beat. LUDO_AI_HARD is reserved for a
 * possible future minimax/lookahead search -- see docs/ARCHITECTURE.md's
 * Roadmap.
 *
 * score_move() accounts for game_logic.c's configurable g->rules (see
 * game_logic.h's ludo_rules) throughout, rather than assuming a single
 * fixed ruleset:
 * - Own-pawn collision is only scored as a real setback when
 *   g->rules.own_pawn_capture is on (see score_landing_at()), with a
 *   small positive term for forming a blockade (g->rules.blockade)
 *   instead when it's off.
 * - A pawn's destination is computed by calling game_logic.c's own
 *   ludo_resolve_move_destination(), not a naive p->steps + roll --
 *   under g->rules.overshoot_bounce the actual landing square can
 *   bounce backward off the end of the home column, and duplicating
 *   that math here would risk it drifting out of sync.
 * - Releasing a pawn from home is scored via score_release(), reached
 *   whenever score_move() is asked to score a pawn that's still
 *   in_play == 0 (only possible at all when
 *   g->rules.mandatory_six_release is off -- under the mandatory
 *   variant, ludo_roll() always releases automatically before the AI
 *   ever gets a choice; see compute_movable_pawns()'s own doc comment
 *   in game_logic.c).
 *
 * Backward movement (g->rules.backward_movement) and blockade-aware
 * strategy are NOT deeply scored -- real strategic sophistication for
 * those is an explicit stretch goal, not core scope. The only hard
 * requirement is that the AI never gets stuck or picks something
 * illegal when they're active. See
 * ludo_ai_choose_pawn_backward()/score_move_backward() below, a
 * deliberately much simpler fallback used only when no forward move is
 * available at all.
 */

#include "ai.h"

/* Scoring weights -- see the file header above and the per-component
 * comments below for what each represents. Kept as named constants
 * (not magic numbers) so a future difficulty level can plausibly reuse
 * this same scoring shape with different weights, rather than a wholly
 * separate function. */
#define WEIGHT_WIN                200000
#define WEIGHT_FINISH               6000
#define WEIGHT_CAPTURE              4000
#define WEIGHT_CAPTURE_NEAR_HOME    3000
#define WEIGHT_OWN_COLLISION_BASE   -500
#define WEIGHT_OWN_COLLISION_PER_STEP -50
#define WEIGHT_ENTRY_SQUARE_LAND   -2000
#define WEIGHT_ENTRY_SQUARE_LEAVE   1500
#define WEIGHT_DANGER_ESCAPE         400
#define WEIGHT_DANGER_STILL_IN      -300
#define WEIGHT_DANGER_APPROACH_OPPONENT 150
#define WEIGHT_PROGRESS_PER_STEP       5
/* A move that places the pawn in its home column -- whether it was
 * already there or crosses in from the ring this move -- is risk-free
 * (no capture/danger heuristic can ever apply there, see score_move())
 * and directly shortens the road to winning, so it deserves a real,
 * explicit incentive rather than competing on the same flat
 * WEIGHT_PROGRESS_PER_STEP as everything else. Sized to clearly beat
 * the ring-tactic bonuses above (WEIGHT_ENTRY_SQUARE_LEAVE,
 * WEIGHT_DANGER_ESCAPE/APPROACH_OPPONENT) but still lose to an actual
 * capture (WEIGHT_CAPTURE alone already exceeds it). */
#define WEIGHT_HOME_COLUMN_ADVANCE_BASE     2000
#define WEIGHT_HOME_COLUMN_ADVANCE_PER_STEP  100

/* Releasing a pawn is only ever a *scored* decision when
 * g->rules.mandatory_six_release is off (see score_release()) -- a
 * first-pass heuristic, not deeply tuned. Scaled down per pawn the
 * player already has racing, on the reasoning that a 4th pawn joining
 * the board matters less than a 2nd. */
#define WEIGHT_RELEASE_BASE               3000
#define WEIGHT_RELEASE_PER_PAWN_ALREADY_OUT 300

/* A small, deliberately modest bonus for landing on a square already
 * occupied by one of the player's own pawns when doing so forms or
 * reinforces a blockade (g->rules.blockade) instead of sending that
 * pawn home (which only happens when g->rules.own_pawn_capture is also
 * off -- the two can only coexist in the first place under that
 * combination). Kept intentionally small/neutral -- blockade strategy
 * is a stretch goal, not something to score aggressively. */
#define WEIGHT_BLOCKADE_FORM                800

/* A capture is worth extra when the captured pawn was this close (or
 * closer) to leaving the ring for its home column -- losing that much
 * progress hurts the opponent more. */
#define NEAR_HOME_RING_SQUARES_REMAINING 6

/* How many ring squares behind ("could reach this square with one
 * throw") counts as a threat -- a die rolls 1-6, so exactly 6. */
#define DANGER_RANGE 6

/*
 * Function: ring_square (internal)
 * Summary: Absolute ring square for a pawn `steps` into its journey,
 *          accounting for its player's entry offset -- the same formula
 *          as game_logic.c's private ring_square() and
 *          board_layout.c's board_ring_cell() dispatch, duplicated here
 *          since game_logic.c doesn't expose it publicly.
 * Syntax:  static int ring_square(int player, int steps);
 * Input:   player - the pawn's owner, 0..LUDO_PLAYERS-1.
 *          steps  - steps travelled since release; must be
 *                    < LUDO_RING_LENGTH.
 * Output:  absolute ring square, 0..LUDO_RING_LENGTH-1.
 */
static int ring_square(int player, int steps)
{
	return (player * (LUDO_RING_LENGTH / LUDO_PLAYERS) + steps) % LUDO_RING_LENGTH;
}

/*
 * Function: ring_distance_behind (internal)
 * Summary: How many forward steps a pawn at `from_square` would need to
 *          land exactly on `to_square`, going the way every pawn
 *          travels (increasing ring square, wrapping at
 *          LUDO_RING_LENGTH). Used to ask "could a pawn sitting at
 *          from_square reach to_square with one throw?".
 * Syntax:  static int ring_distance_behind(int from_square, int to_square);
 * Output:  1..LUDO_RING_LENGTH (never 0 -- same square isn't "behind").
 */
static int ring_distance_behind(int from_square, int to_square)
{
	int d = (to_square - from_square + LUDO_RING_LENGTH) % LUDO_RING_LENGTH;

	return d == 0 ? LUDO_RING_LENGTH : d;
}

/*
 * Function: is_entry_square (internal)
 * Summary: Whether an absolute ring square is one of the four players'
 *          release points -- exposed and contested, since any player
 *          releasing a new pawn there can capture whoever's sitting on
 *          it (see game_logic.c's ludo_roll(), the six-release path,
 *          and capture_at()).
 */
static int is_entry_square(int square)
{
	return square % (LUDO_RING_LENGTH / LUDO_PLAYERS) == 0;
}

/*
 * Function: score_landing_at (internal)
 * Summary: Score the collision/capture consequences of `player`'s pawn
 *          `pawn_index` landing on ring square `square` -- shared by
 *          score_move() (a normal move landing on the ring) and
 *          score_release() (a release landing on the player's own entry
 *          square), so the two can never disagree about how a capture
 *          (or the lack of one, under g->rules.own_pawn_capture) is
 *          scored.
 * Syntax:  static int score_landing_at(const ludo_game *g, int player,
 *                                      int pawn_index, int square);
 * Output:  the total capture/collision/blockade score contribution for
 *          landing there (may be 0, negative, or positive).
 */
static int score_landing_at(const ludo_game *g, int player, int pawn_index, int square)
{
	int other_player, other_pawn, score = 0;

	for (other_player = 0; other_player < LUDO_PLAYERS; other_player++) {
		for (other_pawn = 0; other_pawn < LUDO_PAWNS; other_pawn++) {
			const ludo_pawn *op = &g->players[other_player].pawns[other_pawn];
			int op_square;

			if (other_player == player && other_pawn == pawn_index)
				continue;
			if (!op->in_play || op->finished || op->steps >= LUDO_RING_LENGTH)
				continue;

			op_square = ring_square(other_player, op->steps);
			if (op_square != square)
				continue;

			if (other_player == player) {
				if (g->rules.own_pawn_capture) {
					/* Sends our own earlier pawn home -- a real
					 * setback, scaled by how far it had already come. */
					score += WEIGHT_OWN_COLLISION_BASE
					       + WEIGHT_OWN_COLLISION_PER_STEP * op->steps;
				} else if (g->rules.blockade) {
					/* No capture (the rule is off) -- joining that
					 * pawn here instead forms/reinforces a blockade. */
					score += WEIGHT_BLOCKADE_FORM;
				}
				/* own_pawn_capture off and blockade off: no effect
				 * either way -- the two pawns simply share the square. */
			} else {
				score += WEIGHT_CAPTURE;
				if (op->steps >= LUDO_RING_LENGTH - NEAR_HOME_RING_SQUARES_REMAINING)
					score += WEIGHT_CAPTURE_NEAR_HOME;
			}
		}
	}
	return score;
}

/*
 * Function: score_release (internal)
 * Summary: Score bringing home pawn `pawn_index` into play this turn --
 *          only reachable at all when g->rules.mandatory_six_release is
 *          off (see compute_movable_pawns()'s own doc comment in
 *          game_logic.c): under the mandatory default, ludo_roll()
 *          releases automatically before any choice is ever offered, so
 *          this scoring path was never needed until release became
 *          optional. The release lands exactly on the player's own
 *          entry square (steps == 0) -- NOT advanced further by the
 *          roll (see game_logic.c's ludo_move_pawn(), the "!p->in_play"
 *          branch: the roll places the pawn, it doesn't also move it).
 *
 *          First-pass heuristic (see WEIGHT_RELEASE_BASE's own comment),
 *          not deeply tuned -- still subject to the same capture-on-
 *          landing scoring (score_landing_at()) any other move to that
 *          square would get, since the entry square is exactly where
 *          captures on release actually happen (game_logic.c's
 *          ludo_roll()/ludo_move_pawn() both call capture_at() right
 *          after releasing).
 * Syntax:  static int score_release(const ludo_game *g, int player, int pawn_index);
 */
static int score_release(const ludo_game *g, int player, int pawn_index)
{
	int new_square = ring_square(player, 0);
	int already_racing = 0, i, score;

	for (i = 0; i < LUDO_PAWNS; i++) {
		if (i != pawn_index && g->players[player].pawns[i].in_play
		 && !g->players[player].pawns[i].finished)
			already_racing++;
	}

	score = WEIGHT_RELEASE_BASE - WEIGHT_RELEASE_PER_PAWN_ALREADY_OUT * already_racing;
	score += score_landing_at(g, player, pawn_index, new_square);
	/* new_square is always an entry square by construction (it's the
	 * player's own start square) -- this is exactly the scenario
	 * WEIGHT_ENTRY_SQUARE_LAND represents (exposed to any player
	 * releasing there next), so it applies here too. */
	score += WEIGHT_ENTRY_SQUARE_LAND;

	for (i = 0; i < LUDO_PLAYERS; i++) {
		int other_pawn;

		if (i == player)
			continue;
		for (other_pawn = 0; other_pawn < LUDO_PAWNS; other_pawn++) {
			const ludo_pawn *op = &g->players[i].pawns[other_pawn];
			int op_square;

			if (!op->in_play || op->finished || op->steps >= LUDO_RING_LENGTH)
				continue;

			op_square = ring_square(i, op->steps);
			if (ring_distance_behind(op_square, new_square) <= DANGER_RANGE)
				score += WEIGHT_DANGER_STILL_IN;
		}
	}

	return score;
}

/*
 * Function: score_move (internal)
 * Summary: Score how good it would be for `player` to move pawn
 *          `pawn_index` this turn, per the weights above. Higher is
 *          better; ludo_ai_choose_pawn() picks the movable pawn with the
 *          highest score.
 *
 *          When pawn_index refers to a pawn still waiting at home
 *          (in_play == 0), this is a release rather than an ordinary
 *          move -- delegated entirely to score_release(), since none of
 *          the ring-position-based scoring below applies to a pawn
 *          that isn't on the board yet.
 */
static int score_move(const ludo_game *g, int player, int pawn_index)
{
	const ludo_pawn *p = &g->players[player].pawns[pawn_index];
	int roll = g->last_roll;
	int new_steps;
	int score = 0;
	int old_square, new_square;
	int old_on_ring, new_on_ring;

	if (!p->in_play)
		return score_release(g, player, pawn_index);

	/* Uses game_logic.c's own authoritative destination calculation
	 * (rather than a naive p->steps + roll) so scoring can never
	 * disagree with where the move would actually land under
	 * g->rules.overshoot_bounce -- see ludo_resolve_move_destination()'s
	 * own doc comment. Only ever fails for an illegal destination, which
	 * can't happen here: ludo_ai_choose_pawn() only ever scores a pawn
	 * ludo_movable_pawns() already confirmed movable. */
	ludo_resolve_move_destination(g, pawn_index, roll, &new_steps);

	old_on_ring = p->steps < LUDO_RING_LENGTH;
	new_on_ring = new_steps < LUDO_RING_LENGTH;

	if (new_steps >= LUDO_TOTAL_STEPS) {
		/* This move finishes the pawn -- check whether it's also the
		 * player's last one, i.e. this move wins the game outright. */
		int i, others_finished = 1;

		for (i = 0; i < LUDO_PAWNS; i++) {
			if (i == pawn_index)
				continue;
			if (!g->players[player].pawns[i].finished)
				others_finished = 0;
		}
		return others_finished ? WEIGHT_WIN : WEIGHT_FINISH;
	}

	old_square = old_on_ring ? ring_square(player, p->steps) : -1;
	new_square = new_on_ring ? ring_square(player, new_steps) : -1;

	if (new_on_ring) {
		int other_player, other_pawn;

		score += score_landing_at(g, player, pawn_index, new_square);

		if (is_entry_square(new_square))
			score += WEIGHT_ENTRY_SQUARE_LAND;

		/* Danger: any opponent pawn that could reach new_square (or could
		 * already reach old_square, motivating escape) with a single
		 * throw next turn. */
		for (other_player = 0; other_player < LUDO_PLAYERS; other_player++) {
			if (other_player == player)
				continue;
			for (other_pawn = 0; other_pawn < LUDO_PAWNS; other_pawn++) {
				const ludo_pawn *op = &g->players[other_player].pawns[other_pawn];
				int op_square, dist_to_new;

				if (!op->in_play || op->finished || op->steps >= LUDO_RING_LENGTH)
					continue;

				op_square = ring_square(other_player, op->steps);
				dist_to_new = ring_distance_behind(op_square, new_square);

				if (dist_to_new <= DANGER_RANGE)
					score += WEIGHT_DANGER_STILL_IN;
				else if (old_on_ring
				      && ring_distance_behind(op_square, old_square) <= DANGER_RANGE)
					score += WEIGHT_DANGER_ESCAPE;

				/* Moving to just ahead of an opponent sets up a future
				 * capture of *them* next time it's our turn. */
				if (ring_distance_behind(new_square, op_square) <= DANGER_RANGE)
					score += WEIGHT_DANGER_APPROACH_OPPONENT;
			}
		}
	} else {
		/* Not on the ring after this move -- the early return above
		 * already handled the "this move finishes the pawn" case, so
		 * getting here means the pawn ends this move still inside its
		 * home column, one step closer to finishing (see the weight
		 * comment above for why this needs its own explicit bonus). */
		score += WEIGHT_HOME_COLUMN_ADVANCE_BASE
		       + WEIGHT_HOME_COLUMN_ADVANCE_PER_STEP * (new_steps - LUDO_RING_LENGTH);
	}

	if (old_on_ring && is_entry_square(old_square))
		score += WEIGHT_ENTRY_SQUARE_LEAVE;

	score += WEIGHT_PROGRESS_PER_STEP * new_steps;

	return score;
}

int ludo_ai_choose_pawn(const ludo_game *g, unsigned movable, ludo_ai_difficulty difficulty)
{
	int player = g->current_player;
	int best_pawn = -1, best_score = 0, pawn;

	/* Only one strategy implemented so far -- see this file's top-of-file
	 * comment for the intended EASY/HARD design. */
	(void) difficulty;

	for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
		int score;

		if (!(movable & (1u << pawn)))
			continue;

		score = score_move(g, player, pawn);
		if (best_pawn == -1 || score > best_score) {
			best_score = score;
			best_pawn = pawn;
		}
	}

	return best_pawn;
}

/*
 * Function: score_move_backward (internal)
 * Summary: Score moving pawn `pawn_index` BACKWARD by the current roll
 *          (g->rules.backward_movement) -- deliberately much simpler
 *          than score_move()'s forward scoring, since real backward-
 *          movement strategy is an explicit stretch goal, not a v1
 *          requirement (see this file's top-of-file comment). Reuses
 *          score_landing_at() for capture-on-landing (still a real
 *          tactical bonus, cheap to include), then just prefers
 *          retreating the *least* distance as a tie-breaker among
 *          several backward options -- not a claim that minimal retreat
 *          is always the strategically correct choice.
 * Syntax:  static int score_move_backward(const ludo_game *g, int player,
 *                                         int pawn_index);
 */
static int score_move_backward(const ludo_game *g, int player, int pawn_index)
{
	const ludo_pawn *p = &g->players[player].pawns[pawn_index];
	int roll = g->last_roll;
	/* ludo_movable_pawns_backward() already confirmed p->steps - roll
	 * stays >= 0 -- see game_logic.c's compute_movable_pawns_backward(). */
	int new_steps = p->steps - roll;
	int new_square = ring_square(player, new_steps);
	int score;

	score = score_landing_at(g, player, pawn_index, new_square);
	score += WEIGHT_PROGRESS_PER_STEP * new_steps;

	return score;
}

int ludo_ai_choose_pawn_backward(const ludo_game *g, unsigned movable_backward)
{
	int player = g->current_player;
	int best_pawn = -1, best_score = 0, pawn;

	for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
		int score;

		if (!(movable_backward & (1u << pawn)))
			continue;

		score = score_move_backward(g, player, pawn);
		if (best_pawn == -1 || score > best_score) {
			best_score = score;
			best_pawn = pawn;
		}
	}

	return best_pawn;
}
