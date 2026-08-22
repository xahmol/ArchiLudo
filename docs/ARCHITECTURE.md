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
| `main.c` | Smoke-test WIMP shell only: iconbar icon, quits on click/`Message_Quit`. Not yet wired to `game_logic.c` |
| Sprite/graphics tooling | `tools/riscos_sprite.py`, see [GRAPHICS_TOOLING.md](GRAPHICS_TOOLING.md) -- built and self-verified, no assets made yet |
| Board/dice/pawn rendering | Not started |
| Music/SFX (QTM) | Not started -- see "Roadmap" below |
| Dialogue boxes, menus | Not started |

## Roadmap

Phased plan, confirmed with the user (including the graphics architecture
decision below via an explicit choice):

| Phase | Goal | Status |
|---|---|---|
| 0 | Build environment: ArchieSDK, Arculator profiles, `game_logic.c` + tests, docs set, PRM/wimp-prog mirrors | done |
| 1 | Playable, plain WIMP game: wire `game_logic.c` into a real game window (board/pawns as simple sprites, dice via icon click, Game/File menu, name-entry/restart-confirm dialogues per the porting table below), drag-based save/load | not started |
| 1.5 | Sprite/graphics tooling (`tools/riscos_sprite.py`) | done |
| 2 | Real board/pawn/dice art via the tooling above | not started |
| 3 | Audio: `lib/qtm.c`/`docs/QTM.md` wrapper (see [[archiludo-riscos-project]] memory / `CREDITS.md` for why QTM over archieklang), bundle `QTMModule`, background MOD + dice-roll/capture/win sound effects | not started |
| 4 | Enhanced graphics: full-screen double-buffered gameplay view (see "Graphics architecture: hybrid" below), smooth pawn/dice animation | not started |
| 5 | AI difficulty, credits/options dialogues, `!Sprites`/`!Boot`/`!Run` app-directory packaging | not started |
| 6 | Release: versioned zip, README/screenshots, both Arculator profiles verified | not started |

Phase 1 deliberately comes before any graphics/audio investment -- it
validates the whole rules-engine-to-WIMP wiring while everything's still
easy to change.

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

The next real implementation step is Phase 1: wiring `game_logic.c` into
an actual game window -- see the porting table below for what that reuses
from GeoLudo and what's new.

## Directory layout

```
ArchiLudo/
  src/
    game_logic.c     -- rules engine (portable)
    main.c           -- WIMP shell (ArchieSDK/OSLib only)
  include/
    game_logic.h      -- rules engine API + full rules writeup
    archiludo.h        -- WIMP shell shared declarations
  tests/
    test_game_logic.c -- host-side automated test suite (`make test`)
  tools/
    riscos_sprite.py  -- PNG <-> RISC OS Sprite converter (host-side, Python)
  docs/
    ARCHITECTURE.md    -- this file
    BUILDCHAIN.md       -- ArchieSDK/Makefile/toolchain manual
    GAME_LOGIC.md        -- rules engine manual
    OSLIB.md              -- how this project uses OSLib
    LIBARCHIE.md           -- ArchieSDK's bundled helper library
    GRAPHICS_TOOLING.md    -- the sprite converter and RISC OS sprite format
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
