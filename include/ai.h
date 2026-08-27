#ifndef AI_H
#define AI_H

#include "game_logic.h"

/*
 * ArchiLudo AI
 * ============
 *
 * Chooses which pawn an AI-controlled player should move, given a set of
 * currently-legal choices (from ludo_movable_pawns()). Deliberately kept
 * separate from game_logic.c: that module enforces the rules (what moves
 * are *legal*), this module only ever picks among moves game_logic.c has
 * already approved -- it never bypasses or duplicates rule checks. Pure
 * C, no OSLib/WIMP dependency, host-testable exactly like game_logic.c
 * and board_layout.c (see tests/test_ai.c).
 *
 * Adapted from this game's own prior GEOS port's AI
 * (`/home/xahmol/git/ludo/GEOS/src/main.c`'s `computerchoosepawn()`) --
 * assessed and reused as the basis (same overall shape: score every
 * movable pawn, pick the highest), not a literal port. See ai.c's
 * top-of-file comment for what carried over, what changed, and why (the
 * biggest divergence: ArchiLudo's own-pawn-collision rule differs from
 * GEOS's, so the scoring for that case had to change to match).
 */

/*
 * Type: ludo_ai_difficulty
 * Summary: Which scoring strategy ludo_ai_choose_pawn() uses. Only
 *          LUDO_AI_NORMAL has real strategy behind it so far -- the
 *          others are placeholders so the WIMP shell's difficulty
 *          selection (once built) has somewhere to point, without
 *          needing another API change later. See ai.c for the roadmap
 *          note on what EASY/HARD are expected to become.
 */
typedef enum {
	LUDO_AI_EASY,
	LUDO_AI_NORMAL,
	LUDO_AI_HARD
} ludo_ai_difficulty;

/*
 * Function: ludo_ai_choose_pawn
 * Summary: Choose which of the current player's currently-movable pawns
 *          an AI-controlled player should move this turn.
 * Syntax:  int ludo_ai_choose_pawn(const ludo_game *g, unsigned movable,
 *                                  ludo_ai_difficulty difficulty);
 * Input:   g          - the game in progress; g->current_player and
 *                        g->last_roll must already reflect this turn's
 *                        roll (i.e. call after ludo_roll(), same as the
 *                        WIMP shell does for a human player).
 *          movable    - the bitmask from ludo_movable_pawns(g). Must be
 *                        non-zero -- this function doesn't handle "no
 *                        legal move" (the caller already knows that from
 *                        the same bitmask being zero, same as the human
 *                        path).
 *          difficulty - which scoring strategy to use.
 * Output:  index (0..LUDO_PAWNS-1) of the pawn to move -- always one of
 *          the bits set in `movable`.
 */
int ludo_ai_choose_pawn(const ludo_game *g, unsigned movable, ludo_ai_difficulty difficulty);

/*
 * Function: ludo_ai_choose_pawn_backward
 * Summary: Choose which of the current player's currently-backward-
 *          movable pawns an AI-controlled player should move backward
 *          this turn (see game_logic.h's ludo_movable_pawns_backward(),
 *          only ever non-empty when g->rules.backward_movement is on).
 *          A caller should only ever need this as a fallback for a roll
 *          where ludo_ai_choose_pawn() had nothing to offer at all (its
 *          own `movable` bitmask was 0) but a backward move exists --
 *          otherwise a player who can only move backward would
 *          incorrectly appear stuck. Deliberately much simpler scoring
 *          than the forward path: real backward-movement strategy is an
 *          explicit stretch goal, not a v1 requirement (see ai.c's
 *          top-of-file comment) -- this just avoids getting stuck or
 *          picking illegally, it doesn't play backward movement well.
 * Syntax:  int ludo_ai_choose_pawn_backward(const ludo_game *g,
 *                                           unsigned movable_backward);
 * Input:   g                - the game in progress, after ludo_roll().
 *          movable_backward - the bitmask from
 *                             ludo_movable_pawns_backward(g). Must be
 *                             non-zero.
 * Output:  index (0..LUDO_PAWNS-1) of the pawn to move backward -- always
 *          one of the bits set in `movable_backward`.
 */
int ludo_ai_choose_pawn_backward(const ludo_game *g, unsigned movable_backward);

#endif
