#!/usr/bin/env python3
"""
ArchiLudo edited-sprite import
=================================

Summary: The other half of assets/export_sprites_for_editing.py's
round-trip -- converts whatever PNGs are sitting in assets/edit/ (hand
pixel-corrected in an external editor, or untouched exports) back into
the real packed RISC OS sprite files this project ships:
assets/PawnSprites, assets/!Sprites, assets/!Sprites22. Per explicit
user request ("so you can convert the edited version back to our
application sprites").

For each of the 8 sprites this project has, in order of preference:
1. assets/edit/<name>_16x.png, if present -- downscaled back to native
   resolution first (see downscale_majority()'s own doc comment), on
   the assumption it may have been hand-edited.
2. assets/edit/<name>.png, if present (no _16x counterpart) -- used at
   face value, already native resolution.
3. The sprite's own original native PNG (e.g. assets/pawn_icon0.png)
   if assets/edit/ doesn't exist at all or is missing that file --
   lets this script run safely even before export_sprites_for_editing.py
   has ever been run, reproducing exactly what the generate_*.py
   scripts themselves would produce.

This means it's always safe to run against a partially-edited
assets/edit/ folder -- sprites you didn't touch get rebuilt unchanged
from their own existing artwork, not silently skipped or reset.

Syntax:  python3 assets/import_edited_sprites.py
Output:  assets/PawnSprites, assets/!Sprites, assets/!Sprites22 (packed
         RISC OS sprite files, filetype &FF9) -- also refreshes each
         sprite's own canonical native PNG (assets/pawn_icon0.png etc.)
         in place, so a later assets/export_sprites_for_editing.py run
         picks up the imported result as the new baseline.
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).parent
TOOL = HERE.parent / "tools" / "riscos_sprite.py"
EDIT_DIR = HERE / "edit"
UPSCALE = 16

# (edit/ basename, canonical native PNG path, sprite name, bpp, mode)
# mode 27 = square-pixel (90x90dpi), mode 12 = rectangular-pixel
# (90x45dpi, this project's non-square-mode aspect -- see
# tools/riscos_sprite.py's MODES_BY_BPP doc comment) -- must match
# generate_icon_sprites.py's/generate_app_icon.py's own from-png calls
# exactly, or a re-import would silently drift from what those scripts
# would themselves produce for an unedited sprite.
PAWN_SPRITES = [
    ("pawn0", HERE / "pawn_icon0.png", "pawn0", 4, 27),
    ("pawn1", HERE / "pawn_icon1.png", "pawn1", 4, 27),
    ("pawn2", HERE / "pawn_icon2.png", "pawn2", 4, 27),
    ("pawn3", HERE / "pawn_icon3.png", "pawn3", 4, 27),
]
SQUARE_ICON_SPRITES = [
    ("archiludo_full_sq", HERE / "app_icon_full.png", "!archiludo", 4, 27),
    ("archiludo_half_sq", HERE / "app_icon_half.png", "sm!archiludo", 4, 27),
]
RECT_ICON_SPRITES = [
    ("archiludo_full_rect", HERE / "app_icon_full_rect.png", "!archiludo", 4, 12),
    ("archiludo_half_rect", HERE / "app_icon_half_rect.png", "sm!archiludo", 4, 12),
]


def downscale_majority(img, factor):
    """
    Function: downscale_majority
    Summary: Downscale `img` by exactly 1/factor, choosing the single
             MOST COMMON exact RGBA value within each factor x factor
             block as that output pixel's colour -- robust to a few
             stray/rough pixels near a block boundary (e.g. from a
             slightly-off pencil stroke), unlike a plain NEAREST
             downscale (which would just pick one arbitrary corner
             pixel per block and could land on a stray edit) or a
             blending downscale like BOX (which would introduce exactly
             the soft/anti-aliased edges this whole hard-edged pixel-art
             pipeline is built to avoid -- see assets/generate_app_icon.py's
             own Round 7.38 doc comment on why NEAREST replaced BOX
             throughout this project's sprite generation).
    Syntax:  out = downscale_majority(img, factor)
    Input:   img    - a Pillow RGBA image whose width/height are each an
                      exact multiple of `factor`.
             factor - the integer downscale ratio.
    Output:  a new Pillow RGBA image, (img.width // factor) x
             (img.height // factor).
    """
    w, h = img.width // factor, img.height // factor
    out = Image.new("RGBA", (w, h))
    src = img.load()
    dst = out.load()
    for y in range(h):
        for x in range(w):
            counts = {}
            for by in range(factor):
                for bx in range(factor):
                    px = src[x * factor + bx, y * factor + by]
                    counts[px] = counts.get(px, 0) + 1
            dst[x, y] = max(counts.items(), key=lambda kv: kv[1])[0]
    return out


def resolve_native_image(edit_basename, canonical_native_path):
    """
    Function: resolve_native_image
    Summary: Load the best available source for one sprite, per this
             file's own module docstring's 3-step preference order.
    Syntax:  img = resolve_native_image(edit_basename, canonical_native_path)
    Output:  a Pillow RGBA image at native resolution, and a short
             string saying which source was used (for the printed log).
    """
    big_path = EDIT_DIR / f"{edit_basename}_{UPSCALE}x.png"
    plain_path = EDIT_DIR / f"{edit_basename}.png"

    if big_path.exists():
        big = Image.open(big_path).convert("RGBA")
        if big.width % UPSCALE or big.height % UPSCALE:
            print(f"WARNING: {big_path} is {big.width}x{big.height}, not an exact "
                  f"multiple of {UPSCALE} -- check the canvas wasn't cropped/resized; "
                  f"using it anyway (rounding down)", file=sys.stderr)
        return downscale_majority(big, UPSCALE), f"{big_path} (downscaled {UPSCALE}x)"

    if plain_path.exists():
        return Image.open(plain_path).convert("RGBA"), str(plain_path)

    return Image.open(canonical_native_path).convert("RGBA"), \
        f"{canonical_native_path} (assets/edit/ has nothing for this sprite)"


def rebuild_group(specs, packed_output):
    """
    Function: rebuild_group
    Summary: Resolve, re-quantise, and pack one group of sprites (the 4
             pawns, or the 2 square-pixel app icon variants, or the 2
             rectangular-pixel ones) into their shared packed sprite
             file -- see PAWN_SPRITES/SQUARE_ICON_SPRITES/
             RECT_ICON_SPRITES.
    Syntax:  rebuild_group(specs, packed_output)
    Input:   specs         - a list of (edit_basename, canonical_native_path,
                             sprite_name, bpp, mode) tuples.
             packed_output - path to write the packed sprite file to.
    Output:  none. Writes packed_output and updates each canonical_native_path
             in place to match whatever was actually used.
    """
    temp_sprs = []
    for edit_basename, canonical_native_path, sprite_name, bpp, mode in specs:
        img, source_desc = resolve_native_image(edit_basename, canonical_native_path)
        img.save(canonical_native_path)  # keep the canonical native PNG in sync
        print(f"  {sprite_name}: from {source_desc}")

        tmp_png = HERE / f"_import_{sprite_name.lstrip('!')}.png"
        img.save(tmp_png)
        spr = HERE / f"_import_{sprite_name.lstrip('!')}.spr"
        subprocess.run([sys.executable, str(TOOL), "from-png", str(tmp_png), str(spr),
                         "--name", sprite_name, "--bpp", str(bpp), "--mode", str(mode),
                         "--wimp-palette", "--mask-alpha-threshold", "128"], check=True)
        tmp_png.unlink()
        temp_sprs.append(spr)

    subprocess.run([sys.executable, str(TOOL), "pack", str(packed_output)] +
                    [str(p) for p in temp_sprs], check=True)
    for p in temp_sprs:
        p.unlink()
    print(f"wrote {packed_output}")


def main():
    if not EDIT_DIR.exists():
        print(f"NOTE: {EDIT_DIR} doesn't exist -- nothing has been exported/edited yet, "
              f"rebuilding from each sprite's current canonical PNG unchanged. Run "
              f"assets/export_sprites_for_editing.py first if you want to hand-edit "
              f"anything.", file=sys.stderr)

    print("Pawns -> assets/PawnSprites")
    rebuild_group(PAWN_SPRITES, HERE / "PawnSprites")

    print("App icon (square) -> assets/!Sprites22")
    rebuild_group(SQUARE_ICON_SPRITES, HERE / "!Sprites22")

    print("App icon (rectangular) -> assets/!Sprites")
    rebuild_group(RECT_ICON_SPRITES, HERE / "!Sprites")


if __name__ == "__main__":
    main()
