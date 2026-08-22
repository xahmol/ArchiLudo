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
build/ArchiLudo.elf --(arm-archie-objcopy -O binary)-->  build/ArchiLudo,ff8
```

The `,ff8` suffix is the standard convention for representing a RISC OS
filetype (here, `&FF8` = Absolute, i.e. a directly-runnable executable) on
a non-RISC-OS filesystem -- see `riscos_wimp_reference.md`'s "Filetypes"
section. `arm-archie-objcopy -O binary` strips the ELF wrapper down to the
raw loadable image RISC OS expects.

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
| `make deploy` | `check-hostfs` (verifies `$(ARCULATOR_HOSTFS)` exists) then copies `ArchiLudo,ff8` there |
| `make zip` | versioned release archive via `$(ARCHIEZIP)` (`arm-archie-zip`), which preserves RISC OS filetypes on extraction -- a plain host `zip` would not |
| `make asm` | emits generated ARM assembly (`arm-archie-gcc -S`) for inspection |
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

## Filetype/packaging conventions

See `riscos_wimp_reference.md`'s "Filetypes / packaging" section for the
full table and the eventual `!ArchiLudo` application-directory structure
(`!Run`/`!Boot`/`!Sprites`) once double-click launching matters -- the
current build only produces a bare runnable `ArchiLudo,ff8`.

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
