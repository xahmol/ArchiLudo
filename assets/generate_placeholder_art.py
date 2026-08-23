#!/usr/bin/env python3
"""
ArchiLudo placeholder art generator
====================================

Summary: Procedurally generates the Phase 1 placeholder pawn artwork (see
docs/ARCHITECTURE.md's Roadmap) as PNGs, then converts+packs them into a
single RISC OS Sprite file via tools/riscos_sprite.py. No external image
assets or artist needed for this placeholder set -- it's here so the art
is fully reproducible, and easy to regenerate if the colours/size need to
change before real artwork replaces it in Phase 2.

Syntax:  python3 assets/generate_placeholder_art.py

Output:  assets/pawn0.png .. assets/pawn3.png (source images, kept for
         reference/regeneration) and assets/Sprites (the packed RISC OS
         sprite file the game actually loads, filetype &FF9 --
         see docs/GRAPHICS_TOOLING.md).
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

# Player colours, in game_logic.c's player-index order -- matches
# /home/xahmol/git/ludo/GEOS/src/main.c's startfieldgraphics comments
# exactly (0=green, 1=red, 2=blue, 3=yellow), and must match
# player_rgb/player_name in src/game_view.c.
PLAYER_COLOURS = [
    (30, 160, 60),   # player 0: green
    (220, 30, 30),   # player 1: red
    (30, 140, 220),  # player 2: blue
    (230, 200, 30),  # player 3: yellow
]

PAWN_SIZE = 20

HERE = Path(__file__).parent
TOOL = HERE.parent / "tools" / "riscos_sprite.py"


def make_pawn_png(colour, path):
    """
    Function: make_pawn_png
    Summary: Draw one simple placeholder pawn: a filled circle in the
             player's colour with a small white highlight, on a
             transparent background (the highlight and transparency both
             exercise tools/riscos_sprite.py's masking, not just flat
             colour fill).
    Syntax:  make_pawn_png(colour, path)
    Input:   colour - (r, g, b) tuple.
             path   - output PNG path.
    Output:  none. Writes the PNG at `path`.
    """
    img = Image.new("RGBA", (PAWN_SIZE, PAWN_SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    margin = 1
    d.ellipse([margin, margin, PAWN_SIZE - 1 - margin, PAWN_SIZE - 1 - margin],
              fill=colour + (255,), outline=(0, 0, 0, 255))
    hs = PAWN_SIZE // 4
    d.ellipse([PAWN_SIZE // 2 - hs // 2, PAWN_SIZE // 2 - hs,
               PAWN_SIZE // 2 + hs // 2, PAWN_SIZE // 2],
              fill=(255, 255, 255, 220))
    img.save(path)


def main():
    pngs = []
    for i, colour in enumerate(PLAYER_COLOURS):
        png_path = HERE / f"pawn{i}.png"
        make_pawn_png(colour, png_path)
        pngs.append(png_path)
        print(f"wrote {png_path}")

    spr_paths = []
    for i, png_path in enumerate(pngs):
        spr_path = HERE / f"pawn{i}.spr"
        subprocess.run([sys.executable, str(TOOL), "from-png", str(png_path),
                         str(spr_path), "--name", f"pawn{i}", "--bpp", "4"],
                        check=True)
        spr_paths.append(spr_path)

    packed = HERE / "Sprites"
    subprocess.run([sys.executable, str(TOOL), "pack", str(packed)] +
                    [str(p) for p in spr_paths], check=True)

    for p in spr_paths:
        p.unlink()

    print(f"wrote {packed} (RISC OS filetype &FF9 -- rename to Sprites,ff9 on RISC OS/hostfs)")


if __name__ == "__main__":
    main()
