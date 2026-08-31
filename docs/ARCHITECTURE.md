# ArchiLudo Architecture

## Contents

- [Layering](#layering)
- [Current state](#current-state)
- [Directory layout](#directory-layout)
- [WIMP conventions and gotchas](#wimp-conventions-and-gotchas)
- [Redraw and animation architecture](#redraw-and-animation-architecture)
- [Screen modes and non-square pixels](#screen-modes-and-non-square-pixels)
- [Board and game rendering](#board-and-game-rendering)
- [AI design](#ai-design)
- [Save/load system](#saveload-system)
- [Application packaging](#application-packaging)
- [Known gaps](#known-gaps)
- [Decisions made and not revisited](#decisions-made-and-not-revisited)
- [Porting source: GeoLudo -> ArchiLudo](#porting-source-geoludo---archiludo)
- [Why `game_logic.c` is a clean reimplementation, not a literal port](#why-game_logicc-is-a-clean-reimplementation-not-a-literal-port)

## Layering

ArchiLudo is deliberately split into two layers that never mix:

```
src/game_logic.c   <-- pure C rules engine, zero OSLib/WIMP dependency
src/main.c         <-- RISC OS WIMP shell (iconbar icon, Wimp_Poll loop)
```

`game_logic.c`/`include/game_logic.h` know nothing about RISC OS,
sprites, windows, or SWIs -- they only know about pawns, dice, and
turns. This is what lets the exact same source file be:

- cross-compiled with ArchieSDK's `arm-archie-gcc` into the real game
  (linked into `src/main.c` in the normal `all`/app-directory build), and
- compiled with the **host's own `cc`** and exercised by
  `tests/test_game_logic.c` for automated testing (`make test`), with no
  ArchieSDK, no Arculator, and no RISC OS ROM involved at all.

`board_layout.c` (board geometry) and `ai.c` (AI move choice) follow the
same rule -- both are portable, both have their own host-side test
suites (`test_board_layout.c`, `test_ai.c`).

See [BUILDCHAIN.md](BUILDCHAIN.md) for how the two separate compilation
paths are wired up in the Makefile, and [GAME_LOGIC.md](GAME_LOGIC.md)
for the rules engine's data model and API in detail.

Why this split matters here specifically: the WIMP shell can only be
verified visually, by hand, inside Arculator or on real hardware (see
`CLAUDE.md`'s Testing section -- Arculator has no headless/scriptable
mode). Automating *that* isn't possible. Automating the actual game
rules is, and that's where bugs are cheapest to catch, so the rules
engine is kept 100% independent of the shell precisely so it doesn't
have to share that limitation.

## Current state

| Component | Status |
|---|---|
| `game_logic.c` | Complete rules engine, including the full multi-rule-set/house-rule system (3 presets, 8 toggles -- see [RULES.md](RULES.md) for the player-facing manual, [GAME_LOGIC.md](GAME_LOGIC.md) for the API). Fully unit tested, exhaustive coverage of all 128 toggle combinations |
| `board_layout.c` | The real Mens Erger Je Niet board (see [BOARD_LAYOUT.md](BOARD_LAYOUT.md)), ported from the GEOS edition's own coordinate tables, not invented -- fully unit tested |
| Graphics | Pawns/dice are real sprites (`Wimp_PlotIcon`), board cells and start markers are `os_plot` primitives -- see "Board and game rendering" below |
| `main.c` / `game_view.c` | Playable, extensively refined WIMP game: iconbar icon, game window with animated pawn movement, dice roll animation, movable-pawn highlight rings, capture/win detection, Continue-gated AI turns |
| `rules_view.c`/`setup_view.c`/`save_view.c`/`win_view.c`/`splash_view.c` | Rule Options dialogue (preset picker + all 8 house-rule toggles), New Game/player setup, 5-slot save/load, win/continue dialogue, About/splash screen -- all done |
| Audio (QTM) | Done and live-confirmed on real hardware: 3 selectable background tracks, 6 one-shot SFX embedded as MOD instrument samples, independently switchable On/SFX toggles -- see [QTM.md](QTM.md) |
| AI opponents | Working (`ai.c`) -- three real difficulty tiers (Low/Medium/High, `LUDO_AI_EASY`/`NORMAL`/`HARD`), with a per-player difficulty picker in the New Game dialogue -- see [AI.md](AI.md) |
| Save/load | 5 fixed, renamable slots inside the app directory; rules persist in the save format. Live-confirmed working |
| Application directory packaging | Real `!ArchiLudo` app directory with custom icon (see [BUILDCHAIN.md](BUILDCHAIN.md)) |
| Distribution | `make zip` (filetype-preserving PKZIP), `make disk` (from-scratch ADFS "D"-format disc image), `make deploy`/`make deploy-pibridge` (Arculator hostfs and real PiEconetBridge hardware) -- see [BUILDCHAIN.md](BUILDCHAIN.md) |
| Full-screen enhanced graphics mode | Decided against -- staying with the current windowed WIMP mode permanently (see "Decisions made and not revisited" below) |

## Directory layout

```
ArchiLudo/
  src/
    game_logic.c    -- rules engine (portable)
    board_layout.c  -- board geometry (portable)
    ai.c            -- AI opponent move selection (portable, see docs/AI.md)
    game_view.c     -- the game window: creation, redraw, clicks, animation
    setup_view.c    -- the "New Game" dialogue: names, Human/AI + difficulty per player
    win_view.c      -- the "a player has finished" Continue/New Game/Quit dialogue
    rules_view.c    -- the "Rule Options" dialogue: preset + house rules
    splash_view.c   -- the startup/About window (idi8b logo, version, author)
    save_view.c     -- Save/Load dialogues (5 fixed slots)
    main.c          -- WIMP shell (task lifecycle, iconbar, menu, dispatch)
  include/
    game_logic.h, board_layout.h, ai.h, game_view.h, setup_view.h,
    win_view.h, rules_view.h, splash_view.h, save_view.h -- one header
    per module above, each a full docstring-style API reference
    qtm.h           -- QTM audio wrapper API (see docs/QTM.md)
    archiludo.h     -- WIMP shell shared declarations
  lib/
    qtm.c           -- QTM SWI wrapper implementation
  tests/
    test_game_logic.c, test_board_layout.c, test_ai.c -- host-side
    automated test suites (`make test`)
  tools/
    riscos_sprite.py           -- PNG <-> RISC OS Sprite converter
    test_riscos_sprite.py      -- its own regression test
    mod_embed_sfx.py           -- embeds SFX into each MOD's sample table
    prepare_pibridge_deploy.py -- stages the app dir with .inf sidecars for PiFS
    build_adfs_disk.py         -- from-scratch ADFS disc-image writer
    riscos_readme.py           -- Markdown -> RISC-OS-readable plain text
  assets/
    !Sprites, !Sprites22, PawnSprite -- shipped packed sprite files
    audio/          -- shipped Music1-3 (SFX embedded into their MOD sample tables at
                       build time, not shipped separately -- see mod_embed_sfx.py
                       above) and QTMModule; SfxDice/Release/Move/Capture/Home/Win
                       here are build-time inputs only
    generate_icon_sprites.py, generate_app_icon.py -- asset generators (`make assets`)
    edit/           -- hand-editable PNG copies (`make export-sprites`/`import-sprites`)
    geos_source/    -- local copies of the original GeoLudo bitmap art, for reference
  docs/
    ARCHITECTURE.md  -- this file
    RULES.md         -- full player-facing rules manual (base rules, toggles, presets)
    GAME_LOGIC.md    -- rules engine API/data-model manual
    BOARD_LAYOUT.md  -- board geometry manual
    AI.md            -- AI opponent manual
    QTM.md           -- audio manual (SWI reference, SFX mechanism, player manual)
    TOOLS.md         -- every standalone asset/build tool: usage, requirements, gotchas
    BUILDCHAIN.md    -- ArchieSDK/Makefile/toolchain/distribution manual
    OSLIB.md         -- how this project uses OSLib
    LIBARCHIE.md     -- ArchieSDK's bundled helper library
  app/!Run           -- the app directory's Obey file (checked in without its ,feb suffix)
  riscos_wimp_reference.md -- WIMP/SWI/message reference (curated from the PRM + Pinknoise archive + Fryatt's guide)
  CREDITS.md -- everything this project is built on/ported from
  Makefile
  .env / .env.example
```

## WIMP conventions and gotchas

Facts about RISC OS WIMP programming that this project's own live
testing uncovered and that aren't obvious from the PRM's prose alone
-- keep these in mind for any new window/dialogue/menu work:

- A window's `work_flags` must be a real button type (e.g.
  `wimp_BUTTON_CLICK`) for a custom-plotted (non-icon) background to
  ever receive `Mouse_Click` events at all -- `wimp_BUTTON_NEVER`
  silently swallows every click on that area. A window whose every row
  is plain Wimp icons (no custom-plotted background needing click
  detection) should keep `wimp_BUTTON_NEVER`.
- `Open_Window_Request`/`Close_Window_Request` must be handled
  generically (calling `Wimp_OpenWindow`/`Wimp_CloseWindow` back) or a
  window can't be dragged, resized, or closed.
- Icons need explicit fg/bg colour bits set in `flags` or they render
  invisible (defaults to colour-0-on-colour-0).
- `wimp_ICON_SHADED` is a genuine Wimp-level click guard, not just
  visual -- use it to both show and enforce that a control is inert.
- A `wimp_menu`'s plain entry has only a 12-byte inline text buffer;
  anything longer needs `wimp_ICON_INDIRECTED` (works the same way on a
  `wimp_menu_entry` as on an ordinary icon, since they share the same
  `wimp_icon_data` union).
- When more than one distinct `wimp_menu` exists in an app,
  `Menu_Selection` doesn't say which menu produced it -- track "which
  menu is open" yourself.
- Real RISC OS radio-button icons need the `"Sradiooff,radioon"` sprite
  pair specifically -- `"Soptoff,opton"` is for independent on/off
  checkboxes, not mutually-exclusive radio choices, even though both
  look similar.
- A genuine "pop-up menu field" is two icons: a read-only
  `BUTTON_NEVER` display field plus a separate small sprite-only arrow
  button (`"R5;Sgright,pgright"`) that opens the menu -- not a single
  clickable text field.
- The Filer only probes a directory's `!Sprites` file the first time it
  scans the *containing* folder; sprite names for a directory's own
  Filer icon must be **lowercase** (e.g. `!archiludo`/`sm!archiludo`)
  even though `Wimp_CreateIcon`'s iconbar lookup is case-insensitive.
- `argv[0]` (populated by ArchieSDK's crt0 with the full RISC OS
  invocation path) truncated at its last `.` gives a program's own
  directory reliably, regardless of whether it was launched as a bare
  file or via `!AppDir.!RunImage` -- the standard, correct way for this
  project to locate its own resources, not a bare relative filename.
- `Wimp_PlotIcon`'s icon block uses plain **work-area** coordinates
  (never scroll/origin-adjusted), unlike `os_plot` calls in the same
  redraw context which use screen-absolute coordinates -- these two
  coordinate spaces are not interchangeable even inside the same
  function.
- **`Wimp_PlotIcon` does NOT scale a sprite icon to fit its extent.** A
  plain sprite icon always plots at its native size (source pixel
  count x the sprite's own recorded old-style mode's OS-units/pixel),
  centred via `HCENTRED`/`VCENTRED` -- the icon extent's size has zero
  effect on rendered size. Size the source sprite itself to the wanted
  on-screen size instead.

## Redraw and animation architecture

`src/game_view.c` owns the one game window; `src/main.c` stays a thin
task-lifecycle shell (`Wimp_Initialise`, the `Wimp_Poll` loop, the
iconbar icon and its menu) that dispatches redraw/click/menu events to
whichever module owns the window handle involved.

Two distinct redraw mechanisms are used deliberately for different
purposes:

- **`Wimp_RedrawWindow`** for genuine window exposure -- it
  auto-clears every returned rectangle to the window background before
  returning control, which is correct here (a fresh expose should show
  the current state from scratch). `board_layout.c` classifies every
  cell once at startup; a redraw fills every non-empty cell with a
  flat `colourtrans_set_gcol`+`os_plot` shape and plots each pawn's
  sprite via `Wimp_PlotIcon`.
- **`Wimp_UpdateWindow`** for every animation tick (dice roll, pawn
  slide, pulsing highlight rings, settle diffs) -- it does **not**
  auto-clear, so the caller is fully responsible for erasing the
  previous frame's content before redrawing (an explicit "fill
  background, then redraw" pattern; omitting the erase step leaves
  visible trails/ghosts). Its rectangle is an **input**, unlike
  `Wimp_RedrawWindow`'s, and must be filled in before the call.

Some easy-to-repeat mistakes from getting this wrong, worth remembering
for any future animation work:

- `wimp_draw.box`, once inside the `while (more)` redraw loop, is the
  window's **entire visible area** in screen coordinates -- never the
  small rectangle actually being updated. The real per-iteration
  paintable rectangle is `.clip`.
- The PRM's redraw/update box max x/y (`x1`/`y1`) are **exclusive**,
  unlike `os_PLOT_RECTANGLE`'s inclusive x1/y1 -- under-requesting on
  the upper bound silently crops content flush against that edge with
  no error. The current code pads the *request* box's upper bound by
  8 OS units to compensate, then deliberately clamps back down to the
  true unpadded cell boundary at *erase* time (while the *repaint*
  step still covers the padded region) -- this asymmetric erase/repaint
  split is what eliminates cropping without reintroducing trails.

A "diff redraw" pattern (`snapshot_pawn_positions()` before a state
change, compare after) avoids full-window redraws for the common case
where nothing else on the board changed -- only a capture/collision/
mandatory-release displaces a second pawn. Following an ordinary move
(not the mandatory-release path), a displaced pawn gets its own short
`STEP_CAPTURE_MOVING` slide from its old cell straight to its home
base -- a single straight-line hop, not a retrace of the ring path it
actually travelled (which can span most of the board) -- rather than
the instant reposition the diff redraw alone would otherwise produce;
more than one pawn can be displaced by a single landing (own_pawn_
capture on lets a square hold pawns from several players at once), so
this animates all of them in lockstep on one shared tick counter.

The Throw/Continue button is one physical button whose label and
shading change with game state; AI turns require a Continue click at
every step (including the first roll), driven by a `turn_step` state
machine polled via `Wimp_Poll`'s idle events, never a blocking
busy-wait.

## Screen modes and non-square pixels

ArchiLudo supports all four modes Arculator's own Mode selector offers
for this project's profile -- 12, 15, 27, and 39 -- not just 15. Modes
12/15/39 share a non-square 2x4 OS-unit-per-pixel geometry; mode 27 is
the square-pixel exception (2x2). Window/icon layout and all custom
`os_plot` drawing already work in OS units directly (mode-independent
by construction); only sprites, which store raw pixel data, need any
mode-aware handling at all -- see "Sprite file format" and "Current
rendering approach" below for the full detail (why sprites are drawn
square and tagged mode 27 rather than pre-squished for one specific
mode).

**A manually filled rectangle border needs >=4 OS units of thickness
on mode 15's Y axis to reliably render at all** -- thinner can land
entirely between two pixel-centre sample points and paint nothing.
This bound already covers the largest of ArchiLudo's four supported
modes' pixel spacings, so it stays safe on mode 27's smaller 2-unit
spacing too. See `riscos_wimp_reference.md`'s "Screen modes:
non-square pixels and thin manually-drawn lines" section for the full
underlying PRM-sampling explanation.

## Board and game rendering

Board cells are `os_plot` circles directly on the window background --
ring cells hollow when empty, home-column "lane" markers always
full-saturation regardless of occupancy, home base has no background
fill at all, matching the GEOS reference. Pawns are `Wimp_PlotIcon`
sprites (the detailed silhouette everywhere a pawn is, not a plain
circle for on-track pawns), with an `os_plot`-circle fallback if
sprite loading ever fails. Two or more pawns of the same colour
sharing one cell (a ring blockade, or free home-column manoeuvring)
are nudged into 4 corner slots keyed by pawn index, not draw order, so
all remain visible.

### Current rendering approach

**Pawns and dice are real sprites**, plotted via `Wimp_PlotIcon`
(`assets/PawnSprite`) -- not `os_plot` primitives, and not a bare
`osspriteop` call. Source art for these is drawn **square** and tagged
mode 27 (`--mode 27` in `tools/riscos_sprite.py`, see
[TOOLS.md](TOOLS.md)), rather than pre-squished for one specific
non-square mode. `Wimp_PlotIcon`'s automatic `PutSpriteScaled` computes
its scale factors from a sprite's own declared mode against whatever
mode is actually live, so one correctly-tagged square sprite displays
correctly under all four of ArchiLudo's supported modes (12/15/27/39)
without needing per-mode art variants -- this is why mode 27 is the
right choice for any *new* hand-drawn sprite in this project, not the
non-square 12/15 family.

**The app's own persistent Filer/iconbar icon** follows a different,
narrower convention than in-game sprites: `!Sprites` (old-style mode
15) and `!Sprites22` (old-style mode 21 or 27 depending on the asset,
exactly double linear resolution of the base to compensate for the
aspect difference) -- **not** `!Sprites11`, which uses new-style
sprite mode encoding (RISC OS Select-era, well past 3.10, and not
understood by genuine RISC OS 3.10). This convention applies only to
the app's own icon, not to any in-game runtime-plotted sprite, which
already auto-scales via `Wimp_PlotIcon` with no stored variants needed.

### Sprite file format (RISC OS <=3.1 "old-style")

Source: RISC OS 3 PRM, Volume 1, Chapter 22 "Sprites"
(`~/riscos-dev/prm-mirror/sprites.html`).

**File header (12 bytes)**:

| Offset | Field |
|---|---|
| 0 | number of sprites |
| 4 | offset to first sprite |
| 8 | offset to first free byte |

**Gotcha**: a saved sprite *file* is the in-memory sprite-area layout
with its leading 4-byte "total size" word omitted -- but every offset
inside the file is still expressed as if that word were present. So
`real_file_offset = stored_offset - 4`. The PRM states this in prose
but doesn't spell out the off-by-4 consequence for the two offset
fields directly -- confirmed byte-for-byte against real sprite files
(see "How the format was verified" below).

**Sprite control block (44 bytes, then palette, then image, then mask)**:

| Offset (from CB start) | Field |
|---|---|
| 0 | offset to next sprite (0 for the last one) |
| 4-15 | name, up to 12 ASCII characters, zero-padded |
| 16 | width in words, minus 1 |
| 20 | height in scan lines, minus 1 |
| 24 | first bit used (always 0 in this tool's output) |
| 28 | last bit used (right end of the last row-word) |
| 32 | offset to image data |
| 36 | offset to mask data (equals the image offset if there's no mask) |
| 40 | mode number (old-style; see below) |
| 44... | palette, if bpp <= 8 |

**Palette size is genuinely variable, not always `1 << bpp` entries.**
Real 8bpp sprites (confirmed against several of Steve Fryatt's own
app-icon sprites) very often have **zero** embedded palette entries at
all, relying on the mode's own default. The correct way to determine
palette entry count is to derive it from how many 8-byte entries
actually fit between the fixed 44-byte header and the image-data offset
(`(image_off - 44) // 8`) -- this also correctly handles the documented
256-entry special case with no separate marker check needed. A
palette-less sprite decodes to solid black by design (the documented
"no colour information available" fallback), not a bug.

Each palette entry is two identical 4-byte words (the format
`OS_ReadPalette` returns), each `&BBGGRR00` (blue, green, red, then an
unused low byte) -- **not** `&00RRGGBB`; getting this backwards
produces a blue-tinted image.

Image/mask data is stored row-by-row, top to bottom, each row padded to
a whole number of 32-bit words. Within a word, the **least significant
bits are the left-most pixel** -- pixel 0 goes in bits `0..bpp-1` of
byte 0, pixel 1 in the next `bpp` bits, and so on.

**Old-style mode numbers this project actually uses** (PRM Volume 4
Chapter 95 "Table B: Modes", `~/riscos-dev/prm-mirror/modes.html`):

| Mode | bpp | Pixels | OS units | OS units/pixel |
|---|---|---|---|---|
| 12 | 4 | 640x256 | 1280x1024 | 2x4 (non-square) |
| 15 | 8 | 640x256 | 1280x1024 | 2x4 (non-square) |
| 27 | 4 | 640x480 | 1280x960 | 2x2 (square) |
| 39 | 4 | 896x352 | 1792x1408 | 2x4 (non-square, higher-res) |

Arculator's own Mode selector for this project's profile only offers
modes 12, 15, 27, and 39 -- ArchiLudo supports all four, not just 15.
Modes 12/15/39 share the same non-square 2x4 OS-unit-per-pixel
geometry; 27 is the square-pixel exception. `mode_to_bpp()`'s fallback
table also recognises the square-pixel VGA family generally (modes
25-28, 2x2 OS units/pixel: 25=1bpp, 26=2bpp, 27=4bpp, 28=8bpp) and the
matching 2x4-family modes 20/21, since real external sprite files
(ro-chess's shipped app) use these too.

**How the format was verified**: rather than trust the PRM's prose
alone for the trickier parts (the offset-minus-4 convention, palette
word byte order, pixel bit order, variable palette size, and old-style
mode numbers), this was checked against **real sprite files**: QTM
v1.49's own distribution zip (`!QTMmini/Sprites`, 14 real iconbar icon
sprites, parsed by hand in a Python REPL -- this is what exposed the
offset-minus-4 convention concretely, and confirmed the palette
byte-order and pixel-bit-order fixes); and Steve Fryatt's own
`wimp-prog` tutorial example downloads (`WindowSpriteArea.zip`,
`AppSprite.zip`, `ShapeChooser.zip`, `SpriteIcon.zip`,
`TextAndSpriteIcon.zip`) plus ro-chess's real, shipped
`!Chess/Sprites,ff9` -- every sprite in all five zips and the real
ro-chess file parses without error, several non-trivial ones (multi-
colour palettes, text, masked chess-piece artwork) round-tripped with
**zero pixel differences**, and non-word-aligned widths were
specifically checked for row-boundary artifacts (none found). This
second pass is what found the variable-palette-size and missing-mode
issues already folded into the format description above. Permanent
regression coverage: `tools/test_riscos_sprite.py` (see
[TOOLS.md](TOOLS.md)) reproduces both bug patterns with small
hand-built synthetic sprite files, plus the non-word-aligned round-trip
check.

### Lasting gotchas for hand-drawn pixel art

- **Dither/texture patterns must key on `y // 2`, not `y`**, for any
  pixel art meant to look right on both square (mode 27) and non-square
  (2x4, modes 12/15/39) screens -- RISC OS drops every other source row
  when scaling between a sprite's tagged mode and a coarser live mode,
  so a texture alternating per raw row can lose half its pattern
  silently on the non-square modes. A checkerboard (50/50) dither for
  pawn highlight regions read as solid diagonal stripes rather than a
  blend at small icon sizes for a related reason -- see "Decisions made
  and not revisited" below.
- **Resizing hand-drawn pixel art must use `Image.NEAREST`**, not
  `Image.BOX`/`LANCZOS`, for icon-sized hard-edged art -- smoothing
  resizes produce halos, off-hue speckling at flat-colour boundaries,
  and unpredictable per-output-size shape variance. Decide any dither
  pattern at the *final* pixel grid, never a supersampled working
  resolution.
- **Leave real margin inside the working canvas before any outline
  dilation runs**, or the dilated edge clips against the canvas
  boundary itself (a flat/missing edge on whichever side has the least
  margin). Handled generally via a `CONTENT_SCALE` shrink-toward-centre
  applied to every drawn coordinate, not per-coordinate hand-tuning.
- Fine graphic elements (dice pips, outlines) must be drawn **directly
  at each output's own native resolution**, not drawn once at a shared
  working resolution and resampled per output -- resampling ties the
  exact rendered shape to each output's specific resize ratio.

See [TOOLS.md](TOOLS.md) for the tools that produce and edit this
project's sprite assets (`tools/riscos_sprite.py`, the `assets/
generate_*.py` generators, and the `make export-sprites`/`import-
sprites` hand-editing round-trip).

## AI design

`ludo_ai_choose_pawn()` (`ai.c`) scores every legal move and picks the
highest, adapted from the original GeoLudo edition's own
`computerchoosepawn()` but not a literal port -- own-pawn-collision
scoring, danger/escape distance math, and the win-check were all
recomputed to be correct under this project's own rules rather than
reusing the original's coincidentally-valid shortcuts. See
[AI.md](AI.md) for the scoring weights in detail. Three real
difficulty tiers (Low/Medium/High, `LUDO_AI_EASY`/`NORMAL`/`HARD`),
picked per AI-controlled player in the New Game dialogue.

## Save/load system

5 fixed slots at `<ArchiLudo$Dir>.Slot1`..`.Slot5`, each carrying its
own renamable display name as part of the save data. Both the Save and
Load dialogues re-read all 5 slots' names every time they open. The
save format's magic-tagged header is followed by the name block, then
a rules block (preset + all 8 house-rule booleans), then the full game
state -- packed field-by-field, never a raw struct dump, since padding
isn't a documented contract. Loading a game applies its rules to both
the live game state and the separate value the Rules dialogue reads
when reopened, so a loaded game shows its own actual settings there,
not whatever was last configured for a new game.

An earlier design used drag-and-drop (`Message_DataSave`/
`DataSaveAck`, `DragASprite`) -- fully correct against the PRM, but
abandoned; see "Decisions made and not revisited" below for why.

## Application packaging

See [BUILDCHAIN.md](BUILDCHAIN.md)'s "Application directory" section
for the full `!ArchiLudo` structure, icon design, and distribution
pipeline (`make zip`/`make disk`/`make deploy`/`make deploy-pibridge`).

## Known gaps

- **No backward-movement UI.** `src/game_view.c` has no board-
  interaction path for a backward move at all, for either a human or
  an AI player -- it only ever consults the forward movable-pawns API.
  This is harmless as long as nobody selects a ruleset with the
  Backward toggle on (`Pachisi-style` is the only preset that does),
  but the Rules dialogue does let a player choose that combination
  today: on a roll where only a backward move is legal, the game
  currently just settles as if the player were stuck rather than
  offering the backward option. Needs the move-animation path mirrored
  for the backward direction, and, for a human player, some way to
  actually choose "backward" on the board's click model, which doesn't
  have that concept at all currently.

## Decisions made and not revisited

- **Full-screen enhanced graphics mode** (an `archie-face`-based
  double-buffered, VSync-synced board view during gameplay, returning
  to the desktop otherwise) was planned early on and explicitly
  dropped in favour of staying windowed throughout, which has since
  had extensive animation/flicker work of its own and is considered
  good enough on its own merits. `archie-face` remains a valid
  reference if this is ever reconsidered.
- **`QTM_PlayRawSample`** for one-shot SFX was abandoned entirely after
  extensive live debugging (including disassembling the actual fault
  in Arculator's own debugger) found a genuine internal resampler
  buffer overrun with no fixable register combination. See
  [QTM.md](QTM.md) for the working replacement (MOD-embedded samples
  via `QTM_PlaySample`) -- do not reintroduce this SWI for SFX.
- **Drag-and-drop save/load** (`Message_DataSave`/`DataSaveAck`,
  `DragASprite`) was a fully PRM-correct implementation, abandoned not
  because the code was wrong but because Arculator's HostFS bridge
  never replies to `Message_DataSaveAck`, even for a direct icon-bar
  drop -- traced as an environment limitation, not an ArchiLudo bug.
  Replaced with the current 5 fixed save slots. Don't resurrect
  drag-based save/load without first confirming the target filing
  system actually implements the Filer side of that protocol.
- **Ducking the background music's volume during SFX playback** was
  implemented and worked, but was rejected after direct user feedback
  that it was "really annoying." SFX audibility is handled by loudness
  normalization of the source samples instead -- see
  [QTM.md](QTM.md).
- **CR-only line endings for the plain-text README** were chosen on
  the textbook RISC OS convention, then live-tested wrong for this
  project's actual test setup and switched to LF after direct
  byte-comparison against files already known to work. A reminder that
  general RISC OS conventions should be checked against a real,
  already-working file on the actual test setup before being trusted
  for new build tooling.
- **Pre-squished-canvas source art for one specific non-square mode**
  (e.g. drawing at half height for mode 15, relying on that mode's own
  2x/4x stretch) -- superseded by drawing square and tagging mode 27,
  which lets `Wimp_PlotIcon` handle aspect compensation for every
  supported mode automatically instead of committing to one (see
  "Current rendering approach" above).
- **A checkerboard (50/50) dither** for pawn highlight regions --
  replaced with a sparser staggered dot grid for highlights
  specifically, after the checkerboard read as solid diagonal stripes
  rather than a blend at small icon sizes. The shadow region keeps the
  full checkerboard, which was never reported as a problem there.
- **A 16x-upscaled copy for hand-editing** the shipped sprites --
  dropped as a footgun (edits to the native-resolution file were
  silently discarded in favour of a stale upscaled copy). Editing is
  native-resolution only -- see [TOOLS.md](TOOLS.md).
- **Monochrome/high-contrast pawn rendering** for 1-bit-per-pixel
  displays (pattern-filled pawns, matching the original GEOS edition's
  own `monochromeflag` convention) -- proposed, not implemented,
  deferred indefinitely.
- **Reusing the original GEOS edition's own board-entry-marker
  bitmaps** -- replaced with programmatically drawn circle+arrow
  markers instead.

## Porting source: GeoLudo -> ArchiLudo

`/home/xahmol/git/ludo` holds this game's prior ports across 8-bit
platforms and is the source of truth for the rules and prior art.
`GEOS/` (2023 GEOS edition) is the closest existing analogue to
ArchiLudo's architecture, since GEOS is itself a real WIMP environment:
real menu trees, real dialogue boxes, icon-driven actions.

| GEOS (source) | ArchiLudo (RISC OS Wimp) | Why |
|---|---|---|
| `fieldcoords[40][2]`/`homedestcoords[4][8][2]` (board geometry) | `board_layout.c`'s ring/home-column/home-base tables (see [BOARD_LAYOUT.md](BOARD_LAYOUT.md)) | Ported directly (coordinate-converted), not redesigned -- the same board on purpose |
| `struct menu` tree (`menuGEOS`/`menuGame`/`menuFile`) | `wimp_menu` tree via `Wimp_CreateMenu` | Direct structural match |
| `DlgBoxGetString`/`DlgBoxYesNo`/`DlgBoxOk`/`DlgBoxOkCancel` | A small reusable dialogue-window helper built on plain Wimp windows | Wimp has no built-in dialogue-box kernel call like GEOS does |
| `SaveFile`/`GetFile` (GEOS's directory-listing file picker) | `save_view.c`'s Save/Load dialogues, 5 fixed renamable slots | Idiomatic RISC OS UX, not a literal port -- see "Save/load system" above for why this ended up fixed-slot rather than drag-and-drop |
| `throwicon`/`nexticon` (`struct icontab`) | Ordinary `wimp_icon`s inside the game window | Direct conceptual match |
| `informationCredits()`/`ShowCredits()` (splash and Credits menu item are the same screen) | `splash_view.c`'s About window -- shown at startup and reachable again via the "About" menu entry, same dual role | Direct conceptual match, no separate Credits screen needed |
| `computerchoosepawn()` (AI move choice) | `ai.c`'s `ludo_ai_choose_pawn()` | Assessed and reused as the basis, not a literal port -- see [AI.md](AI.md) for what carried over vs. changed |
| `inputofnames()` (name entry + AI count, `DlgBoxGetString`) | `setup_view.c`'s "New Game" dialogue: one writable name icon + one click-to-toggle Human/AI icon + one click-to-cycle Low/Medium/High difficulty icon per player | More granular than GEOS's single "how many AI players" count -- per-player choice, with a difficulty level GEOS never had |
| Custom per-platform bitmap/colour-card plotting (`interface.c`, `geoscore.s`) | RISC OS Sprites (`OS_SpriteOp`/`Wimp_PlotIcon`) in a window redraw handler | GEOS needed hand-written pixel code per 8-bit machine; RISC OS's sprite plotter is uniform |
| `monochromeflag`, `VDC_CLR0..4`, `Switch4080` | One fixed screen mode family (see "Screen modes" above) | VIDC gives every Archimedes the same colour capability under RISC OS 3.10 |
| Overlay/VLIR loading (`loadoverlay`/`openVLIR`/`closeVLIR`) | None -- program stays resident | 1MB+ RAM makes this unnecessary |
| `gamelogic.c` (`turngeneric`, `pawnselect`, dice/AI) | Reimplemented cleanly as `game_logic.c` (see below) | See [GAME_LOGIC.md](GAME_LOGIC.md) for why this isn't a literal port |

## Why `game_logic.c` is a clean reimplementation, not a literal port

The original 1992 Commodore 128 BASIC program (and its 2023 GEOS C
port) use single/double-letter variable names (`ap`, `as`, `vr`, `vl`,
`vn`, `nr`, `gv`, `ov`, `ro`, `np`, `dp`, `zv`) with no surviving
documentation of what most of them represent -- the original author's
own words: "sparsely documented... sometimes have no clue anymore what
variables do." Copying that exact numeric encoding byte-for-byte would
mean reproducing bugs and ambiguities nobody can currently verify, and
would produce a module nobody could safely modify or test with
confidence.

Instead, `game_logic.c` is implemented directly from the plain-English
rules text maintained consistently across every other port (the "Game
rules" sections of `/home/xahmol/git/ludo/README.md` and
`.../GEOS/README.md`), using clearly-named variables, a simple single
integer "steps travelled" position model (see
[GAME_LOGIC.md](GAME_LOGIC.md)), and a full docstring-style comment
above every function (summary/syntax/input/output) rather than sparse
or absent documentation. This also gave an opportunity to verify the
rules make internal sense as a state machine (they do) and to simplify
where the more powerful Archimedes removes a constraint the 8-bit
versions had to design around (no overlay/VLIR paging, one fixed
screen mode, uniform sprite plotting).
