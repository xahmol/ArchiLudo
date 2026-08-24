#!/usr/bin/env python3
"""
ArchiLudo gradient-shaded pawn preview (EXPLORATORY -- not shipped)
=====================================================================

Summary: Renders one pawn with smooth radial-gradient shading (light/dark
blend of the player hue, instead of the shipped 16-colour version's flat
highlight/shadow blocks) and, separately, what that gradient looks like
once actually encoded as a real 8bpp RISC OS sprite (adaptive palette,
via tools/riscos_sprite.py, mode 28 -- the square-pixel 8bpp mode).

Why this exists / status: per explicit user request, after seeing the
shipped 16-colour flat-shaded design (assets/generate_icon_sprites.py),
to preview whether a smoother "256 colour depth" look (closer to the
pixel-art chess-pawn references the user originally supplied) would be
worth pursuing. This script produces that preview only -- it does NOT
wire into the running game. Doing that for real would need a materially
bigger implementation than the shipped 4bpp/Wimp_PlotIcon path: 8bpp
icon sprites don't go through Wimp_PlotIcon's automatic colour
translation at all (see docs/ARCHITECTURE.md's "Resume here"/round 7.16
point 4), so it would need a hand-built ColourTrans_SelectTable
translation table plus OS_SpriteOp 52 (PutSpriteScaled) called directly
in plot_pawn(), not Wimp_PlotIcon. That work has NOT been started --
this script is preview/decision-support only, kept here (not deleted)
so the tuning already done survives a session restart, per explicit
user instruction ("prepare for restart"), and so a future session can
regenerate/keep iterating without re-deriving the approach.

Tuning history (LIGHT_BLEND/DARK_MULT below): started at 0.6/0.32 (user:
"too dark"), then 0.4/0.55, then settled here at 0.45/0.68 (user: "make
it a notch brighter still") -- as of 2026-08-24 this is the last
approved iteration, but the user had not yet given final sign-off on
this exact preview before the session paused; check for further
feedback before assuming these are final.

Syntax:  python3 assets/experiments/gradient_preview.py [--name pawn0..pawn3] [--wimp-colour N]
Output:  assets/experiments/pawn_gradient_src.png (pre-quantisation
         source art) and assets/experiments/pawn_gradient_encoded.png
         (round-tripped through a real 8bpp sprite file, i.e. what it'd
         actually look like on screen via the OS_SpriteOp 52 path).
"""

import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).parent
ASSETS = HERE.parent
TOOL = ASSETS.parent / "tools" / "riscos_sprite.py"
sys.path.insert(0, str(ASSETS))
sys.path.insert(0, str(TOOL.parent))
from generate_icon_sprites import (  # noqa: E402
    WORK, FINAL, OUTLINE_DILATE_WORK, OUTLINE_COLOUR,
    draw_pawn_silhouette, masked_shape)
from riscos_sprite import WIMP_COLOURS  # noqa: E402

# See "Tuning history" in the module docstring above.
LIGHT_BLEND = 0.45   # fraction of the way from fill_rgb toward white
DARK_MULT = 0.68     # fill_rgb multiplied by this for the shadow end


def build_gradient(fill_rgb):
    """
    Function: build_gradient
    Summary: Build one pawn sprite with smooth radial-gradient shading
             instead of generate_icon_sprites.py's flat highlight/shadow
             blocks -- same silhouette, same anti-aliasing split
             (NEAREST for internal boundaries, blending only the outer
             silhouette edge's alpha) since that fix applies here too,
             but the fill itself is a genuine light-to-dark blend of
             fill_rgb via Image.composite() against a radial gradient
             mask, not a small number of hand-placed flat regions.
    Syntax:  img = build_gradient(fill_rgb)
    Input:   fill_rgb - (r, g, b) tuple, the player's base hue.
    Output:  a Pillow RGBA image, FINALxFINAL.
    """
    silhouette = Image.new("L", (WORK, WORK), 0)
    draw_pawn_silhouette(ImageDraw.Draw(silhouette))
    dilated = silhouette.filter(ImageFilter.MaxFilter(OUTLINE_DILATE_WORK * 2 + 1))

    light = tuple(min(255, int(c + (255 - c) * LIGHT_BLEND)) for c in fill_rgb)
    dark = tuple(int(c * DARK_MULT) for c in fill_rgb)

    grad = Image.radial_gradient("L").resize((WORK * 2, WORK * 2), Image.BICUBIC)
    cx, cy = 135, 85  # bright centre placed upper-left, matching the flat version's highlight
    grad = grad.crop((WORK - cx, WORK - cy, 2 * WORK - cx, 2 * WORK - cy))

    rgb = Image.new("RGB", (WORK, WORK), OUTLINE_COLOUR)
    light_img = Image.new("RGB", (WORK, WORK), light)
    dark_img = Image.new("RGB", (WORK, WORK), dark)
    gradient_fill = Image.composite(light_img, dark_img, grad)
    rgb.paste(gradient_fill, (0, 0), silhouette)

    dot_mask = masked_shape(lambda d: d.ellipse((122, 46, 138, 62), fill=255), silhouette)
    rgb.paste(Image.new("RGB", (WORK, WORK), (255, 255, 255)), (0, 0), dot_mask)

    final_rgb = rgb.resize((FINAL, FINAL), Image.LANCZOS)
    final_alpha = dilated.resize((FINAL, FINAL), Image.BOX)
    final = final_rgb.convert("RGBA")
    final.putalpha(final_alpha)
    return final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wimp-colour", type=int, default=11,
                         help="Wimp colour index to preview (default 11, red)")
    args = parser.parse_args()

    fill_rgb = WIMP_COLOURS[args.wimp_colour]
    img = build_gradient(fill_rgb)
    src_path = HERE / "pawn_gradient_src.png"
    img.save(src_path)
    print(f"wrote {src_path}")

    spr_path = HERE / "pawn_gradient.spr"
    encoded_path = HERE / "pawn_gradient_encoded.png"
    subprocess.run([sys.executable, str(TOOL), "from-png", str(src_path), str(spr_path),
                     "--name", "pawn_grad", "--bpp", "8", "--mode", "28",
                     "--mask-alpha-threshold", "128"], check=True)
    subprocess.run([sys.executable, str(TOOL), "to-png", str(spr_path), "pawn_grad",
                     str(encoded_path)], check=True)
    spr_path.unlink()
    print(f"wrote {encoded_path} (what this would look like as a real 8bpp sprite)")


if __name__ == "__main__":
    main()
