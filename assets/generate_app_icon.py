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
    Summary: Draw the PAWN silhouette only (outline colour only, solid
             fill 255) onto the given ImageDraw -- shared by the
             outline/red-fill passes in build_icon_image(). Round 7.41:
             the die is NO LONGER drawn here at all -- see
             DIE_BOX_WORK's own doc comment (updated) for why it's
             stamped directly at each output's native resolution
             instead, the same fix already applied to the pips in
             round 7.40.
    Syntax:  draw_icon(draw)
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


# Round 7.40: pips are no longer drawn into the WORK canvas at all --
# per live user report that round 7.39's square pips still "look bit
# weird and uneven" across the different output sizes. Root cause: at
# a 320-unit source downsampled by NEAREST to anywhere from 34 down to
# 17 (or 9, for the rectangular-pixel half-size), each pip's exact
# rendered shape/position depends on where NEAREST's sample grid
# happens to fall relative to that pip's edges in WORK space -- which
# differs slightly between the four separate outputs (square/
# rectangular x full/half all resize by different ratios), so the
# SAME nominal pip design ends up looking subtly different in each one
# even though nothing about the pips themselves changed. No amount of
# tuning the WORK-space pip size fixes this, because the problem is
# the resampling step itself, not the shape being resampled.
#
# Round 7.41: the die's own black/white square (not just its pips) has
# exactly the same problem -- per live user report ("outline of die
# should be square, and is not always now as it misses pixel in lower
# right corner"), the die's outline was still going through the shared
# WORK-space silhouette-dilate-then-NEAREST-resize pipeline (the same
# one that still correctly serves the pawn's own organic, curved
# outline), which can round each of the die's four corners slightly
# differently once resampled -- a plain square has no room to hide that
# asymmetry the way a curved pawn silhouette does. So the die is now
# stamped whole (border AND interior, not just pips) directly at each
# output's native resolution too, via stamp_die() below -- draw_icon()
# no longer includes the die at all.
#
# DIE_BOX_WORK is the die's own bounding box (already CONTENT_SCALE'd)
# in WORK-space coordinates; die_box_in() maps it analytically into
# each output image's own native pixel grid (accounting for that
# output's square/rectangular resize ratio and any pre-squish);
# stamp_die()/stamp_pips() then draw directly at that resolution --
# guaranteeing identical relative layout and genuinely proportional
# sizing in every output, with zero dependence on resampling luck. This
# is the same lesson assets/generate_icon_sprites.py's
# build_pawn_image() already documents for its own highlight dither:
# "the dither pattern must be chosen at the FINAL pixel grid, not the
# supersampled one."
DIE_BOX_WORK = sc(150, 8, 306, 164)


def stamp_die(img, die_box):
    """
    Function: stamp_die
    Summary: Draw the die's own body -- a solid black square with a
             white interior inset by a proportional border width --
             directly onto an already-resized final image, at that
             image's own native resolution. See DIE_BOX_WORK's own doc
             comment for why this replaces drawing the die into the
             WORK canvas and letting it get resampled along with
             everything else -- two plain axis-aligned rectangles drawn
             directly in the target's own pixel space can never end up
             asymmetric the way a resampled rounded-rectangle can.
    Syntax:  stamp_die(img, die_box)
    Input:   img     - a Pillow RGBA image, already at its final output
                       size, with the pawn (and the background/outline
                       colour behind where the die will sit) already
                       drawn on it -- the die is opaque, so it cleanly
                       overwrites/occludes the pawn in the overlap area,
                       matching the intended "die in front" composition.
             die_box - (x0, y0, x1, y1), the die's own bounding box in
                       THIS image's own pixel coordinates (see
                       die_box_in()).
    Output:  none. Draws in place.
    """
    x0, y0, x1, y1 = die_box
    w, h = x1 - x0, y1 - y0
    draw = ImageDraw.Draw(img)
    draw.rectangle((x0, y0, x1, y1), fill=(*OUTLINE_COLOUR, 255))
    # Border width: a fixed fraction of the die's own box in THIS
    # output (not a WORK-space size), same proportional-with-a-floor
    # approach as stamp_pips()'s pip size.
    border_w = max(1, round(w / 8))
    border_h = max(1, round(h / 8))
    draw.rectangle((x0 + border_w, y0 + border_h, x1 - border_w, y1 - border_h),
                    fill=(*DIE_COLOUR, 255))


def stamp_pips(img, die_box):
    """
    Function: stamp_pips
    Summary: Draw the 5-pip face directly onto an already-resized final
             image, at that image's own native resolution -- see
             DIE_BOX_WORK's own doc comment for why this replaces
             drawing pips into the WORK canvas and letting them get
             resampled along with everything else. Call AFTER
             stamp_die(), so the pips land on top of the die's white
             interior.
    Syntax:  stamp_pips(img, die_box)
    Input:   img     - a Pillow RGBA image, already at its final output
                       size, with the die's plain white body (see
                       stamp_die()) already drawn on it.
             die_box - (x0, y0, x1, y1), the die's own bounding box in
                       THIS image's own pixel coordinates (see
                       die_box_in()).
    Output:  none. Draws in place.
    """
    x0, y0, x1, y1 = die_box
    w, h = x1 - x0, y1 - y0
    # Same face-"5" corners+centre layout as src/game_view.c's own
    # plot_dice() pips[4] entry. Pip size is a fixed fraction of the
    # die's own box (not of the pip's own WORK-space size), so it's
    # always genuinely proportional to how big the die actually came
    # out in this specific output, with a 1-pixel floor so it can never
    # vanish entirely at the smallest sizes.
    pip_w = max(1, round(w / 6))
    pip_h = max(1, round(h / 6))
    draw = ImageDraw.Draw(img)
    for fx, fy in ((0.25, 0.25), (0.75, 0.25), (0.5, 0.5), (0.25, 0.75), (0.75, 0.75)):
        cx = x0 + fx * w
        cy = y0 + fy * h
        draw.rectangle((cx - pip_w / 2, cy - pip_h / 2, cx + pip_w / 2, cy + pip_h / 2),
                        fill=(*OUTLINE_COLOUR, 255))


def die_box_in(target_w, target_h, source_w, source_h):
    """
    Function: die_box_in
    Summary: Map DIE_BOX_WORK (fixed WORK-space coordinates) into the
             pixel coordinates of a `target_w`x`target_h` image resized
             (via simple independent x/y scaling, matching Image.resize())
             from a `source_w`x`source_h` one -- `source_w`/`source_h`
             need not equal WORK/WORK, since the rectangular-pixel
             outputs resize from an already 2:1-vertically-squished
             WORKxWORK/2 canvas, not the original WORKxWORK one.
    Syntax:  box = die_box_in(target_w, target_h, source_w, source_h)
    Output:  (x0, y0, x1, y1) in the target image's own pixel space.
    """
    dx0, dy0, dx1, dy1 = DIE_BOX_WORK
    # First express the WORK-space box in the actual SOURCE canvas's own
    # coordinates (x is never pre-squished; y is, by source_h/WORK, for
    # the rectangular-pixel path -- 1.0 for the square-pixel path, where
    # source_h == WORK).
    y_pre = source_h / WORK
    sx0, sy0, sx1, sy1 = dx0, dy0 * y_pre, dx1, dy1 * y_pre
    # Then scale from the source canvas into the target image.
    rx, ry = target_w / source_w, target_h / source_h
    return (sx0 * rx, sy0 * ry, sx1 * rx, sy1 * ry)


def build_icon_image():
    """
    Function: build_icon_image
    Summary: Compose the full-colour WORKxWORK BASE icon image -- black
             outline + red pawn fill ONLY (round 7.41: the die, body
             and border alike, is stamped on separately at each
             output's own resolution instead -- see DIE_BOX_WORK's own
             doc comment) -- using the same "resize RGB and alpha
             separately" technique as
             assets/generate_icon_sprites.py's build_pawn_image(), to
             avoid the round 6.3 transparent-edge colour-bleed bug.
    Syntax:  img = build_icon_image()
    Output:  a Pillow RGBA image, WORKxWORK.
    """
    silhouette = Image.new("L", (WORK, WORK), 0)
    draw_icon(ImageDraw.Draw(silhouette))

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
    for y in range(WORK):
        for x in range(WORK):
            if sil_px[x, y]:
                colour = PAWN_COLOUR
            else:
                colour = OUTLINE_COLOUR  # background under the dilated ring
            px[x, y] = colour

    final = rgb.convert("RGBA")
    final.putalpha(dilated)
    return final


def main():
    icon = build_icon_image()

    full = icon.resize((FULL, FULL), Image.NEAREST)
    full_die_box = die_box_in(FULL, FULL, WORK, WORK)
    stamp_die(full, full_die_box)
    stamp_pips(full, full_die_box)
    full_png = HERE / "app_icon_full.png"
    full.save(full_png)

    half = icon.resize((HALF, HALF), Image.NEAREST)
    half_die_box = die_box_in(HALF, HALF, WORK, WORK)
    stamp_die(half, half_die_box)
    stamp_pips(half, half_die_box)
    half_png = HERE / "app_icon_half.png"
    half.save(half_png)
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
    # then downsample to the target sizes. squished itself is an
    # intermediate canvas (WORKxWORK/2), not a final output -- its own
    # height feeds die_box_in()'s source_h so the pip mapping accounts
    # for the pre-squish correctly.
    squished = icon.resize((WORK, WORK // 2), Image.NEAREST)
    squished_h = WORK // 2

    rect_full_h = FULL // 2
    rect_full = squished.resize((FULL, rect_full_h), Image.NEAREST)
    rect_full_die_box = die_box_in(FULL, rect_full_h, WORK, squished_h)
    stamp_die(rect_full, rect_full_die_box)
    stamp_pips(rect_full, rect_full_die_box)
    squished_full_png = HERE / "app_icon_full_rect.png"
    rect_full.save(squished_full_png)

    rect_half_h = HALF // 2 + HALF % 2
    rect_half = squished.resize((HALF, rect_half_h), Image.NEAREST)
    rect_half_die_box = die_box_in(HALF, rect_half_h, WORK, squished_h)
    stamp_die(rect_half, rect_half_die_box)
    stamp_pips(rect_half, rect_half_die_box)
    squished_half_png = HERE / "app_icon_half_rect.png"
    rect_half.save(squished_half_png)

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
    # Round 7.42: squished_full_png/squished_half_png used to be deleted
    # here -- kept now (renamed on disk to app_icon_full_rect.png/
    # app_icon_half_rect.png just above) as stable, native-resolution
    # reference artifacts, matching how the square-pixel app_icon_full/
    # half.png and the pawn sprites' own pawn_iconN.png were already
    # kept. See assets/export_sprites_for_editing.py, which exports all
    # of these (plus nearest-neighbour-upscaled copies) for hand pixel-
    # editing in an external tool, and assets/import_edited_sprites.py
    # to convert edits back into the real packed sprite files.
    print(f"wrote {squished_full_png}, {squished_half_png}")


if __name__ == "__main__":
    main()
