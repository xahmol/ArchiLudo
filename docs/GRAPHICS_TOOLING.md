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
  defaults to 4 (16 colours, RISC OS mode 12 -- the conventional WIMP icon
  depth, and this project's game art depth too, see below); pass `--bpp 8`
  for 256-colour full-screen game assets (mode 15).
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
| 1 | 2   | 0  | 2x4 (pixels twice as tall as wide) |
| 2 | 4   | 8  | 2x4 |
| 4 | 16  | 12 | 2x4 |
| 8 | 256 | 15 | 2x4 |

Mode 12 is what real WIMP icon sprites use (confirmed against a real
file, see below); mode 15 is ArchiLudo's target screen mode -- both share
the same non-square 2x4 OS-unit-per-pixel geometry, so a sprite tagged
with the mode matching its bpp always agrees with the live screen mode.

**Round 5 attempt (reverted in round 6)**: briefly switched this whole
table to modes 1/4/9/13 (320x256 pixels at the same 1280x1024 OS units,
but genuinely square 4x4 OS-unit pixels) plus a `*Configure Mode 13`
screen-mode change, reasoning that square pixels would sidestep the
distortion entirely. Mode 13 turned out not to be selectable under the
user's actual Arculator monitor-type setup, and mode 15 is simply the
normal RISC OS desktop mode regardless -- so this was reverted back to
mode 15/12 with the pixel-aspect compensation moved to the **source art**
instead (see "Round 6 correction" below), which is the approach this
project actually uses now.

**Round 6 correction**: mode 15 (and mode 12, the conventional icon depth)
being 2x4 OS units/pixel means ordinary square-pixel source art (any PNG
drawn with normal square pixels) renders visibly squashed regardless of
what mode the *sprite itself* is tagged with -- confirmed the hard way:
ArchiLudo's pawn placeholder sprites (circles in the source PNG) rendered
as tall thin "bottle" shapes in Arculator. Since window/icon layout and
all custom `os_plot` drawing (board cell fills, etc.) already work in OS
units directly -- mode-independent by construction -- only **sprites**
need compensating, because they store raw pixel data. Fixed at the source:
`assets/generate_placeholder_art.py`'s `MODE15_OS_UNITS_PER_PIXEL = (2, 4)`
drives the drawing canvas size, pre-squished by the *inverse* of that
ratio (half as many rows as columns) so mode 15's own 2x/4x stretch brings
the final on-screen shape back to the intended square/circle. A 40x40
OS-unit pawn is therefore drawn as a 20x10-raw-pixel PNG, not 40x40 --
confirmed correct by round-tripping the packed sprite back to PNG and
resizing it 2x horizontally / 4x vertically to simulate the on-screen
result before ever loading it into Arculator.

## Round 6: reusing GeoLudo's own art

Rather than keep hand-drawing placeholder shapes, Phase 1's round 6
switched to reusing this game's own prior GEOS port's artwork -- the
user's explicit call, and a better placeholder source anyway: GeoLudo's
pawn and board-entry-marker bitmaps are actual game art (a recognisable
chess-pawn silhouette, direction-arrow entry markers), not generic
programmer shapes.

**Source format**: `/home/xahmol/git/ludo/GEOS/assets/*.gbm` files are
XML (`<GeosBitmap>`, `<Width>` in bytes, `<Height>` in lines, `<Data>`
base64-encoded 1-bit-per-pixel raster, MSB = leftmost pixel, bit=1 =
"ink") -- a different bitmap editor's export format from this project's
own old-style Sprite format, decoded by
`assets/generate_placeholder_art.py`'s `decode_gbm()`. Confirmed correct
by rendering `bm_pawn.gbm` this way and comparing it against the actual
in-game pawn shape visible in
`/home/xahmol/git/ludo/GEOS/screenshots/ludo-game-c64.png` -- a clear
chess-pawn silhouette in both.

**What was reused, and how it's actually drawn in GEOS** (established by
reading `GEOS/src/main.c`'s `drawfield()`/`pawnprint()`/`pawnplace()` and
cropping the reference screenshot, not by guessing from the bitmap names
alone):

- `bm_pawn.gbm` -- the detailed pawn silhouette. In GEOS this is only
  ever shown recoloured via `ColorRectangle` (no separate outline
  colour), and turns out to be used **only for the home base display** --
  confirmed by cropping a track dot from the reference screenshot: it's a
  plain filled circle, not the pawn shape. `game_view.c`'s `plot_pawn()`
  mirrors this exactly: the detailed sprite for a pawn still `!in_play`
  (home base), a plain `os_plot` filled circle in the player's full
  colour once released.
- `bm_gstart.gbm`/`bm_rstart.gbm`/`bm_bstart.gbm`/`bm_ystart.gbm` -- the
  four players' board-entry direction-arrow markers (named for the player
  colour whose entry point they mark, matching this project's player
  order exactly). Reused for `CELL_RING_ENTRY` cells in
  `game_view.c`'s `build_cell_kinds()`/`plot_start_marker()`.
- **Not reused this round** (deliberately, to bound scope): `dice1..6.gbm`
  (individual die-face icons -- ArchiLudo's Throw button stays plain
  text for now) and `iconThrow.gbm`/`icobNext.gbm` (GEOS button graphics
  with baked-in bitmap text -- unnecessary since RISC OS Wimp icons
  render real system-font text natively, more cleanly than a baked-in
  bitmap would). Worth reconsidering for Phase 2's real art pass.

**Local copies, not a live path into the sibling repo**: the specific
`.gbm` files used are copied into `assets/geos_source/` (git-tracked) so
`generate_placeholder_art.py` doesn't depend on `/home/xahmol/git/ludo`
remaining checked out at generation time -- consistent with how
`board_layout.c`'s `fieldcoords`/`homedestcoords` conversion is a
one-time port, not a live reference.

**Recolouring + resizing**: `decode_gbm()` gives a 16x16 monochrome mask;
`recolour_and_squish()` resizes it (Pillow LANCZOS, for reasonably smooth
edges before the sprite format's binary mask threshold) directly to the
mode-15-pre-squished target canvas (see the "Round 6 correction" section
above) and flood-fills the masked shape with the player's flat RGB colour
-- one resize does both the "make it the right on-screen size" and "make
it round-trip mode 15's non-square pixels correctly" jobs at once.

## Round 6.1: board-entry markers drawn programmatically instead

The `bm_gstart`/`rstart`/`bstart`/`ystart` sprites described above
rendered far too narrow in Arculator -- reported directly from a
screenshot, not a guess. This is puzzling given every offline check
looked correct *before* it ever reached Arculator: `tools/riscos_sprite.py
info assets/Sprites` reported the right dimensions (16x8, matching the
intended pre-squish maths) with no signs of pack-order corruption, and a
locally-simulated stretch (resizing the packed-then-decoded PNG 2x
horizontally / 4x vertically, the same transform mode 15 itself performs)
rendered a correctly-proportioned ring-with-arrow shape. Whatever the
actual mechanism is (something specific to `xosspriteop_put_sprite_user_coords`
scaling very small sprites, perhaps, or something Arculator-specific --
not chased down further), the pragmatic fix was to stop depending on
sprite plotting for this element entirely: `plot_start_marker()` in
`src/game_view.c` now draws a filled circle (same `MARKER_RADIUS` as
every other board marker, via the same `os_plot` calls) with a white
`os_PLOT_TRIANGLE` arrow on top, computed directly from each player's
board-entry travel direction (green +col/right, red +row/down, blue
-col/left, yellow -row/up -- read off `board_layout.c`'s ring travel
order). This sidesteps the whole sprite-scaling question, guarantees an
exactly-correct size regardless of screen mode, and directly matches what
was asked for ("should look like a normal round but filled in the
corresponding color and an arrow in it"). The `start0-3` sprites/PNGs and
`generate_placeholder_art.py`'s code to build them were removed;
`assets/geos_source/bm_{g,r,b,y}start.gbm` are left in place in case a
real Phase 2 art pass wants to revisit reusing them (e.g. as a proper
bitmap once the sprite question is actually understood, or as a visual
reference for hand-drawn Phase 2 art).

## Round 6.2: on-track pawns also use the detailed sprite

Round 6's `plot_pawn()` drew the detailed pawn sprite only for a pawn
still in its home base, falling back to a plain filled circle once
released onto the ring/home column -- based on cropping a dot out of
`ludo-game-c64.png` and concluding GEOS represents on-track pawns as
plain circles. That conclusion was wrong: the cropped dot was actually an
*empty* home-column lane marker (which **is** correctly a plain filled
circle, permanently, whether or not a pawn sits there -- that part of the
round-6 design stands), not an on-track pawn. `ludo-playerwon.png` (a
later-game screenshot, with real pawns visible on the ring and stacked in
home columns) makes it unambiguous: GEOS shows the detailed pawn
silhouette everywhere a pawn actually is, home base included. Fixed by
having `plot_pawn()` always draw the sprite, regardless of `in_play` --
simpler code than the round-6 version too, since the special-casing was
removed rather than added to.

**Pending, not yet implemented (2026-08-23, noted for later)**: the user
pointed out GEOS's colour-mode pawns have no black outline (confirmed --
this project's flat single-colour recolour already matches, since
`bm_pawn.gbm` is a single-region silhouette with nothing to outline), but
also has a **monochrome/high-contrast mode** with black-outlined,
pattern-filled pawns for 1-bit-per-pixel displays --
`GEOS/src/main.c`'s `pawngraphics[1..3]` ("Pattern 10"/"Pattern 2"/
"Pattern 9", used when `monochromeflag` is set, differentiating players
by dither pattern instead of colour since there's no colour available).
Worth adding as an ArchiLudo option for low-colour-mode/accessibility use
(relevant for a stock ARM2 machine that might run a 16-colour or
monochrome mode rather than 256-colour mode 15) -- would need a mode
toggle (menu item or auto-detect from the current screen mode's colour
count) plus decoding the pattern bitmaps the same way `bm_pawn.gbm` is
decoded now. Not implemented yet -- scope deferred rather than folded
into this round's fixes; revisit alongside Phase 2's real art pass or
Phase 5's menu/dialogue work.

## Round 6.3: pawn colour-blend bug, and dice added

A round-6.2 screenshot showed pawns rendering too small and washed-out --
"colour fill does not show, all colours look the same". Root cause: the
`recolour_and_squish()` composite (`Image.paste(solid, box, resized_mask)`)
blends *every* channel, including RGB, by the mask's strength against the
destination image's starting colour -- which was fully transparent black
(`(0,0,0,0)`). A half-opaque edge pixel (common after `Image.LANCZOS`
resizing a hard-edged mask) therefore came out at roughly half
*brightness*, not full colour at half *alpha*. On a canvas this small
(20x10 raw pixels for a pawn at the time) most of the visible shape *is*
antialiased edge, so nearly the whole sprite washed out toward grey/black
regardless of which player it belonged to. Fixed by setting every
pixel's RGB to the flat player colour unconditionally and driving only
alpha from the mask (`Image.new(..., colour + (0,))` then
`img.putalpha(resized_mask)`), so a partially-opaque edge pixel is a
partially-transparent *true-coloured* pixel. Confirmed by re-rendering
and comparing all four players' pawns side by side -- clearly distinct,
saturated colours, no washing out. `PAWN_SIZE` was also bumped 40 -> 48
OS units (24x12 raw pixels) alongside the fix, for a bolder, clearer
shape at this still-small canvas resolution.

Also added: `dice1..6.gbm` (GEOS's own die-face icons -- black
outline/pips, no player-colour tinting needed) reused via the same
`recolour_and_squish()` pipeline (colour hardcoded to black), displayed
in the panel gap between the status line and Throw
(`src/game_view.c`'s `plot_dice()`) -- addressing a repeated request
("no dice are shown still, nor outcome of the dice throw"). Shows
nothing before the first throw of a turn, and nothing at all if
`assets/Sprites` failed to load (the status text doesn't restate the roll
number any more since round 6 -- this is a cosmetic-only fallback, Throw
remains fully usable regardless).

## Round 6.4: sprite plotting abandoned entirely, primitives everywhere

Despite the round 6.3 colour fix verifying correctly offline (palette
entry 0 confirmed the right RGB, all pixel indices confirmed 0, checked
directly against the actual packed sprite file via
`tools/riscos_sprite.py`'s own `build_palette()`), the next Arculator
screenshot showed pawns as solid black regardless of player, and the die
face's top row visibly cropped. This is the **third** small sprite in a
row (after round 6.1's board-entry markers) to render wrong in Arculator
in a way that never reproduced in any offline check -- the packed
sprite's metadata, palette, and pixel data were all independently
verified correct every time, with no diagnosable common cause found (mode
mismatch, palette translation, and mask thresholding were all considered
and none fit the actual symptoms cleanly).

Given `os_plot` primitives (circles, rectangles, triangles) have been
100% reliable across every single round of this whole Phase 1 effort,
with zero unexplained failures, the decision was made to stop chasing
this and standardise on primitives for **all** of ArchiLudo's Phase 1
board/panel art:

- `plot_pawn()`: two overlapping filled circles (a wider body below a
  narrower head), replacing the GEOS pawn sprite.
- `plot_dice()`: a white square with a black border and the standard pip
  layout, replacing the GEOS die-face sprites.
- (`plot_start_marker()` already did this from round 6.1.)

`src/game_view.c` no longer calls any `osspriteop`/`xosspriteop_*`
function or loads `assets/Sprites` at all -- `#include "oslib/osspriteop.h"`
was removed (`wimpspriteop.h` stays, just for the `wimpspriteop_AREA`
constant `def.sprite_area` is conventionally set to). `make deploy` no
longer copies `assets/Sprites` to hostfs, and actively deletes any stale
copy from an earlier deploy so it can't be mistaken for something the
running game still reads.

**Not deleted**: `tools/riscos_sprite.py` (the sprite format tool itself
-- still correct and useful, e.g. for `info`/`to-png` on real files, and
for whatever Phase 2's actual board/pawn art pipeline turns out to be),
`assets/geos_source/*.gbm` (the local GEOS bitmap copies), and
`assets/generate_placeholder_art.py` (still runs and still produces a
correct, verified `assets/Sprites` -- it's just not consumed by the game
any more). All of this stays available for Phase 2 to pick back up once
there's time to actually debug why small sprites plot wrong in this
project's specific Arculator/ArchieSDK/OSLib combination -- worth
revisiting with a cleaner test harness (e.g. a minimal single-sprite
smoke-test program) rather than debugging it inside the full game.

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
