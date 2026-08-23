# ArchiLudo Architecture

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
| 1 | Playable, plain WIMP game: wire `game_logic.c` into a real game window (board/pawns as simple sprites, dice via icon click) | core loop done, first round of real-hardware-emulator feedback applied, **needs another round of manual Arculator verification**; name-entry/restart-confirm dialogues and drag-based save/load not yet done (deferred to polish alongside Phase 5) |
| 1.5 | Sprite/graphics tooling (`tools/riscos_sprite.py`) + placeholder pawn art | done |
| 2 | Real board/pawn/dice art via the tooling above, replacing the current flat-colour-rectangle placeholder cells with actual board artwork (visual reference: [Mens erger je niet!](https://nl.wikipedia.org/wiki/Mens_erger_je_niet!)) -- the board *shape* itself is already the real one as of Phase 1 (ported from the GEOS edition, see [BOARD_LAYOUT.md](BOARD_LAYOUT.md)), so this phase is about art, not layout | not started |
| 3 | Audio: `lib/qtm.c`/`docs/QTM.md` wrapper (see [[archiludo-riscos-project]] memory / `CREDITS.md` for why QTM over archieklang), bundle `QTMModule`, background MOD + dice-roll/capture/win sound effects | not started |
| 4 | Enhanced graphics: full-screen double-buffered gameplay view (see "Graphics architecture: hybrid" below), smooth pawn/dice animation | not started |
| 5 | AI difficulty, credits/options dialogues, `!Sprites`/`!Boot`/`!Run` app-directory packaging | not started |
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

### Board layout: ported from the GEOS edition, not invented

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
    game_view.c        -- the game window: creation, redraw, clicks
    main.c               -- WIMP shell (task lifecycle, iconbar, dispatch)
  include/
    game_logic.h      -- rules engine API + full rules writeup
    board_layout.h     -- board geometry API
    game_view.h          -- game window API
    archiludo.h            -- WIMP shell shared declarations
  tests/
    test_game_logic.c   -- host-side automated test suite (`make test`)
    test_board_layout.c  -- ditto, for board_layout.c
  tools/
    riscos_sprite.py  -- PNG <-> RISC OS Sprite converter (host-side, Python)
  assets/
    geos_source/                  -- local copies of the GeoLudo .gbm bitmaps reused below
    generate_placeholder_art.py -- recolours/resizes them into Phase 1 art (reproducible)
    pawn0.png .. pawn3.png        -- generated source images (home base pawn)
    start0.png .. start3.png      -- generated source images (ring entry marker)
    Sprites                       -- packed sprite file the game loads
  docs/
    ARCHITECTURE.md    -- this file
    BUILDCHAIN.md       -- ArchieSDK/Makefile/toolchain manual
    GAME_LOGIC.md        -- rules engine manual
    BOARD_LAYOUT.md       -- board geometry manual
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
| `SaveFile`/`GetFile` (GEOS's directory-listing file picker) | RISC OS-native Save box + drag-and-drop (`Message_DataSave`/`DataSaveAck`, `Message_DataOpen`) | Idiomatic RISC OS UX, not a literal port |
| `throwicon`/`nexticon` (`struct icontab`) | Ordinary `wimp_icon`s inside the game window | Direct conceptual match |
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
