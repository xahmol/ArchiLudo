# Graphics tooling manual

How ArchiLudo gets image assets from a modern PC onto RISC OS 3.10, and
why a bespoke tool was needed rather than an existing one.

## Why a custom tool

No suitable existing tool was found for PNG -> RISC OS old-style Sprite
conversion that runs on this PC/Linux side:

- RISC OS itself has a `ConvertPNG` module (OSLib binds it:
  `SDK/include/oslib/convertpng.h`) -- but it's a **native RISC OS SWI**,
  authored 2002 (per its header comment), long after RISC OS 3.10 (1992).
  PNG didn't exist yet in 1992/3, and this module isn't present on real
  RISC OS 3.10 -- it can't be used for this project's target OS even if
  it were runnable from the PC side, which it isn't anyway (it's a module,
  not a standalone host tool).
- No PC-native PNG->RISC-OS-sprite converter turned up in a search of the
  Acorn/RISC OS open-source ecosystem or kieranhj's various demo-tooling
  repos (their PNG/image conversion scripts target BBC Micro screen
  formats -- MODE 1/7 teletext/bitmap layouts -- not the RISC OS Sprite
  file format at all).

So: `tools/riscos_sprite.py`, a from-scratch CLI, built and self-tested
against the actual byte layout rather than against prose alone (see
"How the format was verified" below).

## The tool: `tools/riscos_sprite.py`

Requires Python 3 + Pillow (`pip install Pillow`).

```
tools/riscos_sprite.py info <spritefile>
tools/riscos_sprite.py to-png <spritefile> <sprite-name> <output.png>
tools/riscos_sprite.py from-png <input.png> <output-spritefile> --name NAME
                        [--bpp {1,2,4,8}] [--mode N] [--mask-alpha-threshold N]
tools/riscos_sprite.py pack <output-spritefile> <input-spritefile>...
```

- `info` / `to-png`: inspect and preview an existing sprite file (used to
  validate the reader against real sprite files -- see below).
- `from-png`: quantises the PNG to an adaptive palette (Pillow's
  median-cut, matching how real RISC OS sprite tools embed a bespoke
  palette per sprite rather than forcing one fixed scheme) at the given
  bit depth, and writes a single-sprite RISC OS sprite file. The alpha
  channel becomes the sprite's transparency mask (`--mask-alpha-threshold`,
  default 128; pass a negative number to omit the mask). `--bpp`
  defaults to 4 (16 colours, RISC OS mode 9 -- square-pixel game art depth,
  see below); pass `--bpp 8` for 256-colour full-screen game assets (mode 13).
- `pack`: concatenates several single-sprite files (as `from-png`
  produces) into one multi-sprite sprite area/file, matching how a real
  `!Sprites` file holds many named icons together.

## Sprite file format (RISC OS <=3.1 "old-style")

Source: RISC OS 3 PRM, Volume 1, Chapter 22 "Sprites"
(`~/riscos-dev/prm-mirror/sprites.html`).

**File header (12 bytes)**:

| Offset | Field |
|---|---|
| 0 | number of sprites |
| 4 | offset to first sprite |
| 8 | offset to first free byte |

**Gotcha, confirmed by hand** (this is exactly the kind of thing worth
recording rather than re-discovering): a saved sprite *file* is the
in-memory sprite-area layout with its leading 4-byte "total size" word
omitted -- but every offset inside the file is still expressed as if that
word were present. So `real_file_offset = stored_offset - 4`. The PRM
states this in prose ("the format of the file... is the same as a sprite
area, save that word 1 of the control block is not saved") but doesn't
spell out the off-by-4 consequence for the two offset fields — that part
was worked out and then verified byte-for-byte (see below).

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
| 44... | palette, if bpp <= 8: 2^bpp entries x 8 bytes each |

Each palette entry is two identical 4-byte words (the format
`OS_ReadPalette` returns), each `&BBGGRR00` (blue, green, red, then an
unused low byte) -- **not** `&00RRGGBB`; getting this backwards produces a
blue-tinted image, which is how the mistake first showed up while writing
`sprite_to_image()`.

Image/mask data is stored row-by-row, top to bottom, each row padded to a
whole number of 32-bit words. Within a word, the **least significant
bits are the left-most pixel** -- i.e. pixel 0 goes in bits `0..bpp-1` of
byte 0, pixel 1 in the next `bpp` bits, and so on.

**Old-style mode numbers actually used by this project** (PRM Volume 4
Chapter 95 "Table B: Modes", `~/riscos-dev/prm-mirror/modes.html`):

| bpp | Colours | Mode | OS units/pixel |
|---|---|---|---|
| 1 | 2   | 4  | 4x4 (square) |
| 2 | 4   | 1  | 4x4 (square) |
| 4 | 16  | 9  | 4x4 (square) |
| 8 | 256 | 13 | 4x4 (square) |

**Round 5 correction**: this table originally used modes 0/8/12/15
(640x256 pixels, 2x4 OS units/pixel -- pixels *twice as tall as wide*),
reasoning that mode 12 is the conventional WIMP-icon depth and mode 15 the
natural 256-colour full-screen choice. Both are true, but 2x4 pixels mean
ordinary square-pixel source art (any PNG drawn with normal square pixels)
renders visibly squashed regardless of what mode the *sprite itself* is
tagged with -- the distortion comes from the *screen* mode's own pixel
geometry, not a sprite/screen mode mismatch. Confirmed the hard way: pawn
placeholder sprites (circles in the source PNG) rendered as tall thin
"bottle" shapes in Arculator under mode 15. Modes 1/4/9/13 are the
320x256-pixel counterparts at the same four bit depths, sharing mode
12/15's 1280x1024 OS-unit desktop resolution but with genuinely square 4x4
OS-unit pixels -- switched to these throughout, and to `*Configure Mode
13` instead of 15 for the live screen mode (see `CLAUDE.md`'s Testing
section). Mode 12 remains correct for real WIMP icon sprites specifically
(confirmed against a real file, see below) since *icon* sprites are meant
to render 1:1 with the WIMP's own screen mode-relative scaling, not as
free-standing full-screen game art -- this project doesn't currently
generate any icon sprites, only board/pawn art, so mode 12 isn't actually
used by anything here despite being confirmed correct as a fact.

## How the format was verified

Rather than trust the PRM's prose alone for the trickier parts (the
offset-minus-4 convention, palette word byte order, pixel bit order),
this was checked against **real sprite files** extracted from QTM v1.49's
distribution zip (`!QTMmini/Sprites`, 14 real iconbar icon sprites, (c)
Steve Harrison -- see `docs/QTM.md` and `CREDITS.md`):

1. Parsed the real file's header/CB fields by hand in a Python REPL and
   cross-checked every offset against the file's actual size (7180 bytes)
   and the sprite CB's actual position (byte 12) -- this is what exposed
   the offset-minus-4 convention concretely, not just as PRM prose.
2. Read the real palette bytes and confirmed colours 0-7 are the
   documented linear white->black greyscale (`riscos_wimp_reference.md`'s
   Wimp/Colours section) -- confirming the byte-order fix above.
3. Ran `tools/riscos_sprite.py to-png` on that same real file and visually
   inspected the result (a coherent icon image, not a garbled/scrambled
   one) -- this is what confirmed the pixel bit-order.
4. Round-tripped a synthetic PNG through `from-png` -> `to-png` at both
   4bpp (with an alpha mask) and 8bpp (no mask), and visually confirmed
   both renders matched the source image exactly.

## Updating this file

Add a note here if a new mode/bpp is needed (update `MODES_BY_BPP` in the
tool too), or if any part of the format assumed here turns out wrong once
real assets are tested inside Arculator.
