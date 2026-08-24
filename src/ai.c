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
 *   ArchiLudo's rule is different (see game_logic.c's capture_at(),
 *   "Round 6.5"): landing on your own pawn is legal and sends the
 *   earlier one home. Scored here as a real but *scaled* penalty instead
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
/* Round 7.14: a move that places the pawn in its home column -- whether
 * it was already there or crosses in from the ring this move -- is
 * risk-free (no capture/danger heuristic can ever apply there, see
 * score_move()) and directly shortens the road to winning, so it
 * deserves a real, explicit incentive rather than competing on the same
 * flat WEIGHT_PROGRESS_PER_STEP as everything else. Sized to clearly
 * beat the ring-tactic bonuses above (WEIGHT_ENTRY_SQUARE_LEAVE,
 * WEIGHT_DANGER_ESCAPE/APPROACH_OPPONENT) but still lose to an actual
 * capture (WEIGHT_CAPTURE alone already exceeds it) -- per explicit
 * user report that the AI wasn't visibly prioritising advancing a pawn
 * already in its home stretch. */
#define WEIGHT_HOME_COLUMN_ADVANCE_BASE     2000
#define WEIGHT_HOME_COLUMN_ADVANCE_PER_STEP  100

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
 * Function: score_move (internal)
 * Summary: Score how good it would be for `player` to move pawn
 *          `pawn_index` this turn, per the weights above. Higher is
 *          better; ludo_ai_choose_pawn() picks the movable pawn with the
 *          highest score.
 */
static int score_move(const ludo_game *g, int player, int pawn_index)
{
	const ludo_pawn *p = &g->players[player].pawns[pawn_index];
	int roll = g->last_roll;
	int new_steps = p->steps + roll;
	int score = 0;
	int old_square, new_square;
	int old_on_ring = p->in_play && p->steps < LUDO_RING_LENGTH;
	int new_on_ring = new_steps < LUDO_RING_LENGTH;

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

		for (other_player = 0; other_player < LUDO_PLAYERS; other_player++) {
			for (other_pawn = 0; other_pawn < LUDO_PAWNS; other_pawn++) {
				const ludo_pawn *op = &g->players[other_player].pawns[other_pawn];
				int op_square;

				if (other_player == player && other_pawn == pawn_index)
					continue;
				if (!op->in_play || op->finished || op->steps >= LUDO_RING_LENGTH)
					continue;

				op_square = ring_square(other_player, op->steps);
				if (op_square != new_square)
					continue;

				if (other_player == player) {
					/* Sends our own earlier pawn home -- a real setback,
					 * scaled by how far it had already come. */
					score += WEIGHT_OWN_COLLISION_BASE
					       + WEIGHT_OWN_COLLISION_PER_STEP * op->steps;
				} else {
					score += WEIGHT_CAPTURE;
					if (op->steps >= LUDO_RING_LENGTH - NEAR_HOME_RING_SQUARES_REMAINING)
						score += WEIGHT_CAPTURE_NEAR_HOME;
				}
			}
		}

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
