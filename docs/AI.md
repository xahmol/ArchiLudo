# AI manual

`include/ai.h` / `src/ai.c` -- chooses which pawn an AI-controlled
player should move, given the set of currently-legal choices from
`ludo_movable_pawns()`. Pure C, no OSLib/WIMP dependency, host-testable
exactly like `game_logic.c` and `board_layout.c` -- see
`tests/test_ai.c` (9 tests).

## Why this is a separate module from `game_logic.c`

`game_logic.c` enforces the rules: what moves are *legal*. `ai.c` only
ever picks among moves `game_logic.c` has already approved (via the
`movable` bitmask passed in) -- it never bypasses or duplicates a rule
check itself. This mirrors the existing `game_logic.c`/`board_layout.c`
split: each module has exactly one job, and none of them know about the
WIMP shell at all.

## Assessed from GeoLudo, not a literal port

Per explicit instruction ("Assess my GEOS original one, but feel free to
improve"), `computerchoosepawn()` in
`/home/xahmol/git/ludo/GEOS/src/main.c` was read in full before writing
anything here. Same overall shape carried over: score every currently
movable pawn, pick the highest score. What changed, and why:

- **Board position bookkeeping.** GEOS tracks each pawn as
  `(track, raw-position-within-that-player's-own-lap)` and has to
  un-wrap that into an absolute board square by hand, once per player,
  via four near-identical `if` blocks (`if(turnofplayernr==0 && nn>39 &&
  vn<40) { nn-=36; nr=1; }` etc). This project's `game_logic.c` already
  has a single unified `steps` counter (`0..LUDO_TOTAL_STEPS`) and
  `board_layout.c`-style ring-square arithmetic, so `ai.c` just
  duplicates a small `ring_square()` helper (the same formula as
  `game_logic.c`'s own private one, and `board_layout.c`'s
  `board_ring_cell()` dispatch) instead of needing any of that
  unwrapping.
- **Danger/escape scoring, recomputed correctly.** GEOS's heuristic for
  "is this pawn currently threatened, or would moving it create a
  threat" compares raw same-lap position numbers *across different
  players* directly (`vn - playerpos[y][z][1]`). This isn't actually a
  valid board distance except by coincidence -- each player's lap starts
  at a different absolute ring square, so subtracting two different
  players' own lap-relative numbers doesn't generally mean what it looks
  like it means. Recomputed properly here via `ring_distance_behind()`,
  real absolute-ring-square distance. Also corrected the reachable-in-
  one-throw range from GEOS's `< 6` (i.e. 1-5) to the actual `<= 6` a die
  can roll (1-6) -- GEOS's version misses the exact-6 case.
- **Own-pawn collision, changed to match this project's actual rule.**
  GEOS scores landing on one of its own pawns at -8000 (strongly
  avoided, reflecting that game's rule that this is illegal/blocked).
  ArchiLudo's rule is different (see `game_logic.c`'s `capture_at()`,
  round 6.5 in `docs/ARCHITECTURE.md`'s Phase 1 notes): landing on your
  own pawn is legal and sends the earlier one home. Scored here as a
  real but *scaled* penalty (`WEIGHT_OWN_COLLISION_BASE +
  WEIGHT_OWN_COLLISION_PER_STEP * that pawn's steps`) rather than a
  near-absolute one -- losing a pawn that had barely moved barely
  matters; losing one that was almost home matters a lot -- since for
  ArchiLudo this is sometimes the only legal, or even the objectively
  best, move (e.g. a forced pawn has no alternative at all), not
  something that "never happens" the way GEOS's -8000 effectively
  assumes.
- **Winning-move detection, made exact.** GEOS approximates "is this the
  move that wins the game" via `playerdata[player][1]==1` ("only one
  pawn not yet at its destination", a running counter updated
  elsewhere). `ai.c` checks directly: are the other three pawns actually
  `finished` right now? No assumption about a separately-maintained
  counter staying accurate through every code path.

## Scoring weights

All named constants at the top of `ai.c`, roughly in descending order of
influence:

| Weight | Value | What it rewards/penalises |
|---|---|---|
| `WEIGHT_WIN` | 200000 | This move finishes the player's *last* unfinished pawn -- wins outright. |
| `WEIGHT_FINISH` | 6000 | This move finishes a pawn (not necessarily the last one). |
| `WEIGHT_CAPTURE` | 4000 | Lands on an opponent's pawn, sending it home. |
| `WEIGHT_CAPTURE_NEAR_HOME` | +3000 | ...and that opponent pawn was within `NEAR_HOME_RING_SQUARES_REMAINING` (6) squares of leaving the ring -- capturing costs them more. |
| `WEIGHT_ENTRY_SQUARE_LAND` | -2000 | Lands on any of the four ring-entry squares -- exposed, since any player releasing a new pawn there can capture whoever's sitting on it. |
| `WEIGHT_ENTRY_SQUARE_LEAVE` | +1500 | The pawn's *current* square (before this move) is an entry square -- prioritise moving off it. |
| `WEIGHT_DANGER_STILL_IN` | -300 | After this move, an opponent pawn could reach the new square with one throw next turn. |
| `WEIGHT_DANGER_ESCAPE` | +400 | The pawn was in that kind of danger *before* the move, and this move gets it out of range. |
| `WEIGHT_DANGER_APPROACH_OPPONENT` | +150 | This move puts the pawn within one throw of an opponent -- sets up a capture opportunity next turn. |
| `WEIGHT_OWN_COLLISION_BASE` / `_PER_STEP` | -500, -50/step | Sends one of the player's own other pawns home (see above) -- scaled by how far that pawn had progressed. |
| `WEIGHT_HOME_COLUMN_ADVANCE_BASE` / `_PER_STEP` | +2000, +100/step | The move places the pawn in its home column (already there, or crossing in from the ring this move) without finishing it -- risk-free, no capture/danger heuristic can ever apply there, so this rewards it explicitly rather than letting it compete only on `WEIGHT_PROGRESS_PER_STEP`. Added round 7.14, see below. |
| `WEIGHT_PROGRESS_PER_STEP` | 5 | Small, deliberately minor tie-breaker: prefer the pawn that ends up furthest along. |

`score_move()` in `ai.c` computes and sums these for one candidate move;
`ludo_ai_choose_pawn()` calls it for every bit set in the `movable` mask
and returns whichever pawn scored highest (first one seen wins ties).

## Round 7.14: home-column advances were getting drowned out

Per explicit user report ("AI does not seem to prioritise moving pawns
in destination home area further to the end"): a pawn already safely in
its home column has no capture/danger heuristic that can ever apply to
it (the home column is single-file and off-limits to every other
player's pawns), so before this round such a move only ever earned the
same flat `WEIGHT_PROGRESS_PER_STEP` (5/step) as any other move. That's
easily dwarfed by unrelated ring-tactic bonuses on some *other*
currently-movable pawn -- `WEIGHT_ENTRY_SQUARE_LEAVE` (1500) alone
already outweighs a typical few-step home-column advance's ~200-215
points of pure progress. The AI would therefore often move a ring pawn
for a minor tactical gain instead of a risk-free, directly
win-relevant home-column advance, even with both available for the
same roll.

Fixed by adding `WEIGHT_HOME_COLUMN_ADVANCE_BASE`/`_PER_STEP`, applied
whenever a move ends with the pawn off the ring but not yet finished
(the finishing case already returns early with `WEIGHT_FINISH`/
`WEIGHT_WIN`, so this is specifically "safely closer to winning, not
there yet"). Sized to clearly beat the ring-tactic bonuses
(`WEIGHT_ENTRY_SQUARE_LEAVE`, `WEIGHT_DANGER_ESCAPE`/
`APPROACH_OPPONENT`) but still lose to an actual capture
(`WEIGHT_CAPTURE` alone already exceeds it) -- see
`test_prefers_home_column_advance_over_ring_tactic` and
`test_capture_still_beats_home_column_advance` in `tests/test_ai.c` for
the exact worked-out scoring these weights were chosen against.

## Difficulty levels

Only `LUDO_AI_NORMAL` (the weighted heuristic above) is implemented.
`LUDO_AI_EASY` and `LUDO_AI_HARD` are declared in `ai.h` and accepted by
`ludo_ai_choose_pawn()`, but both currently fall through to the same
`NORMAL` behaviour (`difficulty` is read but not yet branched on) --
they exist so the WIMP shell's eventual difficulty selection (not built
yet) has somewhere to point without needing another API change later.
Intended design, not yet built:

- **Easy**: something a human can find genuinely easier to beat --
  candidates: ignore capture/danger scoring entirely and just prefer
  advancing the frontmost pawn, or pick uniformly at random among legal
  moves.
- **Hard**: a deeper search (e.g. minimax over the next few rolls) rather
  than this purely greedy single-move scoring -- a bigger undertaking,
  not scoped in detail yet.

## How an AI turn actually plays out

`src/game_view.c`'s `advance_ai_turns()` is the loop that drives this:
while the current player is AI-controlled (per
`game_view_configure_players()`, set from `src/setup_view.c`'s "New
Game" dialogue) and nobody's won yet, it calls `ludo_roll()`, then (if
that wasn't a mandatory six-release with nothing to pick)
`ludo_ai_choose_pawn()` + `ludo_move_pawn()`, redrawing and pausing
briefly after each one so a human watching can actually follow what
happened, rather than only ever seeing the final state once several
consecutive AI turns (chained sixes, or multiple AI players in a row in
a mixed human/AI game) have already finished. See that function's doc
comment, and `docs/ARCHITECTURE.md`'s Phase 1 notes ("Round 6.8"), for
why it calls a direct `redraw_now()` rather than `wimp_force_redraw()`
for this.

## Updating this file

Add a note here whenever a weight changes, a difficulty level actually
gets implemented, or a new rule variant (see `docs/ARCHITECTURE.md`'s
"Future: multiple Pachisi/Ludo/Mens Erger Je Niet variants" note) needs
its own scoring differences.
