#!/usr/bin/env python3
"""
ArchiLudo application icon generator
======================================

Summary: Generates the !ArchiLudo application directory's iconbar/Filer
icon -- a red pawn beside a die, per explicit user request ("suggested
icon is one red pawn and a die") -- as two RISC OS old-style sprite
files, following the convention Steve Fryatt's wimp-prog tutorial
documents (Chapter 17, "Creating an Application Directory",
~/riscos-dev/wimp-prog-mirror/wimp-prog/creating-an-application-
directory.html): a square-pixel `!Sprites22` (90x90dpi) for modern/
square screen modes, and a rectangular-pixel `!Sprites` (90x45dpi) for
this project's own non-square modes (12/15/39 -- see
docs/ARCHITECTURE.md's Testing section). Each file holds a "full size"
sprite (named after the app, `!archiludo`) and a "half size" sprite
(`sm!archiludo`, prefixed per the tutorial's own convention, for the
Filer's small-icon views).

Round 7.38: sprite names lowercased from `!ArchiLudo`/`sm!ArchiLudo` --
per the tutorial's own literal example ("!examplapp"/"sm!examplapp" for
a directory named "!ExamplApp"), which is very likely deliberate and
not just a stylistic choice: the Filer's own directory-icon lookup is
suspected to normalise the directory name to lowercase before searching
the sprite pool, unlike Wimp_CreateIcon's own sprite lookup (confirmed
working with the exact-case "!ArchiLudo" for the iconbar icon, round
7.37) which is known to be case-insensitive. Tried after the Filer
directory icon still didn't appear even after a live user retest with a
fresh Filer window -- not confirmed as the actual cause yet, but a
direct, low-risk match to Fryatt's own working convention rather than a
guess at something novel. `src/main.c`'s iconbar icon reference was
NOT changed to match (still references "!ArchiLudo") -- Wimp sprite
name matching is case-insensitive, already proven working at that
exact call site, so there's nothing to fix there.

Design: drawn once at a square WORK=320 supersample canvas (same
technique as assets/generate_icon_sprites.py's pawn art -- solid 0/255
masks, RGB and alpha resized separately to avoid the round 6.3
transparent-edge colour bleed this project already found once), bold
and simplified (thick outline, no dither/shading detail) since the
final sizes are tiny (34x34 down to 17x9 pixels) and fine detail would
just read as noise. The square-pixel outputs (!Sprites22) downsample
the WORK canvas directly; the rectangular-pixel outputs (!Sprites)
first squish it 2:1 vertically (the same "pre-squish the source" trick
this project used for its own mode-15-targeted placeholder art before
the round 7.16 mode-27 pivot -- see tools/riscos_sprite.py's
MODES_BY_BPP doc comment) so mode 12's own 2x4-OS-units/pixel stretch
brings the design back to the right proportions on screen.

Round 7.38 also switched every resize in this file from `Image.BOX` to
`Image.NEAREST` -- per explicit user report that the icon looked
"fuzzy" (BOX resize blends/antialiases across source pixels when
downsampling, which is exactly what a soft photographic image wants
but reads as a grey halo/blur at these tiny icon sizes). NEAREST picks
one source pixel per destination pixel with no blending at all, giving
the same hard-edged, no-antialiasing look classic RISC OS icons use.

Syntax:  python3 assets/generate_app_icon.py
Output:  assets/app_icon_full.png, assets/app_icon_half.png (square-pixel
         source renders, kept for reference) and assets/!Sprites,
         assets/!Sprites22 (packed RISC OS sprite files, filetype &FF9).
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).parent
TOOL = HERE.parent / "tools" / "riscos_sprite.py"
sys.path.insert(0, str(TOOL.parent))
from riscos_sprite import WIMP_COLOURS  # noqa: E402

OUTLINE_COLOUR = WIMP_COLOURS[7]   # black
DIE_COLOUR = WIMP_COLOURS[0]       # white
PAWN_COLOUR = WIMP_COLOURS[11]     # red -- the app icon uses ArchiLudo's
                                    # own red player colour specifically
                                    # (not the player-selectable palette
                                    # generate_icon_sprites.py cycles
                                    # through), per explicit user request.

WORK = 320
# Full-size final pixel dimensions (square -- see Table 17.1 in the
# Fryatt tutorial cited above). Half-size is exactly half, per the
# tutorial's own "sm" convention.
FULL = 34
HALF = 17

# Round 7.39: the raw (unscaled) design below spans x=18..306/y=8..306 --
# only 8-18 WORK units of margin per side before OUTLINE_DILATE_WORK-
# style dilation even runs, nowhere near enough once that dilation is
# 18 (round 7.38's widening, for NEAREST-resize robustness -- see
# build_icon_image()'s own comment). Per live user report ("top black
# line missing"), the dilated outline was being clipped against the
# WORK canvas boundary itself before the sprite ever reached
# Wimp_PlotIcon -- the exact same class of bug
# assets/generate_icon_sprites.py's CONTENT_SCALE fixed for the pawn
# sprites in round 7.33 (see that file's own doc comment for the full
# mechanism). Fixed the same way here: CONTENT_SCALE shrinks every
# drawn coordinate below around CONTENT_CENTRE via sc()/sc_pts(),
# giving every edge real margin instead of resizing the canvas or
# hand-adjusting each hand-picked coordinate.
CONTENT_SCALE = 0.85
CONTENT_CENTRE = WORK / 2


def sc(*coords):
    """Scale a flat x,y,x,y,... coordinate sequence around CONTENT_CENTRE
    by CONTENT_SCALE -- see CONTENT_SCALE's own comment for why."""
    return tuple(CONTENT_CENTRE + (c - CONTENT_CENTRE) * CONTENT_SCALE for c in coords)


def sc_pts(points):
    """Same as sc(), but for a list of (x, y) point tuples (PIL's
    polygon() argument form)."""
    return [tuple(sc(x, y)) for x, y in points]


def sc_len(length):
    """Scale a plain length/radius (not a coordinate -- no CONTENT_CENTRE
    offset) by CONTENT_SCALE, for radii/step sizes computed independently
    of sc()'s own coordinate scaling."""
    return length * CONTENT_SCALE


def draw_icon(draw):
    """
    Function: draw_icon
    Summary: Draw the combined pawn+die silhouette (outline colour
             only, solid fill 255) onto the given ImageDraw -- shared
             by the outline/red-fill/white-fill passes below, which
             each call this with different fill colours to build up
             the layered image (see build_icon_image()).
    Syntax:  draw_icon(draw, outline_grow=0)
    Input:   draw - a PIL.ImageDraw.Draw bound to an 'L' mode image
                    sized (WORK, WORK).
    Output:  none. Draws in place.
    """
    # Pawn -- simplified (no neck-collar/base-ring detail, which would
    # be lost at 17x17 anyway): a round head, a tapering stem, one
    # flared base. Occupies the canvas's left ~60%, full height.
    draw.ellipse(sc(26, 74, 158, 206), fill=255)              # head
    draw.polygon(sc_pts([(64, 176), (120, 176), (140, 272), (44, 272)]), fill=255)  # stem
    draw.rounded_rectangle(sc(18, 268, 166, 306), radius=sc_len(14), fill=255)      # base

    # Die -- a rounded square overlapping the pawn's upper-right
    # shoulder, per a common two-object icon composition (see this
    # file's module docstring). Shows face "5" -- the most
    # recognisably-a-die pip pattern at a glance, matching
    # src/game_view.c's own plot_dice() face layout.
    draw.rounded_rectangle(sc(150, 8, 306, 164), radius=sc_len(18), fill=255)


def die_pips(draw):
    # Face "5": four corners + centre, same layout as game_view.c's
    # plot_dice() pips[4] entry -- kept visually consistent with the
    # in-game die rather than inventing a different pip arrangement.
    # Raw (unscaled) die bounding box, then sc() to match draw_icon()'s
    # own scaling of the same shape -- computing the centre points in
    # raw space first and scaling each one keeps this exactly in step
    # with the die's own outline regardless of CONTENT_SCALE's value.
    cx0, cy0, cx1, cy1 = 150, 8, 306, 164
    step = (cx1 - cx0) / 4
    # Round 7.39: SQUARE pips, not circles -- per live user report that
    # the pips "look bit weird and uneven". A small circle downsampled
    # by NEAREST point-sampling has no guarantee any given row/column
    # of samples passes through its centre, so different pips (whose
    # exact sub-pixel position varies slightly) can end up looking like
    # different, irregular blob shapes. A square's straight edges align
    # far more predictably with a NEAREST sample grid at these sizes --
    # the same reasoning that already favoured a flat rounded_rectangle
    # for the die's own outline over a circle. Round 7.38's radius (17)
    # kept as the half-size here (now scaled).
    half = sc_len(17)
    for gx, gy in ((0, 0), (2, 0), (1, 1), (0, 2), (2, 2)):
        px, py = sc(cx0 + step + gx * step, cy0 + step + gy * step)
        draw.rectangle((px - half, py - half, px + half, py + half), fill=255)


def build_icon_image():
    """
    Function: build_icon_image
    Summary: Compose the full-colour WORKxWORK icon image -- black
             outline, red pawn fill, white die fill, black pips --
             using the same "resize RGB and alpha separately" technique
             as assets/generate_icon_sprites.py's build_pawn_image(), to
             avoid the round 6.3 transparent-edge colour-bleed bug.
    Syntax:  img = build_icon_image()
    Output:  a Pillow RGBA image, WORKxWORK.
    """
    silhouette = Image.new("L", (WORK, WORK), 0)
    draw_icon(ImageDraw.Draw(silhouette))

    die_only = Image.new("L", (WORK, WORK), 0)
    draw = ImageDraw.Draw(die_only)
    draw.rounded_rectangle(sc(150, 8, 306, 164), radius=sc_len(18), fill=255)

    pips = Image.new("L", (WORK, WORK), 0)
    die_pips(ImageDraw.Draw(pips))

    # Outline: silhouette dilated by a fixed margin, same approach as
    # generate_icon_sprites.py's OUTLINE_DILATE_WORK. Round 7.38: widened
    # from 10 to 18 (WORK units) after switching to NEAREST resizing (see
    # this file's own Round 7.38 doc comment) revealed the half-size
    # (17x17) icon's outline mostly vanishing -- NEAREST point-samples
    # roughly one WORK pixel every WORK/HALF ~= 18.8 units, so any
    # feature much thinner than that gap can fall entirely between
    # sample points and disappear in some rows/columns. 18 keeps the
    # outline reliably present at both the full and half sizes.
    from PIL import ImageFilter
    outline_dilate = 18
    dilated = silhouette.filter(ImageFilter.MaxFilter(outline_dilate * 2 + 1))

    rgb = Image.new("RGB", (WORK, WORK))
    px = rgb.load()
    sil_px = silhouette.load()
    die_px = die_only.load()
    pip_px = pips.load()
    for y in range(WORK):
        for x in range(WORK):
            if pip_px[x, y]:
                colour = OUTLINE_COLOUR
            elif die_px[x, y]:
                colour = DIE_COLOUR
            elif sil_px[x, y]:
                colour = PAWN_COLOUR
            else:
                colour = OUTLINE_COLOUR  # background under the dilated ring
            px[x, y] = colour

    final = rgb.convert("RGBA")
    final.putalpha(dilated)
    return final


def main():
    icon = build_icon_image()

    full_png = HERE / "app_icon_full.png"
    icon.resize((FULL, FULL), Image.NEAREST).save(full_png)
    half_png = HERE / "app_icon_half.png"
    icon.resize((HALF, HALF), Image.NEAREST).save(half_png)
    print(f"wrote {full_png}, {half_png}")

    # Square-pixel (!Sprites22, mode 27, 90x90dpi) -- direct downsamples.
    square_specs = [
        (full_png, "!archiludo", FULL, FULL),
        (half_png, "sm!archiludo", HALF, HALF),
    ]
    square_sprs = []
    for src_png, name, w, h in square_specs:
        spr = HERE / f"{name.lstrip('!')}_sq.spr"
        subprocess.run([sys.executable, str(TOOL), "from-png", str(src_png), str(spr),
                         "--name", name, "--bpp", "4", "--mode", "27", "--wimp-palette",
                         "--mask-alpha-threshold", "128"], check=True)
        square_sprs.append(spr)
    sprites22 = HERE / "!Sprites22"
    subprocess.run([sys.executable, str(TOOL), "pack", str(sprites22)] +
                    [str(p) for p in square_sprs], check=True)
    for p in square_sprs:
        p.unlink()
    print(f"wrote {sprites22}")

    # Rectangular-pixel (!Sprites, mode 12, 90x45dpi, 34x17/17x9) --
    # squish the WORK canvas 2:1 vertically first (see module docstring),
    # then downsample to the target sizes.
    squished = icon.resize((WORK, WORK // 2), Image.NEAREST)
    squished_full_png = HERE / "app_icon_full_rect.png"
    squished.resize((FULL, FULL // 2), Image.NEAREST).save(squished_full_png)
    squished_half_png = HERE / "app_icon_half_rect.png"
    squished.resize((HALF, HALF // 2 + HALF % 2), Image.NEAREST).save(squished_half_png)

    rect_specs = [
        (squished_full_png, "!archiludo"),
        (squished_half_png, "sm!archiludo"),
    ]
    rect_sprs = []
    for src_png, name in rect_specs:
        spr = HERE / f"{name.lstrip('!')}_rect.spr"
        subprocess.run([sys.executable, str(TOOL), "from-png", str(src_png), str(spr),
                         "--name", name, "--bpp", "4", "--mode", "12", "--wimp-palette",
                         "--mask-alpha-threshold", "128"], check=True)
        rect_sprs.append(spr)
    sprites = HERE / "!Sprites"
    subprocess.run([sys.executable, str(TOOL), "pack", str(sprites)] +
                    [str(p) for p in rect_sprs], check=True)
    for p in rect_sprs:
        p.unlink()
    print(f"wrote {sprites}")
    for p in [squished_full_png, squished_half_png]:
        p.unlink()


if __name__ == "__main__":
    main()
