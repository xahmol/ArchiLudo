# game_logic manual

`include/game_logic.h` / `src/game_logic.c` -- the platform-independent
Ludo rules engine. See [ARCHITECTURE.md](ARCHITECTURE.md) for why this
is a clean reimplementation of the original BASIC/GEOS logic rather
than a literal port, [RULES.md](RULES.md) for the full player-facing
rules manual (base rules, all 8 house-rule toggles, the 3 presets, and
where they come from), and `tests/test_game_logic.c` for the automated
test suite that exercises every rule.

## Rule-set variants (`ludo_rules`)

Every game has a `ludo_rules` struct (`g->rules`) picking,
independently, how each of 8 toggles behaves -- see
[RULES.md](RULES.md) for what each toggle means for a player.
`ludo_init()` always sets the `Mens Erger Je Niet` preset's own
defaults; `ludo_set_rules()` overrides them when a different preset or
a hand-picked combination is wanted. `ludo_default_rules()` returns the
toggle values for a given preset (`LUDO_VARIANT_MEJN`/`_LUDO`/
`_PACHISI`).

Two toggles have their own dedicated API surface rather than folding
into the ordinary forward-move functions:

- **Backward movement** (`rules.backward_movement`): a second,
  independent bitmask/move pair,
  `ludo_movable_pawns_backward()`/`ludo_move_pawn_backward()`, since a
  pawn can legally have both a forward AND a backward option for the
  same roll. Restricted to pawns still on the shared ring; the home
  column has its own separate toggle.
- **Free home column** (`rules.free_home_column`): implemented as
  simply switching off `home_column_blocked()`'s single-file check
  entirely -- see that function's own doc comment in `game_logic.c`.
  `finish_threshold_for()` (see "Position model" below) is a genuinely
  separate mechanism this toggle does not touch, so successive pawns
  still finish on distinct squares one at a time even with free
  manoeuvring on.

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

A player's start square on the ring is `player_index * 10`
(`LUDO_RING_LENGTH / LUDO_PLAYERS`), so a pawn's absolute ring square is
derived with `ring_square()` in `game_logic.c`: `(player * 10 + steps)
% 40`.

This is simpler than the original's ad-hoc coordinate pairs: one
number per pawn, trivial to print for debugging, and trivial to reason
about for overshoot/capture/blocking checks.

**A finished pawn's `steps` is NOT pinned at a single fixed value.**
Each player has a shrinking "pawns still needed home" target; a pawn
only counts as finished when its landing position reaches that target,
which itself decrements by one every time a pawn finishes. A finished
pawn permanently occupies its own square and still blocks like any
other occupant (`home_column_blocked()`), and each subsequent pawn's
own reachable maximum (`finish_threshold_for()`) is mechanically capped
one square lower for every pawn already parked ahead of it -- so a
player's finished pawns queue into the home column's 4 distinct
squares one at a time, from the far end inward, never stacking, never
leaving a gap. The overshoot check stays against the fixed
`LUDO_TOTAL_STEPS` regardless -- it's blocking, not the overshoot
check, that enforces each pawn's own lower effective ceiling once
earlier pawns occupy the squares above it. This matches the original
GEOS edition's own equivalent mechanism
(`playerdata[player][1]`/`pawnselect()` in
`/home/xahmol/git/ludo/GEOS/src/gamelogic.c`), ground-truthed directly
against that source rather than assumed.

**A player who has already won keeps their own bonus-roll-on-six
regardless of who else has won.** The six-goes-again check tests the
current player's own `all_pawns_finished()`, not whether the overall
game has a winner yet -- a player who just finished obviously has
nothing left to roll for, but everyone else keeps the normal rule
regardless of who else has already won (rule 9 in RULES.md, "remaining
players may continue"). Regression test:
`tests/test_game_logic.c`'s `test_six_bonus_survives_another_players_win()`.

## API

See the docstring comment above each declaration in `game_logic.h` for
the authoritative summary/syntax/input/output description. In short,
the caller's loop looks like this (stopping at the first winner -- a
caller that wants full placement, like `src/game_view.c`'s win dialogue
does, keeps playing past this and reads `g.finish_order[]` instead,
which records every player as they finish, not just the first):

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
decision internally. The caller never needs to special-case any of
that -- it only ever needs to react to `ludo_movable_pawns()` being
empty (roll again) or non-empty (move one of the reported pawns).

When `rules.backward_movement` is on, `ludo_movable_pawns_backward(&g)`
and `ludo_move_pawn_backward(&g, pawn_index)` are checked alongside the
ordinary forward pair -- always empty when the rule is off, so existing
callers that never check it see no change in behaviour.

## Dice randomness and seeding

`ludo_roll()` calls the C library's `rand()` directly for an unforced
roll -- this module has no OSLib dependency, so it cannot seed it
itself (seeding needs a time source, which is a platform call). The
WIMP shell seeds it once, via `src/main.c`'s `seed_random()`, called
right at the start of `archiludo_initialise()` -- before any dice can
possibly be rolled. A caller embedding this module elsewhere (the host
test suite included) is responsible for calling `srand()` itself if it
wants a seed that varies between runs; without one, ArchieSDK's
`rand()` (a plain LCG, `SDK/src/libc/stdlib/rand.c`) starts from a
hardcoded seed of 0 every time, and a host libc's `rand()` typically
defaults to a seed of 1 -- either way, every run would roll the exact
same dice sequence.

`ludo_roll()` also does not take ArchieSDK's `rand()` output on faith:
that LCG's low-order bits are weak (odd multiplier, odd increment --
the low bit toggles on every single call, deterministically), so a
naive `rand() % 6` produces dice whose *parity* strictly alternates
every roll even though the 1-6 distribution looks uniform in
aggregate. `ludo_roll()` shifts the raw value right by 16 bits before
the modulo to use the healthier high-order bits instead -- verified on
the host (a throwaway simulation of the exact same LCG formula) to
bring parity alternation from 100% down to the ~50% real randomness
should show, without changing the roll distribution. This shift is
harmless under a healthy host-compiler `rand()` too (`make test` uses
it unconditionally), so no platform-specific branch was needed.

## What's *not* in this module

Board/dice/pawn rendering, dialogue boxes, menus, and save/load are all
WIMP-shell concerns (`src/main.c` and friends) -- see
[ARCHITECTURE.md](ARCHITECTURE.md)'s porting table for how those map
from the GeoLudo reference. AI pawn-choice strategy (which of several
movable pawns a computer player picks) is also deliberately left to
the caller -- `ludo_movable_pawns()` just reports the legal choices.
