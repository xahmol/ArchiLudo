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
- **FFmpeg** -- used host-side (not shipped with ArchiLudo) for two
  audio asset-prep tasks: converting the bundled SFX source recordings
  to raw 16-bit PCM (`docs/QTM.md`'s "Asset preparation" section), and,
  via `ffprobe`'s bundled `libopenmpt` demuxer, validating
  `tools/mod_embed_sfx.py`'s rewritten `.mod` files still parse as
  correct tracker modules. `https://ffmpeg.org/`

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
  under ArchieSDK -- see `docs/BUILDCHAIN.md`), and specifically Chapter
  17 ("Creating an Application Directory") as the direct structural
  template for `build/!ArchiLudo/`'s `!Run`/`!Sprites`/`!Sprites22`
  layout (see `docs/BUILDCHAIN.md`'s "Application directory" section
  for what was adapted vs. followed as-is).
  `https://www.stevefryatt.org.uk/risc-os/wimp-prog`. Full local mirror:
  `~/riscos-dev/wimp-prog-mirror/`.
- **DiscImageManager** -- Gerald Holdsworth's disc-image utility
  (GPL-3.0). Ground truth for `tools/build_adfs_disk.py`'s ADFS "D"
  format disc-image writer: every byte offset, field width,
  and checksum algorithm was read directly from its
  `DiscImage_ADFS.pas`/`DiscImage_Private.pas` source rather than
  reconstructed from the PRM's prose. No DiscImageManager code is
  copied -- `build_adfs_disk.py` is an independent from-scratch Python
  implementation, released under this project's own GPLv3 as permitted
  by DiscImageManager's licence. `https://github.com/geraldholdsworth/DiscImageManager`

## Music

- **QTM (QTheMusic)** -- the ProTracker/FastTracker MOD player module this
  project uses for background music and sound effects (see
  `docs/QTM.md`). By Steve Harrison ("Phoenix"/"Quantum"), 1993-2023,
  freeware; 32-bit RISC OS support by Jeffrey Lee.
  `http://www.pi-star.co.uk/qtm/`
- **QTMModule binary (v1.49c)** -- bundled directly in ArchiLudo's own
  app directory (`assets/audio/QTMModule`, see `app/!Run`'s `RMEnsure`
  line) rather than assumed present, per this project's established
  "stay playable if an extra isn't there" principle. v1.49c (03 Apr
  2023) was chosen over the older v1.49b originally bundled -- confirmed
  byte-identical (MD5) across all three of `kieranhj/arc-django-2`,
  `bitshifters/aklang`, and `bitshifters/mikroreise`'s own bundled
  copies, i.e. the actual module version this project's `QTM_PlaySample`
  SWI number was confirmed against, not the older one ArchiLudo happened
  to already have. Original v1.49b copied from a real, working ArchieSDK
  example project's own bundled copy,
  `examples/bydctc/data/QTMModule,ffa` in the local ArchieSDK checkout
  (`~/riscos-dev/archiesdk`) -- also where `lib/qtm.c`'s own QTM SWI
  numbers (QTM_Load/QTM_Start/QTM_Clear) were confirmed against real,
  working code (`examples/bydctc/main.c`), not guessed.
- **`lin2LOG` (linear-to-VIDC-log sample converter with a
  `QTM_PlayRawSample` playback test)** -- by Steve Harrison himself
  (QTM's own author, posting as "steve3000"), shared as a BASIC source
  listing on a stardot.org.uk RISC OS porting discussion in response to
  a question about playing one-shot sound effects through QTM. Not
  redistributed or used as code directly (this project's own `lib/qtm.c`
  is an independent C implementation), but its exact register usage --
  `QTM_SoundControl`'s existence and calling convention (undocumented
  anywhere else this project found), and the correct repeat-length value
  for `QTM_PlayRawSample` -- directly corrected two wrong guesses made
  during this project's own earlier debugging (see `docs/QTM.md`'s "SWI
  reference" section). `https://stardot.org.uk/forums/viewtopic.php?t=27420`
- **`kieranhj/arc-django-2`** (GitHub) -- a real, shipped Archimedes
  game, checked for a working `QTM_SoundControl`/`QTM_PlayRawSample`
  reference. Its `lib/swis.h.asm` gave a larger confirmed QTM SWI table
  (including `QTM_Stop`, never confirmed before), and its own working
  `QTM_SoundControl` calls corrected a real misunderstanding this
  project had held (R1 is a behaviour-flags bitmask, not a
  channel-reservation count) -- see `docs/QTM.md`'s "SWI reference"
  section. Read-only reference, no code copied.
  `https://github.com/kieranhj/arc-django-2`
- **`bitshifters/aklang`** (GitHub, "ArchieKlang Announcetro") -- a real
  Bitshifters demo generating sample data at runtime, checked for a
  `QTM_PlayRawSample`/sample-playback reference. Revealed
  `QTM_PlaySample` and `QTM_RegisterSample`'s SWI numbers (previously
  unknown to this project) via its own SWI table, but the demo itself
  doesn't call any of the three sample-playing SWIs -- its samples
  become embedded MOD instruments instead. Read-only reference, no code
  copied. `https://github.com/bitshifters/aklang`
- **`bitshifters/mikroreise`** (GitHub) -- another real Bitshifters
  production on the same demo toolchain, checked for completeness at
  the same time; confirmed the same pattern (only music-level QTM calls,
  no sample-playing SWIs used). Read-only reference, no code copied.
  `https://github.com/bitshifters/mikroreise`
- **"digital innovation1"** (`Music1`) -- background music track 1, by
  dgtlnnvt. ProTracker `.mod`, CC0-compatible per The Mod Archive's own
  distribution terms. `https://modarchive.org/index.php?request=view_by_moduleid&query=38135`
- **"lk's doskpop"** (`Music2`) -- background music track 2, by lks.
  ProTracker `.mod`, same terms as above.
  `https://modarchive.org/index.php?request=view_by_moduleid&query=105208`
- **"on the run"** (`Music3`) -- background music track 3, by Anders
  Lundqvist. ProTracker `.mod`, same terms as above.
  `https://modarchive.org/index.php?request=view_by_moduleid&query=157927`

## Sound effects

- **Kenney** (kenney.nl) -- the "Interface Sounds" pack (click/
  confirmation/error tones -- pawn release and per-move tick SFX) and,
  via its OpenGameArt mirror, the "54 Casino Sound Effects" pack
  (dice-throw SFX). CC0 (public domain); Kenney is one of the most
  widely-used, long-standing CC0 game-asset sources, credited across
  thousands of indie/game-jam projects.
  `https://kenney.nl/assets/interface-sounds`,
  `https://opengameart.org/content/54-casino-sound-effects-cards-dice-chips`
- **"Aargh! (male screams)"** -- the capture SFX (`SfxCapture`,
  `aargh3.ogg` from this pack). Two earlier choices for this specific
  effect were tried and replaced in live testing: a Kenney "Interface
  Sounds" click read as inaudible against the music (a soft ~2.7ms
  attack ramp, unlike this pack's full-scale, near-instant one), and a
  Kenney "Impact Sounds" wood-knock, while clearly audible, didn't read
  as the more expressive reaction the maintainer wanted for a captured
  pawn. This pack aggregates and normalises 8 scream recordings from
  three original sources; `aargh3.ogg` specifically traces to "human
  male scream multi" by JohnsonBrandEditing
  (`https://freesound.org/people/JohnsonBrandEditing/sounds/173944/`),
  CC0. Aggregated/normalised by congusbongus (also credited below for
  the win fanfare), CC-BY 3.0 for the pack as a whole.
  `https://opengameart.org/content/aargh-male-screams`
- **"Glorious Victory Fanfare NES"** -- the game-won SFX (`SfxWin`), a
  Famicom/NES-style chiptune fanfare made with FamiStudio, trimmed to 4
  seconds with a short fade-out (see `lib/qtm.c`'s own asset-preparation
  notes in `docs/QTM.md`). By congusbongus, CC0.
  `https://opengameart.org/content/glorious-victory-fanfare-nes`

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

## RISC OS WIMP design reference

- **`!Chess`/"Archimedes Chess"** -- a real, shipped RISC OS WIMP board
  game (Colin Granville's RISC OS front-end for GNU Chess), consulted
  for design inspiration (per explicit request) on WIMP board-game UI
  patterns -- confirmed its `Wimp_UpdateWindow` usage as the working
  precedent behind the flicker-free animation approach described in
  `docs/ARCHITECTURE.md`'s "Redraw and animation architecture" section,
  and its `hilite_do()` selected-square flash directly inspired
  ArchiLudo's own flashing movable-pawn/hover rings.
  Not used as code (different toolchain and licence -- CHESS GPL, not
  code shared with ArchiLudo), read purely as a reference.
  `https://github.com/marutan/ro-chess`

## Game rules research

- **YM Imports -- "How to Play Ludo"** -- one of two independent external
  Ludo rules references consulted to audit this engine's own rule set
  against mainstream Ludo (per explicit user request) -- see
  `docs/RULES.md`'s "Where these rules come from" section for what the
  audit found and changed. Read-only reference, no code/text reproduced.
  `https://www.ymimports.com/pages/how-to-play-ludo`
- **Ludo Ghar -- "Rules"** -- the second of the two references for the
  same audit. Two of its claims (a blockade "moving as one unit,
  splitting the die roll," and a requirement to capture an opponent
  before entering the home column) were checked and deliberately NOT
  adopted, for lack of corroboration from the other source above or
  general Ludo knowledge -- see `docs/RULES.md`'s "Where these rules
  come from" section. `https://www.ludoghar.co/pages/rules`

## Porting source

- **Ludo / GeoLudo** -- this game's own prior ports across 8-bit
  platforms (Commodore 128 BASIC, 1992; Oric Atmos; TI-99/4a; C128/CC65;
  GEOS, 2023), by Xander Mol, the author of this project. The GEOS
  edition specifically is ArchiLudo's architectural porting source (see
  `docs/ARCHITECTURE.md`'s GeoLudo->Wimp mapping table); ArchiLudo's rules
  engine (`src/game_logic.c`) is a clean reimplementation of the rules
  documented there, not a line-for-line port (see `docs/GAME_LOGIC.md`
  for why). Its board geometry (`src/board_layout.c`) and its **actual
  pawn/die-face artwork** are direct reuses, not just references:
  `assets/geos_source/*.gbm` are local copies of `GEOS/assets/bm_pawn.gbm`
  and `dice1..6.gbm`, recoloured and resized for RISC OS by
  `assets/generate_placeholder_art.py` -- see `docs/GRAPHICS_TOOLING.md`'s
  "Current rendering approach" section. (The board-entry direction
  markers, `bm_{g,r,b,y}start.gbm`, were also tried this way but are
  drawn programmatically instead -- see that same section for why.)
  ArchiLudo's AI opponent
  (`src/ai.c`) is likewise assessed from, not a literal port of,
  GeoLudo's `computerchoosepawn()` (`GEOS/src/main.c`) -- see
  `docs/AI.md` for exactly what carried over and what changed.
  `https://github.com/xahmol/ludo`

## Branding

- **"idi8b" (I Dream In 8 Bits) logo** -- the maintainer's own brand
  identity, shown on ArchiLudo's splash/about window
  (`src/splash_view.c`). Source artwork
  (`idi8b-logo-lowercase.png`/`.ans`/`.petmate`) from the private
  `idreamtin8bits-astro` repo, checked out locally at
  `/home/xahmol/git/idreamtin8bits-astro`; reproduced as a hand-extracted
  grid of flat `os_plot` rectangles rather than a sprite -- see
  `src/splash_view.c`'s own top-of-file comment for why and how. By
  Xander Mol, the author of this project.
- **C64 character ROM dump** (`chargen-901225-01.bin`) -- used one-off,
  offline, to decode the `.petmate` PETSCII source above into an exact
  pixel bitmap (see `src/splash_view.c`'s own top-of-file comment); not
  shipped with or built into ArchiLudo itself. Original ROM copyright
  Commodore Business Machines; this dump found in a local VICE source
  checkout's test-suite data (`~/svn-mirror/vice/data/C64/`), not
  distributed by
  the Debian/Ubuntu `vice` package itself.

## Updating this file

Add an entry here whenever a new external tool, library, module, or
reference source gets used or adapted from -- alongside the inline credit
comment at the point of use, per the global code-attribution convention.
