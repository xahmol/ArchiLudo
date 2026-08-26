#!/usr/bin/env python3
"""
ArchiLudo icon sprite generator (Wimp_PlotIcon pivot)
=======================================================

Summary: Generates the pawn sprites for ArchiLudo's pivot from os_plot
primitives to real sprites plotted via Wimp_PlotIcon (see
docs/ARCHITECTURE.md's "Resume here" section for the full background and
docs/GRAPHICS_TOOLING.md's "Round 7.16" for the sprite-tool validation
that preceded this). Unlike assets/generate_placeholder_art.py (which
reuses/recolours GeoLudo's own GEOS bitmaps), this draws an original
chess-pawn-style silhouette from scratch -- per explicit user request,
inspired by classic pixel-art chess-pawn icon references (round head/
finial, thin neck collar, tapered body, flared two-level base; black
outline; flat-hue fill with a white highlight band/dot and a grey
shadow patch to fake roundness within a limited palette).

Design constraints this follows (see docs/ARCHITECTURE.md's "Resume
here" for the reasoning, and its "Round 7.31" entry for a correction to
this section):
- Drawn SQUARE and tagged mode 27 (not the older mode-15-specific
  pre-squished-canvas convention) -- mode 27 is square-pixel (2 OS
  units/pixel in both axes, confirmed against the PRM's mode table),
  and Wimp_PlotIcon plots an old-style sprite icon at its NATIVE
  pixel-times-mode size, aspect-correct on every screen mode without
  any scaling at all.
  ROUND 7.31 CORRECTION: this file used to claim "Wimp_PlotIcon's own
  PutSpriteScaled scaling handles the aspect compensation" and adjusts
  to any icon extent given to it -- that was never actually true, and
  was never verified against real behaviour until three different
  PAWN_SIZE values in a row (48, 40, 36) produced a visually IDENTICAL
  on-screen pawn size in Arculator. The PRM documents no continuous
  scale-to-extent behaviour for a plain sprite icon at all -- only a
  binary "half size" flag. A sprite icon is plotted at its own native
  size (source pixel count x the sprite's own recorded mode's OS-units-
  per-pixel), centred within whatever extent the icon block gives via
  HCENTRED/VCENTRED, never stretched to fill it. FINAL below is what
  actually controls the on-screen size now -- see its own comment.
- 4bpp, quantised against the fixed 16 Wimp colours (`--wimp-palette`,
  see tools/riscos_sprite.py) -- Wimp_PlotIcon ignores an icon sprite's
  own embedded palette and always translates through those fixed 16, so
  the shading here is deliberately "black outline + one flat Wimp hue +
  white highlight + grey shadow" rather than a true light/dark pair of
  the player's own hue (not every player colour has two Wimp-colour
  entries available -- see the module docstring in tools/riscos_sprite.py
  and docs/ARCHITECTURE.md's "Resume here" point 4 for why).

Anti-aliasing technique: drawn at 10x supersample resolution with solid
(hard 0/255) intermediate masks, resizing the RGB and alpha channels
SEPARATELY at the very end and recombining -- deliberately avoiding the
round 6.3 bug this project already found once (Image.paste()/resize()
blending RGB toward a fully-transparent destination's (0,0,0) colour at
partially-covered edge pixels, washing colours toward grey/black; see
docs/GRAPHICS_TOOLING.md). The background RGB behind the whole
silhouette is set to the SAME colour as the outline, so there's no RGB
seam at the true edge either -- the alpha channel (from a separately
dilated+resized copy of the plain silhouette mask) does all the actual
shape-cutout work.

Syntax:  python3 assets/generate_icon_sprites.py
Output:  assets/pawn_icon0.png .. assets/pawn_icon3.png (per player,
         kept for reference/regeneration -- deliberately NOT named
         pawn0.png..pawn3.png, which are assets/generate_placeholder_art.py's
         own output files; the two generators must not collide) and
         assets/PawnSprites (a packed RISC OS sprite file, filetype &FF9).
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).parent
TOOL = HERE.parent / "tools" / "riscos_sprite.py"
sys.path.insert(0, str(TOOL.parent))
from riscos_sprite import WIMP_COLOURS  # noqa: E402

# Player order matches game_logic.c/game_view.c: 0=green, 1=red, 2=blue,
# 3=yellow. Wimp colour indices chosen from the fixed 16 (see
# tools/riscos_sprite.py's WIMP_COLOURS) -- green=10, red=11, yellow=9
# are the only sensible match for each hue; blue has two candidates (8
# dark blue, 15 light blue) and 15 was chosen as more recognisably
# "blue" at a glance than the very dark navy of 8.
PLAYER_WIMP_COLOUR = [10, 11, 15, 9]  # green, red, blue, yellow
PLAYER_NAMES = ["green", "red", "blue", "yellow"]

OUTLINE_COLOUR = WIMP_COLOURS[7]        # black
HIGHLIGHT_COLOUR = WIMP_COLOURS[0]      # white
SHADOW_COLOUR = WIMP_COLOURS[6]         # dark grey

WORK = 320          # supersampled working resolution (square)
# Final sprite raw pixel size (square -- mode 27, 2 OS units/pixel, see
# above). THIS is what actually controls the pawn's on-screen size --
# not game_view.c's PAWN_SIZE icon-extent constant, which (round 7.31
# correction) only centres the sprite, never scales it. Native on-
# screen footprint = FINAL * 2 OS units; keep this in sync with
# PAWN_SIZE there so the icon extent exactly matches the sprite instead
# of leaving dead padding or forcing an off-centre crop.
#
# Round 7.32: round 7.31's FINAL=18 (36 OS units) fixed the crop but
# was too aggressive a cut from the original 32 (64 OS units, the size
# the user had originally approved) -- per explicit live user report
# ("now they are small and very ugly, we maybe overcompensated").
# Compounded by a real bug in round 7.31's own OUTLINE_DILATE_WORK
# scaling (see its own comment) that left the outline barely rendering
# at 18px at all. Settled on 26 (52 OS units, 6 units/side margin
# inside the 64-unit CELL) -- still real, deliberate margin (versus
# zero at 32), but a much smaller cut from the approved look, and
# enough final pixels for the design's dither/outline detail to
# actually read. Revisit again if either the crop or the "too small"
# complaint resurfaces.
FINAL = 26
# ~1.4 final-px outline width once downsampled -- i.e.
# OUTLINE_DILATE_WORK * FINAL/WORK ~= 1.4, so OUTLINE_DILATE_WORK must
# SCALE UP as FINAL shrinks (not down): 1.4*WORK/FINAL. Round 7.31's
# own value here (8, at FINAL=18) had this backwards -- it scaled
# OUTLINE_DILATE_WORK down in proportion to FINAL, which shrinks the
# rendered outline width QUADRATICALLY (down to ~0.45 final-px, nearly
# invisible) rather than holding it constant -- almost certainly part
# of why round 7.31's sprite looked "ugly" in addition to being too
# small. Fixed direction here: 1.4*320/26 ~= 17.
OUTLINE_DILATE_WORK = 17

# Round 7.33: fixed a genuine bug in THIS script (nothing to do with
# RISC OS/Wimp clipping at all) found after the user reported the
# pawns now "look way better" but are "always cropped of the black
# line at the bottom", every single pawn, every position -- a symptom
# far too consistent to be another redraw/clip-scoping issue (those
# were always intermittent/position-dependent; see docs/ARCHITECTURE.md's
# Round 7.21-7.28 history), and this project's own "ground truth
# verification" habit says to actually check the generated pixels
# before theorising further. Doing that: draw_pawn_silhouette()'s shapes
# span y=18 (head top) to y=302 (base bottom) inside the WORK=320
# canvas -- only 18 WORK units of margin on EACH side before
# OUTLINE_DILATE_WORK's dilation even runs. At 17 (see above), that
# leaves just 1 WORK unit of surviving margin on both edges once
# dilated -- nowhere near enough for final_alpha's own BOX-resize
# antialiasing to represent a soft edge, so that outermost dilated
# ring gets clipped against the canvas boundary itself before it ever
# reaches Wimp_PlotIcon. The base (a flat rounded_rectangle, full width
# right up to its own boundary) clips far more visibly than the head
# (a curved ellipse, only a single-pixel-wide sliver actually reaches
# its extreme y) -- exactly matching "cropped at the bottom", not top.
# Fixed by scaling the whole design down slightly around the canvas
# centre (CONTENT_SCALE below) rather than growing WORK/rescaling every
# hand-tuned coordinate -- gives every edge real breathing room for any
# future OUTLINE_DILATE_WORK/FINAL combination, at the cost of a small,
# likely unnoticeable reduction in how much of its own 26x26 frame the
# opaque pawn fills.
CONTENT_SCALE = 0.90
CONTENT_CENTRE = WORK / 2


def sc(*coords):
    """
    Function: sc
    Summary: Scale a flat sequence of x,y,x,y,... pixel coordinates
             (as PIL's ellipse/rectangle/polygon calls take, whether as
             a single bounding-box tuple or several point tuples spread
             via *args at the call site) around CONTENT_CENTRE by
             CONTENT_SCALE -- see CONTENT_SCALE's own comment for why.
    Syntax:  sc(x0, y0, x1, y1, ...)
    Input:   coords - any number of raw x/y numbers, alternating.
    Output:  a tuple of the same length, each scaled toward/away from
             the canvas centre.
    """
    return tuple(CONTENT_CENTRE + (c - CONTENT_CENTRE) * CONTENT_SCALE for c in coords)


def sc_pts(points):
    """
    Function: sc_pts
    Summary: Same as sc(), but for a list of (x, y) point tuples (the
             form PIL's polygon() takes), returning the same list shape.
    Syntax:  sc_pts([(x0, y0), (x1, y1), ...])
    Input:   points - a list of (x, y) tuples.
    Output:  a list of (x, y) tuples, each scaled toward/away from the
             canvas centre.
    """
    return [tuple(sc(x, y)) for x, y in points]


def draw_pawn_silhouette(draw):
    """
    Function: draw_pawn_silhouette
    Summary: Draw the plain (single-colour) chess-pawn silhouette --
             round head, neck collar, tapered stem, two-level flared
             base -- onto the given ImageDraw, filled white (255) on
             whatever background the caller already set up.
    Syntax:  draw_pawn_silhouette(draw)
    Input:   draw - a PIL.ImageDraw.Draw bound to an 'L' mode image
                    sized (WORK, WORK).
    Output:  none. Draws in place.
    """
    # Head (sphere/finial).
    draw.ellipse(sc(100, 18, 220, 138), fill=255)
    # Neck collar -- a flattened disc, wider than the stem, overlapping
    # the head's base for a smooth join.
    draw.ellipse(sc(104, 126, 216, 172), fill=255)
    # Stem -- gently tapers outward toward the base.
    draw.polygon(sc_pts([(130, 158), (190, 158), (206, 250), (114, 250)]), fill=255)
    # Base, upper ring.
    draw.polygon(sc_pts([(108, 248), (212, 248), (232, 276), (88, 276)]), fill=255)
    # Base, lower foot -- the widest part.
    draw.rounded_rectangle(sc(76, 276, 244, 302), radius=10 * CONTENT_SCALE, fill=255)


def masked_shape(shapes_fn, silhouette_mask):
    """
    Function: masked_shape
    Summary: Draw a set of shapes (e.g. a highlight band, a shadow
             patch) onto a fresh WORKxWORK 'L' mask, then intersect the
             result with `silhouette_mask` so it can never bleed outside
             the pawn's own outline regardless of exact hand-picked
             coordinates.
    Syntax:  m = masked_shape(shapes_fn, silhouette_mask)
    Input:   shapes_fn        - callable(draw) that draws the shape(s).
             silhouette_mask  - the plain pawn silhouette ('L' image,
                                 WORKxWORK) to intersect against.
    Output:  a WORKxWORK 'L' image, 255 only where both the drawn
             shape(s) and the silhouette are set.
    """
    m = Image.new("L", (WORK, WORK), 0)
    shapes_fn(ImageDraw.Draw(m))
    return Image.composite(m, Image.new("L", (WORK, WORK), 0), silhouette_mask)


def highlight_shapes(d):
    # A soft "shine" patch on the head's upper-left, and a matching
    # band down the stem's left edge -- the same upper-left light
    # source convention the reference pixel-art pawns use.
    d.ellipse(sc(95, 35, 165, 100), fill=255)
    d.polygon(sc_pts([(125, 160), (155, 160), (145, 235), (120, 235)]), fill=255)


def shadow_shapes(d):
    # A patch on the lower-right of the head, and down the stem's
    # and base's right edge.
    d.ellipse(sc(165, 60, 220, 130), fill=255)
    d.polygon(sc_pts([(165, 175), (200, 175), (205, 248), (175, 248)]), fill=255)
    d.rectangle(sc(195, 248, 240, 300), fill=255)


def dot_shapes(d):
    # The small bright specular dot every reference image has, near
    # the top of the head -- kept deliberately small (a "shine", not
    # a second highlight region the size of the band above it) and
    # always solid (not dithered, see build_pawn_image()'s doc comment).
    d.ellipse(sc(122, 46, 138, 62), fill=255)


def build_pawn_image(fill_rgb):
    """
    Function: build_pawn_image
    Summary: Compose one player's full shaded pawn sprite -- outline,
             flat hue fill, highlight band/dot, shadow patch -- at
             WORKxWORK, then downsample to FINALxFINAL.

             Round 7.19: the highlight/shadow regions are no longer a
             flat solid colour (plain white / plain grey) -- per
             explicit user feedback that the flat white/grey read as
             "not the player's colour at all" in those regions on real
             hardware. Since only green and blue have a second Wimp
             colour to genuinely shade between (see the module
             docstring), the fix here is an ordered 1-pixel checkerboard
             DITHER between white/fill_rgb (highlight) and
             grey/fill_rgb (shadow) instead -- a classic limited-palette
             pixel-art technique: at normal viewing distance/scale, a
             checkerboard of two colours reads as a blended tint of
             both, staying visibly closer to the player's own hue than
             a flat white/grey block while still giving a lighter/darker
             read for the 3D shading effect. The small solid specular
             dot stays flat white (undithered) -- a single small "shine"
             point rather than a shaded region, dithering it would just
             look like noise at that size.

             Implementation: unlike the flat-colour version (which
             composited colour blocks at WORKxWORK and downsampled with
             NEAREST to avoid blending artefacts -- see docs/GRAPHICS_
             TOOLING.md's round 7.17), the dither pattern must be chosen
             at the FINAL pixel grid, not the supersampled one -- a
             checkerboard drawn at WORK resolution and then downsampled
             would either alias into a solid colour or an unpredictable
             pattern depending on how the dither period lines up with
             the downsample ratio. So the region *masks* (highlight/
             shadow/dot/silhouette) are still built at WORK resolution
             (for smooth, precisely-shaped boundaries) but converted to
             plain per-final-pixel membership booleans via a NEAREST
             resize to FINALxFINAL, and the actual colour choice --
             including the `(x + y) % 2` checkerboard parity -- is made
             directly on that FINALxFINAL grid, pixel by pixel.
    Syntax:  img = build_pawn_image(fill_rgb)
    Input:   fill_rgb - (r, g, b) tuple, the player's Wimp-palette
                        fill colour.
    Output:  a Pillow RGBA image, FINALxFINAL.
    """
    silhouette = Image.new("L", (WORK, WORK), 0)
    draw_pawn_silhouette(ImageDraw.Draw(silhouette))
    dilated = silhouette.filter(ImageFilter.MaxFilter(OUTLINE_DILATE_WORK * 2 + 1))

    highlight_mask = masked_shape(highlight_shapes, silhouette)
    shadow_mask = masked_shape(shadow_shapes, silhouette)
    dot_mask = masked_shape(dot_shapes, silhouette)

    # NEAREST for every mask here: each is already a hard 0/255 region
    # at WORK resolution, and a plain membership boolean is all that's
    # needed per final pixel -- no blending wanted (see docstring).
    silhouette_final = silhouette.resize((FINAL, FINAL), Image.NEAREST)
    highlight_final = highlight_mask.resize((FINAL, FINAL), Image.NEAREST)
    shadow_final = shadow_mask.resize((FINAL, FINAL), Image.NEAREST)
    dot_final = dot_mask.resize((FINAL, FINAL), Image.NEAREST)
    # The one exception: alpha (the true outer silhouette edge) still
    # gets a smoothing resize -- see round 7.17's reasoning, unchanged.
    final_alpha = dilated.resize((FINAL, FINAL), Image.BOX)

    final_rgb = Image.new("RGB", (FINAL, FINAL))
    px = final_rgb.load()
    for y in range(FINAL):
        for x in range(FINAL):
            if dot_final.getpixel((x, y)):
                colour = HIGHLIGHT_COLOUR
            elif silhouette_final.getpixel((x, y)) == 0:
                colour = OUTLINE_COLOUR
            elif highlight_final.getpixel((x, y)):
                # A sparser 1-in-4 dot grid, diagonally staggered every
                # row (not a plain (x%4, y%4) grid, which would still
                # read as straight vertical/horizontal dot columns) --
                # per explicit user feedback that the previous 50%
                # checkerboard read as solid diagonal LINES rather than
                # a subtle dithered tint. The shadow dither below stays
                # a full checkerboard (not asked to change, and being
                # darker/subtler already, is less prone to reading as
                # "lines" in the first place).
                is_dot = (x + 2 * y) % 4 == 0
                colour = HIGHLIGHT_COLOUR if is_dot else fill_rgb
            elif shadow_final.getpixel((x, y)):
                colour = SHADOW_COLOUR if (x + y) % 2 == 0 else fill_rgb
            else:
                colour = fill_rgb
            px[x, y] = colour

    final = final_rgb.convert("RGBA")
    final.putalpha(final_alpha)
    return final


def main():
    spr_paths = []
    for i, wimp_idx in enumerate(PLAYER_WIMP_COLOUR):
        fill_rgb = WIMP_COLOURS[wimp_idx]
        img = build_pawn_image(fill_rgb)
        png_path = HERE / f"pawn_icon{i}.png"
        img.save(png_path)
        print(f"wrote {png_path} ({PLAYER_NAMES[i]}, Wimp colour {wimp_idx})")

        spr_path = HERE / f"pawn{i}.spr"
        subprocess.run([sys.executable, str(TOOL), "from-png", str(png_path),
                         str(spr_path), "--name", f"pawn{i}", "--bpp", "4",
                         "--mode", "27", "--wimp-palette",
                         "--mask-alpha-threshold", "128"], check=True)
        spr_paths.append(spr_path)

    packed = HERE / "PawnSprites"
    subprocess.run([sys.executable, str(TOOL), "pack", str(packed)] +
                    [str(p) for p in spr_paths], check=True)
    for p in spr_paths:
        p.unlink()
    print(f"wrote {packed} (RISC OS filetype &FF9 -- rename to PawnSprites,ff9 on RISC OS/hostfs)")


if __name__ == "__main__":
    main()
