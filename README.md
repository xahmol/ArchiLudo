# ArchiLudo
A Ludo game for Acorn Archimedes

## Contents
[Documentation](#documentation)
[Building from source](#building-from-source)

## Documentation

See [`docs/`](docs/) for the full project documentation:
[`ARCHITECTURE.md`](docs/ARCHITECTURE.md),
[`BUILDCHAIN.md`](docs/BUILDCHAIN.md),
[`GAME_LOGIC.md`](docs/GAME_LOGIC.md),
[`BOARD_LAYOUT.md`](docs/BOARD_LAYOUT.md),
[`OSLIB.md`](docs/OSLIB.md),
[`LIBARCHIE.md`](docs/LIBARCHIE.md),
[`GRAPHICS_TOOLING.md`](docs/GRAPHICS_TOOLING.md); plus
[`riscos_wimp_reference.md`](riscos_wimp_reference.md) for the WIMP/SWI/
message API reference, and [`CREDITS.md`](CREDITS.md) for everything this
project is built on or derived from.

## Building from source

### Prerequisites

| Tool | Purpose | Install |
|---|---|---|
| [ArchieSDK](https://gitlab.com/_targz/archiesdk) | ARM2-targeting cross-compiler (GCC 8.5.0), bundled OSLib | `git clone https://gitlab.com/_targz/archiesdk.git ~/riscos-dev/archiesdk && cd ~/riscos-dev/archiesdk && ./build.sh` |
| [Arculator](https://arculator.hep.org.uk/) | Acorn Archimedes emulator, for testing | already installed at `D:\Retro\Acorn\Arculator_V2.2_Windows` |
| pandoc | optional: README.md -> README.pdf | `sudo apt install pandoc texlive-xetex` |
| Python 3 + Pillow | `tools/riscos_sprite.py`, the PNG->Sprite converter | `pip install Pillow` |

### .env setup

Copy `.env.example` to `.env` and set:

```
ARCHIESDK = /home/xahmol/riscos-dev/archiesdk
ARCULATOR_HOSTFS = /mnt/d/Retro/Acorn/Arculator_V2.2_Windows/hostfs
```

`.env` is gitignored — never commit it.

### Make targets

| Target | Effect |
|---|---|
| `make` / `make all` | build the `build/!ArchiLudo` application directory |
| `make test` | build and run the game-logic unit tests with the host compiler (no ArchieSDK/Arculator needed) |
| `make clean` | remove `build/` |
| `make deploy` | copy `build/!ArchiLudo` to the Arculator hostfs folder |
| `make zip` | versioned release archive (`build/ArchiLudo-vX.Y.Z-<timestamp>.zip`), RISC OS filetypes preserved |
| `make assets` | regenerate the pawn sprites and app icon from their Python generators |
| `make docs` | regenerate `README.pdf` via pandoc |
| `make asm` | emit generated ARM assembly for the current sources |

Testing: boot `configs/ArchiLudo-ARM3-4MB.cfg` (matches real ARM3/4MB
hardware) or `configs/ArchiLudo-ARM2-1MB.cfg` (stock ARM2/1MB compatibility
check) in Arculator with the RISC OS 3.10 ROM, then double-click `!ArchiLudo`
from HostFS via the Filer (see `docs/BUILDCHAIN.md`'s "Application
directory" section for its structure).

**Status**: Phase 1 (see `docs/ARCHITECTURE.md`'s Roadmap) -- a playable
core loop, extensively live-tested and refined in Arculator across many
rounds of feedback (see `docs/ARCHITECTURE.md`'s round history).
