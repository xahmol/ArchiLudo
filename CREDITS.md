# Credits

Everything ArchiLudo's build environment, references, and (eventually)
libraries are built on or derived from. Per this project's code
attribution convention (see the global CLAUDE.md's "Code Attribution"
section), specific credit for anything adapted, ported, or directly
sourced from elsewhere also appears inline, near the code/doc it applies
to -- this file consolidates all of it in one place.

## Toolchain

- **ArchieSDK** -- the ARM2-targeting GCC 8.5.0 cross-compiler, custom
  libc, bundled OSLib, and `libarchie` this project's whole build depends
  on. By Tara "Targz" Colin (C runtime, stdlib, SDK tools, build scripts),
  with Enfys "TôBach" Castle-Roper (ARM assembly/SWIs, `libarchie`) and
  Octavia "Kitsu" Lea (maths, debugging, stdlib), licensing help from Lex
  Bailey. `https://gitlab.com/_targz/archiesdk`
- **OSLib** -- the typed C/assembler API used throughout ArchiLudo's WIMP
  code, bundled with ArchieSDK. By Jonathan Coxhead and the OSLib
  maintainers. `https://ro-oslib.sourceforge.io/`

## Emulation

- **Arculator** -- the Acorn Archimedes emulator this project is tested
  against. By Sarah Walker. `https://github.com/sarah-walker-pcem/arculator`
  (Kieran Connell's `kieranhj/arculator` is a fork of this upstream, not a
  separate project.)

## Reference documentation

- **RISC OS 3 Programmer's Reference Manual** -- Acorn Computers /
  riscos.com Technical Support. Source for the WIMP SWI conventions,
  message protocols, and the Sprite file format this project's
  `tools/riscos_sprite.py` implements. Full local mirror:
  `~/riscos-dev/prm-mirror/`.
- **The Pinknoise archive** -- Acorn-internal documentation collated by
  Robin Watts, including material contributed by "Pelago" (John Veness).
  `https://wss.co.uk/pinknoise/Docs/`
- **Steve Fryatt** -- the `wimp-prog` WIMP-C-programming tutorial guide,
  used as the conceptual/architectural reference for this project's event
  loop (his SFLib/GCCSDK code itself isn't used, since it doesn't build
  under ArchieSDK -- see `docs/BUILDCHAIN.md`).
  `https://www.stevefryatt.org.uk/risc-os/wimp-prog`. Full local mirror:
  `~/riscos-dev/wimp-prog-mirror/`.

## Music

- **QTM (QTheMusic)** -- the ProTracker/FastTracker MOD player module this
  project uses for background music and sound effects (see
  `docs/QTM.md`). By Steve Harrison ("Phoenix"/"Quantum"), 1993-2023,
  freeware; 32-bit RISC OS support by Jeffrey Lee.
  `http://www.pi-star.co.uk/qtm/`

## Archimedes demo/graphics technique reference

- **Kieran Connell (`kieranhj`)** -- `archie-face` ("Fast Archimedes C
  Environment," built on ArchieSDK) is the basis for ArchiLudo's
  full-screen double-buffered/VSync-synced gameplay view (see
  `docs/ARCHITECTURE.md`). Also surveyed: `qtm-vasm` (a vasm port of QTM,
  useful background reading on QTM's internals), `rasterman-vasm`
  (Steve Harrison's "Rasterman" raster-effects engine, ported to vasm --
  reference only, not currently used), `stniccc-archie`/`doom-fire`
  (Archimedes demo-effect showcases, technique reference only).
  `https://github.com/kieranhj`

## Porting source

- **Ludo / GeoLudo** -- this game's own prior ports across 8-bit
  platforms (Commodore 128 BASIC, 1992; Oric Atmos; TI-99/4a; C128/CC65;
  GEOS, 2023), by Xander Mol, the author of this project. The GEOS
  edition specifically is ArchiLudo's architectural porting source (see
  `docs/ARCHITECTURE.md`'s GeoLudo->Wimp mapping table); ArchiLudo's rules
  engine (`src/game_logic.c`) is a clean reimplementation of the rules
  documented there, not a line-for-line port (see `docs/GAME_LOGIC.md`
  for why). Its board geometry (`src/board_layout.c`) and, as of Phase
  1's round 6, its **actual pawn/board-entry-marker artwork** are direct
  reuses, not just references: `assets/geos_source/*.gbm` are local
  copies of `GEOS/assets/bm_pawn.gbm` and `bm_{g,r,b,y}start.gbm`,
  recoloured and resized for RISC OS by
  `assets/generate_placeholder_art.py` -- see
  `docs/GRAPHICS_TOOLING.md`'s "Round 6: reusing GeoLudo's own art".
  `https://github.com/xahmol/ludo`

## Updating this file

Add an entry here whenever a new external tool, library, module, or
reference source gets used or adapted from -- alongside the inline credit
comment at the point of use, per the global code-attribution convention.
