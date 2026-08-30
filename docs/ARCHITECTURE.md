# ArchiLudo Architecture

## Resume here

*(Delete/replace this section as it goes stale -- it exists purely so a
session that starts cold, with no conversation history, knows exactly
where things stand and what to do next. Last rewritten round 7.56,
after this section had drifted quite far out of date -- see this file's
own "Current state"/"Roadmap" tables above for the compact status
summary this section used to try to duplicate; this section now only
covers what those tables can't: genuinely open, non-obvious gaps.)*

**The multi-rule-set / house-rule variant system (3 variants, 8
toggles, Rule Options dialogue) is fully implemented AND live-confirmed
working** -- the user has since actually played a real game under the
`Ludo` preset through the real WIMP shell (round 7.55's blockade/
three-sixes audit conversation), confirming the Rules dialogue, variant
switching, and radio-icon toggles all genuinely work in practice, not
just in code. Extensive follow-up rounds (7.47 through 7.55) fixed real
bugs found this way -- layout clipping, dither/outline rendering, a
duplicate-pawn animation bug, a stale highlight-ring bug, and a real
rules-default gap found via external audit -- see this file's own round
log below for each. Not yet exhaustively stress-tested (no test regime
can claim that for a live WIMP UI), but no longer "unverified."

**One real, still-open gap, easy to forget since nothing currently
surfaces it**: `src/game_view.c` has no board-interaction path for a
*backward* move at all, for either a human or an AI player --
`resolve_roll()` only ever consults `ludo_movable_pawns()` (forward),
never `ludo_movable_pawns_backward()`. This was flagged back in round
7.46 (Phase 4 of the original multi-rule-set plan) and never
subsequently closed. It's harmless as long as nobody selects a ruleset
with `backward_movement` on (`Pachisi-style` is the only preset that
does) -- but the `Rules` dialogue absolutely lets a player do exactly
that today, and on a roll where only a backward move is legal, the game
would currently just settle as if the player were stuck rather than
offering the backward option at all. Real, user-visible risk of an
apparent soft-lock if anyone actually plays `Pachisi-style` for a while.
Worth its own dedicated round before `Pachisi-style` gets recommended to
a real player -- needs `start_move_animation()`'s own cell-by-cell path
building mirrored for the backward direction, and (for a human) some way
to actually choose "backward" on the board's click model, which doesn't
have that concept at all currently.

**Save-file rules persistence: fixed in round 7.57**, still true under
round 7.59's format bump -- `"ALS2"`/`"ALS3"` both carry the 9-byte
rules block right after the magic (see `game_view.c`'s
`serialize_game()`/`deserialize_game()`). Not yet manually verified in
Arculator -- next live-test pass should save a non-MEJN game (e.g.
`Ludo` preset with blockade on) into a slot, load it back, and confirm
the Rules dialogue still shows the right settings afterwards (exercises
the `configured_rules` sync, not just `game.rules`).

**Save/load: pivoted from drag-and-drop to 5 fixed save slots in round
7.59, not yet manually verified in Arculator.** Round 7.57's
`DragASprite` sprite-drag work (and the plain `wimp_drag_box()` outline
before it) never worked live despite being structurally correct against
the PRM -- round 7.58's extensive Arculator debugging session (outline
drag, sprite drag, dropping on an open Filer window, dropping straight
on the icon bar -- 7+ attempts, all logged) traced the message send
itself as succeeding every time (`Wimp_SendMessage` returned no error,
resolved to a real, consistent task handle, drop point genuinely inside
the target's own rectangle) with `Message_DataSaveAck` simply never
arriving in reply, even from a direct icon-bar drop -- most likely
because Arculator's HostFS bridge doesn't implement the Filer side of
that protocol at all, not a bug in ArchiLudo's own code. Rather than
keep chasing an environment limitation, per explicit user request the
whole free-form pathname/drag-and-drop design was replaced with 5 fixed
slots (`<ArchiLudo$Dir>.Slot1` .. `.Slot5`), each carrying its own
renamable display name as part of the save data itself (`"ALS2"` bumped
to `"ALS3"`, see `GAME_VIEW_SLOT_NAME_LEN`/`game_view_peek_slot_name()`
in `game_view.h`/`game_view.c`) -- `save_view.c` was rewritten
end-to-end (window layout, click handling, the whole `DragASprite`/
`Message_DataSave` machinery removed), and `main.c`'s `wimp_USER_MESSAGE`
handling trimmed back to just `Message_Quit`. Next live-test pass should
check: the Save dialogue's 5 rows (rename + Save per row), the Load
dialogue's 5 rows (read-only name, "(empty)" + shaded Load button for
unoccupied slots), and a full save-into-slot-2 / reload-slot-2
round-trip including the slot's own name surviving the round-trip.

**Enhanced full-screen graphics mode: decided against** in round 7.56
("Want to drop the enhanced graphics mode, like the present windowed
mode") -- see this file's own "Decided against" note under "Roadmap"
above. Don't resurrect this without checking that note first.

**Audio (QTM music + SFX): confirmed live and working, round 7.60
through 7.84.** Background music (3 selectable tracks) and SFX (6
one-shot effects, all embedded directly into each track's own MOD
sample table) are both confirmed working in Arculator, independently
switchable via the Music submenu's "On"/"SFX" toggles. `QTM_PlayRawSample`
was abandoned entirely after 14 rounds (7.60-7.73) of live debugging --
including catching the fault live in Arculator's own debugger and
disassembling the actual faulting code -- never finding a parameter
combination that avoids an internal resampler buffer overrun.
`QTM_PlaySample` against MOD-embedded samples (the approach every real
Archimedes codebase checked actually uses) took its own path to get
working: round 7.78 found the real missing piece via the official QTM
distribution archive (`QTM_SoundControl` needs 8-channel mode enabled),
round 7.79 fixed cross-SFX channel contention, and round 7.82 found the
final remaining "silent" SFX weren't a technical bug at all -- just too
quiet against the music, fixed with proper RMS-targeted loudness
normalization rather than the ducking approach that was tried, tested
working, and then rejected as too jarring. Full round-by-round detail in
[QTM.md](QTM.md) (rounds 7.74-7.84). Round 7.75 also fixed music not
stopping when the application quits.

For everything else -- AI difficulty levels beyond `NORMAL` (declared,
unimplemented), release packaging (works, never actually cut) -- see the
"Roadmap" table above; this section deliberately doesn't duplicate it.

## Layering

ArchiLudo is deliberately split into two layers that never mix:

```
src/game_logic.c   <-- pure C rules engine, zero OSLib/WIMP dependency
src/main.c         <-- RISC OS WIMP shell (iconbar icon, Wimp_Poll loop)
```

`game_logic.c`/`include/game_logic.h` know nothing about RISC OS, sprites,
windows, or SWIs -- they only know about pawns, dice, and turns. This is
what lets the exact same source file be:

- cross-compiled with ArchieSDK's `arm-archie-gcc` into the real game
  (linked into `src/main.c` in the normal `all`/`ArchiLudo,ff8` build), and
- compiled with the **host's own `cc`** and exercised by
  `tests/test_game_logic.c` for automated testing (`make test`), with no
  ArchieSDK, no Arculator, and no RISC OS ROM involved at all.

See [BUILDCHAIN.md](BUILDCHAIN.md) for how the two separate compilation
paths are wired up in the Makefile, and [GAME_LOGIC.md](GAME_LOGIC.md) for
the rules engine's data model and API in detail.

Why this split matters here specifically: the WIMP shell can only be
verified visually, by hand, inside Arculator (see the "Testing" section of
the top-level `CLAUDE.md` for why -- Arculator has no headless/scriptable
mode). Automating *that* isn't possible. Automating the actual game rules
is, and that's where bugs are cheapest to catch, so the rules engine is
kept 100% independent of the shell precisely so it doesn't have to share
that limitation.

## Current state

| Component | Status |
|---|---|
| `game_logic.c` | Complete rules engine, INCLUDING the full multi-rule-set/house-rule system (3 variants, 8 toggles -- see [GAME_LOGIC.md](GAME_LOGIC.md)), fully unit tested (33 tests, exhaustive coverage of all 128 toggle combinations -- round 7.53) |
| `board_layout.c` | The real Mens Erger Je Niet board (see [BOARD_LAYOUT.md](BOARD_LAYOUT.md)), ported from the GEOS edition's coordinate tables, not invented -- fully unit tested |
| Sprite/graphics tooling | `tools/riscos_sprite.py`; real pawn/app-icon/dice art generated by `assets/generate_icon_sprites.py`/`generate_app_icon.py`, with a hand pixel-editing round-trip (`assets/edit/`) -- see [GRAPHICS_TOOLING.md](GRAPHICS_TOOLING.md) |
| `main.c` / `game_view.c` | Playable, extensively refined WIMP game: iconbar icon, game window with animated pawn movement, dice roll animation, movable-pawn highlight rings, capture/win detection, Continue-gated AI turns. Many rounds of live Arculator feedback applied and fixed |
| `rules_view.c`/`setup_view.c`/`save_view.c`/`win_view.c`/`splash_view.c` | Rule Options dialogue (variant picker + all 8 house-rule toggles), New Game/player setup, drag-and-drop save/load, win-choice dialogue, About/splash screen (doubles as the "credits" screen) -- all done |
| AI opponents | Working (`ai.c`, adapted to the full multi-rule-set system) -- but only one difficulty level (`LUDO_AI_NORMAL`) has real strategy behind it; `LUDO_AI_EASY`/`LUDO_AI_HARD` are declared but unimplemented, and there's no UI to pick a difficulty per player at all |
| Save-file rules persistence | **Gap**: `save_view.c` doesn't serialize `ludo_rules` at all -- saving a non-MEJN game and reloading it silently reverts to MEJN defaults |
| Music/SFX (QTM) | Not started at all -- no `lib/qtm.c`, no `docs/QTM.md` |
| Release packaging | `make zip` works, but no actual tagged/versioned release has been cut yet; README has no screenshots section |
| Full-screen enhanced graphics mode | **Decided against** (see below) -- staying with the current windowed WIMP mode |

## Roadmap

**Round 7.56 update**: this table had drifted a long way from reality
(last substantively updated early in the project) -- most of "Phase 2"
(real art) and "Phase 5" (AI, dialogues, app packaging) turned out to
already be done, and an entire unscheduled future phase (multi-rule-set
variants) got fully implemented ahead of several "earlier" phases.
Reconciled against the actual codebase state, not just this table's own
prior claims, per explicit user request ("What do we have left on to do
list?"). The full history of *how* everything below got built is in this
file's own round-by-round log further down -- this table is now just a
compact status summary, not the authoritative planning document it once
was.

| Item | Status |
|---|---|
| Build environment (ArchieSDK, Arculator profiles, docs set, PRM/wimp-prog mirrors) | done |
| Playable WIMP game (board/pawns/dice, click + AI turns, animation) | done |
| Real board/pawn/dice/app-icon art | done -- see [GRAPHICS_TOOLING.md](GRAPHICS_TOOLING.md) |
| Multi-rule-set / house-rule variant system (3 variants, 8 toggles, Rules dialogue) | done -- this was the old table's "Phase 7 (future, unscheduled)" item; see [GAME_LOGIC.md](GAME_LOGIC.md) |
| AI opponents | done at one difficulty level; `EASY`/`HARD` unimplemented, no per-player difficulty picker |
| Save/load (5 fixed, renamable slots) | done (round 7.59 -- replaced an earlier drag-and-drop design, see round 7.58/7.59 notes below); not yet live-verified |
| App directory packaging (`!Run`/`!Sprites`/icon) | done |
| Credits/about screen | done (`splash_view.c`) |
| Audio (QTM music + SFX) | done and live-confirmed (rounds 7.60-7.84 -- 3 selectable background tracks + 6 one-shot SFX embedded in each track's MOD sample table, independently switchable via the Music menu's On/SFX toggles, stops cleanly on quit; `QTM_PlayRawSample` abandoned after 14 rounds, see [QTM.md](QTM.md)) |
| Full-screen enhanced graphics mode | **decided against** (round 7.56) -- staying with the present windowed mode, see below |
| Release (versioned zip, README screenshots, both Arculator profiles re-verified) | **not started** |
| Keezen variant (cards instead of dice) | unstarted idea, assessment only, not a commitment -- see the multi-rule-set plan's own "Phase 7" note |

### Decided against: full-screen enhanced graphics mode

Originally planned (a `archie-face`-based full-screen double-buffered,
VSync-synced board view during gameplay, returning to the desktop
otherwise -- see `CREDITS.md`'s Kieran Connell entry) but explicitly
dropped by the user in round 7.56 ("Want to drop the enhanced graphics
mode, like the present windowed mode") in favour of staying with the
current plain `Wimp_RedrawWindow`/`Wimp_UpdateWindow`-driven windowed
board view throughout, which has since had extensive animation/flicker
work of its own (rounds 7.10 onward) and is considered good enough on
its own merits. Left as a historical note, not deleted outright, in
case it's ever reconsidered -- `archie-face` itself remains a valid
reference if so.

### Music/SFX: QTM

QTM (`http://www.pi-star.co.uk/qtm/`, (c) Steve Harrison -- see
`CREDITS.md`) was chosen over `archieklang` (Kieran Connell's port of the
Amiga "AmigaKlang" soft synth) because it's the mature, proven,
WIMP-background-playing MOD player confirmed by its own author to run on
hardware "from the first 512kb, 8MHz, Arthur 0.30 A305" through modern
32-bit RISC OS -- i.e. this project's exact target range -- and it has a
full documented SWI API covering both background music
(`QTM_Load`/`Start`/`Stop`/`Pause`/`Volume`) and one-off sound effects
(`QTM_PlaySample`/`PlayRawSample`/`RegisterSample`) in one library.
`archieklang` is a more experimental soft-synth aimed at full-screen demo
contexts with tighter CPU control; not needed here. Per this project's
SWI-wrapper convention, QTM will be wrapped as `lib/qtm.c`/`qtm.h` with
its own `docs/QTM.md`, not called via raw `_swi()` inline.

### Phase 1 implementation notes

`src/game_view.c` (see [include/game_view.h](../include/game_view.h) for
the API) owns the one game window; `src/main.c` stays the thin
task-lifecycle shell (`Wimp_Initialise`, the `Wimp_Poll` loop, the iconbar
icon and its Quit menu, `Message_Quit`) and just dispatches
redraw/click events whose window handle matches `game_view_window_handle()`.

Window layout: a "Throw" button icon and an indirected-text status line
icon at the top (both real `wimp_icon`s, so the Wimp redraws them for
free), and a `BOARD_GRID_SIZE x BOARD_GRID_SIZE` board area below that's
entirely custom-plotted in the `Redraw_Window_Request` handler --
`board_layout.c` classifies every cell once at startup
(`build_cell_kinds()`), then each redraw fills every non-empty cell with a
flat `colourtrans_set_gcol`+`os_plot` rectangle and plots each pawn's
sprite (`osspriteop_put_sprite_user_coords`, falling back to a plain
coloured square if `assets/Sprites` fails to load) at its
`board_pawn_cell()` position. Clicking the board area converts the click's
screen coordinates to a board cell via the standard `box.x0 - xscroll` /
`box.y1 - yscroll` redraw-origin arithmetic (see the RISC OS 3 PRM's
`Wimp_RedrawWindow` entry, `~/riscos-dev/prm-mirror/wimp.html`) and moves
whichever of the current player's movable pawns (per
`ludo_movable_pawns()`) sits on that cell.

`assets/Sprites` is currently loaded via a bare relative filename
("Sprites"), which only works if it's sitting next to the running
`ArchiLudo,ff8` -- fine for `make deploy`'s flat hostfs layout, but will
need a real `!ArchiLudo` application-directory path once Phase 5 packaging
happens.

**This compiles and links cleanly under `arm-archie-gcc` with no warnings.**
Round 1 of manual Arculator testing (a real screenshot, not just a compile
check) found several real bugs, all now fixed -- recorded here since
they're exactly the kind of mistake worth not repeating:

- **Window wasn't draggable, resizable, or closeable.** `main.c`'s
  `main_dispatch()` didn't handle `Open_Window_Request`/
  `Close_Window_Request` at all -- per the RISC OS 3 PRM, the Wimp expects
  the *owning task* to confirm a reposition/resize/scroll by calling
  `Wimp_OpenWindow` back with the new state; a missing handler means
  dragging/resizing/scrolling visibly does nothing, even though the user's
  input reached the Wimp correctly. Fixed by handling both reason codes
  generically for any window (not just this app's own), and added
  `wimp_WINDOW_SIZE_ICON`/`wimp_WINDOW_TOGGLE_ICON` to the window flags so
  there's actually a resize/maximise icon to use.
- **Status line and Throw button text were unreadable.** Both icons'
  `flags` left the foreground/background colour bits unset, which defaults
  to Wimp colour 0 for both -- i.e. white text on a white fill,
  effectively invisible (the pale "barely readable" text in the
  screenshot was the Wimp's own icon border/fill anti-aliasing showing
  through, not the text itself). Fixed by explicitly OR-ing in
  `wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT` and
  `wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT`.
- **Felt sluggish.** Suspected cause: no `wimp_WINDOW_AUTO_REDRAW`, so the
  Wimp asks the client to fully redraw on every expose/uncover, not just
  genuine content changes.
- **Board didn't look like Mens Erger Je Niet at all.** The Phase 1 board
  shape was an invented square-ring-with-diagonal-home-columns design, not
  the real cross-shaped board -- see "Board layout" below for the fix
  (ported the GEOS edition's actual coordinates instead).

Also set the window ~30% smaller (11x11 grid instead of the old invented
15x15) and reworded the status line to be shorter.

**Round 2** (after adding `wimp_WINDOW_AUTO_REDRAW` above and re-testing)
surfaced a regression and two more real bugs from a second screenshot:

- **`wimp_WINDOW_AUTO_REDRAW` made the board disappear entirely**, while
  the Wimp's own icons (Throw button, status line -- drawn by the Wimp
  itself regardless of this flag) kept appearing correctly. This means
  `Redraw_Window_Request` stopped reaching this app's custom drawing code
  for a freshly-opened window once that flag was set. **Reverted it** --
  performance needs a different fix (fewer plot calls per redraw), not
  this flag; a slower-but-correct window beats a fast-but-blank one. The
  exact mechanism wasn't chased down further since the flag simply isn't
  worth the risk here.
- **Status text was truncated mid-word** ("GREEN to move: click T...").
  Root cause: the system font is a fixed 16 OS units per character (this
  was already documented in `riscos_wimp_reference.md`'s Text section --
  should have been checked *before* picking a window width, not after),
  and the status icon was only sized to fit the board's width (352 units
  = 22 characters), not the longest possible status message (~29
  characters = 464 units needed). Fixed by giving the window a minimum
  content width (528 units, comfortably fitting ~32 characters) independent
  of the board's own width, and centring the board within whichever of
  the two ends up wider.
- **Resize icon present but had no effect.** `xmin`/`ymin` were set equal
  to the window's natural full size, leaving no actual range to shrink
  into -- from the Wimp's perspective there was nothing to resize.
  Lowered both to half the natural size.

**Round 3** surfaced a harder-to-explain report: the ring path and two of
the four home columns weren't visible in a third screenshot, plus a
reported "endless loop of rerolls" after throwing a six with no pawn
movement happening. Unlike rounds 1-2, a careful re-read of
`build_cell_kinds()`/`game_view_redraw()`/`try_move_pawn()` against
`board_layout.c` (already unit-tested correct -- 169/169 checks, including
a distinctness check across every used cell) found no code-level
explanation for a partial-render pattern selective to specific
rows/columns -- every candidate hypothesis (colour contrast, redraw
rectangle clipping, draw-order overwrite, degenerate rectangle geometry)
either didn't fit the reported pattern or couldn't be confirmed from static
code alone. Given the project's own documented fallback for exactly this
situation ("file-based logging via `fopen`/`fprintf` is the fallback for
non-interactive tracing", see this file's Testing section and
`CLAUDE.md`), added a temporary `debug_log()` helper
(`src/game_view.c`) that appends to a plain-text `Log` file next to the
running app (same relative-path resolution as `assets/Sprites`), logging:
`build_cell_kinds()`'s per-kind cell counts (expect 40 ring / 16 home
column / 16 home base / 1 centre), every redraw's rectangle count and
bounds plus a per-kind count of cells actually drawn, and every board
click's computed work-area coordinates, resulting cell, the current
player's movable-pawn mask, and whether a move was found. **Remove this
logging once the round-3 bug is confirmed fixed** -- it's diagnostic
scaffolding, not permanent instrumentation.

Two independent, high-confidence fixes went in alongside the logging
(neither depends on the log's findings, so no reason to hold them back):

- **Ring cell colour was too close to the window background.**
  `wimp_COLOUR_VERY_LIGHT_GREY` (the window's `work_bg`) and the ring
  cells' chosen RGB (220,220,220) are close enough to plausibly render as
  near-invisible against each other on some palettes. Darkened the ring
  fill to (150,150,150) for real contrast regardless of exact palette.
- **The six-mandatory-release status message was indistinguishable from a
  genuine stuck-reroll state.** `ludo_roll()` on a six with a home pawn
  available performs a release, not a move -- `ludo_movable_pawns()`
  correctly returns 0 for *that* roll (the release *was* the action), so
  the old code fell into the generic `"%s rolled %d: Throw again"` branch,
  identical wording to what a genuinely-stuck player would see. Several
  sixes in a row (releasing up to 4 pawns before the player ever gets to
  pick one) is valid per the rules but reads exactly like an "endless
  reroll loop" with wording that gives no hint anything happened. Added a
  `game.just_released`-gated branch with distinct wording ("pawn out,
  Throw again") so a run of sixes is legible as forward progress, not a
  stuck state. Whether there's *also* a genuine pawn-movement bug (the
  "no movement of actual pawns done" part of the report) is exactly what
  the click-side logging above is for -- `try_move_pawn()`'s coordinate
  math was re-checked against `game_view_redraw()`'s and found
  self-consistent, but that's not proof against a runtime-only issue.

**Round 4**: a real screenshot this time (not a secondhand description),
plus the round-3 diagnostic `Log` file never actually appeared. Two
findings, both traced to the same root cause:

- **The ring path now rendered correctly (confirming round 3's contrast
  fix worked), but green's and blue's home columns (the horizontal cross
  bar) still rendered as plain ring grey instead of their pale tint, while
  red's and yellow's (the vertical bar) rendered correctly.** Re-checked
  `board_layout.c`'s actual current data (not from memory this time) and
  `build_cell_kinds()`/`game_view_redraw()` again -- still no code or data
  bug found (the 169-check distinctness test already rules out a cell
  collision). Left as an open question pending real log data (see below).
- **The `Log` file the user was asked to check simply didn't exist**, and
  the pawns were rendering as flat squares (the no-sprite fallback) rather
  than the circular placeholder art -- i.e. `assets/Sprites` was *also*
  silently failing to load, this whole time. Both point at the same root
  cause: a bare relative filename ("Sprites", "Log") doesn't reliably
  resolve against this program's own directory when launched this way (the
  assumption in round 3 that "Sprites" already loading proved relative
  paths worked was wrong -- it was never actually loading; the fallback
  render path just looks passable enough not to have been noticed).
  **Fixed properly**: `main()` now takes `argc`/`argv` (ArchieSDK's crt0
  populates `argv[0]` with the full RISC OS pathname the program was
  invoked as, e.g. `"HostFS:$.ArchiLudo"` -- see its `SDK/src/libc/crt0.s`
  and `argv.c`), and `game_view_initialise()` derives the program's own
  directory from it (truncate at the last `.` path separator) to build
  absolute paths for both `Sprites` and `Log`, rather than trusting the
  current selected directory at launch. This is the standard RISC OS
  convention for a program to find its own resources. Added one more
  targeted log line dumping the exact runtime `cell_kinds`/`cell_owner`
  classification of every cell on both cross bars (row 5 and column 5), so
  the green/blue mystery above gets a definitive answer from the next log
  instead of another round of guessing from source.
- **Also fixed** (not from a specific report, but adjacent and clearly
  wrong once noticed): the Throw button forced a full-window redraw on
  *every* click regardless of whether anything visible changed, causing a
  visible flash on every single roll -- reported as "redraws on every dice
  roll which is annoying". A roll only changes the board when a six
  releases a home pawn onto the ring; an ordinary roll only changes the
  status text, which `refresh_status()` already redraws via
  `wimp_set_icon_state()` without touching the board. Now the full-window
  force-redraw only happens when `game.just_released` is true.

**Round 5**: the round-4 fixes worked -- the `Log` file appeared this
time, and its `build_cell_kinds`/redraw-count dump proved something
important: green's and blue's home column cells were classified as
`CELL_HOME_COLUMN` with the correct owner, *and* the redraw loop drew all
16 home-column cells including theirs (matched the expected count exactly)
-- so the round-4 mystery was never a geometry or draw-loop bug at all,
it was a colour-matching issue. Two real findings from the round-5
screenshot, resolved:

- **Green's/blue's home column tint was too pale to survive
  `colourtrans_set_gcol`'s nearest-palette-entry approximation, while
  red's/yellow's survived.** The old tint formula was a 25%-player/
  75%-white blend (`(rgb+255*3)/4`) -- for green/blue specifically, pale
  enough that it's plausible the palette match collapsed onto (or very
  near) the same entry as the ring's grey. Switched to a 50% blend
  (`(rgb+255)/2`), clearly saturated regardless of exact palette. Added
  `set_gcol()`-returns-the-matched-GCOL + a targeted log line for every
  cross-bar cell so this is directly confirmable from the next log rather
  than inferred.
- **Pawns rendered as tall thin "bottle" shapes instead of circles, and
  the shape was wrong for a completely different reason than expected --
  not a sprite-tool bug** (the packed `assets/Sprites` round-tripped
  perfectly back to PNG with each player's correct colour, confirmed with
  `tools/riscos_sprite.py to-png` + a Pillow colour-count check before
  touching any RISC OS code). The real cause: RISC OS mode 15 (this
  project's chosen 256-colour screen mode, per `CLAUDE.md`'s `*Configure
  Mode 15` instruction) has 640x256 pixels at 1280x1024 OS units -- 2x4 OS
  units per pixel, i.e. pixels twice as tall as wide -- an inherent
  property of that *screen* mode, not something a matching sprite mode
  number can fix. **Switched the target screen mode to 13** (320x256
  pixels, same 1280x1024 OS units, 256 colours, genuinely square 4x4
  OS-unit pixels) and retagged every generated sprite to the matching
  square-pixel old-style mode for its bpp (`tools/riscos_sprite.py`'s
  `MODES_BY_BPP`: 1bpp=4, 2bpp=1, 4bpp=9, 8bpp=13 -- see
  `docs/GRAPHICS_TOOLING.md`'s "Round 5 correction"). `CLAUDE.md`'s Testing
  section now says `*Configure Mode 13`.
- **Also addressed** (explicit user request, not a bug): the board read
  as too small. Doubled `CELL` from 32 to 64 and `PAWN_SIZE` from 20 to 40
  (regenerated `assets/Sprites` at the new size); every other window
  dimension derives from `CELL` via the existing macros, so the whole
  board area roughly doubles without needing per-dimension changes.

**Round 6**: mode 13 turned out not to be selectable under the user's
actual Arculator monitor-type setup (`*Configure Mode 13` did nothing) --
confirmed mode 15 is simply the normal RISC OS desktop mode and reverted
the screen-mode target back to it, moving the non-square-pixel
compensation into the *source art* instead (see
`docs/GRAPHICS_TOOLING.md`'s "Round 6 correction" -- `tools/riscos_sprite.py`'s
`MODES_BY_BPP` is back to 0/8/12/15, and
`assets/generate_placeholder_art.py` draws its canvas pre-squished by
mode 15's own 2x4 OS-unit-per-pixel ratio). This also explained round 5's
"too high"/oversized pawns as a side effect: the round-5 sprites were
tagged mode 9 (4x4 OS units/pixel) at 40x40 raw pixels -- since a
sprite's on-screen OS-unit footprint is `raw_pixels x its own declared
OS-units-per-pixel`, that's a 160x160 OS-unit sprite, four times the
intended 40x40, regardless of the live screen mode. Fixed by the same
mode-15/pre-squish rework (40x40 OS units is now correctly 20x10 raw
pixels tagged mode 12).

**Also this round, per explicit user request**: reused GeoLudo's own pawn
and board-entry-marker artwork instead of hand-drawn placeholder shapes
(see `docs/GRAPHICS_TOOLING.md`'s "Round 6: reusing GeoLudo's own art"),
and redesigned the whole window layout to match GeoLudo's own screen
arrangement (board on the left, a status/controls panel on the right,
Throw positioned lower in the panel rather than ArchiLudo's earlier
invented top-header layout) -- resized for mode 15's OS units rather than
pixel-exact, since RISC OS's fixed-width system font needs more room per
character than GEOS's own font did. Board cells changed from solid
squares to circles (`os_plot`'s native `os_PLOT_CIRCLE`/
`os_PLOT_CIRCLE_OUTLINE`, mode-independent like the rectangle fills they
replaced) to match GEOS's actual look: hollow outline when an empty ring
cell, filled in the owning player's full colour for home-column "lane"
markers (shown at full saturation regardless of occupancy -- confirmed
against the reference screenshot, not a paler background tint as
ArchiLudo's own earlier design assumed) and for on-track pawns. Home base
cells no longer draw any background at all, matching GEOS -- just the
pawns sitting directly on the window background.

**Round 6.1**, from a real round-6 screenshot: the reused
`bm_gstart`/etc. board-entry-marker sprites rendered far too narrow --
puzzling, since the packed sprite's own metadata and a locally-simulated
stretch both looked correct beforehand (see
`docs/GRAPHICS_TOOLING.md`'s "Round 6.1" for the full investigation, which
didn't reach a root cause). Fixed pragmatically by dropping sprite
plotting for this element entirely: `plot_start_marker()` now draws a
filled circle plus a white `os_PLOT_TRIANGLE` arrow, both computed
directly rather than loaded from a sprite -- exactly matching what was
asked for ("should look like a normal round but filled in the
corresponding color and an arrow in it") and sidestepping the unexplained
scaling issue altogether. Also this round: removed the centre "finished
pawns" cell's permanent gold marker (explicit user request -- "why do we
have a colored mid circle as that circle is not part of any home
stretch"; it's now plain background like the home base cells, visible
only once an actual finished pawn sits there), and added a player-colour
swatch next to the name line (matching the small coloured box in GEOS's
own reference screenshot, deferred from the initial round-6 pass).

**Still not fully verified** -- these are all high-confidence fixes for
concretely-identified bugs (a real screenshot each round, not a
guess-and-hope pass) and a from-a-reference-screenshot layout redesign,
but can still only be truly confirmed by looking at it running again in
Arculator. The "pawns don't move" / pawn-click report from round 3 is
still open -- the round-6 `Log` file confirmed no click had actually been
attempted yet (`try_move_pawn`'s trace line never appeared), so this
remains unverified rather than confirmed broken. The Throw-button flicker
fix and six-release wording from round 4 haven't been re-confirmed
against this round's rewritten `refresh_status()`/`game_view_click()`
(behaviour preserved, believed still correct, but not yet re-verified
visually since the whole layout changed underneath them). Whether
on-track pawns should stay as plain filled circles (GEOS-authentic, per
round 6's screenshot-crop research) or show the detailed pawn sprite
instead is an open design question raised by the round-6 user report
("Pawn placement does not place a pawn, but just color fills the
circle") -- not yet resolved either way.

**Round 6.2 correction**: the "plain filled circle" conclusion above was
itself wrong -- a different reference screenshot
(`GEOS/screenshots/ludo-playerwon.png`, a later-game state with real
pawns visible on the ring and in home columns, as opposed to round 6's
early-game crop) makes it unambiguous that GEOS shows the detailed pawn
shape everywhere a pawn actually is. `plot_pawn()` now always draws it,
regardless of `in_play`.

**Round 6.3-6.4**: `plot_pawn()`'s sprite and a new `plot_dice()`
(GEOS's die-face icons, addressing a repeated "no dice shown" request)
both hit the same unexplained small-sprite rendering problem as round
6.1's board-entry markers -- see `docs/GRAPHICS_TOOLING.md`'s "Round 6.3"
and "Round 6.4" for the full investigation. Standardised on `os_plot`
primitives for all board/panel art; `game_view.c` no longer plots any
sprites at all.

**Round 6.4 -- the actual pawn-click bug, found**: the "pawns don't
move" report from round 3 turned out to be a real, simple bug, not a
coordinate-math error (which had been checked and re-checked across four
rounds without success). A debug log showing *every single click*
landing on the Throw icon, and *never* on the board no matter where the
user clicked, led to checking `wimp_window`'s `work_flags` field against
the RISC OS 3 PRM: *"A window definition uses the button type bits to
determine its work area's button type"* -- exactly like an icon's own
button type. This project's window was created with
`work_flags = wimp_BUTTON_NEVER`, meaning a click anywhere on the board
(which is custom-plotted directly onto the work area background, not
made of icons) never generated a `Mouse_Click` event at all. Changed to
`wimp_BUTTON_CLICK`, matching `ICON_THROW`'s own button type. This is
the single highest-value fix of the whole Phase 1 effort so far --
pawn movement was never going to work no matter how correct the
click-coordinate math was underneath it.

**Round 6.5**: with clicking finally working, a round of real gameplay
surfaced several rules/UX issues:

- **Pawns invisible on same-coloured backgrounds** (own home column lane,
  own ring entry marker) -- fixed with a black outline on every pawn
  (`plot_pawn()`: each fill circle is now preceded by a slightly larger
  black one, since `os_PLOT_CIRCLE_OUTLINE` only draws a fixed 1-pixel
  stroke, not an adjustable width).
- **"Pick a pawn" asked even when only one pawn could legally move**,
  including the already-narrowed forced-pawn case. Per explicit request
  ("if there is only one possible pawn that moves, don't ask which pawn
  should move. Only ask if more than one pawn can move"), added
  `single_movable_pawn()` and auto-move in `game_view_click()`'s
  `ICON_THROW` handler whenever `ludo_movable_pawns()` has exactly one
  bit set.
- **A forced pawn landing on another of the same player's own pawns left
  both standing there** instead of sending the earlier one home. Root
  cause in `game_logic.c`'s `capture_at()`: it explicitly skipped
  same-player pawns (`if (p == player) continue`), so only opponent
  pawns were ever eligible to be sent home. Per explicit house rule
  clarification, fixed to only exclude the pawn that just moved itself
  (added a `pawn_index` parameter so it can't send itself home), letting
  any *other* pawn -- same player or not -- on the landing square go home.
  New test: `test_own_pawn_sent_home_on_ring_collision`.
- **Found while implementing the above** (not user-reported, but a real
  latent bug): `ludo_move_pawn()` never reset `last_roll` when a six
  granted a bonus roll without ending the turn (only `ludo_end_turn()`
  did, for the turn-ending case) -- so `ludo_movable_pawns()` kept
  evaluating movability against the *previous* roll before the player had
  actually thrown again. Harmless as long as nothing acted on it, but the
  new auto-move logic depends on this being accurate, so fixed: that
  branch now also resets `last_roll = 0`.
- **Throw button "doesn't look right"**: compared against a real RISC OS
  reference (Steve Fryatt's `introducing-icons` tutorial screenshot,
  `~/riscos-dev/...` isn't local for this one, fetched directly) -- the
  icon *flags* already matched genuine dialogue-button convention
  (`wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_FILLED | HCENTRED |
  VCENTRED`), but real RISC OS buttons in the reference are compact, not
  oversized -- `THROW_WIDTH`/`THROW_HEIGHT` reduced 160x48 -> 120x40 to
  match that proportion (120 gives ~20 units padding either side of
  "Throw"'s 80-unit text width at the system font's fixed 16
  units/character; 40 tall is the system font's own 32-unit height plus
  a modest margin).
- **"Last line of die does not show"** -- not yet explained; re-reading
  `plot_dice()`'s geometry found nothing wrong (a plain DICE_SIZE-square
  box, no asymmetry between top/bottom). Added a debug log of the exact
  computed box each redraw, to check against the window's actual state
  next round rather than guess further.

**Round 6.6**: Throw button given genuine RISC OS press feedback -- the
user pointed at the classic R0-R7 icon border-style reference image and
asked specifically for R1 ("slab out", a raised 3D button look) at rest,
briefly switching to R2 ("slab in", sunken) on click, then back. Since
this needs the icon's *validation string* (the `R` command controls
border type, see `riscos_wimp_reference.md`'s Icons section) mutated at
runtime, `ICON_THROW` was converted from a plain 12-byte inline string to
an indirected icon with its own `throw_text`/`throw_validation` buffers;
a new `flash_throw_button()` rewrites `throw_validation` between "R1" and
"R2" around a short (~0.1s) deliberate busy-wait via
`os_read_monotonic_time()`, calling `wimp_set_icon_state(w, i, 0, 0)`
(a no-op flag change, purely to make the Wimp re-read and redraw the
icon's indirected data) after each change -- the same technique
`refresh_status()` already uses to refresh indirected *text*. This is a
timed flash rather than tied to the actual physical mouse-button release
(there's no separate down/up event in this project's plain `Mouse_Click`
handling) -- R5/R6 ("action button" types) would give a natively
press/release-synchronised highlight with zero app code, but colour-swap
highlighting rather than the specific slab-out/slab-in bevel switch that
was asked for, so weren't used here.

**Round 6.7**: fixed a genuine rules bug, not a misunderstanding -- a
finished pawn was rendering at an invented shared centre cell (5,5),
which the user correctly flagged as "plain wrong in rules". Checked
against GEOS's actual `homedestcoords[player][0..7]` (8 slots per
player: 0-3 home base, 4-7 home column, nothing beyond) rather than
assuming: there is no separate "finished" destination in the original
game at all -- a finished pawn simply stays on its home column's last
square. `board_pawn_cell()` fixed to clamp there instead of dispatching
to a `board_finished_cell()` that's now been removed entirely. Also
bumped `DICE_SIZE` and widened pip spacing for a reported "overlapping
pips" complaint on face 6.

**Round 6.8**: implemented player setup and a first AI opponent, per
explicit request (assess GeoLudo's own AI, feel free to improve, prepare
for future difficulty levels and rule variants). `src/setup_view.c` is a
new "New Game" dialogue window (writable name icon + click-to-toggle
Human/AI icon per player, Start/Cancel), reachable from a new "New Game"
iconbar menu entry; `src/ai.c` is a new, host-testable module (see
[AI.md](AI.md)) providing `ludo_ai_choose_pawn()`, adapted from
GeoLudo's `computerchoosepawn()` -- same overall shape (score every
legal move, pick the highest) but with the "landing on your own pawn"
scoring changed to match this project's own rule (round 6.5: sends the
earlier pawn home, not illegal like in GEOS), the danger/escape scoring
recomputed with real ring-square distance instead of GEOS's same-lap
coordinate shortcut (which isn't actually valid except by coincidence),
and an exact rather than proxied "is this the winning move" check.
`game_view.c` gained `game_view_configure_players()` and
`advance_ai_turns()`, which plays out consecutive AI turns automatically
(with a short pace_delay() and an immediate `redraw_now()` -- not
`wimp_force_redraw()`, which only schedules a redraw for the next
`Wimp_Poll` rather than showing anything right away -- after each roll
and each move, so a human watching can actually follow what an AI
opponent did) whenever it becomes an AI-controlled player's turn.
`LUDO_AI_EASY`/`LUDO_AI_HARD` are declared but not yet distinct from
`LUDO_AI_NORMAL` -- placeholders for later difficulty levels, see AI.md.

**Round 6.9**: added a splash/about window (`src/splash_view.c` +
`include/splash_view.h`), shown automatically once at startup and
reachable afterwards via a new "About" iconbar menu entry. Shows the
"idi8b" (I Dream In 8 Bits, the maintainer's own brand) logo plus
title/version/author/URL text, matching GeoLudo's own splash-screen
convention. Per round 6.4's still-unexplained sprite-plotting failures,
the logo is drawn as flat `os_plot` rectangles (`logo_rects[]`), not a
sprite, consistent with every other piece of ArchiLudo art.

Getting the rectangle data right took three attempts, documented here
since the dead ends are instructive:

1. First attempt: downsample `idi8b-logo-lowercase.png` (from the
   private `idreamtin8bits-astro` repo, checked out locally) via a
   one-off Python block-average + nearest-brand-palette-classify script.
   Hit two real bugs (classifying pre-composited-onto-white pixel
   colours instead of raw RGB shifted pale purple into misclassifying as
   blue; an antialiased noise row needed trimming from the grid) but
   otherwise produced a plausible-looking 42x16 logo.
2. Second attempt, prompted by the user asking to also check the ANSI
   and PETSCII source formats in the same asset folder: tried
   `idi8b-logo-ansi.ans` (CP437 block-character art, aliasing-free by
   construction, seemingly a cleaner source than an antialiased PNG).
   Produced a cleaner-looking 54x10 table -- but the user immediately
   caught that it had lost the design's distinctive thin vertical-bar
   strokes, flattened into wider blocks by the coarser character-cell
   grid. Reverted to the PNG-derived table.
3. Investigating why the PNG lost vertical-bar precision at all led to
   the real fix: the user pointed out the PNG **is a direct screen
   capture of the PETSCII source**, not independent artwork -- its
   apparent "curves" were never real, just PNG antialiasing of
   underlying blocky character-cell pixels. So the PETSCII source
   (`idi8b-logo-lowercase.petmate`, a "Petmate" editor save: a JSON grid
   of C64 screen-code + colour-index per character cell) is the true
   ground truth, and decoding it directly gives a pixel-exact result
   with zero antialiasing softness. Decoding it needs a real C64
   character ROM to turn each screen code into its actual 8x8 bitmap --
   guessing PETSCII glyph bit patterns from memory was considered and
   rejected as too error-prone for a silent wrong-shape bug. Found one
   locally: VICE (the emulator already used as this project's Arculator
   role model for headless-testing feasibility, see Testing below) is
   installed via apt but ships with no ROMs at all (Commodore's
   copyright, standard for open-source distros) -- but a full VICE **source**
   checkout at `~/svn-mirror/vice` (kept for unrelated reasons) bundles
   its *test-suite* ROM dumps, including
   `data/C64/chargen-901225-01.bin`, the standard C64 character ROM.
   Verified against a known glyph (screen code 1, lowercase charset,
   should render as the letter "a") before trusting it. Decoded the
   `.petmate` file's first 11 character rows (row 11 turned out to be a
   separate "IDreamtIn8Bits.com" byline in white-on-white, invisible,
   correctly excluded once found) into a 216x88 pixel grid, merged
   same-colour runs into 55 rectangles, then halved the resolution to
   108x40 (every coordinate happened to come out exactly even for this
   particular font's glyphs, so no precision was lost) to keep the table
   a reasonable size. This is the table actually shipped -- see
   `splash_view.c`'s `logo_rects[]` doc comment for the condensed
   version of this writeup at the point of use.

**Round 7**: a large batch of gameplay/UX feedback after the first real
play-test with an AI opponent, addressed together since most of it
turned out to share one underlying fix.

- **Iconbar click must ask for player details first.** SELECT-clicking
  the iconbar icon used to call `game_view_open()` directly, silently
  starting a default (all-human, unnamed) game -- per explicit report
  ("clicking on taskbar icon a game starts without asking game
  details"). Fixed with a new `game_view_has_started()` flag, set the
  first time `game_view_new_game()` actually runs (i.e. via
  `setup_view.c`'s Start button): `main.c`'s iconbar handler now opens
  `setup_view` instead of `game_view` until that's happened once, then
  reverts to the normal "reopen the game in progress" behaviour.
- **A menu inside the game window, not just the iconbar.** Per report
  ("do not see a menu bar in the main game screen yet") -- RISC OS
  convention is that a MENU click *inside* an app's own window opens its
  menu too, not only the iconbar icon. `main.c`'s dispatch now checks
  `wimp_CLICK_MENU` first, before routing by window, and opens the same
  shared menu regardless of which of this task's own windows (iconbar,
  game, setup, splash) the click landed in -- `Wimp_Poll` only ever
  reports a `Mouse_Click` for a window this task owns, so there's no risk
  of hijacking a click meant for another application.
- **AI turns needed their own pacing, not a fixed timer.** The prior
  design (`advance_ai_turns()`, a tight loop with a ~0.6s
  `pace_delay()` busy-wait between each roll/move) had two problems: no
  dice-roll animation at all, and no way for the human to actually pause
  it -- per explicit report ("AI play does not have any dice throw
  animation, nor waits for advancing" / "activate other button, reading
  continue, and only continue after pressing that"). Rebuilt as an
  explicit `turn_step` state machine (`STEP_IDLE` / `STEP_AWAIT_CONTINUE`
  / `STEP_ROLLING` / `STEP_MOVING`) driven by `Wimp_Poll`'s own
  `Null_Reason_Code` idle events (routed from `main.c` to a new
  `game_view_poll_idle()`) rather than a blocking busy-wait -- each
  animation tick is a non-blocking "has enough real time passed since the
  last tick" check, so the Wimp stays fully responsive (Continue clicks,
  window dragging, etc.) throughout. Whenever it's an AI-controlled
  player's turn, the Throw icon relabels itself "Continue" (same
  physical button, never two -- per explicit follow-up request) and
  every single step of that turn -- including the very first roll --
  waits for a click before proceeding, rather than only the *start* of
  the turn needing one.
- **Dice-roll and pawn-move animation.** `start_roll_animation()` rolls
  the die immediately (the real result is already determined, exactly as
  before) but holds the displayed face on a short cosmetic 1-6 cycle
  (`STEP_ROLLING`, ~0.5s) before revealing it. `start_move_animation()`
  likewise applies the move immediately but holds the pawn's *drawn*
  position at its old cell, sliding it to the new one by linear
  interpolation over a few redraws (`STEP_MOVING`) instead of the
  previous instant jump -- per explicit request ("animate the pawns
  actually moving to the new placement location"). Both apply uniformly
  to human and AI turns; only the single pawn actually mid-move gets the
  interpolated treatment in `plot_pawn()`, everything else draws
  normally.
- **AI status wording, not blank/stale text.** Per explicit request
  ("show the status messages for AI play like throw again etc."),
  `refresh_status()` now branches on `player_is_ai[]` throughout rather
  than only ever describing a human's turn -- "Click Continue" instead
  of "Click Throw", "AI is moving..." while its chosen pawn animates,
  and the same "Pawn released!"/"Throw again" wording a human would see
  on the equivalent roll.
- **Highlighting legal moves and a hover preview.** Per explicit
  request ("if multiple pawns are able to move, suggest a way to
  highlight possible moves. On hover over possible moves, highlight the
  destination"): `game_view_redraw()` now draws an outline ring around
  every currently-movable pawn whenever a human has more than one legal
  choice (single-choice rolls still auto-move immediately, so there's
  nothing to highlight then); a new `preview_destination()` helper
  mirrors `board_pawn_cell()`'s own steps-to-cell math read-only, and
  `game_view_poll_idle()` polls `Wimp_GetPointerInfo` at a coarser
  interval (throttled separately from the animation ticks) to detect
  when the pointer sits over one of those movable pawns and highlight
  the square it would land on.
- **Splash screen polish**, per explicit follow-up requests: text lines
  had zero gap between them ("too cramped without any pixel whiteline"),
  fixed with a `TEXT_LINE_GAP` added into each icon's vertical spacing;
  the version line was truncated to `major.minor.patch` ("would prefer
  full version string including timestamp"), now shows `VERSION` in
  full; the logo now sits on its own dark-grey card
  (`LOGO_BG_PAD`-sized rectangle plotted behind it) rather than directly
  on the window's light background, since the pastel brand colours read
  poorly against it -- a black *outline* around the logo itself was
  tried and rejected first (see the outline-comparison mockups from that
  round): 1px looked clean, but the user preferred no outline once the
  dark card made contrast unnecessary.

**Round 7.1**: follow-up polish after the first real play-test of Round
7's animation/Continue-button rework.

- **"Continue" clipped inside the Throw button.** `THROW_WIDTH` (120)
  was sized only for "Throw" (5 characters); "Continue" (8 characters)
  needs more room at the system font's fixed 16-units/character width.
  Bumped to 168, still comfortably inside `PANEL_WIDTH` (260).
- **Animations were re-plotting the *entire* board every tick.**
  `redraw_now()` always does a full `Wimp_RedrawWindow` pass re-running
  *all* of `game_view_redraw()`'s drawing code (121 cell markers, every
  pawn, the swatch, the die) -- fine for a one-off state change, but
  wasteful called on every single `ROLL_ANIM_TICKS`/`MOVE_ANIM_STEPS`
  animation tick, and visibly slow per report ("dice animation does
  redraw of whole window every scene instead of just the dice redraw" /
  "pawn movement also seems to do redraw every frame... only local
  redraw?"). Fixed with two new synchronous-but-narrow-scope redraw
  paths: `redraw_dice_now()` (used for every `STEP_ROLLING` tick) skips
  straight to `plot_dice()`, nothing else; `redraw_move_animation_area()`
  (used for every `STEP_MOVING` tick) computes the small bounding box of
  grid cells the moving pawn's path can touch (its old cell, new cell,
  plus a one-cell margin) and calls a new shared `draw_board_region()`
  helper -- the same cell-marker/pawn drawing code `game_view_redraw()`
  itself now calls (with the *whole* board as its range), just restricted
  to that box. Wimp_RedrawWindow itself has no "redraw just this
  rectangle" input (it always reports whatever's currently exposed on
  screen), so the saving here is entirely in how much drawing *code* runs
  per exposed rectangle, not in the number of rectangles -- correct and
  effective all the same, since the expensive part was always the ~150
  `os_plot` primitive calls, not the SWI dispatch itself. A deliberate
  scope limit: a *second* pawn's position changing as a side effect of
  the animating one's move (a capture sent home, a six-release) is
  outside the localized region and only reappears once the animation
  finishes and `after_settle()` does its own full redraw -- acceptable,
  since nothing asked for those to animate too.
- **Player-colour swatch got a black outline**, matching `plot_pawn()`'s
  existing outline technique (a slightly larger black rectangle plotted
  first, the colour on top) -- yellow in particular read poorly against
  the panel's light background with no border.
- **"A full menu bar with all of GEOS's menu options" -- checked, mostly
  already equivalent or not applicable.** Read
  `/home/xahmol/git/ludo/GEOS/src/main.c`'s `menuMain`/`menuGame`/
  `menuGEOS`/`menuFile` trees and their handler functions in full before
  answering rather than guessing from the item names alone:
  - `(Re)Start` (`gameRestart()`) re-prompts for player names via the
    same `inputofnames()` GEOS's initial start uses -- this is exactly
    what ArchiLudo's existing "New Game" already does, not a distinct
    "quick restart, same players" feature as the name might suggest.
  - `Color` (`gameColor()`) is a monochrome/colour *hardware* toggle,
    working around 8-bit machines that can't all guarantee colour
    output -- VIDC gives every real Archimedes model full colour under
    RISC OS 3.10 unconditionally, so there's nothing for an equivalent
    option to toggle (this exact reasoning is already recorded in this
    doc's GeoLudo->Wimp mapping table, under `monochromeflag`).
  - `Credits` (`informationCredits()`) and GEOS's own startup splash
    screen are, per `menus.c`'s `ShowCredits()`, *literally the same
    function* -- title, description, version, author, two URLs, an OK
    button -- with no separate deeper attribution list. ArchiLudo's
    `splash_view.c` already plays exactly that dual role (shown at
    startup, reachable again via the "About" menu entry), so adding a
    separate "Credits" entry would just duplicate it with no new
    content, not add real parity.
  - `Load`/`Save`/`Autosave` (the `File` submenu) are the one genuine
    gap: ArchiLudo has no save/load of any kind yet (`Directory
    layout`/Roadmap above already flags "Drag-based save/load still not
    done"). Per the GeoLudo->Wimp mapping table, the intended RISC OS
    equivalent is drag-and-drop `Message_DataSave`/`DataSaveAck` plus a
    Save dialogue, not GEOS's own directory-listing file picker -- a
    real feature to design and build, not something a menu entry alone
    can provide. Not added this round; flagged here for a deliberate,
    separate decision on when to build it, rather than adding a menu
    item that does nothing yet.

**Round 7.2**: drag-and-drop save/load, per explicit user request after
Round 7.1's GEOS-menu-parity review flagged it as the one genuine gap.
New `src/save_view.c`/`include/save_view.h` (two small dialogue windows,
Save and Load, each with a writable pathname icon -- type a path and
click Save/Load or press Return, exactly like a standard RISC OS Save
box's pathname field, no dragging required); the Save dialogue also has
a draggable "File" icon (button type `CLICK_DRAG`, per the RISC OS 3
PRM's icon button-type table -- a plain click reports `wimp_CLICK_SELECT`
same as any button, but holding it past the ~0.2s drag threshold reports
`wimp_DRAG_SELECT` instead, which is what actually starts the
`Wimp_DragBox` outline) for the idiomatic drag-to-Filer flow. Both
directions of the standard `Message_DataSave`/`DataSaveAck`/`DataLoad`/
`DataLoadAck` handshake (`riscos_wimp_reference.md`'s "Save protocol"/
"Load protocol" sections, curated from the PRM before writing any of
this) are implemented: dragging the File icon onto a Filer window (or
any other task's window) sends `Message_DataSave`; the reply's pathname
is where the file actually gets written; dragging an existing save's
Filer icon onto ArchiLudo's *game* window the other way triggers an
unsolicited `Message_DataLoad` (`your_ref == 0`), loaded directly with
no dialogue needed at all. `main.c` gained a `wimp_USER_DRAG_BOX` case
and routes every `wimp_USER_MESSAGE`/`_RECORDED` through
`save_view_message_received()` (a no-op for actions that aren't part of
this handshake).

The save format itself (`game_view_save_to_path()`/
`game_view_load_from_path()`, `src/game_view.c`) is a small fixed-layout
binary snapshot -- every player's configured name/AI setting plus every
field of `ludo_game` -- packed/unpacked byte-by-byte rather than a raw
`fwrite(&game, ...)` struct dump, since struct padding isn't part of any
documented contract. Uses the generic `&FFD` ("Data") RISC OS filetype
rather than a registered one, since double-click-to-open (`Message_
DataOpen`) would need `!Boot`-time filetype/application registration
this project doesn't set up -- dragging onto an already-running
ArchiLudo's game window works regardless and was judged sufficient for
this round; noted as a known limitation in `save_view.h`'s doc comment
rather than silently unsupported.

Checked against the actual GEOS menu handlers
(`fileLoad`/`fileSave`/`fileAutosaveToggle`, `GEOS/src/main.c`) before
writing anything: GEOS's own `Autosave` toggle has no ArchiLudo
equivalent yet (not asked for, and "autosave on every move" is a
separate design decision from "can save/load at all" -- left for later
if wanted, not assumed).

**Round 7.3**: the real cause of Round 7.1's local-redraw work still
visibly breaking, found in the PRM rather than guessed at, plus a
follow-up animation-quality request.

- **The actual bug: `Wimp_RedrawWindow` clears its own rectangles.**
  Per explicit user report ("animation now wipes the whole board without
  redraw"): the RISC OS 3 PRM states plainly that `Wimp_RedrawWindow`/
  `Wimp_GetRectangle` **automatically clear every returned rectangle to
  the window's background colour** before handing control back to the
  app ("the areas to be redrawn are automatically cleared... by the
  Wimp"). Round 7.1's `redraw_dice_now()`/`redraw_move_animation_area()`
  were calling exactly that SWI -- so every single animation tick first
  blanked the *entire* exposed window (there's no caller-supplied clip
  rectangle, see `draw_board_region()`'s doc comment) to light grey, and
  only that tick's narrow `plot_dice()`/`draw_board_region()` call ever
  got repainted, leaving the rest of the board blank until the next full
  redraw. The fix is `Wimp_UpdateWindow` instead -- the PRM's own
  documented use case is "temporary changes to the window, for example,
  when dragging objects," exactly this scenario -- which does not clear,
  preserving whatever was already on screen so the narrow per-tick
  drawing calls are genuinely enough. `game_view_redraw()` itself (the
  real `Redraw_Window_Request` handler) still correctly uses
  `Wimp_RedrawWindow`, where the auto-clear is the *wanted* behaviour
  (start from a clean slate after real window exposure).
- **Sprites investigated as an alternative, not adopted.** Before this
  fix was confirmed, the user asked whether switching the pawn/die art
  from `os_plot` primitives back to sprites might sidestep the wipe
  problem, and asked for research into a pixel-exact 17-sprite set (4
  pawns x 4 players + 1 die). Findings: sprites don't avoid the
  underlying issue -- `OS_SpriteOp`/`Wimp_SpriteOp` plotting still goes
  through the exact same `Wimp_RedrawWindow`/`UpdateWindow` protocol and
  still needs the same "redraw what's underneath" handling when a sprite
  moves (the same technique this project already uses for its `os_plot`
  circles); plotting sprites *outside* that protocol, directly to the
  screen on a timer, would violate RISC OS's cooperative-multitasking
  contract (another window/menu could legally be on top at any moment,
  and only the Wimp's redraw protocol coordinates who's allowed to touch
  which screen pixels when). More importantly, this project has three
  separate, still-unexplained sprite-plotting failures on record (see
  `docs/GRAPHICS_TOOLING.md`'s "Round 6.1"/"6.3"/"6.4" -- pawns solid
  black regardless of player, dice visibly cropped, markers too narrow,
  all independently verified correct offline every time) whose symptoms
  don't match this bug's signature (background-colour blanking) at all,
  meaning switching to sprites now would very likely reintroduce a
  *different*, still-unsolved risk for no benefit, since the actual wipe
  bug is already fixed independent of primitives vs. sprites. Sprites
  remain a legitimate Phase 2 investigation (see that section's "worth
  revisiting with a cleaner test harness" note) but weren't pursued this
  round.
- **Pawn-move animation now follows the real board track.** Per explicit
  follow-up request ("is it possible to follow the valid spaces track?"
  -- the previous straight-line interpolation cut diagonally across the
  board on longer moves instead of visibly hopping along the ring/home
  column). `start_move_animation()` now captures the pawn's steps count
  *before* calling `ludo_move_pawn()`, and builds a `move_anim_path[]` --
  one board cell per intervening step, via a new `cell_for_steps()`
  helper (the same ring/home-column dispatch `board_pawn_cell()` uses,
  generalised to an arbitrary explicit steps value rather than a pawn's
  own current one; `preview_destination()` -- the hover-highlight
  helper, round 7's own addition -- now just calls it too instead of
  duplicating the same logic). The animation interpolates through each
  segment of that path in turn (`MOVE_ANIM_TICKS_PER_CELL` ticks per
  square) rather than one straight line between just the two endpoints,
  so a six-square move visibly traces the actual track. The localized
  redraw region (`redraw_move_animation_area()`) now bounds *all* path
  cells, not just the two endpoints.

**Round 7.4**: a "Load" button added to `setup_view.c`'s "New Game"
dialogue, per explicit request ("the new game dialogue needs a button to
optionally load a previously saved game"). Doesn't duplicate any load
logic -- clicking it just closes the setup dialogue and opens
`save_view.c`'s existing Load dialogue instead, which already restores
its own player names/AI settings from the save file
(`game_view_load_from_path()`), so there's nothing setup-specific left
to configure once a save is loaded. Widened the dialogue's `WINDOW_WIDTH`
calculation to take the max of the player-rows width and the (now
three-button) Start/Load/Cancel row's width, rather than the previous
fixed width that only accounted for the rows above.

**Round 7.5**: Round 7.3's `Wimp_UpdateWindow` fix turned out to be
wrong too -- per explicit follow-up report ("animation is not visible
anymore, only end and begin state"), it silently produced no visible
intermediate frames at all (start and end states came from other,
unrelated full redraws -- `resolve_roll()`'s fallback path and
`after_settle()` -- not from the animation itself). Rather than guess a
*third* time at which synchronous `Wimp_RedrawWindow`/`Wimp_UpdateWindow`
call is "the right one," per the user's explicit request ("research
alternatives: sprites or a real animation that does not completely
redraw on every frame") this was rethought from the actual RISC OS Wimp
architecture rather than patched again:

- **Sprites don't change this problem.** However the pawn/die art is
  drawn, moving it still requires redrawing whatever's underneath the
  old position -- sprites carry no "moves without needing a repaint"
  property, and plotting them *outside* the Wimp's redraw protocol
  (e.g. directly on a timer) would violate RISC OS's cooperative-
  multitasking contract, since another window or menu can legally be on
  top of any part of the screen at any moment and only the Wimp's own
  redraw protocol coordinates who's allowed to touch which pixels when.
- **The actual fix: stop calling Wimp_RedrawWindow/Wimp_UpdateWindow
  directly from inside a tick at all.** Both of Round 7.1's and 7.3's
  attempts tried to synchronously force a redraw pass mid-tick -- fighting
  the Wimp's protocol from two different wrong angles (auto-clear
  scoped too broadly; then a call whose real behaviour didn't match what
  the animation needed). The correct, PRM-standard technique -- and one
  this exact codebase already used successfully *before* any of this
  animation work existed, for `try_move_pawn()`'s panel-only redraw --
  is `Wimp_ForceRedraw` with a small work-area rectangle, then letting
  the Wimp deliver a genuine `Redraw_Window_Request` back through the
  normal `Wimp_Poll` loop on its own schedule, handled by
  `game_view_redraw()` completely unchanged. `Wimp_RedrawWindow`'s
  auto-clear (Round 7.3's finding, still correct) is exactly right for a
  *genuine* redraw request -- the OS, not this code, decides how much of
  the window that pass actually needs to touch, clipped to the
  requested rectangle when nothing's obscuring it. `game_view_redraw()`
  can keep drawing the whole board's worth of `os_plot` calls every
  time without that being expensive for a small forced rectangle, since
  plot calls for content outside whatever VDU graphics window the Wimp
  sets up are cheap no-ops. `redraw_move_animation_area()`/
  `redraw_dice_now()` were replaced with `mark_move_animation_area_dirty()`/
  `mark_dice_area_dirty()` -- both now just call `wimp_force_redraw()`
  with a computed work-area box (a new `cell_range_to_work_box()` helper
  converts a board-cell range to that box, the inverse of
  `cell_centre()`'s per-cell math) and return immediately; the actual
  drawing happens on the next `Wimp_Poll` iteration, which given this
  project's tight poll loop is imperceptibly fast, not a visible delay.

**Round 7.6**: the Save dialogue's draggable icon (Round 7.2) turned out
not to be discoverable -- per explicit report ("thought we were going to
implement icon dragging and dropping? It now asks just the full
filepath"). The drag functionality itself was already implemented and
correct (re-verified against the code, not just assumed); the real
problem was presentation: a plain 40-unit-square icon labelled "File"
doesn't read as "drag me" at a glance, and at that width even that
short label was clipped. Relabelled "Drag" and widened to 64 units so
the label actually fits and the intent is legible.

**Round 7.7**: the player-colour swatch's black outline had an
invisible top border -- per explicit report, correctly self-diagnosed
("perhaps as every two OS lines is one actual screen line?"). Mode 15
is 2x4 OS units per physical pixel (non-square, see this doc's Testing
section) -- a manually `fill_rect()`-drawn border needs at least 4 OS
units of thickness to reliably render (a rasterizer painting a pixel
when its centre falls inside the filled shape only guarantees a hit at
one full pixel's thickness; thinner can land entirely between two
pixel-centre samples and paint nothing), and the swatch's outline used
only 2 -- below the 4-unit vertical minimum, though *above* the smaller
2-unit horizontal one, which is exactly why the top/bottom edges
specifically vanished while the left/right edges still showed. Fixed by
raising it to 4.

Audited the rest of `src/*.c` for the same pattern (a fork agent, since
this seemed likely to explain other rendering reports too, per explicit
user request to "document... including clear strategy to avoid these
issues"): `plot_pawn()`'s outline (`PAWN_SIZE/12` = 4) already satisfied
the rule, which is why pawns never showed the problem, but only by
coincidence of the current `PAWN_SIZE`; `plot_dice()`'s border
(`DICE_SIZE/16` = 4.5, truncating to 4) was *also* only safe by luck of
integer truncation -- a different `DICE_SIZE` would have silently
dropped below the minimum with no warning, so it now has an explicit
`if (border < 4) border = 4;` floor rather than relying on that
coincidence. `setup_view.c`/`save_view.c` have no manual `fill_rect`
borders at all (every border there is a standard Wimp-drawn 3D bevel).
Left genuinely open: `outline_circle()` (`os_PLOT_CIRCLE_OUTLINE`, used
for the ring-track markers and the movable-pawn/hover highlight rings)
is documented in-code as a native "fixed 1-pixel line" stroke, assumed
immune to this bug class (rasterizes at the physical pixel level rather
than as an OS-unit-thick filled shape) -- a standard, conventional
assumption for RISC OS stroke primitives, but not independently proven
against the PRM's `os_plot` reference, which documents the plot code's
existence without spelling out its rasterization guarantee. If a ring
or highlight circle is ever reported as patchy or partially invisible,
revisit this specifically rather than assuming it's automatically safe.

The general rule and this incident are now recorded in
`riscos_wimp_reference.md`'s new "Screen modes: non-square pixels and
thin manually-drawn lines" section (canonical copy updated at
`~/.claude/riscos_wimp_reference.md` first, per the usual convention)
and in this project's own auto-memory, specifically so a future session
checks line/border thickness *first* the next time something like this
gets reported, rather than re-deriving the whole non-square-pixel
mechanism from scratch or chasing an unrelated coordinate-math theory.

**Round 7.8**: a genuine rules bug, found via headless simulation rather
than guessed at from a single manually-reported symptom -- per explicit
report ("green pawn threw two and was one before end of home area...
should be invalid move, but moved 1 place anyway") plus a request to
check whether the debug Log already captured enough to diagnose it (it
didn't -- see below) and whether a *full* headless game could be
simulated at all to hunt for this systematically.

- **The debug Log only ever captured human board clicks**
  (`try_move_pawn()`'s own `debug_log` calls) -- every AI move and every
  human *auto*-move (the "don't ask which pawn" single-choice case)
  went through `start_move_animation()` with no logging at all. Given
  the reported save file's players were Human/AI/AI/AI, and the actual
  supplied Log had zero `try_move_pawn`/`MOVING pawn` entries anywhere
  near the report, the move in question almost certainly happened via
  one of the un-logged paths. Fixed by moving the log call to
  `start_move_animation()` itself (the one function every move --
  human click, human auto-move, AI -- funnels through), logging
  player/human-or-AI/pawn/roll/steps-before/steps-after; also logged
  `resolve_roll()`'s movable-mask decision point. The old
  per-animation-tick `plot_dice()` log line (added chasing the
  since-resolved "last line of die" report, see Round 7.7) was removed
  entirely -- at roughly 8 lines per throw it had come to dominate the
  277KB Log file's size and made the entries that actually mattered
  hard to find, which is exactly what slowed down locating the real bug
  here.
- **Verified game_logic.c itself first, with a real headless
  simulation** (`tests/test_game_logic.c`'s new
  `test_headless_full_games_invariants()`, and `tests/test_ai.c`'s new
  `test_headless_four_ai_games()` using the actual AI, matching the
  reported save's all-but-one-seat-AI setup, per explicit follow-up
  request) rather than trying to manually reconstruct the exact
  reported board position: both play out hundreds of complete, real,
  randomly-rolled games through nothing but the public
  `game_logic.h`/`ai.h` API (exactly what `game_view.c` itself does),
  asserting after every single roll and move that no pawn's steps ever
  goes out of range, `finished` always agrees with
  `steps == LUDO_TOTAL_STEPS`, the finished-pawn count never decreases,
  and -- the check that would catch the reported bug directly -- a
  pawn reported movable by `ludo_movable_pawns()` never actually
  overshoots when moved. `test_game_logic.c` alone runs ~9.5 million
  checks across 500 games in well under a second.
- **The first version of this simulation immediately found a
  failure** -- but it turned out to be a bug in the *test*, not
  `game_logic.c`, and finding that distinction is exactly what
  `game_logic.h`'s own `ludo_roll()` doc comment already warns about: a
  caller must "keep rolling while `ludo_movable_pawns()` is 0 and the
  current_player has not changed." Three consecutive failed tries makes
  `ludo_roll()` silently call `ludo_end_turn()` *internally*, advancing
  `current_player` to a different player and resetting `last_roll` to
  0 -- a fresh, not-yet-thrown state for them -- before returning. The
  test's first draft (and, critically, `game_view.c`'s real
  `resolve_roll()`) trusted whatever `ludo_movable_pawns()` said
  immediately after any roll, without checking whether the roll had
  actually landed on the player it started with. With `last_roll == 0`,
  `compute_movable_pawns()`'s overshoot check (`new_steps =
  p->steps + g->last_roll`) can never trigger -- adding zero never
  overshoots -- so *every* in-play pawn of the new player reports as
  movable, and `resolve_roll()` would go on to auto-move (or, for a
  human with more than one such pawn, invite a click on) a pawn that
  player never actually threw a die for, moving it by zero net steps.
  This is the real mechanism behind the reported bug (a pawn parked
  one square from finishing, "moved" without a valid roll actually
  landing on it) even though the exact manually-observed board position
  couldn't be independently reconstructed from the supplied save file
  (which only captures state *at* save time, not the move history
  leading up to it) or the Log (which, per above, didn't capture the
  move at all). Fixed in `game_view.c`'s `resolve_roll()` by capturing
  who was actually rolling (`roll_anim_player`, set in
  `start_roll_animation()` *before* calling `ludo_roll()`) and checking
  it against `game.current_player` first thing in `resolve_roll()` --
  if they differ, the turn passed automatically and nothing was rolled
  for whoever it is now, so settle straight into their own fresh "click
  Throw"/"click Continue" state instead of resolving a phantom move.
  The same fix was applied to both headless simulations' own roll loops
  (their first drafts had exactly this same bug, which is what
  surfaced it) so they now correctly model the real, fixed calling
  convention rather than the old broken one.
- **This is the first genuinely rules-breaking bug this project's
  `game_logic.c` test suite didn't already have direct coverage for**,
  despite `test_three_failed_tries_passes_turn` existing (round 0/1)
  and testing that the turn *does* pass after three tries -- it just
  never went on to check what a caller does with the state immediately
  afterward. The headless full-game simulations are now permanent
  members of the test suite (`make test`), not one-off debugging
  scripts, specifically so a class of bug like this -- one that only
  shows up from a specific sequence of many turns, not any single
  hand-constructed board position -- has an automated, fast, always-run
  chance of getting caught before it reaches Arculator at all.

**Round 7.9**: the Save dialogue's drag-to-Filer flow, revisited after
Round 7.6's relabelling turned out not to be the real problem -- per
explicit follow-up report ("save is still not a draggable icon where
path changes to where you drag it to"). Two real issues found on
re-reading the code, not just re-asserting it was already correct:

- **A successful drag-save gave zero visible feedback.**
  `save_view_message_received()`'s `Message_DataSaveAck` handler wrote
  the file and replied `Message_DataLoad` correctly, but never touched
  the dialogue window afterward -- no pathname field update, no closed
  window, nothing -- so from the user's side, dragging the icon onto a
  Filer window looked exactly like nothing had happened at all, even
  though the file was in fact being written. Fixed to match the real
  RISC OS Save-box convention (a successful drag reflects the resolved
  path into the pathname field) and, since this dialogue also serves as
  a direct type-a-path Save tool, closes the window afterward -- the
  same unambiguous "done" feedback the typed-path Save button already
  gave.
- **The `wimp_drag` struct passed to `Wimp_DragBox` had uninitialised
  stack fields.** `handle`/`draw`/`undraw`/`redraw` are documented as
  only meaningful for the ASM_FIXED/ASM_RUBBER drag types (8-11), not
  `wimp_DRAG_USER_FIXED` (5) used here, but leaving them as garbage
  rather than explicitly zeroed was an unnecessary risk in a struct
  handed straight to a SWI -- fixed with a `memset` before filling in
  the fields that matter.

**Round 7.10**: the animation flash still visible after Round 7.5's fix
-- per explicit follow-up report ("animation was already a lot better,
but you still see brief redraw of all windows") and a request to
research whether flicker-free small-region WIMP animation is possible
at all, checking tutorials and stardot.org.uk.

Researched properly this time (a research agent, checking the local
PRM/wimp-prog mirrors, Steve Fryatt's site, stardot.org.uk, and reading
the actual source of a real RISC OS board game,
`github.com/marutan/ro-chess`, rather than guessing) rather than trying
a third variant blind. Findings:

- **`Wimp_UpdateWindow` is the correct, PRM-sanctioned technique for
  exactly this** ("the rectangles to be updated are not cleared by the
  Wimp first... this can be called at any time, not just in response to
  a Redraw_Window_Request" -- ~/riscos-dev/prm-mirror/wimp.html). The
  PRM explicitly warns *against* the alternative (plotting directly to
  the screen outside the redraw protocol entirely) for exactly this
  drag/animation scenario, for window-occlusion/multitasking-correctness
  reasons.
- **Round 7.3's earlier attempt at this failed for a findable, mundane
  reason, not because the technique doesn't work**: unlike
  `Wimp_RedrawWindow` (where only `.w` is meaningful on entry -- the
  Wimp computes the rectangle itself), `Wimp_UpdateWindow` takes the
  rectangle as *input* (`w, x0, y0, x1, y1`). That attempt left
  `redraw.box` as uninitialised stack garbage before the call, so the
  Wimp had no valid area to report back and the drawing call inside
  `while (more)` never ran -- read as "no visible frames at all," which
  is exactly what happened.
- **Confirmed against real, shipped example code**: `ro-chess`'s
  `icon_update()` helper sets its redraw block's box to the icon's own
  work-area bounds before calling `Wimp_UpdateWindow`, then plots
  inline in the same `while (more)` loop -- the exact pattern now used
  here. `ro-chess` uses this for its own periodic small-region animation
  (a flashing selected-square highlight, driven by a ~50-centisecond
  timer) and, tellingly, never calls `Wimp_ForceRedraw` anywhere in its
  source at all.
- **stardot.org.uk turned up nothing relevant** -- the matching threads
  are all BBC Micro/Electron bare-metal screen-memory double-buffering
  discussions, not WIMP desktop programming. Reported as a genuine
  "nothing found," not a gap in the search.
- **A second candidate fix was found and rejected**: a window's
  `work_bg` colour can be set to `wimp_COLOUR_TRANSPARENT`, which
  disables the Wimp's auto-clear for *all* redraws of that window,
  requiring no other code change. Rejected after checking
  `draw_board_region()` directly: it explicitly skips `CELL_EMPTY`
  cells (`if (kind == CELL_EMPTY) continue;`) -- the home-base corners
  and gaps between board cells -- relying entirely on the Wimp's own
  background clear to keep those areas correctly grey. Going
  transparent would leave them showing stale content (whatever was
  underneath before) the next time a real window exposure happens, e.g.
  another window dragged across and away -- a real correctness
  regression for a flicker fix that only needed to apply to two small,
  specific animation-tick paths anyway.

Fixed by properly reimplementing `Wimp_UpdateWindow` usage in both
animation-tick functions (renamed `mark_dice_area_dirty()`/
`mark_move_animation_area_dirty()` -> `update_dice_area()`/
`update_move_animation_area()`, since they now redraw synchronously
again rather than merely marking a rectangle dirty for later): the
work-area box is computed exactly as before (`cell_range_to_work_box()`,
`DICE_CENTRE_X`/`DICE_SIZE`) but now assigned into `redraw.box` as
*input* before the call, with the actual drawing (`draw_board_region()`/
`plot_dice()`) done inline in the `while (more)` loop, mirroring
`game_view_redraw()`'s existing structure and ro-chess's real pattern.
`game_view_redraw()` itself (the genuine `Redraw_Window_Request`
handler) is untouched, still correctly using `Wimp_RedrawWindow` --
its auto-clear is exactly what's needed there, for the empty-cell gaps
`draw_board_region()` doesn't paint itself.

**Round 7.11**: flashing movable-pawn/hover highlight rings, per explicit
request after reviewing `github.com/marutan/ro-chess`'s source for
design inspiration (a real, shipped RISC OS board game). Its selected-
square highlight (`hilite_do()`) pulses on a steady timer rather than
sitting static -- the same underlying need ArchiLudo's movable-pawn
rings and hover-destination ring already serve, just drawn once and left
alone. Implemented with the same `Wimp_UpdateWindow` technique Round
7.10 just established: `draw_highlights()` (the ring-drawing logic
itself, extracted out of `game_view_redraw()` unchanged) draws nothing
at all when a new `highlight_flash_on` flag is momentarily false;
`update_highlight_area()` (mirroring `update_dice_area()`/
`update_move_animation_area()`'s shape) redraws just the bounding box
of every currently-relevant cell -- each movable pawn's cell plus the
hover-destination cell -- on every flash toggle (`HIGHLIGHT_FLASH_CS`,
matching ro-chess's own ~50-centisecond cadence), driven from
`game_view_poll_idle()` alongside the existing hover-poll and animation
ticks. The board/pawn content underneath is redrawn on every toggle
regardless of flash phase, on or off -- since `Wimp_UpdateWindow`
doesn't clear anything, that's what actually erases the previous
frame's ring when the flash switches off. The flash state resets to a
fresh, fully-visible phase whenever a new multi-choice highlight
situation begins (`resolve_roll()`'s "wait for a pawn click" branch),
rather than continuing whatever phase an unrelated earlier flash cycle
happened to be in.

**Round 7.12**: two more `Wimp_UpdateWindow` follow-up bugs, both from
the same underlying cause -- per explicit report ("old pawn position is
not restored to old state in interim frames", "after animation is
done, there is still a full redraw, is that needed?", "pulsing
animation... does not pulsate").

- **Missing erase-before-draw.** `Wimp_UpdateWindow` deliberately
  doesn't clear anything (that's the whole point, see Round 7.10) --
  but that means *this code* is now responsible for erasing whatever
  the previous tick drew, and it wasn't. A pawn mid-slide draws at an
  interpolated position between two grid points; nothing else ever
  paints that exact spot (`draw_board_region()` only paints cell
  markers at grid points), so the previous frame's pawn image was never
  actually erased -- each tick just added another circle, leaving a
  visible trail along the path. The exact same missing-erase issue is
  almost certainly *also* why the new flashing highlight rings (round
  7.11) didn't visibly pulsate: the ring's own radius extends past the
  marker/pawn underneath it, so nothing ever fully erased the "on"
  phase's ring during an "off" phase -- it likely looked like a
  permanently-drawn (if slightly patchy) ring throughout, never truly
  toggling off. Fixed with a new `fill_window_background()` helper
  (`Wimp_SetColour` + a plain fill, not a hand-picked RGB, so it always
  matches the real desktop colour scheme) called at the top of every
  `Wimp_UpdateWindow`-based redraw pass (`update_move_animation_area()`,
  `update_highlight_area()`) before the real content is drawn on top --
  `update_dice_area()` didn't need this, since `plot_dice()` already
  unconditionally paints its entire box opaque every call, leaving no
  gaps to begin with.
- **The end-of-animation settle still flashed.** `after_settle()`'s
  final `redraw_now()` was still calling `game_view_redraw()`, i.e.
  still `Wimp_RedrawWindow` under the hood -- meaning every single
  action (a roll settling, a move finishing) ended with exactly the
  same auto-clear flash the whole rest of this animation work exists to
  avoid. Not actually needed: `draw_full_window_content()` (the board/
  highlights/swatch/dice drawing code, extracted here out of
  `game_view_redraw()` so both share it) already repaints every element
  unconditionally on every call regardless of what changed, so there's
  nothing gained by letting the Wimp clear first. `redraw_now()` now
  runs its own `Wimp_UpdateWindow` loop over the whole window extent
  instead of delegating to `game_view_redraw()`. Genuine window
  exposure (`main_dispatch()`'s `wimp_REDRAW_WINDOW_REQUEST` case) is
  completely unaffected, still `Wimp_RedrawWindow` via
  `game_view_redraw()`, where the auto-clear is exactly what's wanted.

**Round 7.13**: three more bugs from the same live play-test session,
per explicit report -- a ghost-pawn rendering regression, a genuine
rules off-by-one, and (once the first two were fixed and shipped) a
follow-up report that the move-slide animation still visibly flickered.

- **Ghost pawns from `redraw_now()`'s missing erase step.** Reported as
  a screenshot: a player with exactly 4 pawns showing 6 red
  circles on screen -- 4 correctly in the home base plus 2 stale
  leftovers elsewhere on the board. Root cause: Round 7.12's
  `redraw_now()` conversion from `Wimp_RedrawWindow` to
  `Wimp_UpdateWindow` got the same treatment as
  `update_move_animation_area()`/`update_highlight_area()` *except* the
  actual `fill_window_background()` call was missed in `redraw_now()`
  itself -- a plain oversight in that commit, not a new mechanism.
  Once a pawn is captured or otherwise moved off a ring square,
  `board_pawn_cell()` never reports that square as occupied again, so
  nothing ever repaints real content over the old, larger, solid pawn
  circle -- the thin grey ring-track outline drawn there is too small to
  cover it. Fixed by adding the same erase call already present in the
  other two functions.
- **Home-column finish off-by-one.** Reported via Log: a pawn with only
  three home-column squares left to travel was allowed to move on a
  roll of four. `LUDO_TOTAL_STEPS` (`include/game_logic.h`) was defined
  as `LUDO_RING_LENGTH + LUDO_HOME_COLUMN_LENGTH`, treating "finished"
  as a square *past* the home column's own 4 squares. Re-checked
  directly against the actual GEOS source this time, rather than
  trusting an earlier docs summary:
  `/home/xahmol/git/ludo/GEOS/src/gamelogic.c`'s `turngeneric()` move
  validity check is `if(vn>7) { gv=1; }` -- GEOS's home-track positions
  are 4..7 (`homedestcoords[player][4..7]`, 4 squares), and anything
  past 7 is rejected outright, meaning position 7 -- the *last* of the 4
  squares -- is simultaneously "as far as you can go" and "finished",
  with no separate square beyond it. This is a distinct bug from Round
  7.8's (that one was a turn-passing state-machine bug; this one is a
  fundamentally wrong step-counting boundary, present since the engine
  was first written) -- confirmed distinct by checking that the reported
  failure (steps=40, roll=4, reaching steps=44) exactly matched the
  *old, buggy* `LUDO_TOTAL_STEPS`'s own "exact landing" rule, i.e. the
  code was internally consistent with a boundary that was simply wrong
  by one from the start. Fixed by changing the macro to
  `LUDO_RING_LENGTH + LUDO_HOME_COLUMN_LENGTH - 1`; all downstream logic
  in `game_logic.c`/`ai.c`/`board_layout.c` references the macro
  symbolically, so needed no code changes, only comment corrections.
  Per explicit user instruction to update test coverage alongside any
  logic-flaw fix: every existing test already asserted against
  `LUDO_TOTAL_STEPS` symbolically (`test_overshoot_not_movable`,
  `test_pawn_finishes_exactly`, `test_winner_detected_when_all_pawns_finish`,
  `test_one_short_overshoot_not_movable`, plus both headless full-game
  simulations' invariants), so no test *logic* needed changing -- `make
  test` re-run afterwards to confirm all ~10.7 million checks across the
  three suites still pass against the corrected boundary, which they do.
- **Move-slide animation redrawing a much larger area than the moving
  pawn.** Reported after the above two fixes shipped: the slide still
  visibly flickered, worse than expected for a single small pawn.
  `update_move_animation_area()` was unioning *every* cell of the whole
  move's path (`move_anim_path[]`, up to 6 cells for a roll of six) into
  one bounding box, and erasing/repainting that entire box on every
  single animation tick -- for a roll that crosses a ring corner this
  can span a large fraction of one side of the board, not just the
  pawn's immediate surroundings. But `plot_pawn()`'s own interpolation
  only ever places the pawn between `move_anim_path[seg]` and
  `move_anim_path[seg + 1]`, the *current* segment -- every other cell
  in the path is irrelevant to this frame. Fixed by computing the
  redraw box from just the current segment's two cells (plus the
  existing one-cell margin). Confirmed this still fully covers the
  previous tick's position (which this call's erase step must also
  cover): within a segment the interpolated position only ever moves
  between the segment's two endpoints, and at the tick where the
  segment itself advances, the previous tick's position was already
  sitting right next to the new segment's own starting cell (at most
  `1/MOVE_ANIM_TICKS_PER_CELL` of a cell away from it) -- well inside
  the existing margin.

**Round 7.14**: AI scoring fix, per explicit user report ("AI does not
seem to prioritise moving pawns in destination home area further to the
end"). A home-column advance had no capture/danger heuristic to earn
points from (correctly -- the home column is off-limits to opponents),
so it only ever scored the same flat per-step progress term as any
other move, routinely losing out to minor ring-tactic bonuses on some
other movable pawn. Fixed by adding an explicit, appropriately-sized
incentive for safely advancing within the home column (still losing to
an actual capture). Full writeup, weight table, and the two new tests'
worked-out scoring: [AI.md](AI.md)'s "Round 7.14" section.

**Round 7.15**: two more polish items, per explicit user report after
confirming Round 7.13's animation-scoping fix looked right ("animation
redraws now much better, pulsating ring now also works. Only thing
still remaining: there is still a full redraw of every window when turn
is finished waiting on throw or continue button... Also can we show the
buttons only when user is supposed to click on it, not in between?").

- **The turn-settle redraw was unconditional and full-window, even
  though it usually had nothing new to show.** `after_settle()` (shared
  by `resolve_roll()` and `resolve_move()`, i.e. called at literally
  every single turn transition) unconditionally called `redraw_now()`
  over the *entire* window. But by the time it runs, the board almost
  always already looks correct: an ordinary move's final resting
  position was already painted by the last animation tick
  (`update_move_animation_area()`), and a roll with no release doesn't
  touch the board at all -- only a capture, an own-pawn collision, or a
  six's mandatory release actually changes anything *else* on the
  board, and none of those are the common case. Fixed with a snapshot/
  diff mechanism: `snapshot_pawn_positions()` records every pawn's
  `board_pawn_cell()`/`in_play` state immediately before the
  state-changing `ludo_move_pawn()`/`ludo_roll()` call (called from
  `start_move_animation()`/`start_roll_animation()`), and
  `update_settle_diff_area()` (called from `resolve_move()`/
  `resolve_roll()`'s `just_released` branch) compares against it
  afterwards, redrawing -- via the same scoped `Wimp_UpdateWindow`
  pattern as everything else in this project's animation code -- only
  the cells of whichever *other* pawn actually got displaced (both its
  old cell, to erase, and its new one, to draw), skipping the one pawn
  whose own move animation already painted its result. If nothing else
  changed (the ordinary case), it does nothing at all: no board redraw
  of any kind. `after_settle()` itself no longer redraws the board --
  each call site now handles that explicitly, matching what it actually
  knows changed: `resolve_roll()`'s "turn passed" and "no legal move"
  branches call nothing extra (genuinely nothing on the board changed);
  its "multiple choices" ending swapped `redraw_now()` for the existing
  `update_highlight_area()` (only the movable-pawn rings are new, not
  the board itself); `game_view_new_game()`/`game_view_load_from_path()`
  still call an explicit, unscoped `redraw_now()` after `after_settle()`,
  since a brand new or freshly loaded game has no "before" snapshot to
  diff against and genuinely needs a full repaint.
- **The Throw/Continue button looked clickable even when clicking it
  would do nothing.** `game_view_click()`'s `ICON_THROW` handler already
  silently ignores clicks outside the two situations the button
  actually means something in (a human's own turn actually needing a
  throw, or `STEP_AWAIT_CONTINUE`) -- but the button never reflected
  that visually, staying in its normal "raised, clickable" look even
  mid-animation, during an AI's own automatic actions, or while a human
  still had a pawn to pick. Fixed with `wimp_ICON_SHADED`, RISC OS's
  standard "this control is currently disabled" icon flag -- confirmed
  from the PRM (`~/riscos-dev/prm-mirror/wimp.html`) that it does
  exactly what's wanted here: "the Wimp draws the icon in a 'subdued'
  way, to indicate that it can't be selected. This also prevents
  selection by clicking" -- a genuine Wimp-level click guard, not just a
  visual cue, sitting alongside (not replacing) `game_view_click()`'s
  own existing guard. `refresh_status()` now computes the exact same
  "is Throw/Continue currently the required action" condition
  `game_view_click()` already checks, and toggles the icon's shaded
  flag (via a single `wimp_set_icon_state()` EOR call, only issued when
  the state actually needs to change, tracked in the new `throw_shaded`
  static) to match.

**Round 7.17**: the sprite pivot itself, implemented -- per explicit
user instruction to proceed after approving the round-7.16-settled plan
and reviewing an inspiration set of pixel-art chess-pawn references.

- **Original pawn art** (`assets/generate_icon_sprites.py`, new,
  separate from `generate_placeholder_art.py` -- deliberately different
  output filenames, `pawn_icon0..3.png`/`PawnSprites`, so the two
  generators don't collide over the older `pawn0..3.png`/`Sprites`):
  a from-scratch chess-pawn silhouette (round head, neck collar,
  tapered stem, flared two-level base) rather than reusing GEOS's own
  bitmap, per the reference images the user supplied. Black outline +
  flat player-hue fill + white highlight band/dot + dark-grey shadow
  patch -- deliberately *not* a true light/dark gradient of the
  player's own hue, since (round 7.16, point 4) `Wimp_PlotIcon` maps a
  4bpp icon's indices onto the 16 *fixed* Wimp colours regardless of
  what's embedded, and not every player hue has two Wimp-colour entries
  to shade between. Drawn square, tagged mode 27, quantised via
  `tools/riscos_sprite.py`'s new `--wimp-palette` mode (round 7.16 left
  this specific fix outstanding; done now: `build_palette()` gained a
  `fixed_palette` parameter, quantising via
  `Image.quantize(palette=<a P-mode image of WIMP_COLOURS>)` instead of
  adaptive median-cut).
- **Anti-aliasing, two false starts before the working approach**: a
  first attempt resized the fully-composited RGBA canvas with LANCZOS
  for both colour and alpha, which produced a stray bright-orange fleck
  at a white/red boundary neither colour is anywhere near (LANCZOS's
  negative-lobe ringing overshooting past both source colours, landing
  on a third, unrelated one once nearest-matched into the 16-colour
  palette). Switching to BOX (no ringing) fixed that but still left
  faint off-hue speckling at *internal* colour-region boundaries (e.g.
  yellow fill against the grey shadow patch). Root cause: blending
  *any* two of these deliberately-flat, hand-placed regions across a
  hard cut, destined for only-16-colour quantisation, risks landing on
  a colour that resembles neither side. Fix: split the two channels --
  only the true *outer* silhouette edge (the alpha channel, from a
  dilated copy of the plain silhouette mask) uses a blending resize;
  every internal boundary uses NEAREST, since a crisp 1-pixel-grid cut
  between hand-placed flat regions reads as ordinary pixel art anyway,
  nothing there needed smoothing in the first place.
- **Wired into the game**: `src/game_view.c` gained
  `load_pawn_sprites()` (called once from `game_view_initialise()`,
  loads `assets/PawnSprites` into a private, `malloc()`'d sprite area
  via `xosspriteop_load_sprite_file()` -- entirely separate from
  `wimpspriteop_AREA`, the Wimp's own shared pool, still used elsewhere
  for `def.sprite_area`) and `plot_pawn()` now plots the loaded sprite
  via `Wimp_PlotIcon` (an indirected sprite icon built fresh each call,
  matching the pattern in `update_move_animation_area()`'s own
  `Wimp_UpdateWindow` loop -- see round 7.16, point 1's confirmation
  that this needs no rework of that redraw architecture) when
  `pawn_sprites_loaded` is set, falling back to the original `os_plot`
  circles otherwise -- per this project's established "the game must
  stay playable if a sprite approach fails again" caution (round
  6.3/6.4). `Makefile`'s `deploy` target now also copies
  `assets/PawnSprites` to the Arculator hostfs folder as
  `PawnSprites,ff9`.
- **Not yet done**: live confirmation in Arculator (build/deploy
  succeeded cleanly, zero warnings, but the user has not yet actually
  seen this render on real Wimp_PlotIcon/PutSpriteScaled machinery --
  do not treat this as fully verified until they have). Also open: the
  user asked for a smoother, gradient-shaded look "for 256 colour
  depth" after seeing the flat 16-colour result -- a gradient preview
  (radial light/dark blend instead of flat highlight/shadow patches)
  was generated and shown for comparison, noticeably closer to the
  original reference images, but is **not wired into the game** and
  can't go through the same `Wimp_PlotIcon` path at all (round 7.16,
  point 4 -- 8bpp icon translation is undefined; would need the
  larger, not-yet-built `OS_SpriteOp 52` + precomputed `ColourTrans`
  table path instead). Decision on whether to pursue that is pending
  the user's live-Arculator evaluation of the 16-colour version first.

**Round 7.18**: first live Arculator contact with the sprite pivot --
per explicit user report, pawns rendered *nothing at all* (not even the
`os_plot` fallback -- consistent with `pawn_sprites_loaded` having been
set, so `Wimp_PlotIcon` was being called, just plotting somewhere
invisible). Root cause: `plot_pawn()`'s icon `extent` was built from
`cx`/`cy` -- the *absolute screen* coordinates `cell_centre()` computes
for `os_plot` calls (`origin_x`/`origin_y`-adjusted) -- but
`Wimp_PlotIcon`'s icon block is documented as "the same format as that
used by Wimp_CreateIcon... this being implicitly the window which is
currently being redrawn" (PRM `wimp.html`), i.e. plain **work-area**
coordinates, exactly like any other icon's fixed extent -- never
translated by scroll/origin. Confirmed against ro-chess's own real,
shipped code: its `BOARD[]`/`icon_update()` never applies any origin
offset to an icon's `.box` before calling `wimp_ploticon()`, and passes
that same untranslated box straight to `Wimp_UpdateWindow`'s own box
parameter too. Every pawn icon was therefore being plotted at the wrong
absolute location -- off the visible window entirely whenever the
window wasn't sitting at OS-unit position (0,0), which in practice is
always.

Fixed with a new `cell_centre_work()` (work-area coordinates, no
`origin_x`/`origin_y`) alongside the existing `cell_centre()`
(screen-absolute); `plot_pawn()` now computes both a `wx`/`wy` pair
(work-area, used for the icon's `extent`) and a `cx`/`cy` pair
(screen-absolute, `= origin_x + wx`/`origin_y + wy`, used only by the
`os_plot` fallback) instead of one pair serving both purposes. The
move-animation interpolation math moved to work-area space too (was
interpolating between two `cell_centre()`-computed screen points).
**Lesson for any future icon-plotting code in this project**: `os_plot`
calls and `Wimp_PlotIcon`'s icon extent are NOT interchangeable
coordinate spaces inside the same redraw loop, even though both are
computed from the same `origin_x`/`origin_y`-carrying redraw context --
worth double-checking against a real working example (not just the PRM
prose) before assuming a coordinate convention here, since this is
exactly the kind of assumption that produces "renders nothing, no error
either" rather than an obviously-wrong result.

**Round 7.19**: pawn art polish, per explicit user feedback after seeing
the flat-shaded 16-colour pawns live in Arculator ("not entirely happy
with the new 16 colour pawn look... can we alternatively not use
instead of white a dither between white and player colour, and
similarly for the grey?"). The solid white highlight and grey shadow
regions read as "not the player's own colour at all" in the round 7.17
design, an inherent limit of only green/blue having a second Wimp
colour to shade within their own hue (round 7.16 point 4). Replaced
both flat colour blocks in `assets/generate_icon_sprites.py` with an
ordered 1-pixel checkerboard dither (white<->fill for the highlight,
grey<->fill for the shadow) -- a classic limited-palette pixel-art
technique that reads as a blended tint at normal viewing scale while
staying visibly closer to the player's own hue than a flat block. The
small solid specular dot stays flat white (dithering something that
small would just look like noise, not a tint). Implementation detail:
the dither has to be decided at the *final* 32x32 pixel grid, not the
10x-supersampled working resolution -- a checkerboard drawn at working
resolution and then downsampled would alias unpredictably depending on
how its period lines up with the downsample ratio; region masks
(highlight/shadow/dot/silhouette) are still built at working resolution
for smooth shape *boundaries*, then converted to per-final-pixel
membership booleans via NEAREST resize, with the actual `(x+y)%2`
checkerboard colour choice made directly on the final grid. Builds
clean, deployed.

**Round 7.19 follow-up, same day**: the 50/50 checkerboard read as
solid diagonal lines rather than a dithered tint once actually seen
live, per further explicit user feedback -- at a 1-pixel-alternating
period, a checkerboard is visually dominated by its diagonal stripe
structure, not perceived as a blend, especially at small icon sizes.
Switched the *highlight* region specifically to a sparser 1-in-4 dot
grid, diagonally staggered per row (`(x + 2*y) % 4 == 0`) so the dots
themselves don't line up into streaks either. The shadow dither
(grey/hue) was left as the full checkerboard -- not asked to change,
and being darker/subtler to begin with, less prone to reading as lines
in the first place. Built, deployed. Not yet confirmed live by the user
at the time of this writeup.

**Round 7.20**: a genuine rules bug, per explicit live user report
("end field of a pawn is one less if previous pawn already landed on
final field. Now the logic stacks the pawns at end field, not
intended"). `home_column_blocked()` excluded already-finished pawns
from the own-pawn blocking check (the assumption being that a finished
pawn is "off the board" and can't block anything), and `ludo_move_pawn()`
finished every pawn at the same fixed `LUDO_TOTAL_STEPS` -- together,
this let every one of a player's pawns converge on and stack on the
single last home-column square instead of queueing into distinct ones.
Ground-truthed against the actual GEOS source rather than assumption
(`/home/xahmol/git/ludo/GEOS/src/gamelogic.c`'s `pawnselect()`, not
`turngeneric()` this time): `playerdata[player][1]` is a *shrinking*
"pawns still needed home" counter, and a pawn only counts as reaching
home when its landing position exactly matches `playerdata[player][1]+3`
-- a target that itself decrements by one every time a pawn reaches it;
GEOS's own blocking check never exempts an already-finished pawn from
blocking a later one either. Fixed with a new `finish_threshold_for()`
(each pawn's own finish line = `LUDO_TOTAL_STEPS` minus however many of
that player's *other* pawns have already finished) and removing the
`!op->finished` exclusion from `home_column_blocked()` -- together these
make finished pawns permanently occupy and block their own square
exactly like any other home-column occupant, so each subsequent pawn's
own reachable maximum is mechanically capped one square lower, and a
player's finished pawns naturally queue into the home column's 4
distinct squares one at a time, from the far end inward. Full rules
writeup: `docs/GAME_LOGIC.md`'s "Round 7.20" note.

Test-side fallout, per explicit standing instruction to update test
coverage alongside any logic fix: both headless full-game simulations
(`tests/test_game_logic.c`, `tests/test_ai.c`) had an invariant assuming
"finished iff steps == LUDO_TOTAL_STEPS", now false for any pawn beyond
a player's first to finish. A first fix attempt recomputed each pawn's
expected threshold *retroactively* from the current snapshot of which
siblings are finished -- also wrong, since an already-finished pawn's
own steps reflects the threshold *at the moment it finished*, which can
be higher than the same formula gives later once a sibling finishes
after it (caught immediately by a small standalone debug harness run
against real gameplay, before it was allowed anywhere near the fix
count). The correct invariant is set-based and order-independent: a
player's finished pawns, as a set, must occupy exactly the topmost N
distinct home-column squares. New direct unit test:
`test_second_finishing_pawn_lands_one_square_short()`, the exact
reported scenario.

**Round 7.21**: a genuinely significant redraw bug, found while chasing
what looked at first like a small, cosmetic report ("die picture crops
the upper black line and halves the right black line"). Static analysis
of `plot_dice()`/`update_dice_area()`'s coordinate math found nothing --
everything landed on clean mode-15 pixel-boundary multiples -- so
diagnostic logging was added instead (this project's established
practice for "code looks right, screen is wrong" bugs). The Log
capture revealed the real story: **`wimp_draw`'s `.box` field, once
inside the `while (more)` redraw loop, is the window's *entire visible
area* in screen coordinates, not the small rectangle actually being
updated** -- confirmed directly against the PRM (`wimp.html`'s
`Wimp_RedrawWindow` entry, whose block format `Wimp_UpdateWindow`
shares: "the first four words are the position of the window's work
area on the screen"). The *actual* per-iteration paintable rectangle is
a separate field, `.clip` ("current graphics window... an area within
the visible work area... The graphics clip window is set to the
returned rectangle") -- which this project's entire scoped-redraw
machinery, built up across rounds 7.10-7.20, had never once read or
used.

This turned out to have two distinct consequences, of very different
severity:

- **The die crop itself**: `plot_dice()` always paints its whole
  intended box unconditionally, relying on the OS's automatic clip
  (set to `.clip`) to restrict it correctly -- so if `.clip` came back
  even slightly smaller than the die's box on specific edges, the
  excess would be silently cropped with no error anywhere. The PRM
  separately notes the *input* box's maximum x/y are **exclusive**,
  unlike `os_PLOT_RECTANGLE`'s own inclusive x1/y1 -- exactly explaining
  why only the *upper* edges (top, right) were affected and never the
  lower ones. Fixed by padding `update_dice_area()`'s requested box's
  x1/y1 by a few OS units (over-requesting is harmless; under-
  requesting silently crops).
- **A much bigger, previously-undiagnosed bug**: `update_move_animation_area()`,
  `update_highlight_area()`, and `update_settle_diff_area()` all erase
  their scoped region with `fill_window_background(redraw.box.x0, ...)`
  before redrawing content -- since `.box` is actually the *entire
  visible window*, not the small few-cell region these functions
  compute and request, **every single tick of every one of these
  animations was wiping the whole visible window to background colour**,
  not just the small intended patch -- exactly matching a live user
  report ("everything on screen redraws on every dice throw") that,
  until this point, had no explained mechanism. `redraw_now()` has the
  same `.box`-based erase call, but is unaffected -- it deliberately
  requests the *entire* window already, so erasing "the whole visible
  area" there is what was wanted all along, not a bug. Fixed by
  switching the three genuinely-scoped functions to erase
  `redraw.clip` instead.

**Why this survived rounds 7.10 through 7.20 despite repeated live
testing**: the small content drawn back on top of the (wrongly) fully-
erased window *looks* like a correctly-scoped update in isolation --
the visible symptom is "everything else transiently flashes/goes blank
around the small thing that changed," which reads as general UI
flicker rather than pointing at a specific mechanism, especially once
several rounds of *other*, real flicker fixes (Wimp_ForceRedraw ->
Wimp_UpdateWindow, animation-segment scoping, settle-diff scoping) had
already visibly improved things. Documented as a general lesson (not
project-specific) in `riscos_wimp_reference.md`'s "Animating a small
region..." section, both project and canonical `~/.claude/` copies, so
this exact field confusion doesn't recur in a future project.

**Round 7.22**: the same exclusive-upper-bound cause as round 7.21's die
crop, found again in a second place -- per explicit live user report,
the pulsing movable-pawn/hover-destination highlight rings cropped at
the top when on the board's top rows, and (worse than the die's purely
cosmetic crop) **left a permanent residue after the flash's "off"
phase**: since the erase step uses the same requested box as the draw,
an upper bound landing exactly on the ring's true edge under-erases
that same sliver every "off" tick, leaving a leftover fragment that
never gets cleaned up. Root-caused to `cell_range_to_work_box()` --
shared by `update_move_animation_area()`, `update_highlight_area()`, and
`update_settle_diff_area()` -- which computed its request box's upper
bounds (`x1`/`y1`) flush against the requested cell range's own edge,
with no allowance for the PRM's documented exclusive-upper-bound
convention. Fixed once, in the shared helper, with an 8-OS-unit pad on
`x1`/`y1` (rather than patching each of the three callers separately) --
this also pre-emptively covers the same latent crop/residue risk for
pawn-animation and settle-diff redraws near the board's own top/right
edges, not just highlights, since nothing had reported those yet purely
by chance of which cells happened to be involved. `redraw_now()`
(hardcodes its own full-window box rather than using the shared helper)
got the same treatment for consistency, padding past the window's own
true edge -- harmless, since the Wimp still clips to the window's real
bounds regardless.

**Round 7.24**: two fixes, per explicit live user report.

- **Loading a game from the setup dialog's Load button didn't open the
  game window.** `setup_view.c`'s `ICON_START` handler explicitly calls
  `game_view_open()` before `game_view_new_game()` -- but neither
  `game_view_new_game()` nor `game_view_load_from_path()` ever open the
  window themselves; both just set `game_started`/load the board state
  and expect the *caller* to open it (the window only actually becomes
  visible on a *separate*, later iconbar click, since
  `game_view_has_started()` being true then routes that click to
  `game_view_open()` -- see `main.c`). `save_view.c`'s Load path
  (`load_view_click()`'s `ICON_LOAD_GO` and `load_view_key_pressed()`'s
  Return-key handler) never got the equivalent `game_view_open()` call
  Start's own handler has. Fixed by adding it to both, inside the
  existing "did the load actually succeed" check (matching Start's
  behaviour of opening unconditionally, since Start's own board-reset
  call can't fail the way a load from an arbitrary path can). The third
  `game_view_load_from_path()` call site (an unsolicited `Message_
  DataLoad`, a file dragged onto the game window itself) needed no
  fix -- the window is provably already open there, since you can't
  drag onto a window that doesn't exist.
- **Pawn-movement animation had visibly slowed down** -- caused by round
  7.23's own diagnostic `debug_log()` call inside `plot_pawn()`, which
  ran for *every visible pawn on every single redraw, animation ticks
  included* -- `debug_log()` opens, writes, and closes the Log file
  from scratch on every call, so this was a real, measurable cost, not
  a red herring. Removed; the box/clip diagnostic logging on
  `update_move_animation_area()`/`update_settle_diff_area()` themselves
  (added this round, see below) runs once per *tick* instead of once
  per *pawn per tick*, several times cheaper, and is enough to
  correlate a cropped pawn's known board position against the redraw
  region actually active at the time -- still investigating a separate,
  live-reported pawn bottom-crop (opposite edge from round 7.21/7.22's
  crops, on a *different* redraw path each time it's been seen -- the
  round 7.21/7.22 fixes only padded the request box's upper bounds,
  which the PRM documents as exclusive; the lower bounds are documented
  as ordinary/inclusive, so the same fix doesn't obviously apply here
  and this needs its own real evidence before touching anything).

**Round 7.25**: per explicit live user report ("several pawns are cropped
... pawn animation is flickery as it seems to redraw larger area than
needed", followed by "Animation was way smoother before, so something you
recently did impacted that") -- a logging-strategy correction, not yet a
fix for the crop itself.

- **Confirmed the round 7.23/7.24 diagnostic `debug_log()` calls were
  still the smoothness problem**, this time from the *once-per-tick* box/
  clip logging round 7.24 added to `update_move_animation_area()`/
  `update_settle_diff_area()` (plus the still-present round 7.21 logging
  in `update_dice_area()`/`plot_dice()`, and the round 7.23 logging in
  `redraw_now()`/`game_view_redraw()`). Once-per-tick is far cheaper than
  round 7.23's once-per-pawn, but a single animated move still spans many
  ticks (`MOVE_ANIM_TICKS_PER_CELL` per cell, times several cells for a
  multi-step roll), and `debug_log()`'s fopen/fprintf/fclose-per-call cost
  (see round 7.24) adds up across all of them -- confirmed by reading a
  fresh Log (227 matching lines from one session) that this logging was
  still firing continuously throughout every animated move. All six of
  these unconditional per-tick/per-redraw `debug_log()` calls removed.
- **Replaced with a self-triggering check instead of no check at all**:
  a new file-scope `dbg_request_x0/y0/x1/y1` (see `src/game_view.c`,
  just above its declaration) records the work-area box that
  `update_move_animation_area()`/`update_highlight_area()`/
  `update_settle_diff_area()`/`redraw_now()`/`game_view_redraw()` each
  actually requested be redrawn (their own `redraw.box`/`redraw->box`,
  i.e. the same value `cell_range_to_work_box()` or the window's own
  extent already computed). `plot_pawn()` now compares its own icon
  extent against that box on *every* call and only calls `debug_log()` if
  the extent doesn't fully fit inside it -- which should never happen in
  correct operation, so the cost is a handful of integer comparisons per
  pawn per tick, not a file open/close. If the reported crop is a
  logic bug in one of those five functions' cell-range math, this will
  log the exact player/pawn/cell/extent/request the next time it's
  reproduced, without the per-tick file I/O cost that caused this same
  complaint twice now (round 7.24's per-pawn logging, and this round's
  once-per-tick logging).
- **Investigated but did not find the crop's root cause via static
  analysis alone** (this round's other work, before turning to the
  logging fix above): checked whether `plot_pawn()`'s Wimp_PlotIcon-based
  sprite path needs the same kind of extent padding the manual
  `fill_rect()`-based die (`update_dice_area()`) needed for round 7.21 --
  concluded no, since `assets/generate_icon_sprites.py`'s own design
  comment states pawn sprites are deliberately scaled to the icon's
  extent by `Wimp_PlotIcon`'s own `PutSpriteScaled` machinery (not
  plotted at native size), so a size mismatch between the sprite's
  stored mode-27 pixel size and `PAWN_SIZE` isn't the cause. Also
  confirmed a *static* (non-animating) pawn's icon extent is always
  comfortably centred at least 8 OS units inside its own cell's box in
  every one of `cell_range_to_work_box()`'s callers (a 48-unit icon
  inside a 64-unit cell), so a simple "extent exceeds its own cell" bug
  was ruled out too. Left as an open question for the new self-
  triggering log to answer with real evidence next time it's reproduced,
  rather than guessing at another speculative fix.

**Round 7.26**: fixed a bug in round 7.25's own crop-detection code, per
explicit live user report ("No redrawing things is still really slow.
Have the feeling that the box/clip round detoriated things. And several
cropped pawns visble in [screenshot]").

- **Root cause**: round 7.25's `dbg_request_x0/y0/x1/y1` were meant to
  hold the WORK AREA box each redraw function had requested, for
  `plot_pawn()` to compare its own (also work-area, round 7.18) icon
  extent against. But all five call sites read them back from
  `redraw.box`/`redraw->box` *after* calling `wimp_update_window()`/
  `wimp_redraw_window()`/`wimp_get_rectangle()` -- and those calls
  overwrite that struct field with SCREEN coordinates (this is exactly
  round 7.21's own `.box`-is-screen-not-work-area finding, which this
  round's own code failed to apply consistently). Comparing a work-area
  icon extent against a screen-coordinate box is essentially meaningless
  once the window isn't sitting at the screen origin, so the "crop"
  check false-positived on **every single pawn drawn** -- confirmed by
  reading a fresh Log showing 422 `plot_pawn: CROP` lines whose logged
  `extent`/`request` pairs don't even overlap in sign (e.g. `extent=
  (272,-384,320,-336)` against `request=(100,100,1096,820)`). This meant
  every pawn draw, on every tick, called `debug_log()` -- reintroducing
  the exact per-pawn logging cost round 7.24 had already found and
  removed once, which is why the animation was reported as still slow
  ("even worse than before" per the user's own phrasing).
- **Fix**: `update_move_animation_area()`/`update_highlight_area()`/
  `update_settle_diff_area()` now set `dbg_request_*` from their own
  *pre-call* local `x0`/`y0`/`x1`/`y1` (the work-area values
  `cell_range_to_work_box()` computed, before `redraw.box` gets
  clobbered), once, before entering the `while (more)` loop rather than
  every iteration. `redraw_now()` sets them from the same literal
  work-area constants it assigns to `redraw.box` before the call.
  `game_view_redraw()` -- the one path where the box is *only* ever
  available in screen coordinates (`Wimp_RedrawWindow`'s output, not
  something the app requests) -- converts it back to work area using the
  same `origin_x`/`origin_y` subtraction the function already does for
  its own drawing (`work = screen - origin`), rather than comparing
  screen against work-area directly.
- **The screenshot's visible crops are still not explained** -- this
  round only fixes the check's own false-positive bug and the resulting
  slowdown; it does not yet contain a real fix for pawn cropping. With
  the false positives gone, the crop check should now only fire on a
  genuine mismatch, so the next reproduction's Log should either show
  real `plot_pawn: CROP` lines pointing at the actual bug, or show none
  at all -- which would mean the crop isn't a `Wimp_UpdateWindow`/clip
  scoping issue at all (see round 7.25's other findings, which already
  ruled out a sprite/extent size mismatch and a same-cell overflow), and
  the next place to look is something outside this box/clip mechanism
  entirely (sprite content itself, or `Wimp_PlotIcon`'s own scaling
  behaviour at whatever screen mode the reproduction was in).

**Round 7.27**: the actual pawn-crop fix, per the user's own hypothesis
after round 7.26 confirmed (with a genuinely fixed, verified-clean Log --
zero real `plot_pawn: CROP` hits across a full fresh session) that a
pawn's own icon extent was never the problem: "is pawn too high and
because it is higher than the field it is on, gets wiped when another
pawn animates past it?" -- exactly right, one level removed from the
pawn's own sprite.

- **Root cause**: `update_move_animation_area()`/`update_highlight_area()`/
  `update_settle_diff_area()` all erase using `redraw.clip.x1/y1` (the
  Wimp-granted clip), and that clip can legitimately be as large as the
  *requested* box -- which `cell_range_to_work_box()` pads by +8 OS units
  on x1/y1 only (round 7.21/7.22, to guard against the PRM's documented
  exclusive-upper-bound shortfall on the *request*). Nothing forces the
  Wimp to shrink the granted clip back down to the true cell boundary
  when the window is large enough to contain the padded request, so the
  erase can paint up to 8 OS units *past* the true edge of `col1`/`row0`
  -- into whichever cell sits just beyond it. The ±1-cell margin these
  three functions already add is a margin around `col0..col1`/
  `row0..row1` themselves (for the *previous tick's position* concern --
  see `update_move_animation_area()`'s own doc comment), not around the
  padded request box, so a neighbour sitting just past that margin was
  never part of this call's own `draw_board_region()` -- erased by the
  padding overrun, never repainted. Since the pad is on y1 (the
  numerically-larger/visually-upper edge), it bleeds into the row
  *above* `row0`, eating into *that* row's own bottom 8 units -- matching
  every "bottom cropped" report in this whole investigation, and
  explaining why it always looked like "some other pawn's animation
  wiped it": whichever animation/highlight/settle-diff call's box
  happened to start one row below the victim was the one doing the
  erasing.
- **Fix**: a new `ERASE_CLAMP_MAX` macro (`src/game_view.c`, just above
  `update_move_animation_area()`) clamps the erase rectangle's x1/y1 to
  the smaller of the Wimp-granted clip and the TRUE, unpadded cell
  boundary (`BOARD_ORIGIN_X + (col1+1)*CELL` / `BOARD_ORIGIN_Y -
  row0*CELL`) -- the *request* box stays generously padded as before
  (still needed so a pawn's own icon extent or a highlight ring's radius
  always gets a big enough clip to draw itself fully), only the erase
  itself now stops exactly at the cell edge, never past it. Applied to
  all three functions' erase calls; `redraw_now()` (whole window, no
  neighbour-cell concept) and `game_view_redraw()` (Wimp's own auto-
  clear, no manual erase at all) needed no equivalent change.
- **Not yet done, logged for later**: the user also flagged that a
  captured/beaten pawn currently teleports straight to its home base
  rather than animating there, and suggested a direct diagonal path
  rather than retracing the whole board route back -- confirmed accurate
  against the current code (`update_settle_diff_area()`'s doc comment
  already states captures are a diff-based redraw, not an animation, by
  original round 7.15 design). This is a feature addition, not a bug,
  and hasn't been implemented -- worth picking up as a small follow-up
  once the crop/flicker investigation is fully closed out.

**Round 7.28**: fixed a regression round 7.27 itself introduced, per
explicit live user report ("On animation, pawn is now not erased") with
a screenshot showing a visible multi-frame diagonal trail of un-erased
pawn images.

- **Why round 7.27 broke this**: its fix shrank the erase rectangle in
  `update_move_animation_area()`/`update_highlight_area()`/
  `update_settle_diff_area()` from the Wimp-granted clip down to the
  true, unpadded cell boundary, to stop it bleeding into a neighbouring
  cell. That was the right diagnosis for the crop, but the same +8 pad
  it clamped away had quietly been serving a second purpose too: on
  whichever tick a pawn is the one actually animating, its real
  `Wimp_PlotIcon`/`PutSpriteScaled`-rendered footprint can paint a
  little past its nominal 48-unit icon extent into that same padding
  zone -- and round 7.27's narrower erase stopped cleaning that up,
  leaving each tick's un-erased paint sitting there as the pawn slid
  along, producing exactly the reported "comet trail" of full pawn
  shapes.
- **Fix**: keeps the erase exactly as wide as the Wimp grants it (back
  to round 7.21-7.26 behaviour, already known correct for a pawn's own
  content) and instead widens what gets *repainted* by one extra cell on
  the padded sides (`col1+1`, `row0-1`, clamped to the grid) in all
  three functions' `draw_board_region()` calls. This guarantees anything
  the padding zone could have touched -- a neighbour's content OR the
  animating pawn's own overflow -- is always repainted by this same
  call, rather than trying to guarantee the erase never touches it in
  the first place. The now-unused `ERASE_CLAMP_MAX` macro from round
  7.27 was removed; its diagnosis is kept as a comment in
  `src/game_view.c` since it's the root of the whole investigation.
- Not yet re-confirmed live -- needs the same quit-and-relaunch-in-
  Arculator step as every round since 7.25 (hostfs replacement alone
  doesn't affect an already-running RISC OS task) before the next
  screenshot/Log can tell us whether both the crop AND the trail are
  actually gone together.

**Round 7.29**: stepped back from the clip/erase-scoping chase (rounds
7.21-7.28) per explicit live user report/suggestion -- round 7.28's
wider repaint neither fixed the crop nor was worth its own cost ("Still
cropped pawns, and the wide repaint increases the flicker. Is making
the pawns less high an option?").

- **Reverted** round 7.28's one-extra-cell-wider `draw_board_region()`
  repaint in all three of `update_move_animation_area()`/
  `update_highlight_area()`/`update_settle_diff_area()` back to the
  plain, tight `col0..col1`/`row0..row1` range (matching round
  7.21-7.26) -- it added a real, reported flicker cost without actually
  fixing the crop, so there was no reason to keep it.
- **`PAWN_SIZE` reduced from 48 to 40** (`src/game_view.c`) -- the
  user's own suggestion, and a more robust fix than continuing to chase
  the exact remaining clip/erase-boundary edge case: since
  `Wimp_PlotIcon`'s icon extent is what `PutSpriteScaled` scales the
  sprite *to*, shrinking it gives the icon more margin inside its fixed
  64-unit cell (12 units/side at 40, versus 8 at 48) -- roughly 50% more
  headroom -- so whatever the last few OS units of overflow actually are
  (still not pinned down with certainty; the current working theory,
  recorded in `cell_range_to_work_box()`'s history comment, is
  `PutSpriteScaled`'s own rendering under the non-square screen modes
  this project must support painting a little past the icon's nominal
  extent) become far less likely to reach a cell boundary at all,
  without needing to identify the exact mechanism first. Also fixed a
  stale doc comment on `PAWN_SIZE` left over from round 6.3 (claimed
  "this project no longer plots any sprites at all", predating round
  7.16/7.17's sprite pivot entirely).
- Not yet re-confirmed live -- as with every round since 7.25, needs a
  full quit-and-relaunch in Arculator (not just hostfs replacement)
  before the next screenshot/Log can confirm whether this actually
  closes out the crop investigation.

**Round 7.30**: `PAWN_SIZE` reduced again, 40 -> 36, per explicit live
user report that round 7.29's 40 wasn't enough margin ("still see
cropping... give 2 pixels top and bottom more margin, so making pawn 36
instead of 40?") plus a direct question about the actual rendered size.
Mode 15's Y axis is 4 OS units/physical pixel, so "2 pixels" of *extra*
margin on each of the top/bottom edges technically works out closer to
40 -> 24, but the user's own concrete number (36) was used as the
instruction rather than the derived arithmetic -- see `PAWN_SIZE`'s own
doc comment in `src/game_view.c` for the full reasoning, including the
fallback os_plot circle radii at this size (`body_radius`=11,
`head_radius`=6, `outline`=3, all `PAWN_SIZE`-derived) for reference.
Not yet re-confirmed live.

**Round 7.31**: the actual root cause of the whole rounds 7.21-7.30
crop investigation, found after the user reported "really see no
smaller sprites" across three different `PAWN_SIZE` values and asked
"Are they dimensioned by code somewhere [else]?" -- exactly the right
question.

- **Root cause, finally confirmed against the PRM**: `Wimp_PlotIcon`
  does **not** scale a sprite icon to fit its extent. The project's own
  sprite-pivot docs (`assets/generate_icon_sprites.py`,
  `plot_pawn()`'s doc comment) had claimed since round 7.16/7.17 that
  it goes through "`PutSpriteScaled` with a proper scale/translation
  table" -- that claim was never actually verified, and turns out to be
  false. The PRM's Icon-flags section documents exactly one sprite-size
  control: a binary `wimp_ICON_HALF_SIZE` flag. A plain sprite icon
  plots at its own NATIVE size (source pixel count × the sprite's
  recorded old-style mode's OS-units-per-pixel), centred in the extent
  via `HCENTRED`/`VCENTRED` -- the extent's size never affects the
  rendered size at all. Mode 27 (this project's pawn-sprite mode) is
  2 OS units/pixel in both axes (confirmed against the PRM's mode
  table, `~/riscos-dev/prm-mirror/modes.html`), and the sprite source
  was 32×32 pixels -- a fixed 64×64 OS-unit native footprint, **exactly
  equal to `CELL` (64)**. Every pawn has always rendered at exactly one
  full cell with zero margin, completely independent of `PAWN_SIZE`
  (48, then 40 in round 7.29, then 36 in round 7.30 -- all produced an
  identical on-screen size, which is what tipped this off). This
  explains the entire crop investigation: with zero margin, any
  imprecision anywhere in the erase/redraw clip machinery (rounding,
  granted-vs-requested clip differences, the exclusive-upper-bound
  quirk) had nothing to absorb it.
- **Fix**: `assets/generate_icon_sprites.py`'s `FINAL` constant (the
  sprite's own source pixel size) reduced from 32 to 18, giving an
  18×18 native sprite = 36×36 OS-unit footprint at mode 27's 2 units/
  pixel -- now genuinely matching `PAWN_SIZE` (36, unchanged from round
  7.30) and giving real margin (14 units/side) inside the 64-unit cell.
  `OUTLINE_DILATE_WORK` scaled proportionally (14 → 8) to keep the same
  relative outline thickness at the smaller final size. Sprites
  regenerated and `assets/PawnSprites` redeployed.
- **Documentation corrected everywhere the false claim appeared**:
  `assets/generate_icon_sprites.py`'s module docstring, `PAWN_SIZE`'s
  own comment and `plot_pawn()`'s doc comment in `src/game_view.c`, and
  a new general lesson added to `riscos_wimp_reference.md` (both the
  project copy and the canonical `~/.claude/riscos_wimp_reference.md`)
  under "Sprites" -- this is a broadly-applicable RISC OS WIMP lesson,
  not ArchiLudo-specific, so it belongs there alongside the earlier
  `.box`-vs-`.clip` and work-area-vs-screen-coordinate corrections from
  the same investigation.
- **Rounds 7.21-7.28's actual code changes are still believed correct
  and were NOT reverted** -- the `.box`-vs-`.clip` fix (7.21), the
  highlight-ring padding fix (7.22), the Load-window-open fix (7.24),
  and the false-positive crop-check fix (7.26) all addressed real,
  independently-confirmed bugs and stay in place. Only the *sprite
  sizing* theory (7.27-7.30's clip-shrinking/repaint-widening/
  extent-shrinking attempts) was chasing a symptom of the real bug
  found here -- 7.27's erase clamp and 7.28's wider repaint were
  already reverted (round 7.29) once they proved not to help, before
  the actual cause was known.
- Not yet re-confirmed live -- needs the same quit-and-relaunch step as
  every round since 7.25, but this is the first round in the whole
  crop investigation backed by a citable primary-source confirmation
  rather than another clip-geometry theory, so there's real reason to
  expect this one closes it out.

**Round 7.32**: two fixes to round 7.31's sprite-shrink, per explicit
live user report ("now they are small and very ugly, we maybe
overcompensated").

- **`FINAL` eased back from 18 to 26** (`assets/generate_icon_sprites.py`)
  -- 18px (36 OS units) was too aggressive a cut from the size the user
  had originally approved (32px/64 units); 26px (52 OS units) keeps real,
  deliberate margin (6 units/side inside the 64-unit `CELL`, versus zero
  before round 7.31) while staying much closer to the approved look and
  giving the design's dither/outline detail enough final pixels to
  actually read. `PAWN_SIZE` (`src/game_view.c`) kept in sync at 52.
- **Fixed a real bug in round 7.31's own `OUTLINE_DILATE_WORK` scaling**
  that made the "ugly" report worse than the size cut alone would have:
  the outline's rendered width in final pixels is
  `OUTLINE_DILATE_WORK * FINAL/WORK`, so keeping it constant as `FINAL`
  shrinks requires `OUTLINE_DILATE_WORK` to scale *up* (inversely) --
  round 7.31 scaled it *down* in proportion to `FINAL` instead (14 -> 8
  alongside 32 -> 18), which shrinks the rendered outline width
  quadratically (down to ~0.45 final-px, in practice barely visible at
  all) rather than holding it steady. Fixed direction: `1.4*320/26 ≈ 17`.
  Sprites regenerated, visually spot-checked at 8x nearest-neighbour
  zoom before rebuilding (clear black outline, legible dither, silhouette
  recognisable) rather than only judging by the numbers this time.
- Not yet re-confirmed live.

**Round 7.33**: a real bug in the sprite generation script itself
(`assets/generate_icon_sprites.py`), unrelated to RISC OS/Wimp clipping
at all -- per explicit live user report that round 7.32's pawns "look
way better again but are now always cropped of the black line at the
bottom", every single pawn, every position. That consistency (not
intermittent, not position-dependent) was the tell that this was a
different class of bug from rounds 7.21-7.28's redraw/clip-scoping
issues -- checked the generated pixels directly rather than theorising
about Wimp behaviour further (this project's "ground truth
verification" habit).

- **Root cause**: `draw_pawn_silhouette()`'s hand-drawn shapes span
  y=18 (head top) to y=302 (base bottom) inside the `WORK=320`
  supersample canvas -- only 18 WORK units of margin on each side
  *before* `OUTLINE_DILATE_WORK`'s dilation (17, set in round 7.32)
  even runs. After dilation, that left just 1 WORK unit of surviving
  margin on both edges -- nowhere near enough for `final_alpha`'s own
  BOX-resize antialiasing to represent a soft edge, so the outermost
  ring of the dilated outline was being clipped against the WORK
  canvas boundary itself, before the sprite ever reached
  `Wimp_PlotIcon`. The base (a flat `rounded_rectangle`, full width
  right up to its own boundary) clips far more visibly than the head
  (a curved ellipse, only a single-pixel-wide sliver actually reaches
  its extreme y) -- exactly matching "cropped at the bottom", never
  reported at the top.
- **Fix**: a new `CONTENT_SCALE = 0.90` shrinks the whole hand-drawn
  design slightly around the canvas centre (`sc()`/`sc_pts()` helpers,
  applied to every coordinate in `draw_pawn_silhouette()`/
  `highlight_shapes()`/`shadow_shapes()`/`dot_shapes()`) rather than
  growing `WORK` or rescaling every hand-tuned coordinate by hand --
  gives every edge real breathing room (margin after dilation now ~15
  WORK units, not 1) for any future `OUTLINE_DILATE_WORK`/`FINAL`
  combination too. `FINAL`/`PAWN_SIZE` themselves untouched (still 26/
  52) -- this is a small, deliberately subtle reduction in how much of
  its own frame the opaque pawn fills, not another size cut.
- **Verified by inspecting actual generated pixels before rebuilding**
  (not just judging by the formula this time): both the bottom row's
  alpha (now fully transparent, a real gap) and an 8x nearest-neighbour
  zoom (clean black outline on all four sides, dither still legible)
  were checked directly against the regenerated PNG. Not yet re-
  confirmed live in Arculator.

**Round 7.34**: a small, pragmatic fix for the last piece of the crop
investigation, per direct live user diagnosis after round 7.33's fix
("did not solve the cropping though when something moves passed" --
i.e. a *different*, smaller crop than round 7.33's, only during nearby
animation) plus a precise visual comparison against the cell's own
marker circle ("Pawn does not stick above the circle at the top... but
does stick out of the circle at the bottom") and a direct suggestion
("Can't we move pawn slightly up?").

- **This confirms round 7.27's original diagnosis was right all
  along**, just never landed cleanly: `cell_range_to_work_box()`'s own
  `+8` request-box padding (on `y1` only, needed for its own PRM-
  documented reason -- round 7.21/7.22) can still bleed up to 8 OS
  units into the row *above* a redraw box's own `row0`, eating into
  that row's bottom edge. `PAWN_SIZE=52` in the 64-unit `CELL` only
  gives 6 units of margin per side -- less than the 8-unit worst case
  -- so a pawn's bottom edge can still be reached by up to 2 of those
  units. (Rounds 7.27/7.28 tried to fix this at the erase/repaint level
  twice and both attempts were reverted for other costs -- narrowing
  the erase caused an under-erase trail, widening the repaint cost
  visible flicker for no benefit -- see their own history entries.)
- **Fix**: a new `PAWN_Y_NUDGE = 4` (`src/game_view.c`) shifts a pawn's
  own centre up by 4 OS units in `plot_pawn()`, applied uniformly to
  both the static and mid-animation position -- giving the bottom edge
  10 units of clearance (comfortably past the 8-unit worst case) at the
  cost of only 2 off the top's spare margin. Safe specifically because
  the padding is asymmetric (`y1` only, never `y0`) -- nothing
  analogous ever bleeds downward into a pawn's top from the row above,
  confirmed by the user's own live visual comparison.
- Much lower-risk than another attempt at precisely re-tuning the
  erase/redraw clip boundaries a third time -- a plain position offset
  can't introduce a new class of bug the way narrowing an erase
  rectangle or widening a repaint range already has, twice. The user
  also offered reverting the whole sprite pivot back to `os_plot`
  circles as an alternative if this round still didn't hold -- worth
  keeping in mind as a fallback, but not taken here since this fix is
  simple, targeted, and directly derived from a precise live
  measurement rather than another theory.
- Not yet re-confirmed live.

**Round 7.35**: "continue playing after the first winner" + "New Game
dialogue always defaults to the in-progress game", per explicit user
request after a full game played through to a win with no logic bugs
found ("on victory of a player, dialogue should ask to continue with
remaining players, or to start new game... for new game dialogue,
defaults always should be the in progress game, unless we just started
a new one").

- **New module `src/win_view.c`/`include/win_view.h`** -- a small
  popup shown the instant any player wins (`after_settle()`'s new
  first check), offering "Continue" or "New Game", following this
  project's established one-window-per-module pattern
  (`splash_view.c`'s layout was the closest template).
- **New `win_acknowledged` flag + `game_paused()` helper** in
  `game_view.c` -- every UI check that used to treat `game.winner !=
  -1` as "the game is over" (the "X WINS!" panel display, the Throw/
  Continue button's shading and click handling, board-click/hover-
  highlight guards, the swatch colour) now checks `game_paused()`
  (`game.winner != -1 && !win_acknowledged`) instead, so once
  "Continue" is chosen, ordinary turn-based play resumes exactly as if
  nothing had happened -- `game.winner` itself is never cleared (the
  engine doesn't support un-winning), only the UI's *reaction* to it
  changes. The Throw/Continue button's old "click it again to play a
  new game" meaning is gone entirely -- New Game is now only reachable
  through the win dialogue (or the iconbar menu, as before).
- **Real engine bug found and fixed while wiring this up**: rule 9
  ("remaining players may continue") was already the documented design
  intent (see `docs/GAME_LOGIC.md`'s rule 9), but `ludo_move_pawn()`'s
  six-goes-again check tested the global `g->winner == -1` rather than
  the current player's own `all_pawns_finished()` -- meaning every
  player lost their own bonus-roll-on-six the instant anyone won,
  never noticed before since nothing previously kept playing past that
  point to exercise it. Fixed, with a new regression test -- see
  `docs/GAME_LOGIC.md`'s own "Round 7.35" entry.
- **`setup_view_open()` now syncs from the live game** every time it's
  opened, via a new `game_view_get_players()` getter -- previously it
  only ever showed its own one-time hardcoded defaults or whatever the
  user had last manually typed/toggled in a *previous* dialogue
  session, never the actual running (or just-won) game's real
  configuration. Falls back to the original hardcoded GREEN/RED/BLUE/
  YELLOW/all-Human defaults only when `game_view_has_started()` is
  still 0 (nothing to sync from yet). This satisfies both request
  halves at once: clicking "New Game" from the win dialogue calls
  `game_view_win_continue()` (marks the win acknowledged) before
  `setup_view_open()`, so the game just finished is still "the live
  game" at the moment the sync happens.
- **Known, accepted rough edge**: the save-file format doesn't record
  whether a winner had already been acknowledged -- reloading a saved
  "continuing past a win" game re-shows the win dialogue once (a
  single extra "Continue" click), rather than changing the save format
  to track it.
- Not yet confirmed live.

**Round 7.36**: the `!ArchiLudo` application directory, per explicit
user request pointing directly at Steve Fryatt's wimp-prog tutorial,
Chapter 17 ("Creating an Application Directory") plus a specific icon
design ("suggested icon is one red pawn and a die") -- resolving the
design-consultation block this file's "Resume here" section had noted
much earlier, since the user specified the design directly instead.
Full structure and reasoning: `docs/BUILDCHAIN.md`'s new "Application
directory" section (what was adapted from the tutorial vs. followed
as-is, e.g. dropping the DDE-specific `*RMEnsure` block since
ArchieSDK doesn't need it -- confirmed against a real ArchieSDK demo's
own shipped `!Run` file) and `~/.claude/makefile_conventions.md`'s new
"RISC OS Application Directories" section (general lessons for future
projects: `!`-in-target-name quoting, the `cp -r` repeat-deploy nesting
gotcha, the `*RMEnsure` toolchain-specificity point).

- **New `assets/generate_app_icon.py`** draws the pawn+die icon once
  at a square `WORK=320` canvas (same technique as
  `generate_icon_sprites.py`'s pawn art), then produces both the
  square-pixel (`!Sprites22`, mode 27, 34x34/17x17) and rectangular-
  pixel (`!Sprites`, mode 12, 34x17/17x9, via a 2:1 vertical squish of
  the same canvas before downsampling) versions Fryatt's Table 17.1
  specifies. Spot-checked at 10-15x nearest-neighbour zoom in both
  aspect ratios before committing -- both read clearly as "a red pawn
  next to a die showing five", even at the tiny half-size.
- **Makefile restructured**: `make all` now builds
  `build/!ArchiLudo/!RunImage,ff8` (objcopy's output path directly,
  not a separate rename step) plus copies in `!Run,feb`/`!Sprites,ff9`/
  `!Sprites22,ff9`/`PawnSprites,ff9` from their checked-in (no comma
  suffix) sources. `make deploy` now merges the whole directory into
  hostfs (`cp -r build/!ArchiLudo/. hostfs/!ArchiLudo/`, with the
  destination `mkdir -p`'d first to dodge the `cp -r` repeat-deploy
  nesting gotcha) and cleans up any pre-7.36 flat files left over from
  an older deploy. `make zip`/`make assets` updated to match.
- **No `src/game_view.c` changes needed at all** for `PawnSprites`/the
  debug `Log` to keep resolving correctly from inside the app
  directory -- `resource_path()`'s `set_app_dir()` already just
  truncates `argv0` at its last `.` separator, which lands on
  `HostFS:$.!ArchiLudo` (the app directory itself) whether the program
  was invoked as a bare file or as `!ArchiLudo.!RunImage` -- confirmed
  by reading that function before assuming a change was needed.
- Not yet confirmed live (double-click launch from the Filer, icon
  appearance under at least one square and one non-square screen mode
  per this project's own multi-mode testing convention).

**Round 7.37**: fixed a plain oversight in round 7.36 -- the whole
app-icon sprite/`IconSprites` pipeline was built and deployed, but
`main.c`'s `create_iconbar_icon()` was never actually changed to use
it, per live user report ("application dir works, but sprite does not
render... task bar icon is still the old AL letter one"). Changed from
a plain-text `"AL"` icon (`wimp_ICON_TEXT`) to a sprite icon
(`wimp_ICON_SPRITE`, `data.sprite = "!ArchiLudo"`) -- no local sprite
area needed, since `app/!Run`'s `IconSprites` line already loads it
into the Wimp's shared sprite pool before this task's `Wimp_Initialise`
even runs.

The Filer's OWN icon for the `!ArchiLudo` directory itself (as opposed
to the iconbar icon this round fixes) is a separate mechanism -- per
Fryatt's tutorial, the Filer only probes a directory's `!Sprites` file
the first time it scans the folder *containing* it, so a Filer window
that was already open (or already scanned hostfs) before `!Sprites`
existed there won't retroactively pick it up without being closed and
reopened (or the parent re-viewed fresh) -- not necessarily a code bug,
unconfirmed until re-tested after a Filer window refresh specifically
(not just relaunching the app itself, which is a different window).

**Round 7.38**: two app-icon fixes, per live user report ("Filer
directory icon still does not show. Icon bar icon does. On icon
itself: anti aliassing makes it fuzzy").

- **Crisp, non-antialiased icon**: every `Image.BOX` resize in
  `assets/generate_app_icon.py` switched to `Image.NEAREST` -- BOX
  blends across source pixels when downsampling (right for a soft
  photographic image, but a grey halo/blur at icon sizes this tiny).
  NEAREST samples one source pixel per destination pixel with no
  blending, matching classic RISC OS icon style. This alone broke the
  half-size (17x17) icon's outline/pips (NEAREST point-samples roughly
  one WORK unit every ~18.8 units at that size, so anything much
  thinner than that gap can fall entirely between sample points and
  vanish in some rows/columns) -- fixed by widening `outline_dilate`
  (10 -> 18 WORK units) and the die pip radius (14 -> 17), so both
  features reliably survive sampling at both sizes. Spot-checked at
  zoom in both sizes before committing, same discipline as round
  7.32's pawn-sprite fix.
- **Sprite names lowercased** (`!ArchiLudo`/`sm!ArchiLudo` ->
  `!archiludo`/`sm!archiludo`) -- an attempted fix for the Filer's own
  directory icon still not appearing even after the user retested with
  a fresh Filer window (ruling out the round 7.37 "just needs a
  refresh" theory). Matches Fryatt's tutorial's own literal example
  ("!examplapp"/"sm!examplapp" for a directory named "!ExamplApp"),
  on the theory that the Filer's directory-icon lookup normalises to
  lowercase before searching the sprite pool, unlike `Wimp_CreateIcon`'s
  own case-insensitive lookup (already proven working at the iconbar,
  round 7.37, with the exact-case name). `src/main.c`'s iconbar
  reference deliberately NOT changed to match -- proven case-
  insensitive already, nothing to fix there. **Not yet confirmed as
  the actual cause** -- still needs a live retest; if the Filer icon
  still doesn't appear after this, the lowercase theory is wrong and
  the real cause needs more investigation (a genuine hostfs/Arculator
  Filer-scan quirk is also possible, separate from anything this
  project's own code controls).

**Round 7.39**: app icon fully working (Filer directory icon confirmed
live -- round 7.38's lowercase-sprite-name theory was correct), two
follow-up fixes per live user report ("pips of dice look bit weird and
uneven though and top black line missing").

- **Top outline missing -- same bug class as round 7.33's pawn sprite,
  just not caught in time**: the die's own top edge (`y=8` in the raw
  design) minus round 7.38's widened `outline_dilate` (18) goes
  negative, clipping the dilated outline against the `WORK` canvas
  boundary itself. Checking the full combined pawn+die bounding box
  afterwards showed every edge was actually at risk (margins from -10
  to 0 units after dilation), not just the top -- the user's report
  happened to name the most visible one. Fixed the same way as round
  7.33: a new `CONTENT_SCALE = 0.85` (`assets/generate_app_icon.py`)
  shrinks every drawn coordinate around the canvas centre via new
  `sc()`/`sc_pts()`/`sc_len()` helpers, giving 12-21 units of real
  margin on every edge instead of hand-adjusting each coordinate.
  Verified this time by checking the actual generated alpha channel's
  four edges are fully transparent (zero) before even looking at a
  render, not just visually spot-checking.
- **Uneven pips -- switched from circles to squares**: a small circle
  downsampled by `NEAREST` point-sampling has no guarantee any given
  row/column of samples passes through its centre, so different pips
  (whose exact sub-pixel position varies slightly) ended up looking
  like different, irregular blob shapes. A square's straight edges
  align far more predictably with a `NEAREST` sample grid at these
  sizes -- the same reasoning that already favoured the die's own flat
  `rounded_rectangle` outline over a circular one.

**Round 7.40**: fixed the app icon's dice pips properly, per live user
report that round 7.39's square pips still "look bit weird and uneven"
across the four output sizes/aspects. Root cause finally identified:
drawing the pips into the shared `WORK=320` canvas and letting
`Image.NEAREST` resample them down to each of the four very different
output sizes (34x34, 17x17, 34x17, 17x9) meant each pip's exact
rendered shape depended on where NEAREST's sample grid happened to
fall relative to that pip's edges in `WORK` space -- which differs
between all four outputs, since each resizes by a different ratio (and
the rectangular ones from an already 2:1-squished intermediate canvas).
No amount of tuning the `WORK`-space pip size could fix this, since the
problem was the resampling step itself, not the shape being resampled
-- the same lesson `generate_icon_sprites.py`'s `build_pawn_image()`
already documents for its own highlight dither ("the dither pattern
must be chosen at the FINAL pixel grid, not the supersampled one").

Fixed by not resampling the pips at all: a new `DIE_BOX_WORK` +
`die_box_in()` analytically map the die's own (already `CONTENT_SCALE`'d)
bounding box into each output image's own native pixel coordinates
(accounting for that output's resize ratio and any pre-squish), and a
new `stamp_pips()` draws the 5 pips directly at that resolution, sized
as a fixed fraction of the die's own box in THAT output (`w/6`,
1-pixel floor) rather than a fixed `WORK`-space size. `build_icon_image()`
no longer draws pips into the shared canvas at all. This guarantees
identical relative layout (pips at 25%/50%/75% fractions of the die's
own box) and genuinely proportional sizing in every output, with zero
dependence on resampling luck -- the tiniest output (17x9,
rectangular-pixel half-size) still looks crude, but predictably so (a
real resolution floor, not arbitrary noise), which is the most that's
achievable at that pixel budget.

**Round 7.41**: fixed the die's own outline symmetry, per live user
report ("outline of die should be square, and is not always now as it
misses pixel in lower right corner") -- the same root cause as round
7.40's pip fix, just not yet applied to the die's own shape. The die
was still going through the shared WORK-space silhouette-dilate-then-
NEAREST-resize pipeline (the one that correctly serves the pawn's own
organic, curved outline), which can round each of a plain square's
four corners slightly differently once resampled -- a curved pawn
silhouette has room to visually absorb that asymmetry, a square has
none.

Fixed the same way as the pips: `draw_icon()` no longer draws the die
at all (pawn only now), and a new `stamp_die()` draws the die's body --
a solid black square with a white interior inset by a proportional
border width -- directly onto each output image at its own native
resolution, called (before `stamp_pips()`) at all four of `main()`'s
output sites. Two plain axis-aligned rectangles drawn directly in the
target's own pixel space can't end up asymmetric the way a resampled
rounded-rectangle can. Spot-checked at zoom in both sizes -- clean,
symmetric square corners in both.

**Round 7.42**: a hand pixel-editing round-trip for every sprite this
project ships, per explicit user request ("save PNG versions or even
better PSd versions of all sprites so i can try to pixel correct them
in Photoshop... convert the edited version back to our application
sprites"). Full writeup: `docs/GRAPHICS_TOOLING.md`'s own "Round 7.42"
section. Two new scripts (`make export-sprites`/`make import-sprites`):
`assets/export_sprites_for_editing.py` writes every sprite (4 pawns, 4
app-icon variants) into `assets/edit/` as native-resolution PNGs plus
16x-upscaled (`Image.NEAREST`, clean-block) editable copies, with a
`README.md` explaining the workflow; `assets/import_edited_sprites.py`
downscales any edited `_16x.png` back down via a majority-colour-per-
block vote, re-quantises against the fixed Wimp palette, and rebuilds
`assets/PawnSprites`/`assets/!Sprites`/`assets/!Sprites22` directly.
PSD export wasn't possible (Pillow can only read PSD, not write it,
and there's no reliable pure-Python alternative) -- PNG is the normal
working format for this kind of flat, hard-edged pixel art anyway, not
really a compromise. Verified lossless (export -> import with no edits
reproduces the original packed sprite files byte-for-byte) and that a
real edit correctly propagates through before handing this off.

**Round 7.43**: Phase 1 of the multiple rule-set / house-rule variant
system, per the approved plan at
`/home/xahmol/.claude/plans/i-want-to-scaffolf-dreamy-lantern.md` (see
this file's "Resume here" section for the plan's own summary and
rollout order). `include/game_logic.h` gained `ludo_variant`
(`LUDO_VARIANT_MEJN`/`_LUDO`/`_PACHISI`) and `ludo_rules` (7 toggles +
which variant produced them); `ludo_game` gained a `rules` field.
`ludo_init()`'s own signature and behaviour are unchanged (still sets
MEJN defaults internally, via the new `ludo_default_rules()`) --
`ludo_set_rules()` is the new, separate entry point for a non-default
ruleset, so every one of the many existing `ludo_init(&g)` call sites
across the test suite keeps testing MEJN behaviour unchanged.

Three toggles wired into their existing decision points:
- **`own_pawn_capture`**: `capture_at()` gained one guard clause
  (`if (p == player && !g->rules.own_pawn_capture) continue;`) --
  landing on your own pawn now simply shares the square when off.
- **`overshoot_bounce`**: a new shared helper,
  `resolve_move_destination()`, replaces the inline overshoot-reject
  math that used to live separately in `compute_movable_pawns()` and
  `ludo_move_pawn()` (they'd have silently drifted apart otherwise). When
  on, a roll that overshoots the home column's end bounces the pawn
  backward by the remainder instead of being illegal. Since this
  project's 4-square home column is *shorter* than a die's max value
  (unlike classic Ludo's 6-square stretch, which always exactly absorbs
  the largest possible overshoot), a big enough bounce can reach back
  past the home column's own entrance -- clamped there rather than
  spilling back onto the shared ring, a judgement call with no rule-book
  precedent found, documented inline. This also required making
  `home_column_blocked()` **direction-aware**: it used to assume
  `to_steps > from_steps` (forward-only movement) and would have wrongly
  reported "clear" for a bounce's backward path -- it now checks
  whichever direction `to_steps` actually indicates.
- **`mandatory_six_release`**: when off, `ludo_roll()`'s old
  unconditional auto-release block only runs `&& g->rules.mandatory_six_release`
  now; instead, `compute_movable_pawns()` offers releasing a home pawn
  as one of the player's ordinary movable choices (on a six, or -- with
  `no_six_needed_last_pawn` also on -- any roll, but only once it's
  truly this player's *last* pawn still at home) and `ludo_move_pawn()`
  gained an `if (!p->in_play)` branch performing the actual release when
  that choice is picked. Deliberately does not create the mandatory
  path's "must move this pawn next" obligation, since that exists to
  compensate for the release being involuntary -- it isn't, when chosen.

Five new tests in `tests/test_game_logic.c`, one per toggle plus the
`no_six_needed_last_pawn` combination, each starting from `ludo_init()`
and flipping exactly one toggle via `ludo_set_rules()` so a regression
elsewhere would show up in one of the many existing MEJN-default tests
instead of being masked. `make test`: 22 tests, all passing (up from
17). Full cross-compile (`make clean && make all`) also verified clean.
Not yet done: Phase 2 (the remaining four toggles, needs a Wikipedia
re-verification step first -- see "Resume here"), Phase 3 (AI), Phase 4
(UI), Phase 5 (save format), Phase 6 (docs, including this project's own
`docs/GAME_LOGIC.md`, updated alongside this round).

**Round 7.44**: Phase 2 of the multiple rule-set / house-rule variant
system -- the three toggles Phase 1 left unimplemented. Re-verified the
plan's two flagged rules against
<https://nl.wikipedia.org/wiki/Mens_erger_je_niet!> first, as required
before writing any code for them: the article's "Aangepaste
spelregels" section confirms both variants exist but gives essentially
no mechanical detail beyond permitting them in general ("soms staat men
toe dat een pion ook achteruit mag slaan" / "...of dat er binnen de
eindcirkels gemanoeuvreerd wordt") -- so both are implemented as this
project's own documented reading rather than a literal transcription
(full detail: `docs/GAME_LOGIC.md`'s "Backward movement and free
home-column" note).

- **`free_home_column`**: the simplest of the three -- `home_column_blocked()`
  gained a one-line early-return when the rule is on, disabling its
  single-file check entirely. `finish_threshold_for()` is untouched (a
  separate mechanism), so finished pawns still queue into distinct
  squares even with free manoeuvring active.
- **`blockade`**: a genuinely new mechanic -- three new internal
  helpers, `pawns_stacked_at()` (counts a player's own pawns on one ring
  square), `ring_blockade_at()` (2+ of some *other* player's pawns on a
  square = blockaded against everyone else), and `ring_path_blocked()`
  (walks every ring square a move would actually cross, in either
  direction, checking each for a blockade). Wired into
  `compute_movable_pawns()`'s ring-movement branch, and into both
  release paths (`ludo_roll()`'s mandatory auto-release and
  `compute_movable_pawns()`'s optional-release bit) via a fourth
  helper, `release_blocked_by_blockade()`, since a released pawn always
  lands on the player's own start square -- a barricade sitting there
  blocks entering play, not just ordinary ring movement.
- **`backward_movement`**: exposed via a genuinely new, parallel public
  API rather than folded into the existing forward-only one -- a pawn
  can have a legal forward move, a legal backward move, both, or
  neither for the same roll, which a single bitmask can't represent
  unambiguously. `ludo_movable_pawns_backward()`/`ludo_move_pawn_backward()`
  mirror `ludo_movable_pawns()`/`ludo_move_pawn()`'s own shape and
  internal-helper-sharing pattern (`compute_movable_pawns_backward()`,
  shared the same way `compute_movable_pawns()` already was).
  Restricted to pawns still on the shared ring (not the home column,
  which has its own separate toggle) and to not moving back past the
  pawn's own start square; still subject to `ring_path_blocked()` when
  `blockade` is also on. `ludo_roll()`'s and `ludo_no_move_possible()`'s
  own "is this player actually stuck" checks were both updated to also
  consult the new backward bitmask -- a player who can only move
  backward isn't stuck, even though the forward-only check alone would
  have said otherwise.

Eight new focused tests (one or two per toggle, covering both the
positive and negative case for blockade specifically -- landing-on vs.
passing-through, and blocked vs. unblocked release) plus a new 200-game
headless simulation under the *entire* Pachisi-style preset (every
toggle active simultaneously) checking looser invariants than the
original MEJN simulation (exact-arithmetic assumptions like "steps
after == steps before + roll" no longer hold once bounce-back and
backward movement are in play) -- steps-in-range and the finished-pawns-
occupy-distinct-squares invariant still do, and do still hold. `make
test`: 30 tests, all passing (up from 22). Full cross-compile also
verified clean. Not yet done: Phase 3 (AI adaptation), Phase 4 (UI),
Phase 5 (save format), Phase 6 (docs, beyond what this round and Round
7.43 already updated) -- see "Resume here".

**Round 7.45**: Phase 3 of the multiple rule-set / house-rule variant
system -- adapting `src/ai.c` to the configurable rules Rounds 7.43-7.44
introduced. Full detail in `docs/AI.md`'s own "Round 7.45" section;
summary:

- Extracted `score_landing_at()`, a helper shared by `score_move()` and
  the new `score_release()`, gating the own-pawn-collision penalty on
  `g->rules.own_pawn_capture` (previously unconditional -- a real bug
  the moment `own_pawn_capture` could ever be off) and adding a small
  `WEIGHT_BLOCKADE_FORM` bonus when landing there forms a blockade
  instead.
- Fixed a genuine mis-scoring bug: `score_move()` computed a move's
  destination as naive `p->steps + roll`, which under
  `g->rules.overshoot_bounce` could exceed `LUDO_TOTAL_STEPS` and get
  scored as finishing (or even winning) a move that actually bounces
  backward into an ordinary position. Fixed by exposing
  `game_logic.c`'s internal `resolve_move_destination()` publicly as
  `ludo_resolve_move_destination()` and having `score_move()` call that
  instead of duplicating the math a second time (and risking it
  drifting out of sync, exactly what caused this bug in the first
  place). Caught with a dedicated regression test constructed to score
  differently under the old vs. new math
  (`test_ai_scores_bounced_destination_not_naive_overshoot`).
- Added `score_release()`: releasing a pawn from home (reachable when
  `g->rules.mandatory_six_release` is off) was never a scored decision
  at all before this round -- under the original mandatory-release
  default, `ludo_roll()` always released automatically before any
  choice existed. First-pass heuristic (`WEIGHT_RELEASE_BASE`, scaled
  down per pawn already racing), reusing `score_landing_at()` for
  capture-on-landing since a release lands on (and can capture on) the
  player's own entry square exactly like an ordinary move there would.
- Added a new, deliberately simple parallel API for backward movement
  (`g->rules.backward_movement`): `ludo_ai_choose_pawn_backward()`/
  `score_move_backward()`. Per the plan's own framing, real backward-
  movement strategy is a stretch goal, not a v1 requirement -- this only
  needs to pick something legal without crashing, which it does (reuses
  `score_landing_at()` for a real capture bonus, otherwise just prefers
  retreating the least distance).

Five new tests in `tests/test_ai.c` (up from 9 to 14) plus a second
headless four-AI-game simulation under the full Pachisi-style preset
(mirroring `tests/test_game_logic.c`'s own engine-only equivalent from
Round 7.44), exercising `ludo_ai_choose_pawn()` and
`ludo_ai_choose_pawn_backward()` together across many complete random
games. `make test` green, clean cross-compile.

**Known gap, deferred to Phase 4** (see "Resume here" above):
`src/game_view.c`'s `advance_ai_turns()` still only calls
`ludo_ai_choose_pawn()` -- it doesn't yet fall back to
`ludo_ai_choose_pawn_backward()` when the forward bitmask is empty but a
backward move is legal. Harmless today (the WIMP shell can only ever
reach `LUDO_VARIANT_MEJN`, which never turns on `backward_movement`),
but must be fixed as part of Phase 4's UI work before a ruleset with
backward movement on becomes actually selectable, or an AI-controlled
game could livelock on such a roll.

**Round 7.46**: Phase 4 of the multiple rule-set / house-rule variant
system -- the "Rule Options" dialogue and its plumbing into New Game.

- **New module `src/rules_view.c`/`include/rules_view.h`**, modelled on
  `win_view.c`'s plain-Wimp-icons shape: a variant row (a click-to-open
  pop-up `wimp_menu`, same pattern `main.c`'s own iconbar/window menu
  already uses -- see riscos_wimp_reference.md's new note), a static
  one-line "Pachisi-style is a curated preset" caveat, 7 house-rule
  toggle rows (each a label plus two paired option icons), and OK/Cancel.
  Each toggle pair uses button type `wimp_BUTTON_RADIO` (11) with a
  shared non-zero ESG (1-7, one per toggle -- this project's first
  genuine multi-icon ESG use) so the Wimp itself enforces "only one of
  the two selected" on a real click; the app only has to sync the
  SELECTED bit by hand when state changes from code (opening the
  dialogue, picking a variant) -- via the same read-then-EOR-only-if-
  different pattern already established for `wimp_ICON_SHADED`. Toggles
  inapplicable to the current variant (per the plan's own applicability
  matrix, `VARIANT_HIDDEN_MASK[]`) get shaded, not hidden/recreated,
  matching this project's established convention. `rule_field()` maps a
  toggle index to its `ludo_rules` struct field so one generic code path
  (build/read/write) handles all 7 toggles instead of 7 repeated blocks.
- **`main.c`'s `Menu_Selection` dispatch** needed a new "which menu is
  open" check (`rules_view_menu_open()`) before its existing
  iconbar-menu handling, since RISC OS only ever has one menu open
  system-wide and a `Menu_Selection` event doesn't itself say which menu
  produced it -- this project's first time creating a SECOND distinct
  `wimp_menu` (see riscos_wimp_reference.md's new note on this gotcha).
- **`game_view.c`** gained `configured_rules` (mirroring
  `configured_name[]`/`player_is_ai[]`'s own existing pattern) plus
  `game_view_configure_rules()`/`game_view_get_rules()`;
  `game_view_new_game()` now calls `ludo_set_rules(&game,
  &configured_rules)` right after `ludo_init()`; `game_view_initialise()`
  seeds `configured_rules` to `ludo_default_rules(LUDO_VARIANT_MEJN)` so
  a game started before the Rules dialogue is ever touched behaves
  identically to before this system existed.
- **`setup_view.c`** gained a 4th button, "Rules..." (window widened,
  `BUTTONS_WIDTH` now accounts for 4 buttons not 3), its own
  `pending_rules` (mirroring `name_buffer[]`), and
  `setup_view_configure_rules()` (called by `rules_view.c`'s OK button --
  the two modules call each other directly, the same bidirectional
  pattern `game_view.c`/`win_view.c` already established in round 7.35).
  `setup_view_open()` now also syncs `pending_rules` from
  `game_view_get_rules()` whenever a game is already in progress, the
  same "always default to the in-progress game" convention round 7.35
  set up for player names/AI settings.

A genuine scope discovery while implementing this (see this file's
"Resume here" section for the full writeup, not repeated here): wiring
`ludo_ai_choose_pawn_backward()` into the actual board/AI-turn driver
turned out to need real path-building/click-handling work in
`game_view.c` that goes well beyond this phase's original "UI" scope --
deliberately NOT attempted this round, flagged as a real limitation
instead of quietly skipped.

`make test` unaffected (no engine/AI files touched) -- still 30
game-logic + 14 AI + 3 board-layout tests, all green. Clean
cross-compile, deployed to the Arculator hostfs
(`build/!ArchiLudo/` -> hostfs `!ArchiLudo/`). **Not yet manually
verified in Arculator** -- per the plan, this is exactly the phase that
needs a real on-hardware/emulator check (ESG exclusivity, popup menu
behaviour, per-variant shading, and that Start actually carries the
chosen rules into a real game) before being considered done.

**Round 7.47**: the round 7.46 live Arculator test above surfaced two
real layout bugs, from a real screenshot (not a secondhand description) --
the New Game dialogue's "Rules..." button and most of the Rule Setup
dialogue's own text were genuinely overflowing their icons, not just
looking cramped: RISC OS clips an icon's redraw to its own extent, so an
HCENTRED string wider than its icon loses characters off both ends
("Rules..." at a 100-unit button rendered as "ules.."; "Six-release" at
150/110-unit label/option boxes rendered as "Six-relea"/"andator"). Both
dialogues' column widths were sized on an optimistic guess rather than
this desktop font's actual metrics -- recalibrated from the screenshot
itself: roughly 14 OS units per character, plus ~16 units of
border/fill padding, which matches "Cancel" (6 chars) just fitting a
100-unit button elsewhere in `setup_view.c` while "Rules..." (8 chars)
didn't.

- **`setup_view.c`**: `BUTTON_WIDTH` 100 -> 110, and the "Rules..."
  button's own label shortened to "Rules" (5 chars, comfortably fits
  even the old width) -- both per explicit user request ("Cant we just
  make text Rules?").
- **`rules_view.c`**: a full layout redesign, not just wider boxes.
  `LABEL_WIDTH`/`OPTION_WIDTH` 150/110 -> 190 each (calibrated against
  the longest label/option string, "Six-release"/"Mandatory", at ~14
  units/char). The single-line Pachisi-authenticity caveat (64
  characters) was never going to fit one row at any of these widths --
  it's now two shorter hand-wrapped lines (`caveat_line_1`/`_2`,
  `ICON_CAVEAT_1`/`_2`) instead of one long clipped one.
- **Real radio icons, replacing the round 7.46 look-alike button
  pairs** -- the user's original request was genuine RISC OS radio
  buttons, and the first cut's two bordered/filled/HCENTRED text icons
  per toggle looked like a pair of push-buttons instead. Reworked per
  Steve Fryatt's "Wimp Programming In C", Chapter 18 ("Sprite Icons and
  Choosing Options") and Chapter 20 ("Radio Icons Revisited") -- each
  option is now one indirected text-and-sprite icon using the `S`
  validation command's two-sprite form (`Soptoff,opton`, the standard
  always-present Wimp Sprite Pool sprites for this exact purpose): no
  border, no fill, left-aligned sprite-then-label, and the Wimp itself
  swaps the sprite as `wimp_ICON_SELECTED` changes -- this project's
  code already only ever toggled that flag (`set_icon_selected()`), so
  no click-handling logic changed, only the icon definitions
  (`init_radio_icon()`, new). `wimp_BUTTON_RADIO` + the existing
  per-toggle ESG scheme (round 7.46) is unchanged and still what
  enforces mutual exclusivity.
- Since option text now lives in an indirected buffer rather than a
  plain icon's inline 12 bytes (a text-and-sprite icon's validation
  string -- needed for the `S` command -- has nowhere to live on a
  non-indirected icon), added `opt_text[TOGGLE_COUNT][2][12]`, filled
  once from `TOGGLES[]` at `rules_view_initialise()` time.

`make test` unaffected (no engine/AI/board-layout files touched) --
still 30 game-logic + 14 AI + 3 board-layout tests, all green. Clean
cross-compile under ArchieSDK (no new warnings), deployed to the
Arculator hostfs. **Not yet manually re-verified in Arculator** -- same
outstanding check as round 7.46's, now against the redesigned layout:
confirm the radio icons render/toggle correctly (sprite swap on click,
ESG exclusivity), text is no longer clipped anywhere in either dialogue,
and the now-taller/wider Rule Setup window still fits comfortably in
mode 15 (and ideally one other mode, per this project's multi-mode
requirement).

**Round 7.47.2**: a second live Arculator screenshot, three more fixes
per direct user feedback on round 7.47 above.

- **Round radio buttons, for real this time.** The round 7.47 toggles
  rendered as square tick-boxes with a cross pattern, not round radio
  buttons -- `RADIO_VALIDATION` had used `"Soptoff,opton"`, which is
  actually the sprite pair for an independent on/off *option* (a
  checkbox), not a mutually-exclusive *radio* choice. Fryatt's own
  guide (the same Chapter 18 round 7.47 was already following) says so
  explicitly, in the section covering exactly this multi-icon case:
  radio icons want `"Sradiooff,radioon"` instead. One-line fix --
  `set_icon_selected()`/the ESG scheme/everything else about the icons
  was already correct, only the sprite names were wrong.
- **`MEJN` written out in full**, per "is there room to write MEJN in
  full, so Mens Erger Je Niet?" -- there was (`VALUE_WIDTH` easily
  fits the 18-character full name). `VARIANT_NAMES[0]` changed
  accordingly (also spelling the third variant out as "Pachisi-style"
  rather than "Pachisi", matching the caveat text's own wording).
  Doing this uncovered a real constraint the first cut hadn't hit:
  `"Mens Erger Je Niet"` no longer fits a `wimp_menu_entry`'s plain
  12-byte inline text buffer, the same limit `src/main.c`'s
  `set_menu_entry()` stays under for its own (shorter) menu -- fixed by
  making the variant pop-up menu's entries indirected
  (`wimp_ICON_INDIRECTED` + `variant_menu_text[3][24]`), confirmed
  against ArchieSDK's own `oslib/wimp.h` that `wimp_menu_entry` shares
  the same `wimp_icon_data` union as an ordinary icon and so supports
  this the same way. `variant_text` (the value icon's own buffer) was
  also widened 16 -> 24 bytes for the same reason.
- **The caveat still didn't fit** even after round 7.47's two-line
  split -- that split's line 1 (41 characters) was still a few
  characters too long for a real render, meaning this file's
  14-units/char estimate undershot slightly. Rather than re-deriving
  the exact metric again, both lines were trimmed further with real
  margin (`caveat_line_1`/`_2`, now 32/30 characters) instead of tuned
  right up against the edge.

`make test` unaffected (still 30+14+3, all green). Clean cross-compile,
redeployed. **Still not yet manually re-verified in Arculator** -- same
outstanding checks as round 7.47's own note above, now against this
round's fixes specifically (do the toggles actually render as round dots
and swap correctly on click, does the full variant name display/menu
correctly, does the caveat now fit on both lines without clipping).

**Round 7.47.3**: from that same round of feedback, a follow-up the
user caught after the round 7.47.2 fixes landed: "the variant pulldown
now shows as a standard textfield, nothing to indicate it is a
pulldown. Cant we make it a visible recognisable pulldown?" -- a fair
catch. `ICON_VARIANT_VALUE` had been a single bordered/filled/
BUTTON_CLICK text icon this whole time, which reads exactly like an
ordinary writable box, with nothing marking it as a drop-down.

Fixed by implementing RISC OS's actual "pop-up menu field" convention,
not a homegrown look-alike -- split into two icons: `ICON_VARIANT_VALUE`
stays a plain bordered read-only display field (now `BUTTON_NEVER`,
`FIELD_WIDTH` wide), sat immediately against a new
`ICON_VARIANT_POPUP`, a small square sprite-only button using the
standard "gright"/"pgright" ("grey right-pointing arrow", raised/
pressed) Wimp Sprite Pool sprites via the `"R5;Sgright,pgright"`
validation string. The click that opens the variant menu moved from the
field to this new button. Based on Steve Fryatt's "Wimp Programming In
C", Chapter 24 ("Pop-up Menus and Other Features") -- confirmed both
the icon recipe (Listing 24.2's validation string) and the actual
screen layout (arrow button immediately after the field, not before or
overlapping it) against that chapter's own reference screenshot before
implementing, rather than guessing the arrangement -- see
[[archiludo_ground_truth_verification]]-style discipline applied to an
external doc source this time, not just this project's own code/GEOS
source.

`make test` unaffected. Clean cross-compile, redeployed. **Not yet
manually verified in Arculator** -- in particular, whether the arrow
button's raised/pressed sprite swap on click actually happens
automatically (Fryatt's text implies the Wimp handles this on its own
for an `R5` pop-up button, unlike the project's own `flash_throw_button()`
press-feedback in `game_view.c`, which manually swaps `R1` validation --
this hasn't been cross-checked against genuine RISC OS 3.10 behaviour,
only taken on the tutorial's word).

**Round 7.48**: a real regression report from the same test session --
"pawn placement on 6 roll ... First the pawn is drawn on a completely
weird position, then moves to a correct position after roll but leaving
the wrong one as artifact." A genuine bug, and a latent one: not
something this round's own UI edits touched, but the first time it was
ever actually reachable through the real WIMP shell, because the Rules
dialogue (round 7.46) is the first thing that ever let a real game
select a variant other than `LUDO_VARIANT_MEJN` -- and this bug only
fires under **optional** six-release (`rules.mandatory_six_release`
off, true for Ludo and Pachisi-style but not MEJN), a code path that
existed since round 7.43 but had never once run through `game_view.c`
before.

Diagnosed from the actual evidence, not guesswork: `!ArchiLudo/Log` in
the Arculator hostfs (this project's own file-based debug log, per
CLAUDE.md's Testing section) still had the exact session from the
screenshot, and showed the smoking gun directly --

```
resolve_roll: player=1 roll=6 movable_mask=0x1
start_move_animation: player=1 AI pawn=0 roll=6 steps 0 -> 0
```

`movable_mask` (not the `just_released` early-return) means this went
through the *optional*-release path in `ludo_move_pawn()`, not
`ludo_roll()`'s automatic mandatory one -- and `steps 0 -> 0` means the
resulting "move" travels zero cells (the pawn goes straight from "not
in play" to its own ring entry square). `start_move_animation()` builds
`move_anim_path[]` with exactly one valid entry for this
(`move_anim_path_len = to_steps - from_steps + 1 = 1`), but
`update_move_animation_area()`'s per-tick segment maths *always* reads
`move_anim_path[seg + 1]` as the segment's "to" cell -- for a 1-long
path that's `move_anim_path[1]`, which this call never wrote. Since
`move_anim_path[]` is a `static` array reused across every animation,
that slot still held whatever cell some *earlier* pawn's longer
animation had last left there -- a stale, unrelated board cell. That
leftover cell became this animation's bogus interpolation endpoint,
which is exactly "drawn on a completely weird position." Worse,
`resolve_move()` deliberately skips re-examining the just-animated pawn
in its own settle-diff redraw afterwards (`update_settle_diff_area()`'s
`skip_player`/`skip_pawn`, round 7.15 -- correct for a *real* move,
where the per-tick animation genuinely did paint the correct final
cell), so the bogus draw from the stale endpoint was never cleaned up
-- a permanent ghost, matching "leaving the wrong one as artifact"
exactly.

**Fix**: `start_move_animation()` now detects a 1-long path and
duplicates `move_anim_path[0]` into `move_anim_path[1]` (extending
`move_anim_path_len` to 2), giving the segment maths a real, correct
(if stationary -- from and to are the same cell) second point instead
of reading uninitialised/stale array data. The pawn now just sits still
through one short animation beat before settling, rather than
flickering to a garbage location -- a minor, harmless cosmetic
difference from an instant placement, not worth chasing further given
the actual bug (the stale-memory read) is what mattered.

`make test` unaffected (game_logic.c/board_layout.c/ai.c untouched --
this was purely a `game_view.c` animation-plumbing bug, not a rules
bug). Clean cross-compile, redeployed. **Not yet manually re-verified
in Arculator** -- ask the user to specifically retest an optional
six-release (Ludo or Pachisi-style variant, a 6 with a home pawn
available) and confirm no ghost pawn appears any more.

**Round 7.49**: a hand pixel-editing session on `pawn0.png` (the green
pawn) surfaced two real, independent bugs in the round 7.42 edit
pipeline, plus a genuine cross-mode rendering bug in the pawn art
itself. Full technical detail in `docs/GRAPHICS_TOOLING.md`'s own
"Round 7.49" section; summarised here:

1. **The two-file (`<name>.png` + `<name>_16x.png`) editing workflow
   was itself a footgun.** The user's first edit went to the native
   `pawn0.png` directly; `import_edited_sprites.py` silently ignored it
   because it always preferred the (stale, unedited) `_16x.png`
   sibling when one existed. Fixed by removing the 16x-upscaled
   "editing copy" entirely, per explicit user request ("update all
   workflows... to generate and edit the 26px variants directly") --
   `export_sprites_for_editing.py` now exports one native-resolution
   PNG per sprite, no `_16x.png`; `import_edited_sprites.py`'s
   `resolve_native_image()` dropped its 16x-preferred branch and
   `downscale_majority()` entirely, now just reading the native
   `assets/edit/<name>.png` at face value. `assets/edit/README.md`
   (regenerated by the export script) now recommends a real pixel-art
   editor (Piskel, or GIMP's Pencil tool with an alpha channel added)
   over MS Paint by name.
2. **MS Paint silently flattens alpha.** That same first edit came
   back fully opaque, background filled solid white instead of
   transparent -- MS Paint has no real alpha-channel support and
   flattens transparency on open/save; this is now called out
   explicitly in the regenerated README. Recovered (that one time) via
   a border flood-fill through contiguous near-white pixels, since the
   genuine highlight dot sits inside the silhouette and is unreachable
   from the canvas edge without crossing non-white pixels.
3. **A genuine cross-mode rendering bug, unrelated to either edit
   mistake above**: the round 7.19 highlight/shadow dither
   (`build_pawn_image()`'s `(x+y)%2` shadow checker and `(x+2y)%4`
   highlight stagger) keys its colour on the pixel's own row, `y`.
   These sprites are tagged mode 27 (2 OS units/pixel, both axes), but
   this project's other three supported modes (12/15/39) are 2x4 OS
   units/pixel -- twice as tall per physical pixel. RISC OS's sprite
   scaling doesn't blend when a sprite's tagged mode differs from the
   current screen mode; it drops every other source row to fit the
   coarser vertical grid (the same "sub-4-OS-unit features vanish"
   behaviour this project already found once for hand-drawn `os_plot`
   rectangles -- see CLAUDE.md's Testing section, [[archiludo_mode15_pixel_thickness]]).
   A `y`-parity dither disagrees between the two rows about to be
   fused on those three modes, silently losing whichever row didn't
   survive -- self-cancelling roughly half the intended texture rather
   than just softening it. Found and diagnosed from the user's own
   report ("the pixel exact dither does not survive[, because] the OS
   pixel is not screen pixel") on their second, alpha-clean hand edit.
   Fixed at the source (`build_pawn_image()`, both dither expressions
   now keyed on `y // 2`, a row-PAIR index, instead of `y`) and applied
   by an equivalent direct pixel fix to the user's actual edited
   `pawn0.png` (collapsing every currently-mismatched fill/shadow or
   fill/highlight row-pair to a fresh, consistent per-pair colour,
   scoped to only those three colours so outline/background/contour
   pixels are never touched) -- preserving the user's real shape edit
   rather than discarding it for a from-scratch regeneration. Verified
   by simulating the mode-12/15/39 row-drop directly (kept only even
   source rows, rescaled) and confirming it now matches the mode-27
   rendering's own texture exactly, where before the fix it would have
   diverged. A small residual (6 mismatched row-pairs in a fresh
   from-generator sprite, down from 23 before the source fix) remains
   at region-mask *boundaries* themselves (where the highlight/shadow
   membership boolean, not the dither colour choice, changes on an odd
   row) -- accepted as the same category of minor, unavoidable
   precision loss the outline/silhouette contour already has on a
   coarser mode, not chased further.
4. Deduced pawn1/pawn2/pawn3 (red/blue/yellow) from the corrected green
   pawn0 both times (the alpha-recovery and the dither fix) via an
   exact-match pixel recolour -- `generate_icon_sprites.py`'s own
   design uses a shared black outline/white highlight/grey shadow
   across all four pawns, only the fill hue differs per player
   (`PLAYER_WIMP_COLOUR` into `tools/riscos_sprite.py`'s
   `WIMP_COLOURS`), so swapping every pixel exactly equal to the green
   fill RGB to each player's own fill RGB carries the user's actual
   shape edit over to all four colours untouched otherwise -- confirmed
   via pixel-colour histograms that all four came out structurally
   identical (transparent/outline/shadow/highlight counts) except for
   the fill hue.

`make test` unaffected (asset-only + Python tooling changes, no C/
build-chain files touched). Clean cross-compile, redeployed. **Not yet
manually verified in Arculator** -- ask the user to confirm all four
pawn colours look right, in particular that the shadow/highlight
texture now reads consistently rather than patchy/missing, in at least
mode 15 (non-square) and mode 27 (square) per this project's own
multi-mode requirement.

**Round 7.50**: a direct follow-up, per explicit user rules for the
pawn art ("clear two pixel black outline", details must "scale so that
they look the same in all modes", white highlight "subtly somewhat
bigger" and the shadow smaller) and for the app icon's dice pips
("matching in all modes", "whitespace should remain between pips and
borders and pips amongst each other"). Full technical detail in
`docs/GRAPHICS_TOOLING.md`'s own "Round 7.50" section; summarised here:

- Backed up the round 7.49 hand-edited pawns to
  `assets/edit/reference/` before touching anything further, per
  explicit user request -- historical record only now, since the user
  then chose to adopt this round's refined script output as the new
  shipped baseline rather than keep the hand edit authoritative.
- **Pawn outline**: `build_pawn_image()`'s alpha now uses `Image.NEAREST`
  instead of a smoothing `Image.BOX` resize (reversing round 7.17's
  original choice), and `OUTLINE_DILATE_WORK` retuned from 17 to 21 --
  both changes verified by direct pixel-histogram comparison against
  the hand-edited reference until the generator's own output landed
  within a few pixels of it (a first guess of 28 overshot badly). Fixes
  a real soft grey-halo artifact the old settings produced (confirmed
  via pixel counts: ~70 partial-alpha pixels in the old output, zero in
  the hand edit), and closes a latent risk that the old sub-2px average
  outline could vanish outright at some points on non-square modes,
  per this project's own established "features under 4 OS units
  vanish" rule.
- **Highlight/shadow region size**: `highlight_shapes()` enlarged ~15%,
  `shadow_shapes()` shrunk ~15% (each scaled about its own centre) --
  a direct, modest read of the user's stated "feel" preference from
  comparing generator output against the hand-edited reference, not an
  exact measured target.
- **Dice pips, a real whitespace bug**: `stamp_pips()` used to position
  pips by fixed fraction of the die box with no explicit minimum-gap
  guarantee at all -- confirmed by rendering and zooming the actual
  shipped icon that pips visibly merge into a solid blob at this
  project's smallest ("half") size. Replaced with a hard integer grid
  (`_pip_axis_layout()`: border+pip+gap+pip+gap+pip+border along each
  axis, every segment >=1px), reusing `stamp_die()`'s own border
  formula. While testing this, found and fixed a real rounding-mismatch
  bug: `stamp_die()` drew from the raw float die box while the new
  `stamp_pips()` rounded its own copy independently, breaking pip
  alignment badly at the smallest rectangular size -- fixed via a
  shared `_round_box()` helper both functions now use identically.
  **One hard, un-tunable limit remains** (accepted as-is, per explicit
  user decision): `half_rect`'s die interior is only 2px tall after its
  own border, and 3 pip-rows with real gaps need at least 5px -- two
  rows visually merge at that one size/mode-aspect combination only
  (the least-scrutinised view, a Filer small-icon on a non-square
  mode); the other three output sizes all show clean gaps throughout.

`make test` unaffected. Clean cross-compile, redeployed. **Not yet
manually verified in Arculator** -- ask the user to confirm the outline
crispness/highlight-shadow feel on all four pawn colours, and that the
dice pips now show clear whitespace in the iconbar/Filer icon, in at
least mode 15 and mode 27.

**Round 7.51**: two direct fine-tuning follow-ups from live user
feedback on round 7.50's actual rendered result. Full technical detail
in `docs/GRAPHICS_TOOLING.md`'s own "Round 7.51" section:

- **Pawn head made rounder**, per "my hand edited pawn was a bit
  'rounder' on the top" -- measured, not just eyeballed: counted opaque
  pixels per row and found the script's head silhouette plateaus at a
  constant width for 11 consecutive rows (reading as a flat-sided
  cylinder) versus the hand-edited reference's 5, peaking higher too
  (14px vs 12px). Root cause: at this generator's small final
  resolution, a circle's own width barely changes near its equator, so
  several rows round to the same integer width -- more pronounced the
  smaller the circle. Fixed by enlarging the head ellipse (120x120 ->
  138x128), top edge deliberately kept at its original canvas position
  rather than grown symmetrically, since growing upward too measurably
  approached this project's own established canvas-clipping risk zone
  (round 7.33's history) once combined with round 7.50's own thicker
  outline dilate.
- **Dice pips still touched the die's own border** -- a real gap in
  round 7.50's own fix, confirmed by direct user report ("the app icon
  pips now have no white space to upper and left border") and by
  rendering the die in isolation. `_pip_axis_layout()` had only ever
  reserved gaps BETWEEN pips, never a margin between the outermost pips
  and the border -- fixed by reserving 2 more segments for that. Since
  the true minimum interior for real margins everywhere is 7px, and
  this project's own primary dev/test mode (mode 15, via `full_rect`'s
  own height axis) only had 5, also enlarged `DIE_BOX_WORK` (156x156 ->
  170x180, per the user's own suggested direction) -- grown
  asymmetrically toward this canvas's own unused space, verified by
  direct rendering to avoid both the pawn silhouette and canvas-edge
  clipping. `half_rect`'s own height axis remains the same accepted
  hard floor from round 7.50 (2px interior even after this growth,
  needs 7) -- everything else now shows real margin on every side.

`make test` unaffected. Clean cross-compile, redeployed. **Not yet
manually verified in Arculator** -- ask the user to confirm the head
now reads rounder and the pips show real whitespace to the border on
every side, not just upper/left.

**Round 7.52**: two more real bugs found from the round 7.51 live
result, one game logic/display, one purely cosmetic. Full technical
detail in `docs/GRAPHICS_TOOLING.md`'s own "Round 7.52" section for the
head-shape correction; summarised here for both:

- **A real duplicate/ghost-pawn bug**, per direct user report ("pawn
  is not removed in base area... now more than four pawns" for both
  green and blue) and a crucial follow-up clue ("only happens after a
  pawn is thrown from the board before"). Root cause, found by
  re-reading `start_move_animation()`: for an *optional* six-release
  (`rules.mandatory_six_release` off) with zero forward travel
  (`to_steps == from_steps == 0`), round 7.48's own fix built a
  *stationary* animation sitting at the ring entry cell -- which
  stopped the earlier "drawn at a random position" symptom, but never
  touched the pawn's TRUE previous on-screen position, its home base
  slot (`board_home_base_cell()`, keyed by `pawn_index`, nothing to do
  with `cell_for_steps()`'s ring/home-column-only mapping) -- so that
  slot's old rendering was simply never erased, left as a permanent
  duplicate forever after. (The "only after being captured" detail
  turned out to be about when the bug became visually obvious, not a
  separate cause -- confirmed `capture_at()` already resets a sent-home
  pawn's `steps` to 0, so a first-time release and a post-capture
  re-release are identical at the code level.) Fixed by recognising
  there is no meaningful animation to run here at all -- home base and
  the ring aren't the same track, nothing to visually "slide along" --
  so this now settles exactly like the mandatory six-release already
  correctly does: a plain diff redraw via `update_settle_diff_area()`,
  which uses the in_play-aware `board_pawn_cell()` and so naturally
  finds and erases the true old position. Supersedes round 7.48's
  approach for this specific case rather than patching it further.
- **Pawn head "cropped at the top"**, a real regression from round
  7.51's own head enlargement, not a rendering/cache bug (ruled that
  out first: the sprite PNG, the packed sprite file round-tripped back
  out, and the deployed hostfs copy all matched byte-for-byte and
  showed no clipped data -- confirmed instead by directly comparing
  round 7.51's output against round 7.50's own baseline row-by-row: the
  baseline had 2 clean solid-outline "cap" rows before fill started
  peeking through; round 7.51's bigger ellipse shrank that to 1,
  reading as a flattened/truncated top rather than a rounded one).
  Retuned smaller (was heading toward matching the reference's own peak
  width exactly; settled for a smaller enlargement that restores the
  full 2-row cap while still measurably rounder than the pre-round-7.51
  original) -- verified against the hand-edited reference's own pixel
  histogram, which this version matches even more closely than round
  7.51's first attempt did.

`make test` unaffected (the ghost-pawn fix touches only
`game_view.c`'s animation plumbing, same as round 7.48; the head fix is
asset-only). Clean cross-compile, redeployed. **Not yet manually
verified in Arculator** -- ask the user to specifically retest: an
optional six-release after a capture (to confirm no duplicate pawn
remains in the home base corner), and the pawn head shape on all four
colours (confirming the top no longer looks cropped/flat).

**Round 7.53**: a testing-coverage gap, prompted by the user asking
whether the test suite covers every possible rule-toggle combination --
it didn't. Before this round, `tests/test_game_logic.c`'s headless
full-game simulations only exercised the 3 curated presets
`ludo_default_rules()` offers (MEJN, Ludo, Pachisi-style); the Rules
dialogue (`src/rules_view.c`) actually lets a player flip any of the 7
house-rule booleans individually on top of any preset, so the real
reachable configuration space is all 2^7 = 128 combinations, not just
3. Rather than hand-writing tests for a meaningful subset or reaching
for randomised fuzzing, 128 turned out small enough to enumerate
**exhaustively** -- every single combination, not a sample -- each run
cheaply (5 games rather than the 200-500 a single-preset simulation
uses). New `test_headless_all_rule_combinations()`, modelled on the
existing Pachisi-preset simulation's own loose, toggle-agnostic
invariants (steps stay in range, finished pawns occupy distinct correct
squares, every game terminates -- not pinning down each toggle's exact
behaviour, which the focused per-toggle tests already do). Confirmed by
inspection that `ludo_rules.variant` itself is never read by any
gameplay logic (only ever *set*, inside `ludo_default_rules()`, for the
Rules dialogue/save-file's own bookkeeping) -- fixed at
`LUDO_VARIANT_MEJN` throughout since only the 7 booleans actually
matter. `make test` now runs 31 game-logic tests (up from 30) -- the three
headless simulation functions combined now play 1340 full games (500 +
200 + 128*5) -- still completing in under 2 seconds -- exhaustive
coverage of the whole reachable rule space cost about as much runtime
as one more chunk of the existing simulations, not 128x it.

**Round 7.54**: a real leftover-highlight-ring bug, per direct live
user report ("a left over highlight artifact") on a pawn nowhere near
the one actually just moved. Root cause, found by re-reading
`try_move_pawn()`/`start_move_animation()`: when a human player has
MORE THAN ONE legal move, `draw_highlights()` shows a ring around every
candidate pawn (not just the eventual choice). Clicking one calls
`start_move_animation()` for just that pawn, whose own per-tick redraw
(`update_move_animation_area()`) only ever touches the CHOSEN pawn's
own path cells -- the other candidates' cells, each still showing a
ring, are never touched by anything again once `step` moves on to
`STEP_MOVING`, leaving them as permanent ghosts until an unrelated full
window redraw happens to paint over them. Fixed by clearing the
highlight area at the very top of `start_move_animation()`, before
anything else changes -- `step`/`current_player`/`last_roll` are all
still exactly what they were when the rings were drawn at that point,
which is what lets `update_highlight_area()` correctly find the same
cells to erase; forcing `highlight_flash_on` off first guarantees an
actual erase rather than just re-confirming whatever was already
showing. Safe to call unconditionally from every caller of this shared
function (the AI and single-choice-auto-move paths never had a ring
showing in the first place, and `update_highlight_area()` is already a
no-op when nothing is highlighted).

`make test` unaffected (`game_view.c` display-only fix). Clean
cross-compile, redeployed. **Not yet manually verified in Arculator**
-- ask the user to specifically retest a human turn with more than one
legal move: click one of several highlighted pawns and confirm no ring
lingers on any of the others afterward.

**Round 7.55**: prompted by a live bug report ("red... lands on the
block, capturing both pieces") that turned out not to be a bug at all
-- the user was playing the `Ludo` preset, which has `blockade` off by
design (only `own_pawn_capture` off, permitting the stack but granting
it no protection) -- confirmed by an exhaustive headless scan against
the real engine (every possible attacker position vs. a fixed blockade
square) finding zero cases where a blockaded path was ever incorrectly
reported movable. That result prompted the user to ask for a proper
audit against two independent external Ludo rules references, which
did turn up real gaps. Full technical detail in `docs/GAME_LOGIC.md`'s
own "Round 7.55" note; summarised here:

- **`Ludo` preset's `blockade` default fixed** (0 -> 1) -- both external
  sources describe blocking as unconditional in standard Ludo, not
  optional, so `own_pawn_capture=0` alone (permitting a stack) without
  `blockade=1` (protecting it) doesn't match real Ludo. This was, in
  effect, the actual design gap the "bug" report surfaced, just not the
  one the report itself suspected.
- **New `three_sixes_forfeit_turn` toggle**: both sources independently
  describe a player's third six in a row as voiding the whole turn
  (release/move/nothing), which this engine had never implemented for
  any preset. Added as its own toggle rather than folded into `Mens
  Erger Je Niet`'s traditional unlimited six-chaining, per explicit
  user decision -- on by default for `Ludo` (source-backed), off for
  `Pachisi-style` (no source evidence either way) and `Mens Erger Je
  Niet` (preserves original behaviour exactly). Implementation: a new
  internal `ludo_game.consecutive_sixes` counter, incremented in
  `ludo_roll()` on every six and reset in `ludo_end_turn()` (so it
  starts fresh for whoever plays next regardless of *why* the previous
  turn ended); the third six calls `ludo_end_turn()` immediately,
  before the mandatory-release check even runs, reusing the exact
  same "turn passed during this roll" detection every caller (WIMP
  shell and the test harness alike) already has for the unrelated
  "three tries looking for a six while stuck" case -- no caller-side
  changes needed at all. Two regression tests
  (`test_three_sixes_forfeit_turn`/`test_three_sixes_no_forfeit_when_
  rule_off`), `make test` now 33 game-logic tests.
- **Two claims from one of the two sources deliberately NOT adopted**
  (a blockade "moving as one unit, splitting the die roll," and a
  "must capture before entering the home column" requirement) -- not
  corroborated by the other source or by general Ludo knowledge, noted
  in the docs as rejected rather than silently ignored.
- **`src/rules_view.c` extended to 8 toggles** -- the new toggle needed
  UI exposure too, matching the existing pattern exactly
  (`TOGGLE_COUNT`, `TOGGLES[]`, `VARIANT_HIDDEN_MASK` all designed to
  scale via that one constant; the window's own height already derives
  from `TOGGLE_COUNT` so no manual resizing was needed). Hidden for
  `Mens Erger Je Niet` specifically, same reasoning as blockade/
  backward/free-home already were -- not part of that preset's own
  traditional identity.
- **Graphical blockade stacking offset**, separately requested by the
  user in the same conversation: `plot_pawn()` used to draw every pawn
  dead-centre on its own cell, so 2+ same-coloured pawns sharing a
  square (a ring blockade, or free home-column manoeuvring) rendered as
  a single sprite with the others completely invisible. New
  `stack_offset()` nudges a pawn into one of 4 corner slots (keyed on
  its own `pawn_index`, not draw order, so a given pawn always lands in
  the same slot relative to its siblings) whenever another of the same
  player's own in-play, unfinished pawns shares its exact cell -- both
  the sprite and `os_plot` fallback paths pick this up automatically,
  since it's applied to the shared `wx`/`wy` before either path reads
  them. Only applied when NOT mid-animation (an animating pawn is a
  single pawn in transit, not meaningfully "stacked" at any instant).

`make test`: 33 game-logic (up from 31) + 3 board-layout + 14 AI tests,
all green. Clean cross-compile, redeployed. **Live-verified in
Arculator** -- user confirmed "seems to work as intended now" after
playing a real game.

**Round 7.56**: a documentation-debt reconciliation, per explicit user
request ("What do we have left on to do list?") after confirming round
7.55's fixes worked -- this file's own "Current state"/"Roadmap" tables
had drifted a long way from reality (last substantively updated early
in the project) and no longer reflected months of subsequent work:
"Phase 2" (real art) and "Phase 5" (AI/dialogues/app packaging) were
both marked "not started" despite being done, and an entire unscheduled
future phase (multi-rule-set variants) had been fully implemented ahead
of several "earlier" phases. Reconciled against the actual codebase
state directly (checked for `lib/qtm.c`/`docs/QTM.md` -- absent, checked
`save_view.c` for rules serialisation -- absent, checked `ai.h` for
difficulty-level implementation -- only `NORMAL` has real strategy,
checked `README.md` -- no screenshots section), not just trusted the
stale table. Also recorded an explicit user decision in the same
conversation: **the planned full-screen enhanced-graphics mode
(`archie-face`-based, double-buffered/VSync-synced, see `CREDITS.md`)
is dropped** -- staying with the current windowed
`Wimp_RedrawWindow`/`Wimp_UpdateWindow` board view, which has had
enough of its own animation/flicker work by now (rounds 7.10 onward) to
stand on its own. Both tables rewritten to a compact, accurate status
summary; this file's own "Resume here" section (which had similarly
drifted, still narrating rounds 7.43-7.46 as "in progress") rewritten
to cover only genuinely open, non-obvious gaps instead of re-summarising
history the tables and round log already cover -- see its own new text
for the two real gaps carried forward (the backward-movement
board-interaction gap from round 7.46, still unresolved; save-file
rules persistence, never implemented). No code changes this round --
documentation only.

**Round 7.57**: closes one of round 7.56's two carried-forward gaps
(save-file rules persistence) and a second issue reported live in the
same conversation (drag-and-drop save/load "still does not seem to
work, and also does not have a draggable app icon"), per explicit user
request. Two independent fixes:

- **Save-file rules persistence** -- `game_view.c`'s `serialize_game()`/
  `deserialize_game()` now read/write a 9-byte `ludo_rules` block (1
  byte per field: `variant` plus the 8 house-rule booleans, see
  `game_logic.h`) right after the file magic, which is bumped
  `"ALS1"` -> `"ALS2"` accordingly (`game_view_load_from_path()`
  cleanly rejects old `"ALS1"` saves rather than partially loading
  them -- an accepted "breaks old saves" trade-off for this hobby
  project). `GAME_VIEW_SAVE_FILE_SIZE` (`game_view.h`) grew by 9 bytes
  to match. On load, the rules are applied via `ludo_set_rules()` to
  `game.rules` **and** separately copied into the static
  `configured_rules` -- the latter is what `setup_view_open()` actually
  reads when the Rules dialogue is opened on an in-progress game, so
  without this second assignment a loaded game would still show stale
  (pre-load, usually MEJN-default) settings if the player opened Rules
  afterwards, even though gameplay itself was correctly using the
  loaded ruleset.
- **Drag-and-drop save/load icon** -- researched genuine RISC OS
  convention for a Save-window "draggable file icon" (no Fryatt
  tutorial covers this specifically; confirmed via the PRM's
  `DragASprite`/`osbyte` chapters, already available as full typed
  OSLib bindings in ArchieSDK's SDK with no custom SWI wrapper needed).
  `save_view.c`'s file icon changed from a bordered/filled text button
  (`"Drag"` label) to a plain `wimp_ICON_SPRITE` icon showing
  `sm!archiludo` -- matching how a real file icon looks, not a button.
  The drag itself now prefers `xdragasprite_start()` (a genuine dragged
  sprite under the pointer, `dragasprite_HPOS_CENTRE |
  dragasprite_VPOS_CENTRE | dragasprite_NO_BOUND |
  dragasprite_DROP_SHADOW`) over the previous plain `wimp_drag_box()`
  outline, gated on the user's own preference read from CMOS RAM (byte
  28, "FileSwitch options", bit `0x02`) via `osbyte2(osbyte_READ_CMOS,
  ...)` -- the PRM is explicit that this preference must be respected,
  not assumed. Falls back to the outline drag if the CMOS bit says
  outline-preferred, or if `xdragasprite_start()` itself returns an
  error. `save_view_drag_ended()` correspondingly calls
  `dragasprite_stop()` unconditionally first when a sprite drag was
  started, per the PRM's instruction that Stop must be called on every
  `User_Drag_Box` regardless of how the drag ended. The underlying
  `Message_DataSave`/`Message_DataSaveAck` file-transfer handshake was
  already structurally correct (confirmed by code review this round)
  and is unchanged -- only the drag's visual/trigger mechanism changed.

Both fixes build clean under ArchieSDK (no warnings) and are deployed,
but **neither has been manually verified live in Arculator yet** -- see
this file's "Resume here" section above for the specific things to
check on the next live-test pass (a non-MEJN save/reload round-trip,
and the sprite-drag file icon's look/behaviour with both CMOS
preference settings).

**Round 7.58**: live Arculator testing of round 7.57's drag-and-drop
work found it genuinely didn't work ("Drag does not show any icon
still, and path does not change on moving to another dir. Dragging in
save game does not do anything") -- an extensive, purely diagnostic
session (no feature code changes) tracing the actual live control flow
via temporary `debug_log()` instrumentation added throughout
`save_view.c`'s click/drag/message-handling functions, since code review
alone had already confirmed everything looked structurally correct
against the PRM. Findings, in order:

- The plain `wimp_drag_box()` outline WAS starting correctly (confirmed
  via logged `wimp_DRAG_SELECT` button-state detection and the drag
  call's own parameters) -- the CMOS `FileSwitch` "drag sprites"
  preference was off on this machine, so the outline (not a sprite) was
  the *correct*, expected behaviour, not a bug. Walked the user through
  setting the CMOS bit directly via `OS_Byte` 161/162 from BASIC
  (`SYS "OS_Byte",161,28,0 TO ,,V%` / `SYS "OS_Byte",162,28,V% OR 2`) --
  no friendlier `*Configure` keyword exists for this specific bit.
- With the CMOS bit set, `xdragasprite_start()` also succeeded (no
  error) every time -- still no drag icon visible.
- `Message_DataSave` was being sent successfully in every attempt (7+
  total across both drag mechanisms) -- `Wimp_SendMessage` never
  returned an error, and resolved to a real, *consistent* task handle
  across repeat drops on the same window. The drop point was confirmed
  (via `Wimp_GetWindowState`, which works on any window handle, not just
  ones this task owns) to genuinely fall inside the target window's own
  rectangle, not its title bar/border.
- `Message_DataSaveAck` never arrived in reply -- not once, across every
  attempt, including a drop directly onto the icon bar's `HostFS` icon
  (`pointer.w == wimp_ICON_BAR`, the simplest and most unambiguous RISC
  OS drop target there is, which also got no reply). Since every attempt
  this session happened to target HostFS specifically (Arculator's own
  bridge to the real host filesystem, not a full ADFS/FileSwitch stack),
  the most likely explanation is that HostFS's module simply doesn't
  implement the Filer side of the `Message_DataSave` protocol at all --
  an environment limitation, not an ArchiLudo bug. Superseded by round
  7.59 before this could be confirmed against a different filing system.

**Round 7.59**: rather than keep chasing round 7.58's unresolved
environment question, per explicit user request ("Maybe we should pivot
and use a dialogue with 5 renamable save slots that save within the
application dir with fixed names, with the save slot name part of the
save data. That removes the whole path and file naming problem") --
drag-and-drop and free-form pathnames are removed entirely, replaced
with 5 fixed save slots:

- `game_view.h`/`game_view.c`: new `GAME_VIEW_SLOT_NAME_LEN` (32), save
  magic bumped `"ALS2"` -> `"ALS3"` with a slot-name block added right
  after it (zero-padded, not just NUL-terminated, so a shorter re-save
  fully overwrites a longer old name). `game_view_save_to_path()` gained
  a `name` parameter; new `game_view_peek_slot_name()` reads just the
  4+32-byte header of a slot file (magic + name) without deserialising
  the whole game, for the dialogues' own slot-list population.
- `save_view.c` rewritten end-to-end: `save_view.h`'s whole `DragASprite`/
  CMOS-preference/`Message_DataSave` machinery removed (including the
  round 7.58 diagnostic instrumentation, no longer needed). Both
  dialogues now show 5 rows for fixed paths `<ArchiLudo$Dir>.Slot1` ..
  `.Slot5` (`build_slot_path()`) -- the Save dialogue's rows are writable
  name fields (pre-filled with the slot's current name, or "Slot N" if
  empty) each with its own Save button; the Load dialogue's rows are
  read-only name display ("(empty)" with a shaded, inert Load button for
  an unoccupied slot) each with its own Load button. Both dialogues
  re-read all 5 slots via `game_view_peek_slot_name()` every time they
  open, so the list is never stale. Per explicit user preference, kept
  as two separate dialogue windows (reached via the existing "Save
  Game"/"Load Game" iconbar menu entries) rather than one combined
  window.
- `main.c`'s `wimp_USER_MESSAGE`/`wimp_USER_MESSAGE_RECORDED` handling
  trimmed to just the `Message_Quit` check -- nothing else needs
  routing now that `save_view_message_received()` is gone. The
  `wimp_USER_DRAG_BOX` case was removed entirely.

Builds clean under ArchieSDK (no warnings) and is deployed, but **not
yet manually verified live in Arculator** -- see this file's "Resume
here" section above for what the next live-test pass should check.

**Round 7.60**: the audio layer, per explicit user request ("Implement
audio layer with suggested mods and fx") following an earlier research
pass (2 ModArchive tracks picked by the user, 6 sound-effect candidates
researched and proposed) and a follow-up request that music be
"selectable and optional... switch track and switch music on off" from a
menu. See [QTM.md](QTM.md) for the full technical writeup; summarised
here:

- **New `lib/qtm.c`/`include/qtm.h`** -- this project's first dedicated
  SWI-wrapper library under the `lib/` directory (per CLAUDE.md's own
  "lib/<name>.c" convention, not yet exercised before this round; the
  Makefile's `SRCFILES`/pattern rules extended to build it alongside
  `src/*.c`). Wraps QTM's own SWIs (`QTM_Load`/`QTM_Start`/`QTM_Clear`/
  `QTM_PlayRawSample`, confirmed via a real working ArchieSDK example and
  the RISC OS Open forum, not guessed -- see QTM.md's SWI reference
  table) via `_kernel_swi()` rather than OSLib's simpler `__swi()`
  attribute, since `QTM_PlayRawSample` needs 9 registers with R0 both an
  input and an output. Presence-checked at startup via
  `xos_swi_number_from_string("QTM_Load", ...)` (an X-form lookup, not a
  hardcoded assumption) -- every other function in the library is a
  silent no-op if QTM isn't available, matching the pawn-sprite-loading
  precedent's "stay playable if an extra falls through" principle.
- **Sample-format research** -- QTM_PlayRawSample needs "8-bit
  logarithmic" (VIDC-format) sample data, confirmed via the RISC OS Open
  forum thread to be close to but distinct from standard mu-law (a real
  trap: the thread's own author tried several plausible-looking wrong
  formats first, all producing barely-recognisable distorted playback).
  Rather than hand-roll an undocumented encoder, `lib/qtm.c` converts
  bundled raw 16-bit PCM samples to VIDC log format **at runtime**, on
  the real target machine, via the RISC OS Sound system's own
  `Sound_SoundLog` SWI (confirmed against its RISC OS 3 PRM entry) --
  guaranteed correct without needing the table's internals, at the cost
  of one open question (the `<<16` scaling used to feed a 16-bit sample
  into the SWI's 32-bit input) that's reasoned, not confirmed against a
  working example, and flagged as the first thing to check if playback
  sounds wrong.
- **Assets**: 2 background music tracks (ProTracker `.mod`, downloaded
  directly from The Mod Archive's API endpoint -- the ordinary web UI
  blocks automated fetches with a 503, the API download endpoint
  doesn't; confirmed genuine 4-channel ProTracker via file magic before
  bundling) and 6 one-shot SFX (dice throw, pawn release, per-move tick,
  capture, reaching home, game won) sourced from Kenney's CC0 packs plus
  one CC0 NES-style victory fanfare (trimmed to 4s -- the original
  ~11s original would have used a disproportionate ~330KB as an 8-bit
  runtime buffer on the ARM2/1MB profile), converted to raw 16-bit mono
  PCM via `ffmpeg`, all bundled flat in the app directory (`QTMModule`,
  `Music1`/`Music2`, `Sfx*`) via new Makefile rules -- see CREDITS.md for
  full attribution and QTM.md for the exact conversion commands.
  `app/!Run` gained an `RMEnsure QTM 1.49 RMLoad <ArchiLudo$Dir>.QTMModule`
  line (only loads ArchiLudo's own bundled copy if a suitable version
  isn't already resident).
- **Wired into 6 game events** in `src/game_view.c`: dice throw
  (`start_roll_animation()`), pawn release (`start_move_animation()`'s
  zero-distance branch AND `resolve_roll()`'s `just_released` branch --
  the optional and mandatory release paths respectively, which apply
  completely differently in the existing code), a normal move
  (`start_move_animation()`'s main path), and capture/reaching-home/
  winning -- all three detected around the single `ludo_move_pawn()`
  call in `start_move_animation()` (its own already-existing capture
  return value; a before/after comparison of the moved pawn's `finished`
  flag and `game.winner` for home/win, mutually exclusive via priority
  win > home > capture).
- **"Music" iconbar/window menu, per the follow-up request**: a new
  submenu (`src/main.c`) with a ticked "On" toggle and one ticked
  "Track N" entry per `QTM_MUSIC_TRACK_COUNT`, ticks refreshed just
  before the menu opens. Picking a track also turns music on if it was
  off (a track pick that silently did nothing because music happened to
  be off would read as broken). Stays visible even when QTM isn't
  available (every action just becomes a no-op) rather than being
  hidden, so the feature's presence isn't itself a signal something's
  missing.

Builds clean under ArchieSDK (no warnings) and is deployed, but **not
yet manually verified live in Arculator at all** -- see QTM.md's own
"Known gaps" section and this file's "Resume here" section above.

**Round 7.61**: round 7.60's first live-test pass, per direct user
report ("First sound FX occurrence (dice throw) gives error message" --
a genuine ARM data abort, not just a RISC OS error box). Two real bugs
found and fixed -- full detail in [QTM.md](QTM.md)'s own "Round 7.61"
section, summarised here: (1) `qtm_play_sfx()`'s SWI register block was
only partially zeroed (R9 left as stack garbage, unlike every other QTM
call in the same file), fixed and confirmed live -- the dice SFX now
plays cleanly with debug logging capturing the exact registers used as
evidence; (2) `SfxWin` (the largest bundled sample) was silently failing
to load, most likely a `malloc()` squeeze against `app/!Run`'s original
256K `WimpSlot` ceiling -- fixed by raising the ceiling to 384K and
trimming the sample itself shorter (4s -> 2.5s). Music confirmed working
live in the same session. Still not live-confirmed: the other 5 SFX
events, the Music menu, and whether the dice SFX actually *sounds*
right (only confirmed to play without crashing so far).

**Round 7.62**: the exact same crash recurred on the very next live
test, at the same address, despite round 7.61's fix -- the debug log
showed `qtm_play_sfx()`'s SWI call itself returning cleanly both times,
with the abort happening moments later with nothing further logged. That
pattern pointed at `QTM_PlayRawSample` starting playback
**asynchronously** (background DMA fill, like any ProTracker-derived
player) rather than consuming the sample during the SWI call -- meaning
the real bug was in what happens *after* the call returns, not the call
itself. Found: R4 (repeat length) was 0, but classic Amiga/ProTracker
sample headers use **1**, not 0, as the "don't loop" sentinel -- a
well-known trap in that format family (QTM being fundamentally a
ProTracker-family player, its raw-sample fill logic almost certainly
inherited the same convention). Fixed: R4 = 1. Full writeup in
[QTM.md](QTM.md)'s "Round 7.62" section. **Not yet re-confirmed live.**

**Round 7.63**: round 7.62's fix didn't work either -- the identical
crash recurred again, unchanged. Rather than guess a fourth register
value, this round used Arculator's own debugger (its `t enable
dataabort` command traps data aborts directly, at the user's
suggestion) to get real evidence instead of another hypothesis. With
the abort trapped (and so suppressed from becoming the usual error
box), the same reproduction steps **didn't crash at all** -- instead the
background music audibly hung on a note the instant the SFX played.
That's channel contention, not a bad register value: `R0 = -1`
("automatic channel selection") searching for a channel the currently-
playing 4-channel module isn't using can end up wrong, handing a raw
sample the SAME channel the module player is actively driving and
corrupting its bookkeeping -- explaining both the earlier hard crashes
and this round's stuck note as one root cause, just different symptoms
depending on exact timing. Fixed: R0 = a **fixed** channel (5, always
outside a 4-channel module's own 1-4 range) instead of automatic
selection. Full writeup in [QTM.md](QTM.md)'s "Round 7.63" section.
**Not yet re-confirmed live.**

**Round 7.64**: round 7.63's fix confirmed live -- crash gone, music
keeps playing fine through repeated SFX triggers -- but revealed a new
symptom: the SFX itself is inaudible, despite `QTM_PlayRawSample`
returning cleanly every time. One more diagnostic (`Sound_Configure`,
read-only) showed why: the RISC OS Sound system itself was only
configured for 4 channels, matching the music -- channel 5 is a valid
*number* as far as QTM's own SWI is concerned, but the underlying
hardware/DMA mixing never actually processes it, since `Sound_Configure`
(not QTM) is what determines how many channels physically exist. Fixed:
`qtm_initialise()` now bumps the channel count to 5 once, early --
before music starts playing at all, to avoid finding out empirically
whether reconfiguring the Sound system live (while something's already
relying on it) is safe. Full writeup in [QTM.md](QTM.md)'s "Round 7.64"
section. **Not yet re-confirmed live.**

**Round 7.65**: two independent additions, per explicit user requests --
a third background music track (`Music3`, "on the run" by Anders
Lundqvist, same ModArchive download/verification process as the first
two) and a genuine Track submenu replacing the old "Track N" entries,
showing each track's real title. The latter needed indirected menu text
(RISC OS menu entries have only a 12-byte inline text field, too short
for "digital innovation1") -- the first use of `wimp_ICON_INDIRECTED` on
a `wimp_menu_entry` rather than a window icon in this project. Full
writeup in [QTM.md](QTM.md)'s "Round 7.65" section. Confirmed live --
"Music submenu works".

**Round 7.66**: round 7.64's channel-count fix confirmed NOT working --
SFX still inaudible (no crash, no hang, no error, just silent). New
hypothesis: Acorn/VIDC-era sound hardware channel counts are commonly
constrained to powers of two, so requesting exactly 5 may have been
silently rounded down to 4. Changed the request to 8, with the actual
echoed-back count now logged to confirm rather than guess again. Full
writeup in [QTM.md](QTM.md)'s "Round 7.66" section. **Not yet
re-confirmed live.**

**Round 7.67**: round 7.66 confirmed not fixed either (channel count
read back inconsistently, at one point not even reflecting the explicit
request at all) -- per direct user request ("Can you find any online
resources on how to do sound FX with QTM?"), found real source code from
**QTM's own author** (Steve Harrison, posting as "steve3000" on a
stardot.org.uk RISC OS porting thread) rather than guessing a sixth
time: a shared BASIC test program (`lin2LOG`, detokenised to read),
proving `QTM_SoundControl` -- undocumented anywhere else found -- must
be called to reserve sample channels before `QTM_PlayRawSample` works at
all, which is why rounds 7.64/7.66's OS-level `Sound_Configure` attempts
were never going to work (wrong layer entirely). Also corrected round
7.62's repeat-length guess (0, not 1) against the author's own real
usage. `QTM_SFX_CHANNEL` changed from 5 to 1, now understood as a
channel within QTM's own reserved sample pool, not a raw hardware
channel. Full writeup in [QTM.md](QTM.md)'s "Round 7.67" section --
highest-confidence fix in this investigation so far, since it's grounded
in the actual author's working code, but **not yet re-confirmed live.**

**Round 7.68**: round 7.67 confirmed still crashing (same asynchronous
data-abort pattern, at a slightly different address). Line-by-line
comparison against the author's own `lin2LOG` register values found a
genuine remaining discrepancy: R8 was 0 in every attempt so far, but the
author's own code passes R8=255 unconditionally, even on its fixed
channel-1 calls -- contradicting round 7.63's assumption (from the SWI's
written docs) that R7/R8 only matter when R0=-1. Fixed: R7=0, R8=255 set
explicitly regardless of channel. Full writeup in [QTM.md](QTM.md)'s
"Round 7.68" section. **Not yet re-confirmed live.**

**Round 7.69**: round 7.68 confirmed still crashing, same fault pattern.
With R7/R8 now matching the author's own reference exactly, the last
register still differing in *kind* (not just value) was R5: this
project used an Amiga period (322), but the author's own `lin2LOG`
calls only ever use small 1-36 "note" values. Changed R5 from
`QTM_SFX_PERIOD` to `QTM_SFX_NOTE` (18, an arbitrary mid-range pick) as
an isolating test -- not a final tuned value, just to determine whether
QTM's period-range support itself is what's unreliable. Full writeup in
[QTM.md](QTM.md)'s "Round 7.69" section. **Not yet re-confirmed live.**

**Round 7.70**: round 7.69 confirmed still crashing -- R5 wasn't it,
every register now matched the author's reference exactly. Per direct
user question, analysed the crash ADDRESS itself rather than the
registers: round 7.63's original crash was at `&0182A768`, but every
crash since round 7.67 has been at a different address, `&0182A6BC` --
two different faults, not one persisting. Round 7.63's fixed-channel-5
fix (before `QTM_SoundControl` existed in this code) was genuinely
stable, just silent; the second crash only appeared once round 7.67
added `QTM_SoundControl` (reserving 4 channels) alongside switching to
channel 1. Diagnosis: reserving exactly 4 channels (matching the
4-channel music) succeeds cleanly before the module loads, but the
module player has no way to know and claims channels 1-4 for itself
anyway once started -- recreating the same class of channel collision
round 7.63 already fixed once, via a different mechanism. Fixed:
reserve 8 channels instead of 4, play SFX on channel 5 again (within
the reservation, outside the module's own 1-4 range). Also, per the
user's own suggestion, deployed QTM's author's own reference test
program directly to Arculator's hostfs as an independent check. Full
writeup in [QTM.md](QTM.md)'s "Round 7.70" section. **Not yet
re-confirmed live.**

**Round 7.71 -- the real root cause, found live**: round 7.70 confirmed
still crashing, but this round produced the actual breakthrough. At the
user's own initiative, with Arculator's data-abort trap still armed
from an earlier round, the debugger caught a live ArchiLudo crash and
was used to disassemble at the exact address RISC OS's own error dialog
reported (`d 182a768`) -- revealing a classic pitch-shifted resampling
read loop (`R1 ASR #12` converting a fixed-point playback position to a
byte offset, reading past the sample buffer's end). This is normal,
required behaviour for an interpolating sample player, not a bug in
`QTM_PlayRawSample` -- the real bug was that this project's own sample
buffers were allocated to exactly the sample's length, with no
lookahead margin, so the legitimate overshoot read walked into unmapped
memory. This retroactively explains why every register-value change
across rounds 7.61-7.69 (R9 zeroing, repeat length, channel selection,
R7/R8, note vs period) never fixed anything -- none of them were the
actual cause. Fixed: sample buffers now allocate 64 bytes of trailing
safety padding (genuine VIDC-log silence, queried via
`sound_sound_log(0)` rather than assumed), leaving the real reported
sample length unchanged. Full writeup in [QTM.md](QTM.md)'s "Round
7.71" section -- highest-confidence fix yet, being the first derived
from watching actual CPU state at the moment of the fault rather than
register comparison or documentation inference. **Not yet re-confirmed
live.**

**Round 7.72 -- the actual mechanism**: round 7.71 confirmed still
crashing at the same address -- 64 bytes of padding wasn't remotely
enough, prompting a deeper live debugging session (continuing to
disassemble forward through the whole resampling routine, at the user's
own initiative). Found the real mechanism: the fill loop is bounded by
output progress only (`CMP R12,R10`), with no check on remaining source
data at all, so any padding amount is fundamentally the wrong kind of
fix; a separate wraparound calculation
(`ADDS R1,R1,R2 / SUBGE R1,R1,R5 LSL #12`) is what's meant to wrap the
read position back once it passes the sample's length, but with repeat
length 0 that subtraction never fires and the position grows unbounded
instead. QTM's own author's `lin2LOG` uses repeat length 0 successfully,
but his samples are long relative to a single fill chunk, so this path
likely never triggered for him -- ArchiLudo's own short SFX (some under
1100 bytes) are exactly the case where it does. Fixed: repeat length set
to the sample's own real length rather than 0, at a real cost -- the
sample will now genuinely loop rather than stop after one play, needing
a follow-up (explicitly silencing the channel once its natural duration
elapses) not yet implemented; this round is specifically an isolating
test to confirm the crash itself is gone first. Full writeup in
[QTM.md](QTM.md)'s "Round 7.72" section. **Not yet re-confirmed live.**

**Round 7.73**: round 7.72 confirmed still crashing, same address --
wraparound fix alone wasn't sufficient. Reinstated a real Amiga period
(322, matching the SFX's actual 11025Hz rate) in place of round 7.69's
arbitrary note value, kept alongside (not instead of) round 7.72's
wraparound fix -- untested in combination until now. Full writeup in
[QTM.md](QTM.md)'s "Round 7.73" section. **Not yet re-confirmed live.**

**Round 7.74 -- SFX disabled after 14 rounds**: round 7.73 confirmed
still crashing, same address, closing out the `QTM_PlayRawSample`
investigation. Checked three more real, shipped Archimedes codebases
(`kieranhj/arc-django-2`, `bitshifters/aklang`, `bitshifters/mikroreise`)
per direct user request -- none use `QTM_PlayRawSample`,
`QTM_PlaySample`, or `QTM_RegisterSample` for one-shot effects at all;
all embed extra sounds as MOD instrument samples instead.
`arc-django-2` also corrected a real standing misunderstanding:
`QTM_SoundControl`'s R1 is a behaviour-flags bitmask, not a
channel-reservation count as rounds 7.67-7.73 believed -- that logic was
removed from `qtm_initialise()`. Decision: `qtm_play_sfx()` disabled as
a no-op (sample loading/conversion kept, unused, as groundwork); music
is unaffected and remains confirmed working live. Full writeup,
including the recommended future approach (embed as MOD instruments,
trigger via `QTM_PlaySample` by index), in [QTM.md](QTM.md)'s "Round
7.74" section.

**Round 7.75 -- music didn't stop on quit**: live-tested regression --
QTM is a relocatable module independent of the ArchiLudo task, so
neither the Quit menu nor `Message_Quit` stopped it; background music
kept playing after the application closed. Fixed with a single
`qtm_set_music_enabled(0)` call in `main()` right after the poll loop
ends (both quit paths converge there) -- reuses the existing public
stop entry point (already calls `qtm_clear()` internally) rather than
adding a new one. `src/main.c`.

**Round 7.76 -- MOD-embedded SFX samples, tried cautiously**: per
direct user request, implemented round 7.74's recommended path --
`tools/mod_embed_sfx.py` (new) splices the 6 bundled SFX into empty
ProTracker sample slots of `Music1`/`Music2`/`Music3` (converted to
8-bit signed linear PCM, the native tracker format -- unrelated to the
VIDC-log format `QTM_PlayRawSample` needed), validated via `ffprobe`'s
libopenmpt demuxer after every run. `Music3`'s own artist-authored
sample table only has 3 free slots (not 6 like the other two tracks),
so slot assignment is per-track (`lib/qtm.c`'s new `sfx_slot[][]`
table) -- Music3 keeps only Capture/Home/Win. `qtm_play_sfx()` now
calls `QTM_PlaySample` (`0x47e54`), but its register convention is
unconfirmed guesswork (no working example found anywhere in this
project's research) -- per explicit user decision, only `QTM_SFX_DICE`
actually calls it for now, closing the loop on where the original crash
saga started with a different SWI; every other event stays a no-op
until this one is confirmed safe live. Full writeup in
[QTM.md](QTM.md)'s "Round 7.76" section. **Not yet re-confirmed live.**

**Round 7.77 -- QTM_PlaySample confirmed live: no crash, but silent**:
live-tested round 7.76's call -- the good news first, no crash at all
(unlike `QTM_PlayRawSample`'s entire 14-round history). But also no
audible sound. Six individually cautious, single-variable experiments
followed (auto vs. fixed channel, matching the QTM module version used
by the reference codebases -- v1.49b swapped for the byte-identical
v1.49c all three bundle, `QTM_SampleVolume`, `QTM_RemoveChannel`, an
explicit in-range channel), each live-tested and reported back before
trying the next. All six were accepted cleanly with no error and no
audible change; one produced a genuinely different output register
value that didn't reproduce on a direct follow-up test. No
documentation or working example of `QTM_PlaySample`'s register
convention exists anywhere this project's research found. Per direct
user decision, raised as a question on stardot.org.uk instead of
continuing to guess:
`https://stardot.org.uk/forums/viewtopic.php?t=33515`. Code is
unchanged from round 7.76's safe, silent, Dice-only state pending a
reply. Full writeup in [QTM.md](QTM.md)'s "Round 7.77" section.

**Round 7.78 -- the real answer, from QTM's own official docs**: the
user found and downloaded the official QTM v1.49 distribution archive
itself (full SWI reference plus assembler source), something none of
this project's earlier research had turned up. Confirmed round 7.76's
`QTM_PlaySample` register guess was already correct, and found the
actual missing piece: `QTM_SoundControl`'s R0 genuinely is a
channel-count switch (4 or 8) -- round 7.67's original belief, wrongly
"corrected away" in round 7.74. ArchiLudo's 4-channel MODs left QTM in
4-channel mode by default, so channels 5-8 were numerically legal but
never actually mixed. Fixed with an 8-channel `QTM_SoundControl` call
in `qtm_initialise()`. Full writeup in [QTM.md](QTM.md)'s "Round 7.78"
section.

**Round 7.79 -- per-SFX channel**: live-tested round 7.78's fix --
confirmed no crash, and one SFX (Release) finally audible. All 6 SFX
had been sharing one fixed channel though, and Dice (always immediately
followed by a Move trigger) was being cut off by it on the same
channel. Fixed by spreading the 6 events across all 4 free channels
(`sfx_channel[]`, `lib/qtm.c`). [QTM.md](QTM.md)'s "Round 7.79" section.

**Round 7.80 -- two refuted theories**: Dice was still silent regardless
of channel (confirmed by directly swapping Dice/Release's channels --
no change). Tried and refuted, via live testing: a real-time settle
delay after triggering Dice, then moving the trigger to fire after
its own redraw instead of before. Neither made a difference -- the real
cause turned out to be simple loudness (round 7.82). [QTM.md](QTM.md)'s
"Round 7.80" section.

**Round 7.81 -- mute vs. shutdown split**: per user request, needed a
way to test SFX independently of music, to rule out masking. This
required `qtm_set_music_enabled(0)` to stop fully clearing QTM (which
also wiped the sample table SFX draw from) -- now mutes via
`QTM_MusicVolume` instead, leaving the song loaded. Actual shutdown
(app quit) is a new dedicated `qtm_shutdown()`. This immediately proved
useful: muting music revealed every "silent" SFX had been playing
correctly all along. [QTM.md](QTM.md)'s "Round 7.81" section.

**Round 7.82 -- it was loudness, not a bug**: root cause found -- the
6 bundled SFX had wildly inconsistent source recording levels (RMS
spread over 10x), and a flat 16-to-8-bit truncation carried that
straight through, so most were simply too quiet to hear over the music.
Peak-normalizing helped but wasn't enough (high peak-to-average sounds
like Dice barely got louder); ducking the music volume was tried,
live-tested working, but rejected by direct user feedback as "really
annoying" and fully reverted. Final fix: RMS-targeted soft-clip
(tanh) loudness normalization in `tools/mod_embed_sfx.py`, calibrated
against Release's own proven-audible original loudness. All 6 SFX
confirmed audible over music, no ducking needed. [QTM.md](QTM.md)'s
"Round 7.82" section.

**Round 7.83 -- forum follow-up posted**: root cause summarized back to
the stardot thread from round 7.77, for anyone else who finds it via
search. [QTM.md](QTM.md)'s "Round 7.83" section.

**Round 7.84 -- independent SFX on/off toggle**: per explicit user
request, SFX and music are now separately switchable (e.g. music with
no SFX). New `qtm_set_sfx_enabled()`/`qtm_sfx_enabled()`, wired into the
Music submenu as a second ticked toggle alongside "On". [QTM.md](QTM.md)'s
"Round 7.84" section.

**Round 7.85 -- real-hardware deploy target**: added `make deploy-pibridge`
to the Makefile, deploying to a PiEconetBridge (Econet-over-IP bridge on
a Raspberry Pi) via `rsync` over SSH -- a genuinely separate target from
`make deploy` (Arculator's hostfs), not a replacement, per explicit user
decision. Connection details (`PIBRIDGE_USER`/`PIBRIDGE_HOST`) live in
`.env`, matching the existing `ARCULATOR_HOSTFS`/Ultimate II+ `ULTIP1`
convention; `check-pibridge` verifies SSH reachability first, the same
"fail fast with a clear error" shape as `check-hostfs`. See
[BUILDCHAIN.md](BUILDCHAIN.md)'s "Other targets" table.

**Round 7.86 -- password auth, not SSH keys**: round 7.85 assumed
SSH-key-based auth; the user actually connects to this Pi with
password auth (via FileZilla/SFTP), not keys. Switched `check-pibridge`/
`deploy-pibridge` to `sshpass` (password from `SSHPASS`, passed as an
environment variable rather than `-p`, so it doesn't appear in `ps`
output), with `StrictHostKeyChecking=accept-new` so a first-time
connection needs no other interactive prompt either. `PIBRIDGE_PATH`
also moved from a Makefile default into `.env` alongside the new
`PIBRIDGE_PASS`, per explicit user request -- all four connection
details (`PIBRIDGE_USER`/`HOST`/`PASS`/`PATH`) now live there together.

**Round 7.87 -- PiFS filetype preservation**: the first real deploy to
the PiEconetBridge showed every file's RISC OS filetype was lost --
PiFS (the PiEconetBridge fileserver) doesn't use Arculator hostfs's
`,xxx` suffix convention at all. Read PiFS's own source
(`cr12925/PiEconetBridge` on GitHub, `utilities/fs.c`) to find the real
answer: it stores file attributes either as Linux extended attributes
(the default) or, per-file, in a classic Acorn `.inf` sidecar file --
confirmed the exact format directly from `fs_read_attr_from_file()`/
`fs_write_attr_to_file()`. Added `tools/prepare_pibridge_deploy.py`,
which converts `$(APPDIR)`'s `,xxx`-suffixed build output into PiFS's
expected plain-filename + `.inf`-sidecar form (encoding each file's
filetype into a standard RISC OS "stamped" load/exec address, per the
same convention every RISC OS cross-dev tool uses) before
`deploy-pibridge` rsyncs it -- the `.inf` route was chosen over xattrs
since it needs no filesystem feature support on either end of the
transfer, just ordinary files. Live-tested working end to end against
the real hardware. Also fixed a real security issue caught in the same
round: the rsync recipe line wasn't prefixed with `@`, so `make
deploy-pibridge`'s own command echo printed the SSH password in plain
text to the terminal on every run.

The Phase 1 board shape now comes directly from
`/home/xahmol/git/ludo/GEOS/src/main.c`'s `fieldcoords[40][2]` and
`homedestcoords[4][8][2]` tables (converted `col = raw_x/2`,
`row = (raw_y-3)/2` onto an 11x11 grid), per the user's explicit request
to match that version rather than a new design -- see
[BOARD_LAYOUT.md](BOARD_LAYOUT.md) for the conversion detail and how it
was verified (rendered as an image and visually compared against the
classic cross shape before writing it into `board_layout.c`). Player
colours/order also now match the GEOS source's `startfieldgraphics`
comments exactly: 0=green, 1=red, 2=blue, 3=yellow (previously an
arbitrary order).

## Directory layout

```
ArchiLudo/
  src/
    game_logic.c     -- rules engine (portable)
    board_layout.c    -- board geometry (portable)
    ai.c               -- AI opponent move selection (portable, see docs/AI.md)
    game_view.c         -- the game window: creation, redraw, clicks, animation
    setup_view.c         -- the "New Game" dialogue: names, Human/AI per player
    win_view.c            -- the "a player has won" Continue/New Game dialogue
    rules_view.c            -- the "Rule Options" dialogue: variant + house rules
    splash_view.c              -- the startup/About window (idi8b logo, version, author)
    save_view.c                  -- Save/Load dialogues + drag-and-drop file transfer
    main.c                          -- WIMP shell (task lifecycle, iconbar, dispatch)
  include/
    game_logic.h      -- rules engine API + full rules writeup
    board_layout.h     -- board geometry API
    ai.h                -- AI API
    game_view.h          -- game window API
    setup_view.h          -- setup dialogue API
    win_view.h             -- win-choice dialogue API
    rules_view.h              -- rule options dialogue API
    splash_view.h               -- splash/About window API
    save_view.h                   -- Save/Load dialogue + drag-and-drop API
    archiludo.h                     -- WIMP shell shared declarations
  tests/
    test_game_logic.c   -- host-side automated test suite (`make test`)
    test_board_layout.c  -- ditto, for board_layout.c
    test_ai.c              -- ditto, for ai.c
  tools/
    riscos_sprite.py  -- PNG <-> RISC OS Sprite converter (host-side, Python;
                          not currently consumed by the running game -- see
                          docs/GRAPHICS_TOOLING.md's "Round 6.4")
  assets/
    geos_source/                  -- local copies of the GeoLudo .gbm bitmaps
    generate_placeholder_art.py -- recolours/resizes/packs them (reproducible;
                                    output currently unused by the game, kept
                                    for Phase 2 -- see above)
    pawn0.png .. pawn3.png        -- generated source images
    dice1.png .. dice6.png         -- generated source images
    Sprites                        -- packed sprite file (currently unused)
  docs/
    ARCHITECTURE.md    -- this file
    BUILDCHAIN.md       -- ArchieSDK/Makefile/toolchain manual
    GAME_LOGIC.md        -- rules engine manual
    BOARD_LAYOUT.md       -- board geometry manual
    AI.md                   -- AI opponent manual
    OSLIB.md                -- how this project uses OSLib
    LIBARCHIE.md             -- ArchieSDK's bundled helper library
    GRAPHICS_TOOLING.md      -- the sprite converter and RISC OS sprite format
  riscos_wimp_reference.md -- WIMP/SWI/message reference (curated from the PRM + Pinknoise archive + Fryatt's guide)
  CREDITS.md -- everything this project is built on/ported from
  Makefile
  .env / .env.example
```

## Porting source: GeoLudo -> ArchiLudo

`/home/xahmol/git/ludo` holds this game's prior ports across 8-bit
platforms and is the source of truth for the rules and prior art.
`GEOS/` (2023 GEOS edition) is the closest existing analogue to
ArchiLudo's architecture, since GEOS is itself a real WIMP environment:
real menu trees, real dialogue boxes, icon-driven actions. The mapping
below (also recorded in the approved scaffolding plan,
`~/.claude/plans/i-want-to-scaffolf-dreamy-lantern.md`) is the porting
strategy for the UI layer once it's built:

| GEOS (source) | ArchiLudo (RISC OS Wimp) | Why |
|---|---|---|
| `fieldcoords[40][2]`/`homedestcoords[4][8][2]` (board geometry) | `board_layout.c`'s ring/home-column/home-base tables (see [BOARD_LAYOUT.md](BOARD_LAYOUT.md)) | Ported directly (coordinate-converted), not redesigned -- the user explicitly wanted the same board |
| `struct menu` tree (`menuGEOS`/`menuGame`/`menuFile`) | `wimp_menu` tree via `Wimp_CreateMenu` | Direct structural match |
| `DlgBoxGetString`/`DlgBoxYesNo`/`DlgBoxOk`/`DlgBoxOkCancel` | A small reusable dialogue-window helper built on plain Wimp windows | Wimp has no built-in dialogue-box kernel call like GEOS does |
| `SaveFile`/`GetFile` (GEOS's directory-listing file picker) | `save_view.c`'s Save/Load dialogues (typed pathname) + drag-and-drop (`Message_DataSave`/`DataSaveAck`/`DataLoad`) -- see "Round 7.2" below | Idiomatic RISC OS UX, not a literal port -- `Message_DataOpen` (double-click-to-open) not implemented, see `save_view.h`'s doc comment |
| `throwicon`/`nexticon` (`struct icontab`) | Ordinary `wimp_icon`s inside the game window | Direct conceptual match |
| `informationCredits()`/`ShowCredits()` (splash and Credits menu item are literally the same screen, see "Round 7.1" below) | `splash_view.c`'s About window -- shown at startup and reachable again via the "About" menu entry, same dual role | Direct conceptual match, no separate "Credits" needed -- see "Round 7.1" below for why one wasn't added |
| `computerchoosepawn()` (AI move choice) | `ai.c`'s `ludo_ai_choose_pawn()` | Assessed and reused as the basis (score every legal move, pick the highest), not a literal port -- see [AI.md](AI.md) for what carried over, what changed to fit this project's own rules, and what was fixed rather than faithfully copied |
| `inputofnames()` (name entry + AI count, `DlgBoxGetString`) | `setup_view.c`'s "New Game" dialogue: one writable name icon + one click-to-toggle Human/AI icon per player | More granular than GEOS's single "how many AI players" count -- per-player choice, not just a count, per explicit request |
| Custom per-platform bitmap/colour-card plotting (`interface.c`, `geoscore.s`) | RISC OS Sprites (`OS_SpriteOp`) in a window redraw handler | GEOS needed hand-written pixel code per 8-bit machine; RISC OS's sprite plotter is uniform |
| `monochromeflag`, `VDC_CLR0..4`, `Switch4080` | One fixed screen mode | VIDC gives every Archimedes the same colour capability under RISC OS 3.10 |
| Overlay/VLIR loading (`loadoverlay`/`openVLIR`/`closeVLIR`) | None -- program stays resident | 1MB+ RAM makes this unnecessary |
| `gamelogic.c` (`turngeneric`, `pawnselect`, dice/AI) | Reimplemented cleanly as `game_logic.c` (see below) | See [GAME_LOGIC.md](GAME_LOGIC.md) for why this isn't a literal port |

## Why `game_logic.c` is a clean reimplementation, not a literal port

The original 1992 Commodore 128 BASIC program (and its 2023 GEOS C port)
use single/double-letter variable names (`ap`, `as`, `vr`, `vl`, `vn`,
`nr`, `gv`, `ov`, `ro`, `np`, `dp`, `zv`) with no surviving documentation
of what most of them represent -- the author's own words: "sparsely
documented... sometimes have no clue anymore what variables do." Copying
that exact numeric encoding byte-for-byte would mean reproducing bugs and
ambiguities nobody can currently verify, and would produce a module nobody
could safely modify or test with confidence.

Instead, `game_logic.c` is implemented directly from the plain-English
rules text the author has maintained consistently across every other port
(the "Game rules" sections of `/home/xahmol/git/ludo/README.md` and
`.../GEOS/README.md`), using clearly-named variables, a simple single
integer "steps travelled" position model (see
[GAME_LOGIC.md](GAME_LOGIC.md)), and a full docstring-style comment above
every function (summary/syntax/input/output) rather than sparse or absent
documentation. This also gave an opportunity to verify the rules make
internal sense as a state machine (they do) and simplify where the more
powerful Archimedes removes a constraint the 8-bit versions had to design
around (see the table above).
