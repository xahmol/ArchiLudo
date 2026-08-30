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

Sprite names are lowercase (`!archiludo`/`sm!archiludo`), per the
tutorial's own literal example ("!examplapp"/"sm!examplapp" for a
directory named "!ExamplApp") -- the Filer's own directory-icon lookup
normalises the directory name to lowercase before searching the sprite
pool, unlike `Wimp_CreateIcon`'s own sprite lookup (used for the
iconbar icon, which stays exact-case "!ArchiLudo" in `src/main.c`),
which is case-insensitive.

Design: drawn once at a square WORK=320 supersample canvas (same
technique as assets/generate_icon_sprites.py's pawn art -- solid 0/255
masks, RGB and alpha resized separately, since compositing them
together before resizing bleeds colour into the transparent edge), bold
and simplified (thick outline, no dither/shading detail) since the
final sizes are tiny (34x34 down to 17x9 pixels) and fine detail would
just read as noise. The square-pixel outputs (!Sprites22) downsample
the WORK canvas directly; the rectangular-pixel outputs (!Sprites)
first squish it 2:1 vertically so mode 12's own 2x4-OS-units/pixel
stretch brings the design back to the right proportions on screen --
see tools/riscos_sprite.py's MODES_BY_BPP doc comment for the mode
geometry this compensates for.

Every resize in this file uses `Image.NEAREST`, not `Image.BOX` --
BOX resize blends/antialiases across source pixels when downsampling,
which reads as a grey halo/blur at these tiny icon sizes. NEAREST
picks one source pixel per destination pixel with no blending at all,
giving the hard-edged, no-antialiasing look classic RISC OS icons use.

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

# The raw (unscaled) design below spans x=18..306/y=8..306 -- on its
# own, not enough margin per side to survive OUTLINE_DILATE_WORK-style
# dilation (18 WORK units, for NEAREST-resize robustness -- see
# build_icon_image()'s own comment) without the dilated outline
# clipping against the WORK canvas boundary itself before the sprite
# ever reaches Wimp_PlotIcon -- the same class of bug CONTENT_SCALE
# fixes for the pawn sprites in assets/generate_icon_sprites.py (see
# that file's own doc comment for the full mechanism). Fixed the same
# way here: CONTENT_SCALE shrinks every drawn coordinate below around
# CONTENT_CENTRE via sc()/sc_pts(), giving every edge real margin
# instead of resizing the canvas or hand-adjusting each hand-picked
# coordinate.
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
             outline/red-fill passes in build_icon_image(). The die is
             NOT drawn here at all -- see DIE_BOX_WORK's own doc
             comment for why it, like the pips, is stamped directly at
             each output's native resolution instead.
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


# Neither the pips nor the die's own square outline are drawn into the
# WORK canvas at all. At a 320-unit source downsampled by NEAREST to
# anywhere from 34 down to 9 pixels, a shape's exact rendered position
# depends on where NEAREST's sample grid happens to fall relative to
# its edges in WORK space -- which differs between the four separate
# outputs (square/rectangular x full/half all resize by different
# ratios), so the same nominal design ends up looking subtly different
# in each one even though nothing about the shape itself changed. No
# amount of tuning the WORK-space size fixes this, because the problem
# is the resampling step itself, not the shape being resampled. This
# matters more for the die than for the pawn: the shared WORK-space
# silhouette-dilate-then-NEAREST-resize pipeline that correctly serves
# the pawn's organic, curved outline can round each of a plain square's
# four corners slightly differently once resampled, with no curve to
# hide the asymmetry the way the pawn's silhouette does.
#
# Fix: the die (border, interior, and pips) is stamped directly at each
# output's own native resolution instead, via stamp_die()/stamp_pips()
# below -- draw_icon() only ever draws the pawn. DIE_BOX_WORK is the
# die's own bounding box (already CONTENT_SCALE'd) in WORK-space
# coordinates; die_box_in() maps it analytically into each output
# image's own native pixel grid (accounting for that output's square/
# rectangular resize ratio and any pre-squish); stamp_die()/
# stamp_pips() then draw directly at that resolution -- guaranteeing
# identical relative layout and genuinely proportional sizing in every
# output, with zero dependence on resampling luck. This is the same
# lesson assets/generate_icon_sprites.py's build_pawn_image() documents
# for its own highlight dither: the dither pattern must be chosen at
# the final pixel grid, not the supersampled one.
#
# The box is sized (170x180, asymmetrically grown to leave more room to
# the right and below) to leave real margin between the outer pips and
# the die's own border at every output size, including the smallest
# ("half") icon -- _pip_axis_layout() reserves both the inter-pip gaps
# and a margin segment on each side of the 3-pip row (see that
# function's own doc comment) rather than filling the entire interior
# edge-to-edge, and the box itself was grown until even the smallest
# icon has enough interior pixels for that margin to be visible.
# stamp_die()/stamp_pips() draw exact rectangles with no dilation
# margin to protect (unlike the pawn's dilated outline), so the only
# constraint here is the box itself staying inside the canvas, checked
# directly.
DIE_BOX_WORK = sc(140, 10, 310, 190)


def _round_box(die_box):
    """
    Function: _round_box (internal)
    Summary: Round a (x0, y0, x1, y1) box to whole pixels ONCE, shared
             by stamp_die() and stamp_pips() -- both must agree on the
             exact same integer box, or the pip grid can misalign
             against the die's actual painted border/interior (e.g. one
             function rounding a fractional dimension up while the
             other only ever paints the unrounded, smaller extent) at
             this project's smallest ("half rect") icon size, where the
             die box is only a few pixels across.
    Syntax:  x0, y0, x1, y1 = _round_box(die_box)
    """
    x0, y0, x1, y1 = die_box
    return round(x0), round(y0), round(x1), round(y1)


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
                       die_box_in()). Rounded to whole pixels via
                       _round_box() -- see its own doc comment for why
                       this must match stamp_pips()'s own rounding
                       exactly.
    Output:  none. Draws in place.
    """
    x0, y0, x1, y1 = _round_box(die_box)
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


def _pip_axis_layout(interior_size):
    """
    Function: _pip_axis_layout (internal)
    Summary: Lay out "margin, pip, gap, pip, gap, pip, margin" along one
             axis of the die's white interior as a hard INTEGER grid --
             every one of the 7 segments at least 1px -- so a pip can
             never touch another pip OR the interior's own edge (i.e.
             the die's border), regardless of how small `interior_size`
             is. The 2 outer margins are their own reserved segments,
             with the same >=1px guarantee as the 2 inter-pip gaps, so
             the outermost pips never end up flush against the border.
    Syntax:  pip, margin_a, gap_a, gap_b, margin_b = _pip_axis_layout(interior_size)
    Input:   interior_size - the die's white interior extent along one
                             axis, in pixels (already excludes the
                             die's own border -- see stamp_pips()).
    Output:  (pip, margin_a, gap_a, gap_b, margin_b) -- pip is the pip
             square's extent along this axis; margin_a/margin_b are the
             gaps between the interior's own edges and the outermost
             pips; gap_a/gap_b are the two inter-pip gaps. All >= 1
             whenever interior_size >= 7 (the true minimum for 3 pips +
             4 spacing segments at 1px each -- see DIE_BOX_WORK's own
             doc comment for why the die box is sized to make this
             achievable at every icon size this project actually ships
             except the smallest rectangular one); below
             that, pip still floors at 1 and the remaining space is
             split across the 4 spacing segments as evenly as possible,
             which can still leave some at 0 if interior_size < 7.
    """
    pip = max(1, (interior_size - 4) // 3)
    while pip > 1 and 3 * pip + 4 > interior_size:
        pip -= 1
    remaining = max(0, interior_size - 3 * pip)
    base = remaining // 4
    extra = remaining - base * 4
    spaces = [base + (1 if i < extra else 0) for i in range(4)]
    margin_a, gap_a, gap_b, margin_b = spaces
    return pip, margin_a, gap_a, gap_b, margin_b


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

             Pip positions and sizes are a hard integer grid within the
             die's own white interior (border + pip + gap + pip + gap +
             pip + border along each axis, via _pip_axis_layout()), not
             fractional (0.25/0.5/0.75 of the box) positioning -- a
             fractional approach has no explicit minimum-gap guarantee,
             and at this project's smallest ("half") icon size the
             ~7x7px die box would leave pips reading as a solid merged
             blob with no visible whitespace at all. The border width
             matches stamp_die()'s own
             `round(w/8)`/`round(h/8)` formula exactly, so the pip grid
             sits flush against the same white interior the die itself
             actually painted, not a separately-computed approximation
             of it.
    Syntax:  stamp_pips(img, die_box)
    Input:   img     - a Pillow RGBA image, already at its final output
                       size, with the die's plain white body (see
                       stamp_die()) already drawn on it.
             die_box - (x0, y0, x1, y1), the die's own bounding box in
                       THIS image's own pixel coordinates (see
                       die_box_in()).
    Output:  none. Draws in place.
    """
    ix0, iy0, ix1, iy1 = _round_box(die_box)
    w, h = ix1 - ix0, iy1 - iy0
    # Same border formula as stamp_die() -- must match exactly, since
    # the pip grid below is laid out against the interior THAT border
    # actually leaves.
    border_w = max(1, round(w / 8))
    border_h = max(1, round(h / 8))
    interior_x0, interior_y0 = ix0 + border_w, iy0 + border_h
    interior_w = w - 2 * border_w
    interior_h = h - 2 * border_h

    pip_w, margin_wa, gap_wa, gap_wb, margin_wb = _pip_axis_layout(interior_w)
    pip_h, margin_ha, gap_ha, gap_hb, margin_hb = _pip_axis_layout(interior_h)

    col0 = interior_x0 + margin_wa
    col1 = col0 + pip_w + gap_wa
    col2 = interior_x0 + interior_w - margin_wb - pip_w
    row0 = interior_y0 + margin_ha
    row1 = row0 + pip_h + gap_ha
    row2 = interior_y0 + interior_h - margin_hb - pip_h

    draw = ImageDraw.Draw(img)
    # Same face-"5" corners+centre layout as src/game_view.c's own
    # plot_dice() pips[4] entry.
    for px, py in ((col0, row0), (col2, row0), (col1, row1), (col0, row2), (col2, row2)):
        draw.rectangle((px, py, px + pip_w - 1, py + pip_h - 1), fill=(*OUTLINE_COLOUR, 255))


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
             outline + red pawn fill only (the die, body and border
             alike, is stamped on separately at each output's own
             resolution instead -- see DIE_BOX_WORK's own doc comment)
             -- using the same "resize RGB and alpha separately"
             technique as assets/generate_icon_sprites.py's
             build_pawn_image(), to avoid bleeding colour into the
             transparent edge.
    Syntax:  img = build_icon_image()
    Output:  a Pillow RGBA image, WORKxWORK.
    """
    silhouette = Image.new("L", (WORK, WORK), 0)
    draw_icon(ImageDraw.Draw(silhouette))

    # Outline: silhouette dilated by a fixed margin, same approach as
    # generate_icon_sprites.py's OUTLINE_DILATE_WORK. 18 (WORK units) is
    # wide enough to stay reliably present at both the full and half
    # sizes under NEAREST resizing -- NEAREST point-samples roughly one
    # WORK pixel every WORK/HALF ~= 18.8 units at the half size, so any
    # feature much thinner than that gap can fall entirely between
    # sample points and disappear in some rows/columns.
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
    # squished_full_png/squished_half_png are kept on disk (renamed to
    # app_icon_full_rect.png/app_icon_half_rect.png just above) as
    # stable, native-resolution reference artifacts, matching how the
    # square-pixel app_icon_full/half.png and the pawn sprites' own
    # pawn_iconN.png are already kept. See assets/export_sprites_for_editing.py,
    # which exports all
    # of these (plus nearest-neighbour-upscaled copies) for hand pixel-
    # editing in an external tool, and assets/import_edited_sprites.py
    # to convert edits back into the real packed sprite files.
    print(f"wrote {squished_full_png}, {squished_half_png}")


if __name__ == "__main__":
    main()
