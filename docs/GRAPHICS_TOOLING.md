# Graphics tooling manual

How ArchiLudo gets image assets from a modern PC onto RISC OS 3.10, and
how the game actually renders its board, pawns, and dice today.

## Contents

- [Why a custom tool](#why-a-custom-tool)
- [The tool: `tools/riscos_sprite.py`](#the-tool-toolsriscos_spritepy)
- [Sprite file format (RISC OS <=3.1 "old-style")](#sprite-file-format-risc-os-31-old-style)
- [Current rendering approach](#current-rendering-approach)
- [How the format was verified](#how-the-format-was-verified)
- [Lasting gotchas](#lasting-gotchas)
- [Editing sprites by hand](#editing-sprites-by-hand)
- [Approaches tried and not used](#approaches-tried-and-not-used)

## Why a custom tool

No suitable existing tool was found for PNG -> RISC OS old-style Sprite
conversion that runs on this PC/Linux side:

- RISC OS itself has a `ConvertPNG` module (OSLib binds it:
  `SDK/include/oslib/convertpng.h`) -- but it's a **native RISC OS SWI**,
  authored 2002, long after RISC OS 3.10 (1992). PNG didn't exist yet in
  1992/3, and this module isn't present on real RISC OS 3.10 -- it can't
  be used for this project's target OS even if it were runnable from the
  PC side, which it isn't anyway (it's a module, not a standalone host
  tool).
- No PC-native PNG->RISC-OS-sprite converter turned up in a search of the
  Acorn/RISC OS open-source ecosystem.

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
                        [--wimp-palette]
tools/riscos_sprite.py pack <output-spritefile> <input-spritefile>...
```

- `info` / `to-png`: inspect and preview an existing sprite file.
- `from-png`: quantises the PNG to a palette at the given bit depth and
  writes a single-sprite RISC OS sprite file. The alpha channel becomes
  the sprite's transparency mask (`--mask-alpha-threshold`, default
  128; pass a negative number to omit the mask). `--bpp` defaults to 4
  (16 colours); pass `--bpp 8` for 256-colour assets. `--mode` overrides
  the bpp-implied default mode (`MODES_BY_BPP`) -- used to tag sprites
  with mode 27 specifically (see "Current rendering approach" below).
  `--wimp-palette` quantises against the fixed 16 standard Wimp colours
  instead of an adaptive per-sprite palette -- required for any sprite
  meant to be plotted via `Wimp_PlotIcon` as an indirected icon, since
  the Wimp auto-translates a 1/2/4bpp indirected sprite's colour indices
  onto those fixed 16 regardless of what the sprite's own embedded
  palette says.
- `pack`: concatenates several single-sprite files into one multi-sprite
  sprite area/file, matching how a real `!Sprites` file holds many named
  icons together.

## Sprite file format (RISC OS <=3.1 "old-style")

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

## Current rendering approach

**Pawns and dice are real sprites**, plotted via `Wimp_PlotIcon`
(`assets/PawnSprite`) -- not `os_plot` primitives, and not a bare
`osspriteop` call. Source art for these is drawn **square** and tagged
mode 27 (`--mode 27`), rather than pre-squished for one specific
non-square mode. `Wimp_PlotIcon`'s automatic `PutSpriteScaled` computes
its scale factors from a sprite's own declared mode against whatever
mode is actually live, so one correctly-tagged square sprite displays
correctly under all four of ArchiLudo's supported modes (12/15/27/39)
without needing per-mode art variants -- this is why mode 27 is the
right choice for any *new* hand-drawn sprite in this project, not the
non-square 12/15 family.

**Board cell fills and the ring-entry start markers stay `os_plot`
primitives** -- circles for cell fills, a filled circle plus a white
`os_PLOT_TRIANGLE` arrow (direction computed per player) for start
markers -- drawn directly onto the window background, not sprites.

**The app's own persistent Filer/iconbar icon** follows a different,
narrower convention than in-game sprites: `!Sprites` (old-style mode
15) and `!Sprites22` (old-style mode 21 or 27 depending on the asset,
exactly double linear resolution of the base to compensate for the
aspect difference) -- **not** `!Sprites11`, which uses new-style
sprite mode encoding (RISC OS Select-era, well past 3.10, and not
understood by genuine RISC OS 3.10). This convention applies only to
the app's own icon, not to any in-game runtime-plotted sprite, which
already auto-scales via `Wimp_PlotIcon` with no stored variants needed.

**A manually filled rectangle border needs >=4 OS units of thickness on
mode 15's Y axis to reliably render at all** -- thinner can land
entirely between two pixel-centre sample points and paint nothing. This
bound already covers the largest of ArchiLudo's four supported modes'
pixel spacings, so it stays safe on mode 27's smaller 2-unit spacing
too. See `riscos_wimp_reference.md`'s "Screen modes: non-square pixels
and thin manually-drawn lines" section.

## How the format was verified

Rather than trust the PRM's prose alone for the trickier parts (the
offset-minus-4 convention, palette word byte order, pixel bit order,
variable palette size, and old-style mode numbers), this was checked
against **real sprite files**:

1. QTM v1.49's own distribution zip (`!QTMmini/Sprites`, 14 real
   iconbar icon sprites) -- parsed by hand in a Python REPL and
   cross-checked every offset against the file's actual size, which is
   what exposed the offset-minus-4 convention concretely; confirmed
   colours 0-7 are the documented linear white->black greyscale
   (confirming the palette byte-order fix); rendered via `to-png` and
   visually inspected (confirming pixel bit order); round-tripped a
   synthetic PNG through `from-png` -> `to-png` at both 4bpp (with an
   alpha mask) and 8bpp (no mask) with an exact visual match.
2. Steve Fryatt's own `wimp-prog` tutorial example downloads
   (`WindowSpriteArea.zip`, `AppSprite.zip`, `ShapeChooser.zip`,
   `SpriteIcon.zip`, `TextAndSpriteIcon.zip`) and ro-chess's real,
   shipped `!Chess/Sprites,ff9` -- every sprite in all five zips and
   the real ro-chess file parses without error; decoded and visually
   inspected several non-trivial ones (multi-colour palettes, text,
   masked chess-piece artwork), all correct; round-tripped an 8bpp and
   a 4bpp sprite with **zero pixel differences**; specifically checked
   non-word-aligned widths for row-boundary artifacts -- none found.
   This pass is what found the variable-palette-size and missing-mode
   issues already folded into the format description above.

**Permanent regression test**: `tools/test_riscos_sprite.py`
(self-contained, host Python + Pillow, no external files needed)
reproduces both bug patterns above with small hand-built synthetic
sprite files, plus the non-word-aligned round-trip check. Run with
`python3 tools/test_riscos_sprite.py`.

## Lasting gotchas

- **Dither/texture patterns must key on `y // 2`, not `y`**, for any
  pixel art meant to look right on both square (mode 27) and non-square
  (2x4, modes 12/15/39) screens -- RISC OS drops every other source row
  when scaling between a sprite's tagged mode and a coarser live mode,
  so a texture alternating per raw row can lose half its pattern
  silently on the non-square modes. A checkerboard (50/50) dither for
  pawn highlight regions read as solid diagonal stripes rather than a
  blend at small icon sizes for a related reason -- see "Approaches
  tried and not used" below.
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

## Editing sprites by hand

`make export-sprites` / `make import-sprites` (`assets/edit/`) round-
trip the shipped sprites to plain PNGs and back, at native resolution
only. Recommended editors: Piskel or GIMP with alpha support --
explicitly **not MS Paint**, which silently flattens alpha to opaque
and fills the transparent background solid white.

## Approaches tried and not used

- **Pre-squished-canvas source art for one specific non-square mode**
  (e.g. drawing at half height for mode 15, relying on that mode's own
  2x/4x stretch) -- superseded by drawing square and tagging mode 27,
  which lets `Wimp_PlotIcon` handle aspect compensation for every
  supported mode automatically instead of committing to one.
- **A checkerboard (50/50) dither** for pawn highlight regions --
  replaced with a sparser staggered dot grid for highlights
  specifically, after the checkerboard read as solid diagonal stripes
  rather than a blend at small icon sizes. The shadow region keeps the
  full checkerboard, which was never reported as a problem there.
- **A 16x-upscaled copy for hand-editing** -- dropped as a footgun
  (edits to the native-resolution file were silently discarded in
  favour of a stale upscaled copy). Editing is native-resolution only.
- **Monochrome/high-contrast pawn rendering** for 1-bit-per-pixel
  displays (pattern-filled pawns, matching the original GEOS edition's
  own `monochromeflag` convention) -- proposed, not implemented,
  deferred indefinitely.
- **Reusing the original GEOS edition's own board-entry-marker
  bitmaps** -- replaced with programmatically drawn circle+arrow
  markers instead.
