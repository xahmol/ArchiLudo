# AI manual

`include/ai.h` / `src/ai.c` -- chooses which pawn an AI-controlled
player should move, given the set of currently-legal choices from
`ludo_movable_pawns()`. Pure C, no OSLib/WIMP dependency, host-testable
exactly like `game_logic.c` and `board_layout.c` -- see
`tests/test_ai.c`.

## Why this is a separate module from `game_logic.c`

`game_logic.c` enforces the rules: what moves are *legal*. `ai.c` only
ever picks among moves `game_logic.c` has already approved (via the
`movable` bitmask passed in) -- it never bypasses or duplicates a rule
check itself. This mirrors the existing `game_logic.c`/`board_layout.c`
split: each module has exactly one job, and none of them know about
the WIMP shell at all.

## Assessed from GeoLudo, not a literal port

`computerchoosepawn()` in `/home/xahmol/git/ludo/GEOS/src/main.c` was
read in full before writing this module. Same overall shape carried
over: score every currently movable pawn, pick the highest score. What
changed, and why:

- **Board position bookkeeping.** GEOS tracks each pawn as
  `(track, raw-position-within-that-player's-own-lap)` and has to
  un-wrap that into an absolute board square by hand, once per player,
  via four near-identical `if` blocks. This project's `game_logic.c`
  already has a single unified `steps` counter and `board_layout.c`-
  style ring-square arithmetic, so `ai.c` just duplicates a small
  `ring_square()` helper (the same formula as `game_logic.c`'s own
  private one) instead of needing any of that unwrapping.
- **Danger/escape scoring, recomputed correctly.** GEOS's heuristic for
  "is this pawn currently threatened, or would moving it create a
  threat" compares raw same-lap position numbers *across different
  players* directly. This isn't actually a valid board distance except
  by coincidence -- each player's lap starts at a different absolute
  ring square, so subtracting two different players' own lap-relative
  numbers doesn't generally mean what it looks like it means.
  Recomputed properly here via `ring_distance_behind()`, real absolute-
  ring-square distance. Also corrected the reachable-in-one-throw range
  from GEOS's `< 6` (i.e. 1-5) to the actual `<= 6` a die can roll
  (1-6) -- GEOS's version misses the exact-6 case.
- **Own-pawn collision, changed to match this project's actual rule.**
  GEOS scores landing on one of its own pawns at -8000 (strongly
  avoided, reflecting that game's rule that this is illegal/blocked).
  ArchiLudo's Own capture rule can instead be off, in which case
  landing on your own pawn is legal and sends the earlier one home (or
  forms a blockade -- see below). Scored here as a real but *scaled*
  penalty rather than a near-absolute one -- losing a pawn that had
  barely moved barely matters; losing one that was almost home matters
  a lot -- since for ArchiLudo this is sometimes the only legal, or
  even the objectively best, move (e.g. a forced pawn has no
  alternative at all), not something that "never happens" the way
  GEOS's -8000 effectively assumes.
- **Winning-move detection, made exact.** GEOS approximates "is this
  the move that wins the game" via a separately-maintained running
  counter. `ai.c` checks directly: are the other three pawns actually
  `finished` right now? No assumption about that counter staying
  accurate through every code path.

## Scoring weights

All named constants at the top of `ai.c`, roughly in descending order
of influence:

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
| `WEIGHT_OWN_COLLISION_BASE` / `_PER_STEP` | -500, -50/step | Sends one of the player's own other pawns home (see above) -- scaled by how far that pawn had progressed. Only applied when the Own capture toggle is on. |
| `WEIGHT_BLOCKADE_FORM` | +800 | Landing on the player's own other pawn when Own capture is off but Blockade is on -- forms/reinforces a blockade instead of a collision. Deliberately modest (blockade strategy is a stretch goal, not deep tactics). |
| `WEIGHT_RELEASE_BASE` / `_PER_PAWN_ALREADY_OUT` | +3000, -300/pawn | Releasing a home pawn into play (only ever a scored choice when the Six-release toggle is Optional) -- a first-pass heuristic, scaled down slightly for each pawn already racing. |
| `WEIGHT_HOME_COLUMN_ADVANCE_BASE` / `_PER_STEP` | +2000, +100/step | The move places the pawn in its home column (already there, or crossing in from the ring this move) without finishing it -- risk-free, no capture/danger heuristic can ever apply there, so this rewards it explicitly rather than letting it compete only on `WEIGHT_PROGRESS_PER_STEP` (see below; a home-column advance used to lose out to unrelated ring-tactic bonuses on some other movable pawn before this weight was added). |
| `WEIGHT_PROGRESS_PER_STEP` | 5 | Small, deliberately minor tie-breaker: prefer the pawn that ends up furthest along. |

`score_move()` in `ai.c` computes and sums these for one candidate
move; `ludo_ai_choose_pawn()` calls it for every bit set in the
`movable` mask and returns whichever pawn scored highest (first one
seen wins ties). `score_landing_at()` is a shared helper for exactly
what happens when a move lands on an occupied square (own-pawn
collision, blockade formation, or an opponent capture), used by both
an ordinary move and a release from home, so the two can never
disagree about the rules currently in effect
(`g->rules.own_pawn_capture`/`g->rules.blockade`).

A pawn's destination is computed via `game_logic.c`'s own
`ludo_resolve_move_destination()`, not a naive `steps + roll` sum --
under the Overshoot=Bounce toggle the true landing square can bounce
backward off the end of the home column, and a naive sum well past the
board's total step count could make `score_move()` think a move
finishes (or even wins) the game when it actually bounces back into an
ordinary position.

**Backward movement** (the Backward toggle) is deliberately **not**
deeply scored -- real strategic sophistication for it (and for
blockade formation beyond the one term above) is an explicit stretch
goal, not core scope. A separate, much simpler function pair handles
it: `ludo_ai_choose_pawn_backward()`/`score_move_backward()`, meant to
be called only as a fallback when `ludo_ai_choose_pawn()`'s own forward
`movable` bitmask is empty but a legal backward move exists (otherwise
a player who can only move backward would incorrectly look stuck). It
still reuses `score_landing_at()` for a real capture-on-landing bonus,
then just prefers retreating the least distance as a tie-breaker -- no
attempt at real backward-movement strategy.

**`src/game_view.c`'s `advance_ai_turns()` does not call
`ludo_ai_choose_pawn_backward()` yet** -- the fallback exists in `ai.c`
but isn't wired into the actual AI-turn driver. Any preset with the
Backward toggle on (`Pachisi-style` is the only built-in one) is fully
selectable from the Rule Options dialogue today, so an AI-controlled
game playing under it could hit a roll where only a backward move is
legal and currently just settle as stuck rather than taking the
backward option -- the same gap `docs/ARCHITECTURE.md`'s "Known gaps"
section describes for the human/board-click side of the same feature.

## Difficulty levels

`ludo_ai_difficulty` (`ai.h`) has three values, chosen per AI-controlled
player in `src/setup_view.c`'s "New Game" dialogue (a Low/Medium/High
button next to that player's Human/AI toggle, shaded while the player
is Human) and passed through
`game_view_configure_players()`/`game_view_get_players()` to
`ludo_ai_choose_pawn()`:

- **`LUDO_AI_EASY`** ("Low") -- `score_move_easy()`/`score_release_easy()`
  keep only the objectively decisive terms from the table above: win,
  finish, capture (including a free capture on release), the risk-free
  home-column-advance bonus, and plain per-step progress. It never
  skips a free win or capture (so it never reads as broken the way
  picking uniformly at random would -- random play routinely ignores an
  obvious win), but has zero danger, entry-square, or blockade
  awareness. One deliberate exception: `score_release_easy()` still
  applies a decisive penalty when a release would land on (and, under
  Own capture, send home) one of the player's *own* pawns -- without it
  a player with exactly one pawn left at home and one other pawn
  sitting on the entry square could get stuck alternately releasing
  onto, and being released onto by, that same pawn forever (a real
  non-terminating game `tests/test_ai.c`'s
  `test_headless_ai_all_rule_combinations()` found this way).
- **`LUDO_AI_NORMAL`** ("Medium") -- the full weighted heuristic
  described above, unchanged.
- **`LUDO_AI_HARD`** ("High") -- `NORMAL`'s full scoring plus a genuine
  one-ply lookahead, `score_lookahead_penalty()`: for each candidate
  move it clones the game, applies the move via `game_logic.c`'s own
  tested `ludo_move_pawn()`, then simulates the next opponent's most
  likely response (predicted with `NORMAL`'s own `score_move()`,
  deliberately not itself another level of lookahead) across all 6
  possible next rolls via `ludo_roll()`/`ludo_move_pawn()` again, and
  penalises a move that leaves one of the player's own pawns newly
  capturable. All three tiers share a `choose_best_pawn()` helper
  (score every bit in the `movable` mask with a given scoring function,
  return the highest -- first one seen wins ties).

**AI-vs-AI win rate does not rank the tiers the way per-move scoring
sophistication would suggest**, and this is expected, not a bug: Ludo
is dice-dominated enough that `EASY`'s decisive-only heuristic already
sits close to a local win-rate optimum against a non-adversarial
opponent. A multi-seed measurement (many seeds, many games each, not
just `tests/test_ai.c`'s own single fixed-seed run) found `EASY`
winning roughly as often as, or somewhat more often than, both `NORMAL`
and `HARD` under the default Mens Erger Je Niet ruleset, and no
reasonable retuning of the danger/entry-square weights or the lookahead
penalty's magnitude closed that gap -- proactive danger-avoidance
mostly trades away real progress for a risk reduction a dice-driven
game doesn't reliably reward when the opponent isn't specifically
hunting your weaknesses. What *is* real and tested: `NORMAL`/`HARD`
make genuinely better *individual* move choices than `EASY` in the
specific scenarios the hand-crafted tests check (escaping a threatened
pawn, avoiding a pointless own-collision, etc.) -- which is what
matters against a human opponent who can set up and exploit a
weakness deliberately. `tests/test_ai.c`'s
`test_hard_win_rate_not_regressed()` reflects this: it's a regression
guard against something badly broken, not a claim that `HARD`
statistically dominates `EASY` in AI-vs-AI play.

## How an AI turn actually plays out

`src/game_view.c`'s `advance_ai_turns()` is the loop that drives this:
while the current player is AI-controlled (set from `src/setup_view.c`'s
"New Game" dialogue) and nobody's won yet, it calls `ludo_roll()`, then
(if that wasn't a mandatory six-release with nothing to pick)
`ludo_ai_choose_pawn()` + `ludo_move_pawn()`, redrawing and pausing
briefly after each one so a human watching can actually follow what
happened, rather than only ever seeing the final state once several
consecutive AI turns (chained sixes, or multiple AI players in a row
in a mixed human/AI game) have already finished. It calls a direct
redraw function rather than the Wimp's own deferred force-redraw for
this, since the latter only schedules a redraw for the next
`Wimp_Poll` and wouldn't show anything until the whole AI loop
finished.

## Updating this file

Add a note here whenever a weight changes, a difficulty level actually
gets implemented, or a rule variant needs its own scoring differences.
