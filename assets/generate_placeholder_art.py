#!/usr/bin/env python3
"""
ArchiLudo placeholder art generator
====================================

Summary: Generates ArchiLudo's Phase 1 placeholder art (see
docs/ARCHITECTURE.md's Roadmap) by recolouring and resizing bitmaps from
this game's own prior GEOS port, rather than drawing new shapes from
scratch -- the user's call: reuse the existing GeoLudo artwork properly
resized until bespoke Phase 2 art is designed, instead of inventing
placeholder shapes. Source bitmaps are local copies (see
assets/geos_source/) of the matching files under
/home/xahmol/git/ludo/GEOS/assets/ -- see CREDITS.md and
docs/GRAPHICS_TOOLING.md's "Round 6: reusing GeoLudo's own art" for the
full writeup of what was reused and why. (The board-entry direction
markers this round *also* first tried reusing -- bm_gstart/rstart/bstart/
ystart.gbm -- are drawn programmatically in src/game_view.c instead as of
round 6.1; see that file's plot_start_marker() and
docs/GRAPHICS_TOOLING.md's "Round 6.1" for why. The .gbm files themselves
are left in assets/geos_source/ in case they're useful again later.)
dice1..6.gbm (GEOS's own die-face icons) were added in round 6.3 to give
the Throw button's outcome an actual on-screen face, per repeated user
request -- see game_view.c's plot_dice().

Then converts+packs the result into a single RISC OS Sprite file via
tools/riscos_sprite.py.

Syntax:  python3 assets/generate_placeholder_art.py

Output:  assets/pawn0.png .. assets/pawn3.png, assets/dice1.png ..
         assets/dice6.png (recoloured source images, kept for
         reference/regeneration) and assets/Sprites (the packed
         RISC OS sprite file the game actually loads, filetype &FF9 --
         see docs/GRAPHICS_TOOLING.md).
"""

import base64
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

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

# Mode 15 -- this project's target screen mode, per docs/GRAPHICS_TOOLING.md
# and CLAUDE.md's Testing section -- is 640x256 pixels at 1280x1024 OS
# units: 2 OS units per pixel horizontally, 4 vertically. Round 5 tried
# switching to mode 13 (which has genuinely square 4x4 OS-unit pixels)
# specifically to sidestep this, but mode 13 turned out not to be
# selectable in the user's Arculator monitor-type setup, and mode 15 is
# simply the normal RISC OS desktop mode anyway -- so ArchiLudo targets
# mode 15 and compensates for its non-square pixels here instead: source
# art is drawn on a canvas pre-squished by the INVERSE of mode 15's pixel
# aspect (half as many rows as columns, in raw pixel terms), so that once
# mode 15 stretches each raw pixel back out (2 OS units wide, 4 tall) the
# final on-screen shape is the intended square/circle, not the raw
# canvas's own (very flat) aspect. See docs/GRAPHICS_TOOLING.md's "Round 6
# correction" for the full writeup and tools/riscos_sprite.py's
# MODES_BY_BPP for the matching old-style sprite mode per bpp.
MODE15_OS_UNITS_PER_PIXEL = (2, 4)  # (x, y)

# On-screen sizes in OS units (square) -- must match src/game_view.c's
# PAWN_SIZE / DICE_SIZE.
PAWN_SIZE = 48
DICE_SIZE = 56

HERE = Path(__file__).parent
GEOS_SOURCE = HERE / "geos_source"
TOOL = HERE.parent / "tools" / "riscos_sprite.py"


def decode_gbm(path):
    """
    Function: decode_gbm
    Summary: Decode a GEOS Bitmap XML file (.gbm -- the format used by
             whatever bitmap editor authored the source files this
             project copied into assets/geos_source/) into a Pillow 'L'
             image: 255 where a bit is set (the shape/"ink"), 0
             elsewhere. Confirmed against a real decode: rendering
             bm_pawn.gbm this way reproduces the same chess-pawn
             silhouette visible in the GEOS screenshots
             (screenshots/ludo-game-c64.png etc in the sibling `ludo`
             repo) before this function was trusted with anything else.
    Syntax:  img = decode_gbm(path)
    Input:   path - filesystem path to a .gbm file.
    Output:  a Pillow 'L' image, (width_bytes*8) x height.
    """
    text = Path(path).read_text()
    width_bytes = int(re.search(r"<Width>(\d+)</Width>", text).group(1))
    height = int(re.search(r"<Height>(\d+)</Height>", text).group(1))
    data = base64.b64decode(re.search(r"<Data>(.*?)</Data>", text).group(1))

    img = Image.new("L", (width_bytes * 8, height))
    px = img.load()
    for y in range(height):
        for xb in range(width_bytes):
            byte = data[y * width_bytes + xb]
            for bit in range(8):
                val = (byte >> (7 - bit)) & 1
                px[xb * 8 + bit, y] = 255 if val else 0
    return img


def recolour_and_squish(mask_img, colour, target_pixel_size):
    """
    Function: recolour_and_squish
    Summary: Turn a monochrome ink/background mask (from decode_gbm) into
             an RGBA image tinted flat with `colour` where the mask was
             set and fully transparent elsewhere -- matching how GEOS
             itself renders these bitmaps in colour mode (BitmapUp draws
             the shape, then ColorRectangle floods it with the player's
             actual colour; there's no separate outline colour, confirmed
             by inspecting a real in-game screenshot crop, see
             docs/GRAPHICS_TOOLING.md) -- resized to `target_pixel_size`,
             the mode-15-pre-squished raw pixel canvas (see
             MODE15_OS_UNITS_PER_PIXEL above).
    Syntax:  img = recolour_and_squish(mask_img, colour, target_pixel_size)
    Input:   mask_img          - a Pillow 'L' image (from decode_gbm).
             colour            - (r, g, b) tuple.
             target_pixel_size - (width, height) in raw pixels.
    Output:  a Pillow RGBA image at target_pixel_size.
    """
    # Round 6.3 fix: the previous version built this via Image.paste(solid,
    # box, resized_mask) -- for LANCZOS-resized (i.e. partially transparent
    # at the edges) mask pixels, paste() *blends* every channel including
    # RGB by the mask strength against the destination's starting colour
    # (0,0,0,0), so a half-opaque edge pixel came out at roughly half
    # brightness, not full colour at half alpha. On a canvas this small
    # (20x10 raw pixels for a pawn) most of the visible shape *is*
    # antialiased edge, so this washed almost the whole sprite out toward
    # grey/black regardless of player colour -- exactly the "colour fill
    # doesn't show, all colours look the same" report. Fixed by setting
    # every pixel's RGB to the flat player colour unconditionally and
    # driving only the alpha channel from the mask, so a partially-opaque
    # edge pixel is a partially-transparent *true-coloured* pixel, not a
    # dimmed one.
    resized_mask = mask_img.resize(target_pixel_size, Image.LANCZOS)
    img = Image.new("RGBA", target_pixel_size, colour + (0,))
    img.putalpha(resized_mask)
    return img


def main():
    pawn_mask = decode_gbm(GEOS_SOURCE / "bm_pawn.gbm")

    pawn_pixel_size = (PAWN_SIZE // MODE15_OS_UNITS_PER_PIXEL[0],
                        PAWN_SIZE // MODE15_OS_UNITS_PER_PIXEL[1])
    dice_pixel_size = (DICE_SIZE // MODE15_OS_UNITS_PER_PIXEL[0],
                        DICE_SIZE // MODE15_OS_UNITS_PER_PIXEL[1])

    spr_paths = []
    for i, colour in enumerate(PLAYER_COLOURS):
        png_path = HERE / f"pawn{i}.png"
        recolour_and_squish(pawn_mask, colour, pawn_pixel_size).save(png_path)
        print(f"wrote {png_path}")
        spr_path = HERE / f"pawn{i}.spr"
        subprocess.run([sys.executable, str(TOOL), "from-png", str(png_path),
                         str(spr_path), "--name", f"pawn{i}", "--bpp", "4"],
                        check=True)
        spr_paths.append(spr_path)

    # dice1..6 -- GEOS's own die-face icons (black pips/outline), reused
    # plain rather than recoloured (there's no player colour involved).
    for face in range(1, 7):
        dice_mask = decode_gbm(GEOS_SOURCE / f"dice{face}.gbm")
        png_path = HERE / f"dice{face}.png"
        recolour_and_squish(dice_mask, (0, 0, 0), dice_pixel_size).save(png_path)
        print(f"wrote {png_path}")
        spr_path = HERE / f"dice{face}.spr"
        subprocess.run([sys.executable, str(TOOL), "from-png", str(png_path),
                         str(spr_path), "--name", f"dice{face}", "--bpp", "4"],
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
