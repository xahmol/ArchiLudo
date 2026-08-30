# Compiler / build chain manual

Everything about ArchiLudo's cross-compilation toolchain, Makefile, and
deployment pipeline. See [ARCHITECTURE.md](ARCHITECTURE.md) for the
source-code layering this build chain serves, and [OSLIB.md](OSLIB.md) /
[LIBARCHIE.md](LIBARCHIE.md) for the libraries it links against.

## Why ArchieSDK, not mainline GCCSDK

Mainline GCCSDK (the toolchain behind Steve Fryatt's guides and
ro-oslib.sourceforge.io) only emits **32-bit APCS** binaries. That
physically cannot run on ARM2/ARM3 or genuine 26-bit RISC OS -- only on
later 32-bit-capable RISC OS (RISC OS 5 / Iyonix and up). Since ArchiLudo
targets the maintainer's real upgraded Archimedes (A305, ARM3, RISC OS
3.10) and stock ARM2 machines, that toolchain is a non-starter here.

**ArchieSDK** (`https://gitlab.com/_targz/archiesdk`) exists specifically
to close that gap: GCC 8.5.0 -- the last GCC release with ARM2 support --
plus its own lightweight libc, bundled OSLib, and its own
binutils/objcopy/zip tooling, all confirmed (see below) to target ARM2/26-bit
by default.

## Installation

```
git clone https://gitlab.com/_targz/archiesdk.git ~/riscos-dev/archiesdk
cd ~/riscos-dev/archiesdk
./build.sh
```

`build.sh` needs `wget tar make gcc g++` and compiles GCC 8.5.0 + binutils
2.42 + OSLib + Info-ZIP from source into `tools/`, `build/`, and `SDK/`
under the ArchieSDK checkout. This takes a while (a full GCC bootstrap).

**Known gotcha hit during this project's setup**: if the host has a
`/usr/local/bin/makeinfo` from a separately-installed newer Texinfo whose
compiled Perl XS module doesn't match the system's `perl` (symptom:
`MiscXS.c: loadable library and perl binaries are mismatched`), binutils'
`doc/bfd.info` target fails and aborts the whole build. Nothing in the
build actually needs the generated `.info` docs. Fix without touching
system Perl/Texinfo:

```
PATH="/usr/bin:/bin:$PATH" MAKEINFO=/usr/bin/makeinfo ./build.sh
```

(forces the distro's own working `makeinfo` ahead of the broken one on
`PATH`, and also overrides the `MAKEINFO` variable binutils' `configure`
picks up directly).

## Confirmed toolchain defaults

Verified directly against the built toolchain for this project (don't
just trust `config.mk` comments -- check `arm-archie-gcc -v` output):

```
$ arm-archie-gcc -v -c t.c -o t.o
...
COLLECT_GCC_OPTIONS='-v' '-c' '-o' 't.o' '-mcpu=arm2' '-mfloat-abi=soft' '-marm' '-march=armv2'
```

So **the default target is genuinely ARM2** (`-mcpu=arm2 -march=armv2
-mfloat-abi=soft`, no hardware FPU) -- no `-mcpu`/`-march` flags are
needed in the project Makefile, and a single build satisfies both the
maintainer's real ARM3 A305 and stock ARM2 A305/A310 hardware, since ARM3
is instruction-compatible with ARM2-targeted code.

Default include search path (`arm-archie-gcc -E -Wp,-v -`):

```
tools/lib/gcc/arm-archie/8.5.0/include
tools/lib/gcc/arm-archie/8.5.0/include-fixed
SDK/include            <-- OSLib, libarchie, and the ArchieSDK libc headers live here
```

Available libraries (`SDK/lib/`): `libc.a`, `libm.a`, `libarchie.a`,
`libOSLib32.a`, plus `crt0.o`/`crtheap.o`/`crti.o`/`crtn.o` (startup code,
linked automatically).

## Toolchain binaries (`config.mk`)

`~/riscos-dev/archiesdk/config.mk` (included by the project Makefile,
see below) defines:

```makefile
ARCHIECC      = $(ARCHIESDK)/tools/bin/arm-archie-gcc
ARCHIEAS      = $(ARCHIESDK)/tools/bin/arm-archie-as
ARCHIEAR      = $(ARCHIESDK)/tools/bin/arm-archie-ar
ARCHIEOBJCOPY = $(ARCHIESDK)/tools/bin/arm-archie-objcopy
ARCHIEZIP     = $(ARCHIESDK)/tools/bin/arm-archie-zip

CFLAGS = -mno-thumb-interwork -Wdouble-promotion -Wfloat-conversion -Wall -Wextra
```

`-mno-thumb-interwork` must not be removed (per ArchieSDK's own comment).
`-Wdouble-promotion`/`-Wfloat-conversion` are recommended because of the
libc's float-only math (see below) -- an accidental `double` is a real
performance cost on ARM2/3 with no hardware FPU. `config.mk` **sets**
`CFLAGS` (doesn't append), so the project Makefile always uses `CFLAGS +=`
after `include`-ing it, never `CFLAGS =`.

## The project Makefile

Two independent build paths share the same source tree:

### RISC OS build (`make` / `make all`)

```
src/*.c  --(arm-archie-gcc, one .o per source, -MMD -MP dependency tracking)-->  build/*.o
build/*.o --(arm-archie-gcc link, -lOSLib32)-->  build/ArchiLudo.elf
build/ArchiLudo.elf --(arm-archie-objcopy -O binary)-->  build/!ArchiLudo/!RunImage,ff8
```

`arm-archie-objcopy -O binary` strips the ELF wrapper down to the raw
loadable image RISC OS expects. The output lands in a real application
directory, `build/!ArchiLudo/` -- see "Application directory" below for
the full structure. The `,ff8`/`,feb`/`,ff9` suffixes are the standard convention
for representing a RISC OS filetype (`&FF8` = Absolute executable,
`&FEB` = Obey, `&FF9` = Sprite) on a non-RISC-OS filesystem -- see
`riscos_wimp_reference.md`'s "Filetypes" section.

Requires `ARCHIESDK` (from `.env`) to be set; the Makefile only enforces
this for goals that actually need the ARM toolchain (`all`, `deploy`,
`zip`, `asm` -- anything except `test`/`clean`), via:

```makefile
GOALS := $(or $(MAKECMDGOALS),all)
NEEDS_ARCHIESDK := $(filter-out test clean,$(GOALS))
ifneq ($(NEEDS_ARCHIESDK),)
  ... require ARCHIESDK, include $(ARCHIESDK)/config.mk ...
endif
```

### Host test build (`make test`)

```
tests/test_game_logic.c + src/game_logic.c --(host $(HOSTCC), plain -std=c99)--> build/test_game_logic
```

Deliberately bypasses ArchieSDK entirely -- no `ARCHIESDK`/`.env` needed,
no cross-compiler, no emulator. This is the whole point of keeping
`game_logic.c` free of OSLib/WIMP includes: see
[ARCHITECTURE.md](ARCHITECTURE.md) and [GAME_LOGIC.md](GAME_LOGIC.md).

### Other targets

| Target | Effect |
|---|---|
| `make deploy` | `check-hostfs` (verifies `$(ARCULATOR_HOSTFS)` exists) then copies the whole `build/!ArchiLudo/` directory there (contents merged into an already-existing `hostfs/!ArchiLudo/` via `cp -r SRC/. DEST/`, not nested a level deeper on repeat deploys -- the classic `cp -r` gotcha), and removes any legacy flat `ArchiLudo,ff8`/`PawnSprite,ff9`/`Sprites,ff9` left over in hostfs from an older application-directory-less deploy |
| `make deploy-pibridge` | deploys to a real-hardware target -- a PiEconetBridge (Econet-over-IP bridge on a Raspberry Pi) at `PIBRIDGE_USER@PIBRIDGE_HOST:PIBRIDGE_PATH`, all four connection details (including `PIBRIDGE_PASS`) from `.env`. Password auth via `sshpass` (matching how the user already connects with FileZilla over SFTP -- needs `sudo apt install sshpass`), not SSH keys; `check-pibridge` checks `sshpass` is installed and the Pi is reachable first. Before rsyncing, `tools/prepare_pibridge_deploy.py` converts `$(APPDIR)`'s `,xxx`-suffixed files into PiFS's own expected format (plain filenames + a `.inf` sidecar per file carrying filetype/date as a RISC OS "stamped" load/exec address) into `build/pibridge-stage/` -- PiFS does NOT understand the `,xxx` convention Arculator's hostfs uses (confirmed by reading PiFS's own source; see that script's own doc comment for the full format detail). The deploy itself is `rsync -av --delete` over SSH (via `sshpass`) of that staged directory, rather than `deploy`'s local `cp` of `$(APPDIR)` directly -- a genuinely separate/independent target from the Arculator emulator deploy above, not a replacement for it |
| `make zip` | versioned release archive via `$(ARCHIEZIP)` (`arm-archie-zip`) with the `-,` flag (required for RISC OS filetype preservation -- a plain host `zip` preserves nothing regardless), bundling `README.pdf` and a plain-text, LF-line-ended `ReadMe,fff` (via `tools/riscos_readme.py`) instead of the raw Markdown. `make zip`/`make disk` keep the full version+timestamp in their output filenames so multiple builds can be told apart in `build/` -- before copying either onto real classic-Econet hardware (a PiEconetBridge or similar old-style fileserver), rename it to 10 characters or fewer with no dot, since such fileservers silently truncate longer names in a way that makes the file unreadable (Arculator's hostfs and a plain download/extract elsewhere have no such limit) |
| `make disk` | an ADFS "D" format (800KB) disc image (`build/ArchiLudo-vX.Y.Z-<timestamp>.adf`) containing just `make zip`'s output, filetyped `&A91` (Zip). Written from scratch by `tools/build_adfs_disk.py` -- no third-party disc-image tool -- ground-truthed against Gerald Holdsworth's DiscImageManager source (GPL-3.0) and independently verified (see that script's own doc comment) |
| `make asm` | emits generated ARM assembly (`arm-archie-gcc -S`) for inspection |
| `make assets` | regenerates `assets/PawnSprite` and `assets/!Sprites`/`!Sprites22` (the app icon) from their Python generators -- see "Application directory" below |
| `make docs` | regenerates `README.pdf` via `pandoc` (warns and skips if pandoc isn't installed, never fails the build) |
| `make clean` | removes `build/` entirely |

### Automatic dependency tracking

Unlike the Oscar64 projects in this same author's other repos (which
hand-list every source file because Oscar64's `#pragma compile` chains are
invisible to `make`), `arm-archie-gcc` is a real, standard GCC. ArchiLudo
uses ordinary `-MMD -MP` dependency generation instead:

```makefile
SRCFILES = $(wildcard src/*.c)
OBJFILES = $(patsubst src/%.c,build/%.o,$(SRCFILES))
DEPFILES = $(OBJFILES:.o=.d)
...
-include $(DEPFILES)
```

Each `build/*.o` gets a matching `build/*.d` listing the headers it
actually included; touching a header correctly triggers exactly the
`.c` files that use it to recompile, with no manually-maintained list to
keep in sync as the game grows past a single `main.c`.

## `.env` / deployment path

`ARCHIECOPYPATH` in ArchieSDK's own `config.mk` is a built-in
copy-to-hostfs step, but ArchiLudo keeps that concern out of the shared
toolchain config and in the project's own `.env` instead (mirroring how
`ULTIP1`/`ULTHOST` device IPs are handled in this author's other
cross-compiler projects) -- see `ARCULATOR_HOSTFS` in `.env.example`.
`check-hostfs` verifies the path exists (guards against the Windows drive
not being mounted in WSL) before `deploy` copies anything there.
`PIBRIDGE_USER`/`PIBRIDGE_HOST`/`PIBRIDGE_PASS`/`PIBRIDGE_PATH` follow
the same `.env` convention for `make deploy-pibridge`'s real-hardware
target -- all four in `.env` rather than a Makefile default (per
explicit user request), since password auth means there's no sensible
non-secret default to fall back to anyway.

## Filetype/packaging conventions

See `riscos_wimp_reference.md`'s "Filetypes / packaging" section for the
full filetype table. `Application directory` below covers ArchiLudo's own
structure.

## Application directory

Following Steve Fryatt's wimp-prog
tutorial, Chapter 17 ("Creating an Application Directory",
<https://www.stevefryatt.org.uk/risc-os/wimp-prog/creating-an-application-directory>,
local mirror at
`~/riscos-dev/wimp-prog-mirror/wimp-prog/creating-an-application-directory.html`).
`make all` now assembles a real `!ArchiLudo` application directory in
`build/`, not a bare runnable file:

```
build/!ArchiLudo/
  !RunImage,ff8    -- the compiled binary (objcopy output, see above)
  !Run,feb          -- Obey file the Filer executes on double-click
                       (checked into the repo as app/!Run, no comma
                       suffix -- the Makefile adds it when copying)
  !Sprites,ff9       -- iconbar/Filer icon, rectangular-pixel (90x45dpi,
                        mode 12) for this project's own non-square screen
                        modes (12/15/39)
  !Sprites22,ff9      -- the same icon, square-pixel (90x90dpi, mode 27)
                         for mode 27
  PawnSprite,ff9       -- pawn sprite pool, found via resource_path()'s
                           argv0-derived app_dir (src/game_view.c),
                           which truncates argv0 at the last "."
                           separator to get "HostFS:$.!ArchiLudo"
                           regardless of whether the program was
                           invoked as a bare file or as an app
                           directory's own !RunImage
```

**Deliberately does NOT include** the tutorial's own `*RMEnsure` block for
`CallASWI`/`FPEmulator`/`SharedCLibrary` -- those guard against the Acorn
DDE's runtime dependencies (the shared C library / floating-point
emulator modules a DDE-compiled program branches directly into via a
registration handshake), which don't apply to ArchieSDK: it links its own
self-contained libc, confirmed against a real ArchieSDK demo's own
`!Run` file (`examples/bydctc/data/!Run,feb` in the ArchieSDK checkout),
which has no such lines either. `app/!Run`'s own top-of-file comment has
the full reasoning. The `RMEnsure UtilityModule 3.10` version check *is*
kept -- that's a genuine requirement of this project's real hardware
target (RISC OS 3.10), not a DDE toolchain artefact.

**Icon design**: a red pawn beside a die (`assets/generate_app_icon.py`,
`make assets` to regenerate). Drawn once at a square `WORK=320`
supersample canvas (same anti-aliasing technique as
`generate_icon_sprites.py`'s pawn art -- solid masks, RGB/alpha resized
separately to avoid a transparent-edge colour-bleed artifact), bold and
simplified (no dither/shading detail, which would be lost at these
sizes anyway) since the final sizes are tiny: 34x34/17x17 for the
square-pixel `!Sprites22`, 34x17/17x9 for the rectangular-pixel
`!Sprites` (Fryatt's Table 17.1's standard "full size"/"half size"
dimensions). The rectangular-pixel version is generated by squishing the
same WORK canvas 2:1 vertically before downsampling, so mode 12's own
2x4-OS-units/pixel stretch brings it back to the right proportions on
screen rather than looking squashed. Packed at 4bpp against the fixed
Wimp palette (`--wimp-palette`, mode 12 for `!Sprites`, mode 27 for
`!Sprites22` -- `tools/riscos_sprite.py`'s `MODES_BY_BPP[4]` gives 12 as
the non-square 4bpp mode matching mode 15's own aspect), matching every
other icon-plotted sprite in this project.

**Not done**: formal resource allocation (Fryatt's tutorial, "A note
about allocation" -- registering the `ArchiLudo` name/sprite/system-
variable prefix with RISC OS Open's central database) -- the tutorial
itself frames this as essential only "before sending an application to
anyone else," which doesn't apply yet for this personal/hobby project.
Revisit if ArchiLudo is ever distributed more widely.

## Known ArchieSDK libc quirks

- Math functions are **float-only internally**: `double cos(double x)`
  just calls `float cosf(float x)` -- there is currently no true
  double-precision path. Shouldn't matter for Ludo's dice/board integer
  logic.
- `printf` has no `%e`/`%E` (scientific notation) or size specifiers yet;
  all integer arguments are treated as `uint32_t`.
- `-Wdouble-promotion`/`-Wfloat-conversion` (already in `config.mk`'s
  default `CFLAGS`) catch accidental `float`->`double` promotion, which is
  expensive with no hardware FPU on ARM2/3 -- don't remove these warnings.
