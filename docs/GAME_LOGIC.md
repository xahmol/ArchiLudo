# game_logic manual

`include/game_logic.h` / `src/game_logic.c` -- the platform-independent
Ludo rules engine. See [ARCHITECTURE.md](ARCHITECTURE.md) for why this is
a clean reimplementation of the original BASIC/GEOS logic rather than a
literal port, and `tests/test_game_logic.c` for the automated test suite
that exercises every rule described here.

## Rules implemented (the "Mens Erger Je Niet" house-rules variant)

1. Each of the 4 players has 4 pawns, starting in their home base.
2. A pawn can only leave home on a roll of six, entering play at the
   player's start square on the shared 40-square ring.
3. **House rule**: rolling a six *mandatorily* releases a pawn from the
   home base if one is available (not an optional choice as in some Ludo
   variants). The player is then obliged to move that specific pawn with
   their *next* roll -- unless the home base is now empty, in which case
   there's nothing to place and the extra roll is a free choice.
4. Rolling a six always grants an extra roll after the move, chained for
   as many sixes in a row as are rolled.
5. If every one of a player's pawns is either still at home or stuck in
   the home column with no legal move, the player gets up to three
   consecutive rolls looking for the six needed to free a pawn. If none
   of the three is a six, the turn passes with no move made.
6. Landing exactly on a square occupied by another pawn sends that pawn
   back to its owner's home base -- an opponent's pawn (a normal
   capture) or, per this project's house rule, one of the *same*
   player's own other pawns too (e.g. a forced pawn released by a six
   landing back on the square an earlier release is still parked on: the
   earlier one goes home rather than the two just stacking on one
   square). Only applies on the shared ring -- see rule 7 for the
   separate (stricter) home column rule.
7. A player's own pawns cannot pass, or land on, another of their own
   pawns already in their home column (a single-file final stretch) --
   the blocking pawn must be moved out of the way first. This applies
   just as much to a pawn that has already *finished*: it stays parked
   at the exact square it finished on for the rest of the game, so it
   blocks a later pawn from passing or landing there exactly like any
   other occupant (see rule 8 and "Position model" below for what this
   means for where a pawn actually finishes).
8. A pawn must reach the very end of its home column on an exact roll; a
   roll that would overshoot is not a legal move for that pawn. If that
   pawn's own true end square is already occupied by one of the
   player's own already-finished pawns (rule 7), its effective "end" is
   whichever square is actually free -- each pawn that finishes shrinks
   the next one's target by exactly one square, so a player's finished
   pawns queue into the home column's 4 squares one at a time instead of
   stacking (see "Round 7.20" below).
9. The first player to get all four pawns to the end wins; remaining
   players may continue to decide runner-up order (the engine simply
   skips already-finished players in `ludo_end_turn()` rather than ending
   the game).

## Position model

A pawn's position is a single integer, `steps`, counting squares
travelled since release from home:

```
steps == 0 .. LUDO_RING_LENGTH-1        on the shared 40-square ring
steps == LUDO_RING_LENGTH .. TOTAL-1    in the player's own home column
steps == TOTAL - N                      finished, where N = however many
                                         of this player's OTHER pawns had
                                         already finished when this one did
```

A player's start square on the ring is `player_index * 10` (`LUDO_RING_LENGTH
/ LUDO_PLAYERS`), so a pawn's absolute ring square is derived with
`ring_square()` in `game_logic.c`: `(player * 10 + steps) % 40`.

This is simpler than the original's ad-hoc coordinate pairs: one number
per pawn, trivial to print for debugging, and trivial to reason about for
overshoot/capture/blocking checks.

**Round 7.20**: a finished pawn's `steps` is NOT always pinned at
`LUDO_TOTAL_STEPS` -- reported live as pawns visibly stacking on the same
final square once more than one of a player's pawns had finished.
Ground-truthed against the actual GEOS source
(`/home/xahmol/git/ludo/GEOS/src/gamelogic.c`), not a docs summary:
`pawnselect()`'s `playerdata[player][1]` is a *shrinking* "pawns still
needed home" counter, and a pawn only counts as reaching home when its
landing position exactly matches `playerdata[player][1]+3` -- a target
that decrements by one every time a pawn reaches it. GEOS's own blocking
check (`turngeneric()`) never exempts an already-finished pawn from
blocking a later one either. Put together: a finished pawn permanently
occupies its own square and still blocks like any other occupant
(`home_column_blocked()` in `game_logic.c`), and each subsequent pawn's
own reachable maximum (`finish_threshold_for()`) is mechanically capped
one square lower for every pawn already parked ahead of it -- so
finished pawns queue into the home column's 4 distinct squares one at a
time, from the far end inward, never stacking, never leaving a gap. The
overshoot check (rule 8's "not a legal move") stays against the fixed
`LUDO_TOTAL_STEPS` regardless -- matching GEOS's own always-absolute
`if(vn>7) gv=1` -- it's blocking, not the overshoot check, that enforces
each pawn's own lower effective ceiling once earlier pawns occupy the
squares above it.

**Round 7.35**: rule 9's "remaining players may continue" was always the
documented intent, but a real bug in `ludo_move_pawn()` meant it was never
actually fair once exercised for real: the six-goes-again check (rule 4)
tested the global `g->winner == -1` instead of the current player's own
`all_pawns_finished()`, so the moment ANY player won, EVERY remaining
player permanently lost their own bonus-roll-on-six for the rest of the
game -- never noticed while the WIMP UI simply ended the game at the
first winner (see `src/win_view.c`'s new "continue with remaining
players" dialogue, `docs/ARCHITECTURE.md`'s Round 7.35, for what finally
exercised this path for the first time). Fixed to check
`!all_pawns_finished(g, player)` -- a player who just finished obviously
has nothing left to roll for, but everyone else keeps the normal rule
regardless of who else has already won. Regression test:
`tests/test_game_logic.c`'s `test_six_bonus_survives_another_players_win()`.

## API

See the docstring comment above each declaration in `game_logic.h` for the
authoritative summary/syntax/input/output description. In short, the
caller's loop looks like this:

```c
ludo_game g;
ludo_init(&g);

while (g.winner == -1) {
    int roll = ludo_roll(&g, 0); /* 0 = real random roll; 1..6 to inject a value */

    unsigned movable = ludo_movable_pawns(&g);
    if (movable == 0)
        continue; /* either more of the 3 tries left, or the turn already passed -- just roll again */

    int chosen = pick_a_pawn(movable); /* human input or AI, from the movable bitmask */
    ludo_move_pawn(&g, chosen);
}
```

`ludo_roll()` handles the mandatory six-releases-a-pawn rule and the
three-tries-when-stuck rule internally; `ludo_move_pawn()` handles
capture, win detection, and the six-grants-another-roll/end-of-turn
decision internally. The caller never needs to special-case any of that
-- it only ever needs to react to `ludo_movable_pawns()` being empty
(roll again) or non-empty (move one of the reported pawns).

## What's *not* in this module

Board/dice/pawn rendering, dialogue boxes, menus, and save/load are all
WIMP-shell concerns (`src/main.c` and friends) -- see
[ARCHITECTURE.md](ARCHITECTURE.md)'s porting table for how those map from
the GeoLudo reference. AI pawn-choice strategy (which of several movable
pawns a computer player picks) is also deliberately left to the caller --
`ludo_movable_pawns()` just reports the legal choices.
