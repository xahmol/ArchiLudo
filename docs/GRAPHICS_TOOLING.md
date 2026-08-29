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

## Round 7.16: validated against real Fryatt/ro-chess sprites, two bugs found; multi-resolution and multi-mode findings

Per explicit user request, before trusting the tool for the sprite pivot
(see `docs/ARCHITECTURE.md`'s "Resume here" section): re-validated it
against *real, external* RISC OS sprite files rather than only this
project's own round-trips, since "the generated sprites... looked awful
not only in colour, but also in pixel dimension and aspect ratio all
wrong" (a report from before the pivot's root-cause research -- almost
certainly the already-diagnosed `OS_SpriteOp 34`/bpp-mismatch display
bug, not this tool, but worth confirming the tool independently rather
than assuming).

**Real reference files used**: Steve Fryatt's own `wimp-prog` tutorial
example downloads -- `WindowSpriteArea.zip`, `AppSprite.zip`,
`ShapeChooser.zip`, `SpriteIcon.zip`, `TextAndSpriteIcon.zip`
(`https://www.stevefryatt.org.uk/files/wimp-prog/*.zip`, linked from the
`sprite-icons-and-areas`/`sprite-icons-and-choosing-options`/
`creating-an-application-directory`/`introducing-icons` tutorial pages;
not in the local HTML-only mirror, fetched directly) -- and ro-chess's
real, shipped `!Chess/Sprites,ff9` (already checked out locally from
earlier sprite-pivot research this session).

**Bug 1 -- palette entry count wrongly assumed `1 << bpp`.**
`read_sprite_file()` always read `1 << bpp` palette entries (256 for any
8bpp sprite). Per the PRM (sprites.html): "256 colour modes may be an
exception... Most 256 colour sprites will have 16 palette entries...
some generated by programs will have a full 256 palette entries" --
palette size is genuinely variable, and (confirmed against several of
Fryatt's real 8bpp app-icon sprites) very often **zero** -- an app icon
with no embedded palette at all, relying on the mode's own default. The
old assumption either ran past the end of the file (a crash, hit on
`WindowSpriteArea.zip`'s `!ExamplApp/Sprites` and `AppSprite.zip`'s
`!ExamplApp/!Sprites`) or -- worse -- silently misread a *different*
sprite's real image bytes as bogus palette colours for any file with
enough trailing data to survive the over-read without crashing. Fixed:
derive the palette entry count from how many 8-byte entries actually
fit between the fixed 44-byte header and `image_off`
(`(image_off - SPRITE_CB_FIXED_SIZE) // 8`) instead of assuming a fixed
size -- this also correctly handles the documented 256-entry-palette
special case (PRM: such a sprite sets `image_off` to `2048+44` itself,
which already yields exactly 256 via this same formula, no separate
`&82C` marker check needed).

**Bug 2 -- four real modes missing from `mode_to_bpp()`'s fallback
table.** ro-chess's actual `Sprites,ff9` uses mode 27 for its `hilite`
and `!chess` sprites -- crashed with "unrecognised old-style sprite mode
27" before this fix. Modes 25-28 are the PRM's Table B "640x480,
1280x960 OS units" square-pixel VGA family (25=1bpp, 26=2bpp, 27=4bpp,
28=8bpp -- 2x2 OS units/pixel, genuinely square, unlike modes 12/15/20/
21's 2x4). Added to `other_modes_bpp`.

**Validation results, once both fixes were in**: every sprite in all
five downloaded Fryatt zips and ro-chess's real file now parses without
error via `info`. Decoded (`to-png`) and visually inspected several,
including non-trivial ones with genuine multi-colour palettes and text
(Fryatt's `!Sprites22`-variant `!examplapp` app icon -- a crisp green
"Eg" icon) and detailed masked artwork (ro-chess's `wwqueen`/`wbknight`
chess pieces, `hilite`'s masked outline square) -- all rendered
correctly: right shape, right mask, right colours. Palette-less real
sprites correctly decode to solid black (the documented, deliberate
fallback for "no colour information available" -- not a bug) rather
than crashing or showing garbage. Round-tripped (`from-png` back into a
sprite, decoded again, compared pixel-for-pixel against the original
decode) both an 8bpp sprite (Fryatt's 34x34 `!examplapp`, mode 21) and a
4bpp one (ro-chess's 48x48 `wwqueen`, mode 20): **zero pixel
differences** in both cases. Also specifically checked non-word-aligned
widths (ro-chess's 58x13 `stop` and 34x40 `!chess`, neither a multiple
of the 8-pixels-per-word/4bpp packing) for edge/garbage-column
artifacts at the row boundary -- none found; the `width_words`/
`last_bit` maths (already re-derived and checked algebraically this
session) holds.

**Also checked ArchiLudo's own current `assets/Sprites`** (the pawn/dice
art) with the now-fixed tool: `info`/`to-png` report/render exactly as
intended (24x12 pawns, 28x14 dice, correct player colours, correct
masks) -- this tool was not the source of the originally-reported
"awful... dimension... aspect ratio wrong" sprites; that remains
attributed to the already-diagnosed `OS_SpriteOp 34` display bug (see
`docs/ARCHITECTURE.md`'s "Resume here" section), now understood and
being fixed via the pivot to `Wimp_PlotIcon`.

**New finding: the real "multi-resolution sprite" convention, and its
RISC OS 3.10 applicability.** Fryatt's example apps each ship
`!Sprites`/`!Sprites11`/`!Sprites22` for their persistent Filer/iconbar
icon (per explicit user instruction to follow this same convention for
ArchiLudo's own sprites "where applicable"). Inspected all three
variants directly:
- `!Sprites` (base) and `!Sprites22`: plain **old-style** mode numbers
  (15, 21) -- `!Sprites22` is exactly double linear resolution of the
  base (`!examplapp` 34x17 -> 34x34, i.e. Y doubled to compensate for
  mode 15's own non-square 2x4 aspect against mode 21's 2x2 -- both
  fully decoded and round-tripped correctly above).
- `!Sprites11`, however, uses **new-style** sprite mode encoding (the
  mode word's bit 0 set -- confirmed against ArchieSDK's own
  `oslib/osspriteop.h`, `osspriteop_NEW_STYLE = 0x1`, whose neighbouring
  fields are explicitly commented `/*RISC OS Select*/` in that header --
  RISC OS Select is a considerably later RISC OS variant, well past
  3.10). Our RISC OS 3 PRM mirror -- comprehensive for the 3.10 era --
  never mentions new-style sprites at all, corroborating that this is a
  later-OS feature genuine RISC OS 3.10 on real ARM2/ARM3 hardware does
  not understand.
  **Conclusion**: this convention is for an app's *own persistent
  Filer/iconbar icon* (matching the earlier sprite-pivot research's
  finding that it's unrelated to in-game runtime-plotted sprites, which
  already auto-scale via `PutSpriteScaled` with no stored variants
  needed) -- and even there, only the `!Sprites`/`!Sprites22` old-style
  pair is meaningful on ArchiLudo's real RISC OS 3.10 target;
  `!Sprites11`'s new-style variant should not be built. Applies to the
  still-not-started `!ArchiLudo` application-directory work (see
  `docs/ARCHITECTURE.md`'s "Resume here"), not the pawn/dice sprites.

**Also confirmed: Arculator's actual available screen modes for this
project's profile are 12, 15, 27, and 39** (per a real screenshot of its
Palette/Mode selector), not just 15 -- per explicit user instruction,
ArchiLudo should support all four, not assume 15 specifically. Checked
their geometry against the PRM's Table B:

| Mode | bpp | Pixels | OS units | OS units/pixel |
|---|---|---|---|---|
| 12 | 4 | 640x256 | 1280x1024 | 2x4 (non-square) |
| 15 | 8 | 640x256 | 1280x1024 | 2x4 (non-square) |
| 27 | 4 | 640x480 | 1280x960 | 2x2 (square) |
| 39 | 4 | 896x352 | 1792x1408 | 2x4 (non-square, higher-res) |

This turns out to already be compatible with, not a complication of, the
in-progress architecture: the window/board layout is entirely in OS
units (mode-independent by construction, see `docs/ARCHITECTURE.md`),
the manual `os_plot` minimum-fill-thickness rule (>=4 OS units, see
`docs/ARCHITECTURE.md`'s mode-15 pixel-thickness note) was chosen as a
worst-case bound already covering the largest of these four pixel
spacings (4 units, shared by 12/15/39) so it stays safe on mode 27's
smaller 2-unit spacing too, and -- most importantly -- `Wimp_PlotIcon`'s
automatic `PutSpriteScaled` scaling (the whole basis of the sprite
pivot) computes its scale factors from the sprite's *own* declared mode
against whatever mode is actually current, so a single correctly-tagged
sprite displays correctly under all four without needing per-mode art
variants. **Recommendation for the sprite pivot's actual art**: draw
source sprites *square* and tag them with mode 27 (2x2, square) rather
than continuing the mode-15-specific pre-squished-canvas convention
`assets/generate_placeholder_art.py` currently uses -- this lets
`Wimp_PlotIcon` do 100% of the aspect compensation for every mode
(including the currently-awkward non-square ones) via its own scale
factors, rather than ArchiLudo doing it by hand for one specific mode
and needing to re-derive it if the target mode ever changes.

**New permanent regression test**: `tools/test_riscos_sprite.py`
(self-contained, host Python + Pillow, no external files needed --
reproduces the two bug patterns above with small hand-built synthetic
sprite files rather than depending on the external Fryatt/ro-chess
downloads being present) locks in both fixes plus the non-word-aligned
round-trip check, matching this project's usual convention of turning a
manually-found bug into a permanent check rather than a one-off
verification. Run with `python3 tools/test_riscos_sprite.py`.

## Round 7.42: hand pixel-editing round-trip (`assets/edit/`)

Per explicit user request ("save PNG versions... so i can try to pixel
correct them in Photoshop... you can convert the edited version back to
our application sprites"). Two new scripts, a matched export/import
pair (see each one's own doc comment for the full detail):

- `assets/export_sprites_for_editing.py` (`make export-sprites`) writes
  every sprite this project ships (the 4 pawn colours, the 4 app-icon
  size/aspect variants) into `assets/edit/` as plain PNGs -- both at
  their real native resolution (e.g. 26x26 for a pawn -- too small to
  usefully click individual pixels in most editors) and a
  `Image.NEAREST`-upscaled 16x version (e.g. 416x416) meant to actually
  be edited, since every source pixel becomes a clean, individually-
  clickable 16x16 block. Also writes `assets/edit/README.md`, the
  user-facing workflow instructions (open the `_16x.png`, hard-edged
  pencil only, save over the same file, run the import script).
- `assets/import_edited_sprites.py` (`make import-sprites`) downscales
  each edited `_16x.png` back to native resolution via a
  majority-colour-per-16x16-block vote (robust to a few stray pixels
  near a block edge, unlike a plain corner-pixel-per-block downscale --
  see `downscale_majority()`'s own doc comment), re-quantises against
  the fixed Wimp palette exactly as `generate_icon_sprites.py`/
  `generate_app_icon.py` already do, and rebuilds
  `assets/PawnSprites`/`assets/!Sprites`/`assets/!Sprites22` directly
  from the result -- bypassing the original Python-drawn designs
  entirely for whichever sprites were actually edited. Sprites left
  untouched in `assets/edit/` are rebuilt unchanged from their own
  existing artwork, so it's safe to edit only some of them.

**Why PNG, not PSD**: Pillow (the only image library available in this
project's tooling) can only *read* PSD files, not write them, and
there's no reliable pure-Python PSD writer to reach for instead. Not
much of a loss here anyway -- every one of these sprites is already a
single flat "layer" (solid colour regions plus a hard alpha mask, no
blend modes or multiple layers to preserve), exactly what PNG
represents natively; PNG is also the normal working format for pixel
art at this scale in practice, not a compromise forced by tooling
limits.

**Verified lossless round-trip** before handing this off: ran export
then import with zero edits made, and diffed the resulting
`PawnSprites`/`!Sprites`/`!Sprites22` byte-for-byte against the
originals -- identical. Also confirmed a real edit (painting one whole
16x16 block a different flat colour) correctly propagates all the way
through to the packed sprite file and the refreshed canonical native
PNG.

## Round 7.49: dropped the 16x editing copy; fixed a real cross-mode dither bug

A real hand-editing session (`pawn0.png`, the green pawn) surfaced two
bugs in round 7.42's own edit pipeline, plus a genuine bug in the pawn
art's rendering across this project's supported modes.

**The two-file workflow was a footgun.** `import_edited_sprites.py`'s
`resolve_native_image()` always preferred `<name>_16x.png` over the
plain `<name>.png` when both existed. The user's first edit went
directly to the native `pawn0.png` (not the `_16x.png` the round 7.42
workflow expected) -- and was silently discarded, since the stale,
never-touched `pawn0_16x.png` sibling still existed and took priority.
Per explicit user request ("update all workflows... to generate and
edit the 26px variants directly"), the 16x copy is gone entirely:

- `export_sprites_for_editing.py` now writes exactly one PNG per
  sprite, at native resolution -- no `Image.NEAREST` 16x upscale, no
  `UPSCALE` constant.
- `import_edited_sprites.py`'s `resolve_native_image()` dropped its
  16x-preferred branch; it now just reads `assets/edit/<name>.png` at
  face value if present, falling back to the sprite's own canonical
  native PNG otherwise (2-step preference, was 3). `downscale_majority()`
  (majority-colour-per-16x16-block voting) is gone -- nothing calls it
  any more, since there's no upscaled copy to downscale back down.
- `assets/edit/README.md` (regenerated by the export script) drops
  every 16x-specific instruction and adds an explicit "Editor
  recommendation" section: **not** MS Paint (see next finding), prefer
  [Piskel](https://www.piskelapp.com/) (free, purpose-built, hard-edged
  pencil by default, real alpha support) or GIMP's Pencil tool with an
  alpha channel added; Aseprite (~$20) as the professional option.

**MS Paint silently flattens alpha.** The user's first edit came back
with every pixel fully opaque (255) and the background filled solid
white, instead of the expected fully-transparent background plus a
small opaque highlight dot -- classic MS Paint has no real
alpha-channel support and flattens transparency on open/save; it also
doesn't offer a genuinely hard-edged 1px pencil in every tool mode,
which explains the handful of anti-aliased near-black/near-white stray
pixels the same edit introduced. Recovered (this one time, not a
reusable script -- a one-off fix for a single botched edit) via a
border flood-fill: walk every pixel reachable from the canvas edge
through contiguous near-white pixels and set it back to fully
transparent. This can't be confused with the genuine highlight dot,
which sits inside the silhouette surrounded by non-white outline/fill
pixels and so is unreachable from the border without crossing them.
Verified via pixel-colour histograms that the recovered structure
(counts of transparent/outline/shadow/highlight pixels) matched the
untouched reference pawn1.png exactly.

**A genuine cross-mode rendering bug in the dither itself**, found from
the user's own second (alpha-clean) edit and their own correct
diagnosis ("the pixel exact dither does not survive[, because] the OS
pixel is not screen pixel"): `build_pawn_image()`'s round-7.19
highlight/shadow dither (a `(x+y)%2` checkerboard for the shadow, a
`(x+2y)%4` staggered grid for the highlight) chooses its colour based
on the pixel's own row, `y`. These pawn sprites are tagged mode 27 (2 OS
units/pixel, both axes -- square). This project's other three supported
modes (12, 15, 39 -- see CLAUDE.md's "Multi-mode requirement") are all
2x4 OS units/pixel: physical pixels TWICE as tall as a mode-27 sprite
pixel. When RISC OS plots an old-style sprite whose own tagged mode
differs from the current screen mode, it doesn't blend -- it scales by
simple row/column replication or dropping to match the coarser grid
(the same mechanism behind this project's own already-documented
"features under 4 OS units vanish in mode 15" finding for hand-drawn
`os_plot` rectangles, `~/.claude/memory/archiludo_mode15_pixel_
thickness.md`). Two adjacent sprite rows (each 2 OS units tall) get
fused into one physical pixel on modes 12/15/39, and only one of them
survives. A dither keyed on plain `y` parity puts DIFFERENT colours in
those two rows by design -- so on the fused modes, whichever row didn't
survive is just gone, silently cancelling roughly half of the intended
texture rather than merely softening it (this is why it read as
"missing", not just "different", to the user).

**Fix**: key both dither expressions on `y // 2` (the row-PAIR index)
instead of `y` -- `(x + y//2) % 2` for the shadow, `(x + 2*(y//2)) % 4`
for the highlight stagger. Both rows of every pair now agree, so it's
correct regardless of which one a given mode's scaling keeps. Applied
in two places: the fix in `build_pawn_image()` itself (for any future
from-scratch regeneration), and an equivalent direct pixel-level fix
applied to the user's actual edited `pawn0.png` (deliberately NOT
regenerated from `generate_icon_sprites.py`, which would have discarded
their real shape edit) -- for every vertical pair of rows at a given
column, if the two current colours differ and both belong to
`{fill_rgb, SHADOW_COLOUR, HIGHLIGHT_COLOUR}`, both rows are overwritten
with a single colour chosen by the same `y//2`-keyed formula; pairs
touching the outline, background, or anything outside those three
colours are left completely untouched, so the fix never touches
silhouette contour or edit content it doesn't understand. Verified two
ways: (1) simulating the mode-12/15/39 row-drop directly (keeping only
even source rows, then rescaling) and confirming it now matches the
mode-27 rendering's own texture exactly, where before the fix the two
would visibly diverge; (2) a fresh from-generator sprite dropped from
23 mismatched row-pairs to 6, with the remaining 6 confirmed (by
listing their coordinates) to be scattered, isolated cases at
highlight/shadow region-mask *boundaries* -- i.e. where the mask
membership itself, not the dither colour choice, changes on an odd
row -- accepted as the same minor, unavoidable category of precision
loss the outline/silhouette contour already has on a coarser mode.

Pawn1/pawn2/pawn3 were re-derived from the corrected pawn0 both times
(after the alpha recovery, and again after the dither fix) via the
same exact-match fill-colour swap described in this file's own "Round
7.42" section's design constraints: `build_pawn_image()` shares one
black outline/white highlight/grey shadow across all four players, so
recolouring is just swapping every pixel exactly equal to the green
fill RGB `(0,153,0)` to each player's own fill RGB (red `(221,0,0)`,
blue `(0,187,255)`, yellow `(238,238,0)`, from `PLAYER_WIMP_COLOUR`
into `tools/riscos_sprite.py`'s `WIMP_COLOURS`) -- verified via pixel
histograms that all four came out structurally identical except for
the fill hue.

## Round 7.50: crisp uniform outline, resized highlight/shadow, and a real dice-pip gap bug

Follow-up to round 7.49, per explicit user rules for the pawn art
("clear two pixel black outline", "details need to scale so that they
look the same in all modes", highlight "subtly somewhat bigger" and
shadow smaller) and for the app icon's dice pips ("matching in all
modes", "whitespace should remain between pips and borders and pips
amongst each other"). `assets/edit/reference/pawn0-3.png` (backed up
before touching anything, per explicit user request) hold the round
7.49 hand-edited pawns for the record -- the user then chose to adopt
this round's refined script output as the new shipped baseline instead
of keeping the hand edit authoritative, so the reference copies are
historical only, not consumed by any current tooling.

**Pawn outline, made crisp and uniformly >=2 final-px.**
`build_pawn_image()`'s alpha channel used to come from a smoothing BOX
resize (`final_alpha = dilated.resize((FINAL, FINAL), Image.BOX)`,
deliberate since round 7.17) at a dilate radius targeting only ~1.4
final-px (`OUTLINE_DILATE_WORK = 17`). Direct pixel comparison against
the hand-edited reference showed why this never read as a clean line:
the reference has ZERO partial-alpha pixels (only fully opaque or fully
transparent), while the old generator output had ~70 of them forming a
soft grey halo around the whole silhouette. Fixed in two parts:
`final_alpha` now uses `Image.NEAREST` (hard binary edge, reversing the
round 7.17 choice per this new explicit preference), and
`OUTLINE_DILATE_WORK` was retuned by sweeping several values and
comparing each one's pixel-colour histogram against the reference's own
(374 transparent / 140 outline / 135 fill out of 26x26) -- a first
guess of 28 badly overshot (grew the WHOLE silhouette outward, not just
the line width, since NEAREST leaves no soft falloff to absorb the
difference into); 21 lands closest (378 / 144 / 128). This also closes
a real latent risk from the old ~1.4px average: since it's below this
project's own established "features under 4 OS units (2 native mode-27
px) vanish on non-square modes" floor (see round 7.49's own dither
writeup and CLAUDE.md's Testing section), the outline could have
vanished outright at points along its contour that happened to be only
1px thick locally, on modes 12/15/39 specifically.

**Highlight enlarged ~15%, shadow shrunk ~15%.** `highlight_shapes()`'s
ellipse and polygon, and `shadow_shapes()`'s ellipse/polygon/rectangle,
each scaled about their own centre -- a direct, modest read of the
user's stated preference after comparing generator output against the
hand-edited reference side by side, not derived from an exact
measurement (the difference is a "feel", not a precise target).

**A real dice-pip whitespace bug in `generate_app_icon.py`.**
`stamp_pips()` positioned pips using fixed fractions of the die box
(corners at 0.25/0.75, centre at 0.5) with pip size `max(1,
round(box/6))` -- no explicit minimum-gap guarantee at all. Rendering
and zooming the actual shipped icon at this project's smallest ("half")
size confirmed the pips visibly merge into a solid blob with no
whitespace, exactly matching the user's report. Replaced with a hard
integer grid via a new `_pip_axis_layout()` helper: border + pip + gap
+ pip + gap + pip + border along each axis, every one of the 5 interior
segments (pip/gap/pip/gap/pip) at least 1px, computed independently per
axis and reusing `stamp_die()`'s own border formula exactly (via a new
shared `_round_box()` helper -- see next finding for why sharing it
matters). Verified by rendering and zooming all 4 real output sizes:
`full_sq` (34x34) and `half_sq` (17x17) now show 5 clearly separated
pips with real gaps, a large visible improve­ment over the merged blob;
`full_rect` (34x17) likewise.

**Found and fixed a rounding-mismatch bug while testing this**:
`stamp_die()` drew straight from the raw float `die_box`, while the new
`stamp_pips()` rounded its own copy independently -- at the smallest
rectangular-pixel output (`half_rect`, 17x9 -- a ~7.0x3.7px die box),
`round(3.7) = 4` while `stamp_die`'s own float rectangle only ever
painted ~3.7px of actual height, so the pip grid's assumed interior
didn't match what the die itself really painted, visibly breaking the
layout (misaligned/overlapping pips). Fixed by extracting `_round_box()`
and having both functions round the SAME box the SAME way, once.

**One genuine, un-fixable-by-tuning limit, left as-is per explicit user
decision**: at `half_rect`'s size, the die's white interior (after its
own 1px border) is only 2px tall -- fitting 3 pip-rows with real 1px
gaps needs at least 5px (1+1+1+1+1). No parameter choice changes this;
it's a hard consequence of the icon being that physically small. Two
pip rows visually merge at this one size/mode-aspect combination only
(the least-scrutinised view -- a Filer small-icon on a non-square
mode); `full_sq`/`half_sq`/`full_rect` are all unaffected and show
clean gaps throughout.

`make test` unaffected (asset-only + Python tooling changes). Clean
cross-compile, redeployed. **Not yet manually verified in Arculator**
-- ask the user to confirm all four pawn colours read as intended
(especially the outline crispness and highlight/shadow feel) and that
the dice pips show clear whitespace in the iconbar/Filer icon, in at
least mode 15 and mode 27 per this project's multi-mode requirement.

## Round 7.51: rounder pawn head, and pips still touching the die's own border

Two direct fine-tuning follow-ups to round 7.50's live results.

**Pawn head, "a bit rounder on top" than the generator's own output.**
Measured the actual difference by counting opaque pixels per row across
the head region in both `assets/pawn_icon0.png` (script) and
`assets/edit/reference/pawn0.png` (the round 7.49 hand edit): the
script's head silhouette width plateaus at a constant 12px for 11
consecutive rows (3 through 13) before narrowing, reading as a
flat-sided cylinder; the hand edit peaks higher (14px) and for far
fewer rows (5, rows 4-8) before visibly narrowing again -- a genuinely
rounder profile, not just a subjective impression. Root cause: at this
generator's small final resolution (a ~4.4 final-px circle radius), a
plain circle's own width changes very little near its equator (the
derivative of a circle's width curve approaches zero there), so several
rows near the middle naturally round to the same integer pixel width
even for a mathematically perfect circle -- more pronounced the smaller
the circle is relative to the final pixel grid. A bigger head gives its
curve more distinct FINAL rows to express itself across before 26x26
quantisation flattens it, which is what the hand edit's own wider head
achieves. `draw_pawn_silhouette()`'s head ellipse enlarged from a
120x120 circle to a 138x128 ellipse -- top edge kept at the SAME
canvas position (not grown symmetrically), since growing it upward too
was tried first and moved the shape measurably closer to this
project's own established canvas-clipping risk zone (round 7.33's
history) once the round 7.50 outline dilate is added on top; growing
down/outward from a fixed top edge avoided that risk entirely while
still landing very close to the reference's own row-by-row width
profile (verified by direct measurement, not just visual comparison).

**Dice pips still touched the die's own border**, per direct live user
report on the actual round 7.50 rendered icon -- a real gap in that
round's own fix. `_pip_axis_layout()` only ever reserved the 2
INTER-pip gaps (`pip, gap, pip, gap, pip` filling the WHOLE interior),
never a margin between the outermost pips and the interior's own
edges (= the die's border) -- confirmed by rendering the die in
isolation (no pawn overlap to confuse the read) and observing every
corner pip flush against two border edges simultaneously. Fixed by
extending the layout to 7 segments (`margin, pip, gap, pip, gap, pip,
margin`), all still >=1px. This immediately fixes the two "full" sizes
(`full_sq`/`full_rect`), which already had enough room -- but exposed
that the true minimum interior for genuine margins everywhere is 7px
(3 pips + 4 spacing segments), and `full_rect`'s own HEIGHT axis (this
project's actual primary dev/test mode's own aspect) only had 5.
Following the user's own suggested direction ("increasing the dice
size... enough to have that white space"), `DIE_BOX_WORK` was enlarged
from a 156x156 box to 170x180 -- grown asymmetrically (extended right
and down into this canvas's own generous unused space there, not about
its own centre) to avoid the pawn silhouette, which sits to the
lower-left; verified safe by directly rendering the full combined icon
and confirming no overlap and no canvas-edge clipping (a different,
lower risk than the pawn's own dilate-based clipping history, since
`stamp_die()`/`stamp_pips()` draw exact rectangles with nothing to
protect via margin except the box's own literal bounds). Result: every
output size now gets real margin on every axis except `half_rect`'s
own height (still the same accepted hard floor from round 7.50 -- 2px
interior there even after this growth, needs 7).

`make test` unaffected. Clean cross-compile, redeployed. **Not yet
manually verified in Arculator** -- ask the user to confirm the head
now reads rounder on all four pawn colours, and that the dice pips show
real whitespace to the border on every side (not just upper/left) in
the actual rendered iconbar/Filer icon.

## Round 7.52: round 7.51's own head enlargement cropped the top

A direct live user report on round 7.51's actual rendered result:
"pawn is now cropped at the top." (A second, separate bug reported in
the same message -- a duplicate/ghost pawn left in the home base corner
after a release -- was a `game_view.c` animation-plumbing bug, not a
graphics/asset one; see `docs/ARCHITECTURE.md`'s own "Round 7.52" entry
for that half.)

**Ruled out a stale/caching explanation first, with evidence, not
assumption**: compared `assets/pawn_icon0.png` (the source PNG) against
`tools/riscos_sprite.py to-png` run on the actual packed
`assets/PawnSprites` file (i.e. exactly what `Wimp_PlotIcon` draws
from) -- byte-for-byte identical, no data lost in packing. Also
`md5sum`-compared the local `assets/PawnSprites` against the deployed
copy in the Arculator hostfs -- identical. Row-by-row pixel inspection
of the actual PNG data showed no missing/incomplete rows either. All of
this ruled out "stale cache" or "packing/deploy bug" as the
explanation, before looking for a real design regression instead.

**The real cause**: round 7.51's head enlargement (120x120 circle ->
138x128 ellipse) measurably shrank how much of the head's very top is
PURE outline before fill starts showing through. Confirmed by directly
counting: round 7.50's own baseline has 2 clean solid-outline "cap"
rows (rows 1-2) before green first appears at row 3; round 7.51's
bigger ellipse only has 1 (fill already visible by row 2). A shorter
cap reads as a flattened/truncated top rather than a smoothly rounded
one -- exactly matching "cropped." Also confirmed the outline dilate
radius (`OUTLINE_DILATE_WORK`) *couldn't* fix this on its own: swept
21/23/25/27 and row 2 kept showing fill regardless, while the overall
outline pixel count grew well past the hand-edited reference's own
target with each step -- the cap length is a property of the ellipse's
own geometry near its pole, not something a bigger dilate radius
compensates for.

**Fix**: retuned the head ellipse smaller (95, 18, 225, 138) -- crucially
using the SAME top-edge position (y=18) as the original, pre-round-7.51
ellipse, so there's zero incremental canvas-clipping risk beyond what
round 7.50 already carried. This restores the full 2-row clean cap
while still measurably rounder than the original (peak width 13 vs 12,
narrowing again after only 4 rows at that peak vs. the original's
11-row flat plateau) -- and, checked against the hand-edited reference's
own pixel-colour histogram (transparent/outline/fill counts out of
26x26), this version actually matches it MORE closely than round 7.51's
first (bigger, 14-peak) attempt did: 374/142/134 here vs. the
reference's own 374/140/135, versus round 7.51's 378/144/128.

General lesson for any future shape retuning at this resolution: a
"rounder"/bigger shape and a "solid cap before curvature starts"
property trade off against each other at small pixel counts, and dilate
radius does not trade one for the other -- both need to be checked
together (peak width AND per-row cap solidity, not just one), against
the real reference's own row-by-row data, not just a single overall
"looks about right" render.

`make test` unaffected (asset-only). Clean cross-compile, redeployed.
**Not yet manually verified in Arculator** -- ask the user to confirm
the head no longer looks cropped/flat at the top on any of the four
pawn colours.

## Updating this file

Add a note here if a new mode/bpp is needed (update `MODES_BY_BPP` in the
tool too), or if any part of the format assumed here turns out wrong once
real assets are tested inside Arculator.
