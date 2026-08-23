/*
 * ArchiLudo game logic engine -- implementation.
 * See include/game_logic.h for the full rules writeup and API docs.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "game_logic.h"

/*
 * Function: all_pawns_finished (internal)
 * Summary: Whether every one of a player's four pawns has finished.
 * Syntax:  static int all_pawns_finished(const ludo_game *g, int player);
 * Input:   g      - the game in progress.
 *          player - index of the player to check.
 * Output:  1 if all four pawns are finished, 0 otherwise.
 */
static int all_pawns_finished(const ludo_game *g, int player)
{
	int i;

	for (i = 0; i < LUDO_PAWNS; i++) {
		if (!g->players[player].pawns[i].finished)
			return 0;
	}
	return 1;
}

/*
 * Function: find_home_pawn (internal)
 * Summary: Locate a pawn still waiting in a player's home base.
 * Syntax:  static int find_home_pawn(const ludo_game *g, int player);
 * Input:   g      - the game in progress.
 *          player - index of the player to check.
 * Output:  index of the first pawn with in_play == 0, or -1 if all four
 *          of the player's pawns have already been released.
 */
static int find_home_pawn(const ludo_game *g, int player)
{
	int i;

	for (i = 0; i < LUDO_PAWNS; i++) {
		if (!g->players[player].pawns[i].in_play && !g->players[player].pawns[i].finished)
			return i;
	}
	return -1;
}

/*
 * Function: ring_square (internal)
 * Summary: Convert a player's "steps travelled" into an absolute square
 *          on the shared 40-square ring, taking their start offset into
 *          account (player N starts N*10 squares around the ring).
 * Syntax:  static int ring_square(int player, int steps);
 * Input:   player - index of the pawn's owner.
 *          steps  - steps travelled since release; must be < LUDO_RING_LENGTH.
 * Output:  absolute ring square (0..LUDO_RING_LENGTH-1).
 */
static int ring_square(int player, int steps)
{
	return (player * (LUDO_RING_LENGTH / LUDO_PLAYERS) + steps) % LUDO_RING_LENGTH;
}

/*
 * Function: capture_at (internal)
 * Summary: Send home any *other* pawn occupying the ring square that
 *          "player" has just landed a pawn on -- opponents' pawns
 *          (a normal capture) and, per this project's house rule, the
 *          player's *own* other pawns too (if one of your own pawns is
 *          already sitting on the square another of your own pawns just
 *          landed on -- e.g. a forced pawn released by a six landing back
 *          on the square an earlier release is still parked on -- the
 *          earlier one gets sent home rather than the two just stacking
 *          on one square). `pawn_index` identifies the pawn that just
 *          moved so it never sends itself home (its own `steps` was
 *          already updated to match `square` by the caller before this
 *          runs). Pawns in a home column are never eligible, since each
 *          player's home column is private to them.
 * Syntax:  static int capture_at(ludo_game *g, int player, int pawn_index,
 *                                int steps);
 * Input:   g          - the game in progress.
 *          player     - the moving player (whose pawn just landed).
 *          pawn_index - index of the pawn that just landed (excluded from
 *                       the scan, so it can't send itself home).
 *          steps      - the landing pawn's steps travelled; capture only
 *                       applies if this is still on the shared ring.
 * Output:  1 if another pawn was sent home, 0 otherwise.
 */
static int capture_at(ludo_game *g, int player, int pawn_index, int steps)
{
	int square, p, i, captured = 0;

	if (steps >= LUDO_RING_LENGTH)
		return 0;

	square = ring_square(player, steps);

	for (p = 0; p < LUDO_PLAYERS; p++) {
		for (i = 0; i < LUDO_PAWNS; i++) {
			ludo_pawn *op = &g->players[p].pawns[i];

			if (p == player && i == pawn_index)
				continue; /* the pawn that just landed here, not a collision */

			if (op->in_play && !op->finished && op->steps < LUDO_RING_LENGTH
			 && ring_square(p, op->steps) == square) {
				op->in_play = 0;
				op->steps = 0;
				captured = 1;
			}
		}
	}
	return captured;
}

/*
 * Function: home_column_blocked (internal)
 * Summary: Whether moving a pawn from "from_steps" to "to_steps" (both at
 *          or past the start of the home column) would pass through or
 *          land on another of the same player's pawns already there. The
 *          home column is a single-file final stretch, so both passing
 *          over and landing on an own pawn are illegal.
 * Syntax:  static int home_column_blocked(const ludo_game *g, int player,
 *                                         int pawn_index, int from_steps,
 *                                         int to_steps);
 * Input:   g          - the game in progress.
 *          player     - the moving pawn's owner.
 *          pawn_index - index of the moving pawn (excluded from the check).
 *          from_steps - the moving pawn's steps before the move.
 *          to_steps   - the moving pawn's steps after the move.
 * Output:  1 if blocked by an own pawn, 0 if the path is clear.
 */
static int home_column_blocked(const ludo_game *g, int player, int pawn_index,
                                int from_steps, int to_steps)
{
	int j;

	for (j = 0; j < LUDO_PAWNS; j++) {
		const ludo_pawn *op;

		if (j == pawn_index)
			continue;
		op = &g->players[player].pawns[j];
		if (op->in_play && !op->finished && op->steps >= LUDO_RING_LENGTH
		 && op->steps > from_steps && op->steps <= to_steps)
			return 1;
	}
	return 0;
}

/*
 * Function: compute_movable_pawns (internal)
 * Summary: The actual rule evaluation behind ludo_movable_pawns() --
 *          shared by ludo_roll() (to decide whether this was a "wasted"
 *          attempt for the three-tries-for-a-six rule) and the public
 *          ludo_movable_pawns() accessor, so both always agree.
 * Syntax:  static unsigned compute_movable_pawns(const ludo_game *g);
 * Input:   g - the game in progress, after a roll has been recorded.
 * Output:  bitmask of movable pawns for the current player (see
 *          ludo_movable_pawns() in game_logic.h for the exact meaning).
 */
static unsigned compute_movable_pawns(const ludo_game *g)
{
	int player = g->current_player;
	unsigned mask = 0;
	int i;

	if (g->just_released)
		return 0; /* this roll's action was the mandatory placement itself */

	if (g->forced_pawn != -1)
		return (unsigned) (1u << g->forced_pawn);

	for (i = 0; i < LUDO_PAWNS; i++) {
		const ludo_pawn *p = &g->players[player].pawns[i];
		int new_steps;

		if (!p->in_play || p->finished)
			continue;

		new_steps = p->steps + g->last_roll;
		if (new_steps > LUDO_TOTAL_STEPS)
			continue; /* would overshoot past the end of the home column */

		if (new_steps >= LUDO_RING_LENGTH
		 && home_column_blocked(g, player, i, p->steps, new_steps))
			continue; /* blocked by our own pawn in the home column */

		mask |= (unsigned) (1u << i);
	}
	return mask;
}

void ludo_init(ludo_game *g)
{
	memset(g, 0, sizeof(*g));
	g->current_player = 0;
	g->last_roll = 0;
	g->tries_remaining = 3;
	g->forced_pawn = -1;
	g->pending_forced_pawn = -1;
	g->just_released = 0;
	g->winner = -1;
}

int ludo_roll(ludo_game *g, int forced_roll)
{
	int roll = (forced_roll >= 1 && forced_roll <= 6) ? forced_roll : (rand() % 6) + 1;

	g->last_roll = roll;
	g->just_released = 0;

	/* A forced-pawn obligation created by a six last turn only takes
	 * effect starting with this fresh roll (see house rule in the header). */
	g->forced_pawn = g->pending_forced_pawn;
	g->pending_forced_pawn = -1;

	if (roll == 6 && g->forced_pawn == -1) {
		int home_pawn = find_home_pawn(g, g->current_player);

		if (home_pawn != -1) {
			ludo_pawn *p = &g->players[g->current_player].pawns[home_pawn];

			p->in_play = 1;
			p->steps = 0;
			capture_at(g, g->current_player, home_pawn, 0);

			/* Placement itself is this roll's action; the obligation to
			 * move this same pawn applies to the *next* roll (the bonus
			 * roll granted for having thrown a six). */
			g->pending_forced_pawn = home_pawn;
			g->just_released = 1;
			return roll;
		}
	}

	if (compute_movable_pawns(g) == 0) {
		g->tries_remaining--;
		if (g->tries_remaining <= 0)
			ludo_end_turn(g);
	}
	return roll;
}

unsigned ludo_movable_pawns(const ludo_game *g)
{
	return compute_movable_pawns(g);
}

int ludo_no_move_possible(const ludo_game *g)
{
	return compute_movable_pawns(g) == 0;
}

int ludo_move_pawn(ludo_game *g, int pawn_index)
{
	int player = g->current_player;
	ludo_pawn *p = &g->players[player].pawns[pawn_index];
	int roll = g->last_roll;
	int captured = 0;

	p->steps += roll;
	if (p->steps >= LUDO_TOTAL_STEPS) {
		p->steps = LUDO_TOTAL_STEPS;
		p->finished = 1;
	} else if (p->steps < LUDO_RING_LENGTH) {
		captured = capture_at(g, player, pawn_index, p->steps);
	}

	g->forced_pawn = -1; /* this roll's obligation, if any, is now fulfilled */

	if (g->winner == -1 && all_pawns_finished(g, player))
		g->winner = player;

	if (roll == 6 && g->winner == -1) {
		/* Extra roll for the same player -- current_player is left
		 * unchanged; the caller simply calls ludo_roll() again. Reset
		 * last_roll (ludo_end_turn() does this for the turn-ending case
		 * below, but this branch skipped it entirely) -- otherwise
		 * ludo_movable_pawns()/compute_movable_pawns() would keep
		 * evaluating movability against this six for a second pawn before
		 * the player has actually thrown again, since last_roll==0 is
		 * what ludo_roll() and refresh_status() use to recognise "no
		 * fresh roll yet". */
		g->last_roll = 0;
	} else {
		ludo_end_turn(g);
	}

	return captured;
}

void ludo_end_turn(ludo_game *g)
{
	int next = g->current_player;
	int scanned;

	for (scanned = 0; scanned < LUDO_PLAYERS; scanned++) {
		next = (next + 1) % LUDO_PLAYERS;
		if (!all_pawns_finished(g, next))
			break;
	}

	g->current_player = next;
	g->last_roll = 0;
	g->tries_remaining = 3;
	g->forced_pawn = -1;
	g->pending_forced_pawn = -1;
	g->just_released = 0;
}
