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
[`OSLIB.md`](docs/OSLIB.md),
[`LIBARCHIE.md`](docs/LIBARCHIE.md); plus
[`riscos_wimp_reference.md`](riscos_wimp_reference.md) for the WIMP/SWI/
message API reference.

## Building from source

### Prerequisites

| Tool | Purpose | Install |
|---|---|---|
| [ArchieSDK](https://gitlab.com/_targz/archiesdk) | ARM2-targeting cross-compiler (GCC 8.5.0), bundled OSLib | `git clone https://gitlab.com/_targz/archiesdk.git ~/riscos-dev/archiesdk && cd ~/riscos-dev/archiesdk && ./build.sh` |
| [Arculator](https://arculator.hep.org.uk/) | Acorn Archimedes emulator, for testing | already installed at `D:\Retro\Acorn\Arculator_V2.2_Windows` |
| pandoc | optional: README.md -> README.pdf | `sudo apt install pandoc texlive-xetex` |

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
| `make` / `make all` | build `build/ArchiLudo,ff8` |
| `make test` | build and run the game-logic unit tests with the host compiler (no ArchieSDK/Arculator needed) |
| `make clean` | remove `build/` |
| `make deploy` | copy the built app to the Arculator hostfs folder |
| `make zip` | versioned release archive (`build/ArchiLudo-vX.Y.Z-<timestamp>.zip`), RISC OS filetypes preserved |
| `make docs` | regenerate `README.pdf` via pandoc |
| `make asm` | emit generated ARM assembly for the current sources |

Testing: boot `configs/ArchiLudo-ARM3-4MB.cfg` (matches real ARM3/4MB
hardware) or `configs/ArchiLudo-ARM2-1MB.cfg` (stock ARM2/1MB compatibility
check) in Arculator with the RISC OS 3.10 ROM, then run `ArchiLudo` from
HostFS via the Filer.
