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

## Updating this file

Add a note here if a new mode/bpp is needed (update `MODES_BY_BPP` in the
tool too), or if any part of the format assumed here turns out wrong once
real assets are tested inside Arculator.
