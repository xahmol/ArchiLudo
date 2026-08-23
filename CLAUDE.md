# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

ArchiLudo is a WIMP Ludo game for the Acorn Archimedes, targeting genuine
26-bit RISC OS 3.10 — specifically the maintainer's real upgraded A305
(ARM3, 4MB RAM), while staying compatible with stock A305/A310 (ARM2, much
less RAM).

**Read `docs/` before making non-trivial changes** — it's the primary
reference for this project, kept up to date as development progresses:

| Doc | Covers |
|---|---|
| `docs/ARCHITECTURE.md` | Layering (`game_logic.c` vs WIMP shell), directory structure, GeoLudo→Wimp porting map |
| `docs/BUILDCHAIN.md` | ArchieSDK toolchain, Makefile targets, confirmed compiler defaults/gotchas |
| `docs/GAME_LOGIC.md` | Rules engine data model and API |
| `docs/BOARD_LAYOUT.md` | Board geometry (grid cells, ring/home-column/home-base mapping) |
| `docs/OSLIB.md` | How this project uses OSLib, where to look up anything it doesn't cover |
| `docs/LIBARCHIE.md` | ArchieSDK's bundled helper library |
| `docs/GRAPHICS_TOOLING.md` | The PNG->Sprite converter in `tools/`, RISC OS sprite file format |

`riscos_wimp_reference.md` (project root) is the curated WIMP/SWI/message
reference; `~/riscos-dev/prm-mirror/` and
`~/riscos-dev/wimp-prog-mirror/wimp-prog/` are full local offline mirrors
of the PRM and Steve Fryatt's `wimp-prog` guide for anything the curated
reference doesn't cover in enough depth. `CREDITS.md` lists everything
this project is built on, ported from, or otherwise sourced from —
**every new external tool/library/reference used gets an entry there**,
alongside an inline credit comment at the point of use (see the global
CLAUDE.md's "Code Attribution" section).

**Every library this project creates or wraps gets its own `docs/<NAME>.md`
manual** (not just header docstrings) — e.g. `docs/QTM.md` once the QTM
wrapper library exists. SWI-based APIs get wrapped in a dedicated C
library (`lib/<name>.c`/`include/<name>.h`) rather than called via raw
`_swi()`/`_swix()` inline — this project's preference, matching how OSLib
and `libarchie` already work.

**Keep all of the above updated with new insights while building** — a
message-protocol detail, a compiler gotcha, a rule clarification, a new
OSLib area used for the first time. Documentation debt here is treated the
same as code debt.

**Code convention for this project**: generously documented, not tersely —
every function gets a comment block with Summary/Syntax/Input/Output (see
`include/game_logic.h` for the pattern), and variables are clearly named.
This is a deliberate project-specific exception to the usual terse-comment
default: the original 30-year-old BASIC/GEOS source this game is ported
from used cryptic 1-2 letter variable names with no surviving
documentation, to the point its own author can no longer reliably explain
parts of it — see `docs/ARCHITECTURE.md`'s note on why `game_logic.c` is a
clean reimplementation rather than a literal port.

## Architecture (summary — see `docs/ARCHITECTURE.md` for detail)

Three layers that never mix: `src/game_logic.c` (pure C rules engine),
`src/board_layout.c` (pure C board geometry, maps rules-engine state onto
grid cells), and the WIMP shell (`src/main.c` task lifecycle/iconbar,
`src/game_view.c` the game window/redraw/clicks — raw OSLib `Wimp_Poll`
event loop, no Toolbox, since RISC OS 3.10 doesn't ship it and it's not
worth the RAM on a stock 1MB machine). The first two layers have no
OSLib/WIMP dependency and are unit tested with `make test` using the host
compiler. Window/icon definitions are built directly in C as OSLib
structures rather than a Wimp template file.

**Current status**: Phase 1 (see `docs/ARCHITECTURE.md`'s Roadmap) — a
playable core loop, first-round Arculator feedback already applied (real
board layout ported from the GEOS edition instead of an invented one;
`Open_Window_Request`/`Close_Window_Request` were unhandled, which is why
the window couldn't be dragged/closed; icons had no explicit fg/bg colour
so text was invisible; `wimp_WINDOW_AUTO_REDRAW` was missing, which is
likely why it felt slow). Still needs another round of manual Arculator
verification.

Porting source: `/home/xahmol/git/ludo`, specifically `GEOS/` — see
`docs/ARCHITECTURE.md`'s GeoLudo→Wimp mapping table.

## Toolchain (summary — see `docs/BUILDCHAIN.md` for detail)

**ArchieSDK**, not mainline GCCSDK (mainline only produces 32-bit APCS
binaries, which cannot run on ARM2/ARM3 or real 26-bit RISC OS at all).
Cloned+built at `~/riscos-dev/archiesdk`. `.env` (gitignored, copy from
`.env.example`) sets `ARCHIESDK` and `ARCULATOR_HOSTFS`.

- `make` / `make all` — cross-compiles to `build/ArchiLudo,ff8`
- `make test` — builds and runs the game-logic unit tests with the **host**
  compiler; needs no ArchieSDK, no Arculator, no RISC OS at all
- `make deploy` — copies the built app to the Arculator hostfs folder
- `make zip` — versioned, filetype-preserving release archive
- `make docs` — regenerates `README.pdf` via pandoc

## Testing

Automated: `make test` (see `docs/GAME_LOGIC.md`) — the rules engine is
fully unit tested and needs no emulator.

Manual/visual only, since Arculator has no headless or scriptable mode
(checked its bundled source directly — plain wxWidgets+SDL2 GUI, no
autotype/socket/remote-control interface; its only CLI hook is
`Arculator.exe <config-name>` to skip the config picker): boot
`configs/ArchiLudo-ARM3-4MB.cfg` (matches the maintainer's real hardware)
or `configs/ArchiLudo-ARM2-1MB.cfg` (stock ARM2/1MB compatibility check) in
the Arculator install at `D:\Retro\Acorn\Arculator_V2.2_Windows`. On first
boot of a profile, run `*Configure Mode 15` once from a command line (F12)
to switch the desktop to 256-colour mode 15 — RISC OS saves this to CMOS
itself, so it only needs doing once per profile (not something to set via
Arculator's own `.cfg` file — its `display_mode` key is an Arculator
rendering option, unrelated to the RISC OS screen mode; confirmed by
reading Arculator's own source). Debugging
there: Arculator's built-in ARM debugger (breakpoints, register/memory
view, disassembly) is the primary tool, since nothing like the Reporter
module is assumed present on stock RISC OS 3.10; file-based logging via
`fopen`/`fprintf` is the fallback for non-interactive tracing.

## License

GPLv3 (see `LICENSE`). Any code added to this repository should be
compatible with GPLv3 licensing.
