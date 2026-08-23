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
| `main.c` / `game_view.c` | Playable Phase 1 WIMP game: iconbar icon opens a game window with a Throw button and status line; board cells + pawns drawn each redraw from `board_layout.c` + `game_logic.c` state; clicking a pawn's cell moves it; iconbar menu has Quit. Compiles and links cleanly under ArchieSDK. **First round of Arculator feedback applied** (see "Phase 1 implementation notes" below) -- **needs another round of manual verification** |
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
- **Felt sluggish.** The window didn't set `wimp_WINDOW_AUTO_REDRAW`, so
  the Wimp asked the client to fully redraw (41 filled cells + up to 16
  sprite plots, each several SWI calls, on an emulated ARM) on *every*
  expose/uncover, not just on genuine content changes. Re-added
  `wimp_WINDOW_AUTO_REDRAW` (the app already calls `Wimp_ForceRedraw`
  itself whenever the game state actually changes, so this doesn't lose
  any needed redraws).
- **Board didn't look like Mens Erger Je Niet at all.** The Phase 1 board
  shape was an invented square-ring-with-diagonal-home-columns design, not
  the real cross-shaped board -- see "Board layout" below for the fix
  (ported the GEOS edition's actual coordinates instead).

Also set the window ~30% smaller (11x11 grid instead of the old invented
15x15) and reworded the status line to be shorter, since board size and
message length both feed directly into how much of this ever fits inside
Mode 15's window space.

**Still not fully verified** -- these are all high-confidence fixes for
concretely-identified bugs, not a guess-and-hope pass, but the redraw
origin/click-coordinate arithmetic in particular can still only be truly
confirmed by looking at it running again in Arculator.

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
    generate_placeholder_art.py -- procedural Phase 1 pawn art (reproducible)
    pawn0.png .. pawn3.png        -- generated source images
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
