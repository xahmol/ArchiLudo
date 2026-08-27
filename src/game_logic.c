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
 *          "player" has just landed a pawn on -- always for opponents'
 *          pawns (a normal capture); for the player's *own* other pawns,
 *          only when g->rules.own_pawn_capture is on (this project's
 *          original, default behaviour -- e.g. a forced pawn released by
 *          a six landing back on the square an earlier release is still
 *          parked on sends that earlier one home rather than the two
 *          just stacking on one square; with own_pawn_capture off, the
 *          two simply share the square instead, the starting point for
 *          the separate "blockade" house rule). `pawn_index` identifies
 *          the pawn that just moved so it never sends itself home (its
 *          own `steps` was already updated to match `square` by the
 *          caller before this runs). Pawns in a home column are never
 *          eligible, since each player's home column is private to them.
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

			if (p == player && !g->rules.own_pawn_capture)
				continue; /* house rule off -- own pawns just share the square */

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
 *
 *          Round 7.20 correction: a *finished* pawn used to be excluded
 *          from this check (the assumption being that a finished pawn is
 *          "off the board" and can't block anything) -- this was wrong,
 *          confirmed by reading the actual GEOS source
 *          (`/home/xahmol/git/ludo/GEOS/src/gamelogic.c`'s
 *          `turngeneric()`): its own blocking loop tests
 *          `playerpos[player][y][1]<=vn && playerpos[player][y][1]>3`
 *          against *every* other pawn's raw position with no exemption
 *          for one that has already reached the end -- a finished pawn
 *          is simply a pawn parked at a fixed home-column square,
 *          exactly like any other occupant, still fully able to block a
 *          later pawn from landing on or passing its square. This is
 *          also what makes finished pawns naturally queue into the home
 *          column's 4 distinct squares one at a time (see
 *          finish_threshold_for()) instead of stacking on the single
 *          last square -- per explicit user report ("the logic stacks
 *          the pawns at end field, not intended").
 *
 *          Direction-aware since the multi-rule-set work: to_steps may
 *          be *less* than from_steps when rules.overshoot_bounce sends a
 *          pawn backward off the end of the home column (see
 *          resolve_move_destination()) -- the excluded square is always
 *          the pawn's own starting square (from_steps), with every other
 *          square actually passed through or landed on checked in
 *          whichever direction the move actually travels.
 * Syntax:  static int home_column_blocked(const ludo_game *g, int player,
 *                                         int pawn_index, int from_steps,
 *                                         int to_steps);
 * Input:   g          - the game in progress.
 *          player     - the moving pawn's owner.
 *          pawn_index - index of the moving pawn (excluded from the check).
 *          from_steps - the moving pawn's steps before the move.
 *          to_steps   - the moving pawn's steps after the move -- may be
 *                       less than from_steps for a bounced (backward) move.
 * Output:  1 if blocked by an own pawn (finished or not), 0 if the path
 *          is clear.
 */
static int home_column_blocked(const ludo_game *g, int player, int pawn_index,
                                int from_steps, int to_steps)
{
	int j;

	for (j = 0; j < LUDO_PAWNS; j++) {
		const ludo_pawn *op;
		int on_path;

		if (j == pawn_index)
			continue;
		op = &g->players[player].pawns[j];
		if (!op->in_play || op->steps < LUDO_RING_LENGTH)
			continue;

		if (to_steps >= from_steps)
			on_path = (op->steps > from_steps && op->steps <= to_steps);
		else
			on_path = (op->steps < from_steps && op->steps >= to_steps);

		if (on_path)
			return 1;
	}
	return 0;
}

/*
 * Function: resolve_move_destination (internal)
 * Summary: Compute where a pawn would actually end up if moved by `roll`
 *          steps from its current position, accounting for the
 *          overshoot-bounce house rule (g->rules.overshoot_bounce) --
 *          shared by compute_movable_pawns() (to decide legality) and
 *          ludo_move_pawn() (to apply the move), so the two can never
 *          disagree about where a bounced move actually lands.
 * Syntax:  static int resolve_move_destination(const ludo_game *g, int player,
 *                                               int pawn_index, int roll,
 *                                               int *out_steps);
 * Input:   g          - the game in progress.
 *          player     - the pawn's owner.
 *          pawn_index - the pawn being considered.
 *          roll       - the die value being applied.
 * Output:  1 and *out_steps set to the resulting steps value if this is a
 *          legal destination (home-column blocking is checked separately
 *          by the caller); 0 if the move is illegal outright (overshoot
 *          past the end of the home column with bounce disabled).
 */
static int resolve_move_destination(const ludo_game *g, int player, int pawn_index,
                                     int roll, int *out_steps)
{
	int from_steps = g->players[player].pawns[pawn_index].steps;
	int new_steps = from_steps + roll;

	/* Deliberately checked against the fixed LUDO_TOTAL_STEPS here, NOT
	 * this pawn's own (possibly lower) finish_threshold_for() -- matches
	 * GEOS's own `if(vn>7) gv=1`, always against the absolute maximum. A
	 * destination above this pawn's actual dynamic threshold but still
	 * <= LUDO_TOTAL_STEPS is rejected separately by home_column_blocked()
	 * (every square above the threshold is already occupied by an
	 * earlier-finished pawn), not here. */
	if (new_steps > LUDO_TOTAL_STEPS) {
		if (!g->rules.overshoot_bounce)
			return 0; /* would overshoot past the end of the home column */

		/* Bounce off the end: the excess pips are walked back the other
		 * way from the very last square. */
		new_steps = LUDO_TOTAL_STEPS - (new_steps - LUDO_TOTAL_STEPS);

		/* A bounce can, in principle, reach back past the home column's
		 * own entrance -- LUDO_HOME_COLUMN_LENGTH (4 squares) is shorter
		 * than the largest possible overshoot (5, from a roll of 6 with
		 * only 1 step left to go), unlike classic Ludo's 6-square home
		 * stretch which exactly matches the die and never hits this
		 * case. Clamped at the entrance rather than letting the pawn
		 * bounce back onto the shared ring, which no bounce-back rule
		 * variant this project's research found actually intends. */
		if (new_steps < LUDO_RING_LENGTH)
			new_steps = LUDO_RING_LENGTH;
	}

	*out_steps = new_steps;
	return 1;
}

/*
 * Function: finish_threshold_for (internal)
 * Summary: The "steps" value a specific pawn must reach right now to
 *          become finished -- LUDO_TOTAL_STEPS minus however many of
 *          this player's *other* pawns have already finished. Ground-
 *          truthed against GEOS's `gamelogic.c` (`pawnselect()`):
 *          `playerdata[player][1]` is a shrinking "pawns still needed
 *          home" counter, and a landing only counts as reaching home
 *          when it exactly matches `playerdata[player][1]+3` -- a
 *          target that itself decrements by one every time a pawn
 *          reaches it. Combined with home_column_blocked() no longer
 *          exempting finished pawns, this makes each successive pawn's
 *          own furthest legal square exactly one less than the previous
 *          pawn's, so they queue into the home column's 4 squares from
 *          the far end inward -- never stacking, never leaving a gap
 *          (a pawn can only ever legally reach up to this exact value:
 *          every square above it is already occupied by an earlier-
 *          finished pawn and therefore blocked, and LUDO_TOTAL_STEPS
 *          itself is the hard ceiling no roll can exceed).
 * Syntax:  static int finish_threshold_for(const ludo_game *g, int player,
 *                                          int pawn_index);
 * Input:   g          - the game in progress.
 *          player     - the pawn's owner.
 *          pawn_index - the pawn in question (excluded from its own count).
 * Output:  the steps value at or above which this pawn is finished.
 */
static int finish_threshold_for(const ludo_game *g, int player, int pawn_index)
{
	int i, finished_ahead = 0;

	for (i = 0; i < LUDO_PAWNS; i++) {
		if (i != pawn_index && g->players[player].pawns[i].finished)
			finished_ahead++;
	}
	return LUDO_TOTAL_STEPS - finished_ahead;
}

/*
 * Function: compute_movable_pawns (internal)
 * Summary: The actual rule evaluation behind ludo_movable_pawns() --
 *          shared by ludo_roll() (to decide whether this was a "wasted"
 *          attempt for the three-tries-for-a-six rule) and the public
 *          ludo_movable_pawns() accessor, so both always agree.
 *
 *          When rules.mandatory_six_release is off, this also reports a
 *          home pawn as "movable" (release it into play) whenever the
 *          roll would otherwise qualify for release -- a six always
 *          qualifies, and rules.no_six_needed_last_pawn additionally
 *          qualifies any roll when this is the player's own last pawn
 *          still at home. ludo_roll()'s own automatic-release block only
 *          runs at all when rules.mandatory_six_release is on, so the
 *          two never both offer a release for the same roll.
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

	if (!g->rules.mandatory_six_release) {
		int is_last_home_pawn = 0;
		int home_pawn = find_home_pawn(g, player);

		if (home_pawn != -1 && g->rules.no_six_needed_last_pawn) {
			int others_home = 0;

			for (i = 0; i < LUDO_PAWNS; i++) {
				if (i != home_pawn && !g->players[player].pawns[i].in_play
				 && !g->players[player].pawns[i].finished)
					others_home++;
			}
			is_last_home_pawn = (others_home == 0);
		}

		if (home_pawn != -1 && (g->last_roll == 6 || is_last_home_pawn))
			mask |= (unsigned) (1u << home_pawn);
	}

	for (i = 0; i < LUDO_PAWNS; i++) {
		const ludo_pawn *p = &g->players[player].pawns[i];
		int new_steps;

		if (!p->in_play || p->finished)
			continue;

		if (!resolve_move_destination(g, player, i, g->last_roll, &new_steps))
			continue; /* would overshoot past the end of the home column (bounce disabled) */

		if (new_steps >= LUDO_RING_LENGTH
		 && home_column_blocked(g, player, i, p->steps, new_steps))
			continue; /* blocked by our own pawn in the home column (finished or not) */

		mask |= (unsigned) (1u << i);
	}
	return mask;
}

/*
 * Function: ludo_default_rules
 * Summary: See include/game_logic.h's own doc comment for the public
 *          contract. The per-variant matrix implemented here (see
 *          docs/GAME_LOGIC.md for the same table in prose):
 *
 *            toggle                   | MEJN | Ludo | Pachisi-style
 *            -------------------------+------+------+---------------
 *            mandatory_six_release    |  1   |  0   |  0
 *            own_pawn_capture         |  1   |  0   |  0
 *            overshoot_bounce         |  0   |  0   |  1
 *            blockade                 |  0   |  0   |  1
 *            backward_movement        |  0   |  0   |  1
 *            free_home_column         |  0   |  0   |  1
 *            no_six_needed_last_pawn  |  0   |  0   |  0
 *
 *          "Pachisi-style" is a curated preset built from the toggles
 *          this engine actually has (evoking blockading, bounce-back,
 *          backward movement, and free home-column play), not a
 *          faithful reimplementation of traditional Pachisi's own board
 *          shape or cowrie-shell dice mechanic -- see the multi-rule-set
 *          planning notes and docs/GAME_LOGIC.md for the full caveat.
 */
ludo_rules ludo_default_rules(ludo_variant variant)
{
	ludo_rules r;

	memset(&r, 0, sizeof(r));
	r.variant = variant;

	switch (variant) {
	case LUDO_VARIANT_LUDO:
		r.mandatory_six_release = 0;
		r.own_pawn_capture = 0;
		r.overshoot_bounce = 0;
		r.blockade = 0;
		r.backward_movement = 0;
		r.free_home_column = 0;
		r.no_six_needed_last_pawn = 0;
		break;

	case LUDO_VARIANT_PACHISI:
		r.mandatory_six_release = 0;
		r.own_pawn_capture = 0;
		r.overshoot_bounce = 1;
		r.blockade = 1;
		r.backward_movement = 1;
		r.free_home_column = 1;
		r.no_six_needed_last_pawn = 0;
		break;

	case LUDO_VARIANT_MEJN:
	default:
		r.mandatory_six_release = 1;
		r.own_pawn_capture = 1;
		r.overshoot_bounce = 0;
		r.blockade = 0;
		r.backward_movement = 0;
		r.free_home_column = 0;
		r.no_six_needed_last_pawn = 0;
		break;
	}
	return r;
}

void ludo_set_rules(ludo_game *g, const ludo_rules *rules)
{
	g->rules = *rules;
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
	g->rules = ludo_default_rules(LUDO_VARIANT_MEJN);
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

	/* This whole automatic-release block only applies under
	 * rules.mandatory_six_release -- when it's off, releasing a home
	 * pawn is instead offered as one of the player's ordinary movable
	 * choices by compute_movable_pawns() (see its own doc comment), and
	 * ludo_move_pawn() performs the actual release when that choice is
	 * picked. The two paths never overlap for the same roll. */
	if (roll == 6 && g->forced_pawn == -1 && g->rules.mandatory_six_release) {
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
	int finish_at;
	int new_steps;

	if (!p->in_play) {
		/* Optional six-release (rules.mandatory_six_release off): the
		 * player chose to bring this home pawn into play instead of
		 * moving one already on the board -- compute_movable_pawns()
		 * only ever offers this bit when the roll qualifies (see its
		 * own doc comment), so no roll-value check is needed here.
		 * Deliberately does NOT set pending_forced_pawn/just_released
		 * the way ludo_roll()'s automatic mandatory-release path does --
		 * that "must move this same pawn next" obligation exists
		 * specifically to compensate for the release being involuntary;
		 * it doesn't apply when the player chose to release on purpose. */
		p->in_play = 1;
		p->steps = 0;
		captured = capture_at(g, player, pawn_index, 0);
		g->forced_pawn = -1;

		/* A release can only ever be offered on a qualifying roll (a six,
		 * or -- under no_six_needed_last_pawn -- any roll for a player's
		 * last home pawn); a genuine six still grants its usual bonus
		 * roll, but a non-six release (last-home-pawn exception) simply
		 * ends the turn like any other ordinary move. A release can
		 * never itself finish a game (steps == 0), so no winner check is
		 * needed here unlike the normal move path below. */
		if (roll == 6)
			g->last_roll = 0;
		else
			ludo_end_turn(g);

		return captured;
	}

	if (!resolve_move_destination(g, player, pawn_index, roll, &new_steps)) {
		/* Should be unreachable -- ludo_movable_pawns() already filtered
		 * this pawn out via the same resolve_move_destination() call if
		 * this were the case. Defensive no-op rather than undefined
		 * behaviour if a caller ever passes an unfiltered pawn_index. */
		return 0;
	}
	p->steps = new_steps;
	/* Round 7.20: each pawn's own finish line, not a single shared
	 * LUDO_TOTAL_STEPS for all four -- see finish_threshold_for(). A
	 * legal move can never actually exceed this (every square above it
	 * is occupied by an earlier-finished pawn and therefore blocked by
	 * home_column_blocked(), and LUDO_TOTAL_STEPS is the hard ceiling
	 * no roll can exceed regardless), so the clamp below is defensive,
	 * matching this function's previous style. */
	finish_at = finish_threshold_for(g, player, pawn_index);
	if (p->steps >= finish_at) {
		p->steps = finish_at;
		p->finished = 1;
	} else if (p->steps < LUDO_RING_LENGTH) {
		captured = capture_at(g, player, pawn_index, p->steps);
	}

	g->forced_pawn = -1; /* this roll's obligation, if any, is now fulfilled */

	if (g->winner == -1 && all_pawns_finished(g, player))
		g->winner = player;

	/* Round 7.35: checks THIS player's own all_pawns_finished(), not the
	 * global g->winner == -1 -- per the new "continue playing after the
	 * first winner" mode (see docs/ARCHITECTURE.md's Round 7.35). The
	 * old g->winner == -1 check meant that once ANY player won, EVERY
	 * remaining player permanently lost their own six-goes-again bonus
	 * for the rest of the game, which was never exercised/noticed while
	 * the game simply ended at the first winner. A player who just
	 * finished obviously has nothing left to roll for, but a player who
	 * hasn't finished should keep the normal bonus regardless of
	 * whether someone else already has. */
	if (roll == 6 && !all_pawns_finished(g, player)) {
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
