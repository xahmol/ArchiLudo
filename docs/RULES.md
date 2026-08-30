# ArchiLudo Rules Manual

The complete rules of ArchiLudo: the base game, every house-rule variant
and toggle, and the three built-in presets. This is the player-facing
rules reference — for the engine's internal data model and C API, see
[GAME_LOGIC.md](GAME_LOGIC.md).

## Contents

- [The board](#the-board)
- [Base rules](#base-rules)
- [House-rule toggles](#house-rule-toggles)
- [Built-in presets](#built-in-presets)
- [Choosing and changing rules in-game](#choosing-and-changing-rules-in-game)
- [Where these rules come from](#where-these-rules-come-from)

## The board

Four players, four pawns each, a 40-square shared ring, and a 4-square
home column per player (the classic "Mens Erger Je Niet" / Ludo cross
board). Each player's pawns start in their own home base and must
travel all the way around the shared ring to their own home column
entrance, then up that column to finish.

## Base rules

These always apply, regardless of which variant or toggles are active:

1. Each player has 4 pawns, starting in their home base.
2. A pawn can only leave home and enter play on a roll of six, entering
   at the player's own start square on the shared ring.
3. Rolling a six always grants an extra roll after the move, chained
   for as many sixes in a row as are rolled (unless the **3 sixes**
   toggle below cuts this short).
4. If every one of a player's pawns is stuck — still at home, or
   blocked in the home column with no legal move — that player gets up
   to 3 consecutive rolls looking for the six needed to free a pawn. If
   none of the three is a six, the turn passes with no move made.
5. Landing exactly on a square occupied by another pawn sends that
   pawn back to its owner's home base — a capture. Whether this also
   applies to your own pawns is governed by the **Own capture** toggle
   below.
6. A pawn must reach the exact end of its home column to finish;
   whether overshooting is illegal or bounces the pawn back is
   governed by the **Overshoot** toggle below.
7. The first player to get all four pawns home wins. The other players
   may keep playing to decide runner-up order — the game doesn't end
   the moment someone wins.

## House-rule toggles

Every game has 8 independent toggles. Each can be set individually
regardless of which preset you started from — picking a preset is a
one-time starting point, not a lock.

| Toggle | Off / first setting | On / alternate setting |
|---|---|---|
| **Six-release** | **Mandatory** — rolling a six *automatically* releases a home pawn if one is waiting, and the player must move that specific pawn on their next roll (unless the home base is now empty, in which case the extra roll is a free choice) | **Optional** — releasing a pawn on a six is just one of the player's ordinary choices, with no follow-up obligation |
| **Own capture** | **On** — landing on your own other pawn sends it home too, exactly like landing on an opponent | **Off** — the two pawns simply share the square (this is what makes a **Blockade** possible) |
| **Overshoot** | **Blocked** — a roll that would carry a pawn past the exact end of its home column is not a legal move for that pawn | **Bounce** — the pawn instead bounces backward off the end by the remainder, clamped so it can never bounce back past the home column's own entrance onto the ring |
| **Blockade** | **Off** — no effect | **On** — two or more of a player's own pawns stacked on one ring square (only possible with Own capture off) block every *other* player from landing on or passing through that square, including releasing a new pawn there if it's the blockaded player's own start square |
| **Backward** | **Off** — no effect | **On** — a pawn already on the shared ring may move backward by the current roll instead of forward, as long as it doesn't go back past its own start square or (with Blockade also on) through a blockaded square. Never applies inside the home column |
| **Free home column** | **Off** — a player's own pawns block each other single-file in the home column, same as the ring | **On** — that blocking is lifted; a player's pawns may pass or land on each other freely in the home column (finished pawns still never literally overlap — see below) |
| **Last pawn** | **Needs six** — entering play always requires rolling a six, no exception | **Any roll** — a player's own *last* pawn still at home (their other three already in play or finished) may be released on any roll |
| **3 sixes** | **Chain** — sixes chain indefinitely, no cap (the traditional Mens Erger Je Niet behaviour) | **Forfeit** — a player's *third* six in a row is itself void: no release, no move, the turn ends immediately, exactly as if it had been a genuinely stuck roll |

A few notes that apply regardless of the toggles above:

- **Finished pawns still block.** A pawn that has finished stays
  parked on the exact square it finished on for the rest of the game —
  it blocks a later pawn from passing or landing there exactly like
  any other occupant. Because of this, each successive pawn's own
  finishing square shrinks by one square (so a player's finished pawns
  queue into the home column's 4 squares one at a time, from the far
  end inward, never stacking and never leaving a gap) — this happens
  automatically and isn't affected by the Free home column toggle.
- **Backward movement is ring-only.** It never applies inside a home
  column, regardless of the Free home column toggle.

## Built-in presets

Three curated combinations of the toggles above, offered as starting
points from the Rule Options dialogue's Variant menu:

| Toggle | Mens Erger Je Niet | Ludo | Pachisi-style |
|---|---|---|---|
| Six-release | Mandatory | Optional | Optional |
| Own capture | On | Off | Off |
| Overshoot | Blocked | Blocked | Bounce |
| Blockade | Off | **On** | On |
| Backward | Off | Off | On |
| Free home column | Off | Off | On |
| Last pawn | Needs six | Needs six | Needs six |
| 3 sixes | Chain | Forfeit | Chain |

**Mens Erger Je Niet** is ArchiLudo's default and traditional ruleset —
mandatory six-release, own-pawn capture instead of blockading, no
bounce-back, sixes chain without limit.

**Ludo** matches the mainstream international rules most players
expect: releasing is optional, your own pawns share a square instead
of capturing each other (which is what lets a blockade form), and a
third six in a row forfeits the turn.

**Pachisi-style** is a curated combination evoking Pachisi's
best-known distinguishing features — blockading, bounce-back instead
of a blocked overshoot, backward movement, and free home-column
manoeuvring. **It is not a faithful reimplementation of traditional
Pachisi**: real Pachisi uses a different board shape with safe squares
and a cowrie-shell throw instead of a single six-sided die, neither of
which ArchiLudo's board or dice attempt to reproduce.

## Choosing and changing rules in-game

Open **Rules...** from the New Game dialogue to reach the Rule Options
dialogue: pick a Variant from its pop-up menu to reset every toggle to
that preset's values, then adjust any individual toggle afterward if
you want a custom mix. Confirm with **OK** to carry your choices into
the game about to start, or **Cancel** to discard changes.

A loaded save game carries its own rules with it (whatever was active
when it was saved), and reopening the Rules dialogue afterward shows
that game's actual settings, not whatever was last configured for a
new game.

## Where these rules come from

The base rules and most toggles come directly from the traditional
"Mens Erger Je Niet" ruleset this project ports from a 1992 GEOS
edition (see [ARCHITECTURE.md](ARCHITECTURE.md)'s porting notes). The
**Blockade** and **3 sixes** defaults were set after cross-checking two
independent external Ludo references, which both describe blockading
as an unconditional consequence of two own pawns sharing a square in
standard Ludo (not merely optional) and both mention a three-sixes
forfeit rule that ArchiLudo's original ruleset was missing entirely —
so `Blockade` defaults on for the `Ludo` preset specifically, and the
3-sixes-forfeit toggle exists as its own option rather than being
folded into Mens Erger Je Niet's traditional unlimited chaining.

**Backward movement** and **Free home column** manoeuvring are both
mentioned only in passing, with no further mechanical detail, by the
Dutch-language source this project checked for underspecified
"sometimes allowed" variants (a pawn moving backward, or manoeuvring
within the home stretch) — no roll condition, no restriction to ring
vs. home column, no word on how either interacts with capturing. Both
toggles are therefore ArchiLudo's own documented reading of an
underspecified rule, not a literal transcription: backward movement
was implemented as ring-only, exact-roll, capped at the pawn's own
start square, and still subject to a blockade if one is active; free
home-column manoeuvring was implemented as simply lifting the
single-file blocking rule inside the home column, without changing how
finished pawns queue into their finishing squares (see the notes
above).
