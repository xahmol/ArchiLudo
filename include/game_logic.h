#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

/*
 * ArchiLudo game logic engine
 * ============================
 *
 * Platform-independent Ludo rules engine -- no OSLib/WIMP dependency, so it
 * compiles and runs unmodified both under the ArchieSDK ARM cross-compiler
 * (as part of the real game) and under the host's native compiler (for the
 * automated test suite in tests/test_game_logic.c).
 *
 * This is a from-scratch, cleanly-named reimplementation of the rules from
 * the original 1992 Commodore 128 BASIC program (see /home/xahmol/git/ludo),
 * not a line-for-line port of it. That original uses single/double-letter
 * variable names (ap, as, vr, vl, vn, nr, gv, ov, ro, np, dp, zv) with no
 * surviving documentation of what most of them mean, which makes faithfully
 * copying its exact internal encoding both unverifiable and a poor basis for
 * a maintainable, testable module. Instead, this engine is implemented
 * directly from the plain-English rules text the author has maintained
 * across all of the game's other ports (see the "Game rules" section of
 * /home/xahmol/git/ludo/README.md and .../GEOS/README.md), which is the
 * authoritative, unambiguous specification of the intended "Mens Erger Je
 * Niet" house-rules variant:
 *
 *   - Each of the 4 players starts with 4 pawns in their home base.
 *   - A pawn can only leave home on a roll of six, entering play at the
 *     player's start square on the shared ring.
 *   - This variant's house rule: rolling a six *mandatorily* places a new
 *     pawn from the home base if one is available (not an optional choice
 *     as in some Ludo variants), and the player is then obliged to move
 *     that specific pawn with their next roll -- unless the home base is
 *     now empty, in which case there is nothing to place and the extra
 *     roll is a free choice as normal.
 *   - Rolling a six always grants the player an extra roll after their
 *     move, chained for as many sixes as are rolled in a row.
 *   - If every one of a player's pawns is either still at home, or stuck
 *     in the home column with no legal move, the player gets up to three
 *     consecutive rolls looking for the six needed to free a pawn; if none
 *     of the three is a six, the turn passes with no move made.
 *   - Landing exactly on a square occupied by an opponent's pawn sends
 *     that pawn back to its owner's home base.
 *   - A player's own pawns cannot pass, or land on, another of their own
 *     pawns already in their home column (a single-file final stretch) --
 *     the blocking pawn must be moved out of the way first.
 *   - A pawn must reach the very end of its home column on an exact roll;
 *     a roll that would overshoot past the end is not a legal move for
 *     that pawn.
 *   - The first player to get all four pawns to the end wins; the
 *     remaining players may continue playing to decide the runner-up
 *     order (the engine itself simply skips already-finished players
 *     rather than ending the whole game at the first win).
 *
 * Suggested improvement over the original: the 1992 BASIC/GEOS versions
 * track each pawn's position as an ad-hoc pair of small integers whose
 * exact encoding is no longer documented. Here, a pawn's position is a
 * single "steps travelled since release from home" counter, which is
 * simpler to reason about, print for debugging, and unit test:
 *
 *   steps == 0 .. LUDO_RING_LENGTH-1   pawn is on the shared 40-square ring
 *   steps == LUDO_RING_LENGTH .. TOTAL-1   pawn is in its own home column
 *   steps == LUDO_TOTAL_STEPS          pawn has finished
 *
 * A player's start square on the ring is (player_index * 10), so the same
 * steps counter plus the player index is all that's needed to derive the
 * pawn's absolute board square: see ring_square() in game_logic.c.
 */

#define LUDO_PLAYERS            4
#define LUDO_PAWNS               4
#define LUDO_RING_LENGTH        40
#define LUDO_HOME_COLUMN_LENGTH  4
#define LUDO_TOTAL_STEPS        (LUDO_RING_LENGTH + LUDO_HOME_COLUMN_LENGTH)

/*
 * Type: ludo_pawn
 * Summary: State of a single pawn.
 *   in_play  - 0 while still waiting in the home base, 1 once released.
 *   finished - 1 once the pawn has completed its home column.
 *   steps    - squares travelled since release (0..LUDO_TOTAL_STEPS);
 *              meaningless while in_play is 0.
 */
typedef struct {
	int in_play;
	int finished;
	int steps;
} ludo_pawn;

/*
 * Type: ludo_player
 * Summary: The four pawns belonging to one player.
 */
typedef struct {
	ludo_pawn pawns[LUDO_PAWNS];
} ludo_player;

/*
 * Type: ludo_game
 * Summary: Complete state of one game in progress.
 *   current_player      - index (0..LUDO_PLAYERS-1) of the player to move.
 *   last_roll           - the most recent die value (1..6), set by ludo_roll().
 *   tries_remaining     - attempts left this turn to roll a six while every
 *                         pawn is stuck at home or blocked in the home column.
 *   forced_pawn         - pawn index the current roll must move (the
 *                         "move this pawn with the next throw" house rule),
 *                         or -1 if the player has a free choice.
 *   pending_forced_pawn - internal: set the roll after a six mandatorily
 *                         releases a home pawn, promoted to forced_pawn on
 *                         the *following* ludo_roll() call. Not for use
 *                         outside game_logic.c.
 *   just_released       - internal: true for the single roll in which a
 *                         six mandatorily released a home pawn, so that
 *                         roll correctly reports no movable pawns (the
 *                         placement *was* that roll's move). Not for use
 *                         outside game_logic.c.
 *   winner              - index of the first player to finish all four
 *                         pawns, or -1 if the game is still undecided.
 */
typedef struct {
	ludo_player players[LUDO_PLAYERS];
	int current_player;
	int last_roll;
	int tries_remaining;
	int forced_pawn;
	int pending_forced_pawn;
	int just_released;
	int winner;
} ludo_game;

/*
 * Function: ludo_init
 * Summary: Set up a brand new game: all pawns at home, player 0 to start.
 * Syntax:  void ludo_init(ludo_game *g);
 * Input:   g - pointer to an (uninitialised) game to fill in.
 * Output:  none. g is fully initialised on return.
 */
void ludo_init(ludo_game *g);

/*
 * Function: ludo_roll
 * Summary: Roll the die for the current player and apply any rule that
 *          triggers automatically on the roll itself (the mandatory
 *          release of a home pawn on a six). Does not move any pawn that
 *          was already on the board -- call ludo_movable_pawns() next to
 *          see what the player may do with this roll, then ludo_move_pawn().
 * Syntax:  int ludo_roll(ludo_game *g, int forced_roll);
 * Input:   g           - the game in progress.
 *          forced_roll - 1..6 to inject a specific value (used by the test
 *                        suite and by an AI player that pre-computes a
 *                        move), or 0 to roll randomly via rand().
 * Output:  the die value rolled (1..6). If this roll leaves the current
 *          player with no legal move at all (see ludo_no_move_possible()),
 *          one of the player's three attempts is consumed, and once all
 *          three are used without a six, the turn is passed automatically
 *          (see ludo_end_turn()) -- the caller does not need to detect
 *          this itself, only keep rolling while ludo_movable_pawns() is 0
 *          and the current_player has not changed.
 */
int ludo_roll(ludo_game *g, int forced_roll);

/*
 * Function: ludo_movable_pawns
 * Summary: Report which of the current player's pawns can legally be
 *          moved with the most recent roll (accounting for overshoot past
 *          the end of the home column, blocking by the player's own pawns
 *          already in their home column, and the "forced pawn" house rule).
 * Syntax:  unsigned ludo_movable_pawns(const ludo_game *g);
 * Input:   g - the game in progress, after a call to ludo_roll().
 * Output:  bitmask where bit N is set if pawn N may legally move; 0 if no
 *          pawn can move with this roll.
 */
unsigned ludo_movable_pawns(const ludo_game *g);

/*
 * Function: ludo_no_move_possible
 * Summary: Convenience test equivalent to (ludo_movable_pawns(g) == 0).
 * Syntax:  int ludo_no_move_possible(const ludo_game *g);
 * Input:   g - the game in progress, after a call to ludo_roll().
 * Output:  1 if the current player cannot move any pawn with this roll,
 *          0 otherwise.
 */
int ludo_no_move_possible(const ludo_game *g);

/*
 * Function: ludo_move_pawn
 * Summary: Move one of the current player's pawns by the most recent
 *          roll. Applies capture-on-landing, win detection, and either
 *          grants the player another roll (six was rolled) or ends their
 *          turn (any other value) -- the caller does not need to call
 *          ludo_end_turn() itself after a move.
 * Syntax:  int ludo_move_pawn(ludo_game *g, int pawn_index);
 * Input:   g          - the game in progress.
 *          pawn_index - index (0..LUDO_PAWNS-1) of one of the pawns
 *                       reported movable by ludo_movable_pawns(). Passing
 *                       a pawn that isn't currently movable is a caller
 *                       error and produces undefined board state.
 * Output:  1 if this move captured an opponent's pawn, 0 otherwise.
 */
int ludo_move_pawn(ludo_game *g, int pawn_index);

/*
 * Function: ludo_end_turn
 * Summary: End the current player's turn and advance to the next player
 *          who has not already finished all four pawns. Called
 *          internally by ludo_move_pawn() and ludo_roll() where the
 *          rules require it; exposed so a caller can also end a turn
 *          explicitly after observing ludo_no_move_possible() with no
 *          tries remaining, if it prefers not to rely on the automatic
 *          handling inside ludo_roll().
 * Syntax:  void ludo_end_turn(ludo_game *g);
 * Input:   g - the game in progress.
 * Output:  none. current_player, last_roll, tries_remaining and
 *          forced_pawn are all reset ready for the next player's turn.
 */
void ludo_end_turn(ludo_game *g);

#endif
