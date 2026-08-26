# ArchiLudo Architecture

## Resume here

*(Delete/replace this section once the in-progress work below is
confirmed and settled -- it exists purely so a session that starts
cold, with no conversation history, knows exactly where things stood
and what to do next. Last updated 2026-08-26, commit pending -- see
`git log` for the actual latest; everything described below is
committed and pushed, working tree clean. Full background/reasoning:
round 7.16 in `docs/GRAPHICS_TOOLING.md`, rounds 7.17-7.20 just above in
this file's Phase 1 notes, `docs/GAME_LOGIC.md`'s "Round 7.20" note, and
the [[archiludo-sprite-pivot-plan]] memory.)*

**Sprite rendering itself is confirmed working** -- the round 7.18
coordinate-space bug (pawns not rendering at all) is fixed and the user
has since given live, on-screen feedback on the pawns' *look*, so
rendering itself is no longer in question.

**Two things fixed this same live-testing round, neither yet
re-confirmed by the user after the fix:**
1. **Round 7.19, pawn shading**: per explicit user feedback ("not
   entirely happy with the new 16 colour pawn look... dither between
   white and player colour, and similarly for the grey"), the flat
   white/grey highlight/shadow blocks were replaced with a checkerboard
   dither against the player's own hue. Built, deployed. Check for the
   user's reaction to this before doing any further pawn-art tuning.
2. **Round 7.20, a real rules bug**: pawns were stacking on the same
   final home-column square once more than one of a player's pawns had
   finished (`home_column_blocked()` wrongly exempted finished pawns
   from blocking). Fixed by ground-truthing GEOS's actual "shrinking
   finish line" mechanic (`finish_threshold_for()`) -- see
   `docs/GAME_LOGIC.md`'s "Round 7.20" and the dedicated new test
   `test_second_finishing_pawn_lands_one_square_short()`. All ~10.16M
   headless-simulation checks pass. Built, deployed. Check for the
   user's confirmation this actually resolved what they saw before
   considering it done.

**Still pending a decision, not blocking either of the above:** whether
to build the full `OS_SpriteOp 52` + `ColourTrans`-table plotting path
needed to ship a smoother gradient-shaded pawn look ("256 colour
depth"). A preview (`assets/experiments/gradient_preview.py` --
exploratory, NOT wired into the game, kept in the repo specifically so
this survives a session restart) was generated and iterated on twice
per direct user feedback ("too dark" -> "a notch brighter still"),
currently at `LIGHT_BLEND=0.45`/`DARK_MULT=0.68` (see that script's own
"Tuning history" docstring) -- the user had not given final sign-off on
this exact iteration as of round 7.19/7.20's feedback (they may have
moved on to the dithered 16-colour version instead, which addresses
some of the same "doesn't look like the player's colour" concern more
cheaply -- worth asking whether the gradient path is still wanted at
all before investing in it). Don't start that larger implementation
unprompted either way.

**Still open, not blocking any of the above:** building a proper
`!ArchiLudo` application directory (per an earlier explicit user
request, now also covering a `!Sprites`/`!Sprites22` app-icon pair --
`!Sprites11` should NOT be built, it uses new-style sprite encoding
genuine RISC OS 3.10 doesn't understand, see round 7.16) -- blocked on
an explicit `AskUserQuestion` consultation on design choices (app icon,
`!Boot`/`!Run`/`!Help` scope, save-file filetype registration, Makefile
`deploy`/`zip` changes) that has not happened yet; do not build this
without asking first, per that instruction.

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
| `game_logic.c` | Complete rules engine (see [GAME_LOGIC.md](GAME_LOGIC.md)), fully unit tested |
| `board_layout.c` | The real Mens Erger Je Niet board (see [BOARD_LAYOUT.md](BOARD_LAYOUT.md)), ported from the GEOS edition's coordinate tables, not invented -- fully unit tested |
| Sprite/graphics tooling | `tools/riscos_sprite.py`, see [GRAPHICS_TOOLING.md](GRAPHICS_TOOLING.md); placeholder pawn art generated via `assets/generate_placeholder_art.py` |
| `main.c` / `game_view.c` | Playable Phase 1 WIMP game, laid out to match GeoLudo's own screen arrangement (board left, status/controls panel right -- see "Phase 1 implementation notes" below, "Round 6"): iconbar icon opens a game window with a player-name line, action-status line, and Throw button; board cells (circles, matching GEOS) + pawns (reused/recoloured GeoLudo art) drawn each redraw from `board_layout.c` + `game_logic.c` state; clicking a pawn's cell moves it; iconbar menu has Quit. Compiles and links cleanly under ArchieSDK. **Six rounds of Arculator feedback applied** (see "Phase 1 implementation notes" below) -- **needs another round of manual verification, in particular the pawn-click/movement path** |
| Music/SFX (QTM) | Not started -- see "Roadmap" below |
| Dialogue boxes (name entry, AI count, save/load), AI opponents | Not started |

## Roadmap

Phased plan, confirmed with the user (including the graphics architecture
decision below via an explicit choice):

| Phase | Goal | Status |
|---|---|---|
| 0 | Build environment: ArchieSDK, Arculator profiles, `game_logic.c` + tests, docs set, PRM/wimp-prog mirrors | done |
| 1 | Playable, plain WIMP game: wire `game_logic.c` into a real game window (board/pawns as simple sprites, dice via icon click) | core loop, click handling, and rules bugs (own-pawn ring collision, finished-pawn placement) all fixed across six-plus rounds of real-hardware-emulator feedback -- see "Phase 1 implementation notes" below. **Drag-and-drop save/load done** -- see "Round 7.2" below |
| 1.5 | Sprite/graphics tooling (`tools/riscos_sprite.py`) + placeholder pawn art | done, though round 6.3/6.4 dropped sprite *plotting* in the running game in favour of `os_plot` primitives after repeated unexplained rendering failures -- the tooling itself stays for Phase 2, see `docs/GRAPHICS_TOOLING.md`'s "Round 6.4" |
| 2 | Real board/pawn/dice art via the tooling above, replacing the current flat-colour/`os_plot`-primitive placeholder cells with actual board artwork (visual reference: [Mens erger je niet!](https://nl.wikipedia.org/wiki/Mens_erger_je_niet!)) -- the board *shape* itself is already the real one as of Phase 1 (ported from the GEOS edition, see [BOARD_LAYOUT.md](BOARD_LAYOUT.md)), so this phase is about art, not layout. Also where round 6.4's sprite-plotting mystery should get a proper second look | not started |
| 3 | Audio: `lib/qtm.c`/`docs/QTM.md` wrapper (see [[archiludo-riscos-project]] memory / `CREDITS.md` for why QTM over archieklang), bundle `QTMModule`, background MOD + dice-roll/capture/win sound effects | not started |
| 4 | Enhanced graphics: full-screen double-buffered gameplay view (see "Graphics architecture: hybrid" below), smooth pawn/dice animation | not started |
| 5 | AI difficulty, credits/options dialogues, `!Sprites`/`!Boot`/`!Run` app-directory packaging | **player setup (names, Human/AI per player) and a first AI opponent done early** -- see [AI.md](AI.md) and "Phase 1 implementation notes" below, "Round 6.8". Difficulty levels beyond the one implemented, and credits/options dialogues, still not started |
| 6 | Release: versioned zip, README/screenshots, both Arculator profiles verified | not started |
| 7 (future, unscheduled) | Expand beyond this one "Mens Erger Je Niet" variant to support multiple Pachisi/Ludo/Mens Erger Je Niet house-rule variants, selectable by the player | not started -- see note below |

Phase 1 deliberately comes before any graphics/audio investment -- it
validates the whole rules-engine-to-WIMP wiring while everything's still
easy to change.

### Future: multiple Pachisi/Ludo/Mens Erger Je Niet variants

Not scheduled, but recorded so it shapes later design choices rather than
being retrofitted: the user wants ArchiLudo to eventually offer several
house-rule variants (Pachisi, standard Ludo, this Dutch "Mens Erger Je
Niet" ruleset), selectable by the player, rather than only the one variant
`game_logic.c` implements today. When that work starts, the rules that
currently live as `#define`s and hardcoded behaviour in `game_logic.c`
(ring/home-column length, whether a six mandatorily releases a home pawn,
the three-tries-for-a-six rule, capture-on-landing, home-column blocking)
will need to become a parameterised ruleset the engine is configured with,
rather than the single fixed variant. Don't preemptively generalise
`game_logic.c` for this now -- it's explicitly future/unscheduled work,
and premature abstraction before a second variant is actually being built
would just be guessing at what needs to vary.

### Graphics architecture: hybrid

WIMP shell (iconbar, menus, dialogues, save/load) for everything outside
actual gameplay; the board view switches to a **full-screen
double-buffered mode, VSync-synced**, during play for smooth dice/pawn
animation, then returns to the desktop -- a well-established RISC OS game
pattern, and the user's explicit choice over keeping the board inside a
plain `Wimp_RedrawWindow`-driven window throughout. Modelled on Kieran
Connell's `archie-face` framework (itself built on ArchieSDK -- see
`CREDITS.md`), which already provides double buffering, `EventV`-driven
VSync timing, mouse polling, and plot/trig helpers.

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
    splash_view.c          -- the startup/About window (idi8b logo, version, author)
    save_view.c              -- Save/Load dialogues + drag-and-drop file transfer
    main.c                     -- WIMP shell (task lifecycle, iconbar, dispatch)
  include/
    game_logic.h      -- rules engine API + full rules writeup
    board_layout.h     -- board geometry API
    ai.h                -- AI API
    game_view.h          -- game window API
    setup_view.h          -- setup dialogue API
    splash_view.h           -- splash/About window API
    save_view.h               -- Save/Load dialogue + drag-and-drop API
    archiludo.h                 -- WIMP shell shared declarations
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
