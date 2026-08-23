#!/usr/bin/env python3
"""
ArchiLudo sprite tool
=====================

Summary: A from-scratch CLI for converting between PNG images and old-style
(RISC OS <=3.1) Sprite files, since no existing PC-side tool for this was
found (see docs/GRAPHICS_TOOLING.md) and RISC OS's own PNG-aware module
(ConvertPNG) postdates RISC OS 3.10 and isn't available on this project's
real target hardware.

Sprite file/area and sprite control block layout is taken from the RISC OS
3 Programmer's Reference Manual, Volume 1, Chapter 22 "Sprites" (local
mirror: ~/riscos-dev/prm-mirror/sprites.html) -- credited here per this
project's code-attribution convention. The "offsets are relative to a
virtual 4-byte-earlier position" quirk documented there (a sprite FILE
omits the in-memory area header's leading "total size" word, but every
offset inside the file is still expressed as if that word were present)
was additionally verified byte-for-byte against real sprite files bundled
with QTM v1.49 (c) Steve Harrison -- see tools/README.md for how.

Syntax:
    riscos_sprite.py info <spritefile>
    riscos_sprite.py to-png <spritefile> <sprite-name> <output.png>
    riscos_sprite.py from-png <input.png> <output-spritefile> --name NAME
                     [--bpp {1,2,4,8}] [--mode N] [--mask-alpha-threshold N]
    riscos_sprite.py pack <output-spritefile> <input-spritefile>...

See docs/GRAPHICS_TOOLING.md for the full writeup of the format and the
tool's design.
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("This tool needs Pillow: pip install Pillow")

# Old-style RISC OS screen/sprite mode numbers, by bits-per-pixel.
#
# Picked from Volume 4 Chapter 95 "Table B: Modes" (local mirror:
# ~/riscos-dev/prm-mirror/modes.html): all four are 640x256 pixels at
# 1280x1024 OS units -- 2x4 OS units per pixel, i.e. pixels twice as TALL
# as wide, non-square. ArchiLudo targets mode 15 (this project's chosen
# 256-colour screen mode -- it's the normal RISC OS desktop mode, and mode
# 13's square pixels turned out not to be worth chasing: it wasn't even
# selectable under the user's Arculator monitor-type setup). Sprites are
# tagged with the mode matching mode 15's own aspect for their bpp, so
# there's no sprite/screen mode mismatch -- the non-square-pixel distortion
# this caused for ArchiLudo's pawn art (round circles rendering as tall
# thin "bottle" shapes) is instead compensated for in the SOURCE art itself
# (see assets/generate_placeholder_art.py's MODE15_OS_UNITS_PER_PIXEL,
# which pre-squishes the drawing canvas by the inverse ratio so mode 15's
# stretch brings it back to the intended shape). See
# docs/GRAPHICS_TOOLING.md's "Round 6 correction" for the full writeup.
MODES_BY_BPP = {
    1: 0,   # 2 colours,   2x4 OS units
    2: 8,   # 4 colours,   2x4 OS units
    4: 12,  # 16 colours,  2x4 OS units
    8: 15,  # 256 colours, 2x4 OS units
}

SPRITE_CB_FIXED_SIZE = 44  # bytes: next_offset, name(12), 6 more u32 fields


def read_sprite_file(path):
    """
    Function: read_sprite_file
    Summary: Parse a RISC OS old-style sprite file into a list of sprite
             dicts, each with name/width/height/bpp/mode/palette/has_mask
             plus the raw image and (if present) mask byte data.
    Syntax:  sprites = read_sprite_file(path)
    Input:   path - filesystem path to a sprite file.
    Output:  list of dicts, one per sprite, in file order.
    """
    data = Path(path).read_bytes()
    count, first_sprite_v, first_free_v = struct.unpack_from("<3I", data, 0)
    off = first_sprite_v - 4  # the "omitted leading word" convention

    sprites = []
    for _ in range(count):
        next_off = struct.unpack_from("<I", data, off)[0]
        name = data[off + 4:off + 16].split(b"\x00")[0].decode("ascii")
        (width_words_m1, height_lines_m1, first_bit, last_bit,
         image_off, mask_off, mode) = struct.unpack_from("<7I", data, off + 16)

        bpp = mode_to_bpp(mode)
        width_words = width_words_m1 + 1
        height = height_lines_m1 + 1
        pixels_per_word = 32 // bpp
        width_px = width_words * pixels_per_word - (pixels_per_word - 1 - (last_bit // bpp))

        has_mask = mask_off != image_off
        palette = []
        if bpp <= 8:
            n_colours = 1 << bpp
            pal_off = off + SPRITE_CB_FIXED_SIZE
            for c in range(n_colours):
                w0 = struct.unpack_from("<I", data, pal_off + c * 8)[0]
                r, g, b = (w0 >> 8) & 0xff, (w0 >> 16) & 0xff, (w0 >> 24) & 0xff
                palette.append((r, g, b))

        row_bytes = width_words * 4
        image_start = off + image_off
        image_bytes = data[image_start:image_start + row_bytes * height]

        mask_bytes = b""
        if has_mask:
            mask_start = off + mask_off
            mask_bytes = data[mask_start:mask_start + row_bytes * height]

        sprites.append({
            "name": name, "width_px": width_px, "height": height,
            "bpp": bpp, "mode": mode, "row_bytes": row_bytes,
            "palette": palette, "has_mask": has_mask,
            "image_bytes": image_bytes, "mask_bytes": mask_bytes,
        })
        off += next_off

    return sprites


def mode_to_bpp(mode):
    """
    Function: mode_to_bpp
    Summary: Map an old-style RISC OS screen/sprite mode number to its bits
             per pixel, for the handful of modes this project actually
             uses (see MODES_BY_BPP). Raises for anything else, rather
             than guessing -- add the mode explicitly if a new one is
             needed.
    Syntax:  bpp = mode_to_bpp(mode)
    Input:   mode - RISC OS mode number, as stored in a sprite's control
                    block or passed to OS_Byte 135 / *WimpMode.
    Output:  bits per pixel (1, 2, 4, or 8).
    """
    for bpp, m in MODES_BY_BPP.items():
        if m == mode:
            return bpp
    # A handful of other well-known old-style modes this project doesn't
    # currently use but that a sprite file found in the wild might contain.
    other_modes_bpp = {
        1: 2, 4: 1, 9: 4, 13: 8, 18: 1, 19: 2, 20: 4, 21: 8,
    }
    if mode in other_modes_bpp:
        return other_modes_bpp[mode]
    raise ValueError(f"unrecognised old-style sprite mode {mode} -- add it to mode_to_bpp()")


def sprite_to_image(sprite):
    """
    Function: sprite_to_image
    Summary: Decode one parsed sprite (from read_sprite_file) into a
             Pillow RGBA image, applying its palette and mask (if any).
    Syntax:  image = sprite_to_image(sprite)
    Input:   sprite - one dict as produced by read_sprite_file().
    Output:  a Pillow Image in RGBA mode.
    """
    bpp = sprite["bpp"]
    width, height, row_bytes = sprite["width_px"], sprite["height"], sprite["row_bytes"]
    palette = sprite["palette"]
    img = Image.new("RGBA", (width, height))
    pixels = img.load()

    pixels_per_byte = 8 // bpp
    mask_val = (1 << bpp) - 1

    for y in range(height):
        row = sprite["image_bytes"][y * row_bytes:(y + 1) * row_bytes]
        mask_row = sprite["mask_bytes"][y * row_bytes:(y + 1) * row_bytes] if sprite["has_mask"] else None
        for x in range(width):
            byte_i = (x * bpp) // 8
            shift = (x * bpp) % 8
            colour_index = (row[byte_i] >> shift) & mask_val
            r, g, b = palette[colour_index] if colour_index < len(palette) else (0, 0, 0)
            alpha = 255
            if mask_row is not None:
                mask_bit = (mask_row[byte_i] >> shift) & mask_val
                alpha = 255 if mask_bit else 0
            pixels[x, y] = (r, g, b, alpha)

    return img


def cmd_info(args):
    for s in read_sprite_file(args.spritefile):
        print(f"{s['name']:12s} {s['width_px']:4d}x{s['height']:<4d} "
              f"{s['bpp']}bpp mode={s['mode']} mask={'yes' if s['has_mask'] else 'no'}")


def cmd_to_png(args):
    sprites = {s["name"]: s for s in read_sprite_file(args.spritefile)}
    if args.name not in sprites:
        sys.exit(f"no sprite named {args.name!r} in {args.spritefile} "
                  f"(have: {', '.join(sprites)})")
    sprite_to_image(sprites[args.name]).save(args.output)
    print(f"wrote {args.output}")


def build_palette(image, n_colours):
    """
    Function: build_palette
    Summary: Quantise an RGBA image to at most n_colours using Pillow's
             adaptive (median-cut) palette, matching how real RISC OS
             sprite-creation tools embed a bespoke palette per sprite
             rather than forcing a single fixed 16/256-colour scheme.
    Syntax:  quantised, palette = build_palette(image, n_colours)
    Input:   image     - a Pillow RGBA image.
             n_colours - target palette size (2, 4, 16, or 256).
    Output:  (quantised, palette) -- quantised is a Pillow "P"-mode image
             indexed into palette, a list of (r, g, b) tuples of length
             n_colours (padded with black if the image used fewer).
    """
    rgb = image.convert("RGB")
    quantised = rgb.quantize(colors=n_colours, method=Image.MEDIANCUT)
    raw_palette = quantised.getpalette()[:n_colours * 3]
    palette = [tuple(raw_palette[i:i + 3]) for i in range(0, len(raw_palette), 3)]
    while len(palette) < n_colours:
        palette.append((0, 0, 0))
    return quantised, palette


def write_sprite_file(path, name, image, bpp, mode, mask_alpha_threshold):
    """
    Function: write_sprite_file
    Summary: Write a single sprite (built from a Pillow image) out as a
             complete, standalone RISC OS old-style sprite file -- ready
             to *SLoad/Wimp_SpriteOp-merge on RISC OS, or to be combined
             with others via this tool's `pack` command.
    Syntax:  write_sprite_file(path, name, image, bpp, mode, mask_alpha_threshold)
    Input:   path                  - output file path.
             name                  - sprite name (max 12 ASCII characters).
             image                 - a Pillow RGBA image to convert.
             bpp                   - bits per pixel (1, 2, 4, or 8).
             mode                  - RISC OS mode number to stamp in the
                                      sprite (must match bpp -- see
                                      MODES_BY_BPP).
             mask_alpha_threshold  - alpha values below this are made
                                      transparent in the sprite's mask;
                                      pass None to omit the mask entirely.
    Output:  none. Writes the file at `path`.
    """
    if len(name) > 12:
        sys.exit(f"sprite name {name!r} is longer than 12 characters")
    n_colours = 1 << bpp
    quantised, palette = build_palette(image, n_colours)

    width, height = image.size
    pixels_per_word = 32 // bpp
    width_words = (width + pixels_per_word - 1) // pixels_per_word
    row_bytes = width_words * 4
    last_used_pixel_in_last_word = (width - 1) % pixels_per_word
    first_bit = 0
    last_bit = last_used_pixel_in_last_word * bpp + (bpp - 1)

    q_pixels = quantised.load()
    alpha = image.split()[-1].load() if image.mode == "RGBA" else None

    image_bytes = bytearray(row_bytes * height)
    mask_bytes = bytearray(row_bytes * height) if mask_alpha_threshold is not None else b""

    for y in range(height):
        for x in range(width):
            colour_index = q_pixels[x, y]
            byte_i = y * row_bytes + (x * bpp) // 8
            shift = (x * bpp) % 8
            image_bytes[byte_i] |= (colour_index & ((1 << bpp) - 1)) << shift
            if mask_alpha_threshold is not None:
                solid = (alpha[x, y] if alpha else 255) >= mask_alpha_threshold
                if solid:
                    mask_bytes[byte_i] |= ((1 << bpp) - 1) << shift

    has_mask = mask_alpha_threshold is not None
    image_off = SPRITE_CB_FIXED_SIZE + n_colours * 8
    mask_off = image_off + len(image_bytes) if has_mask else image_off
    next_off = mask_off + (len(mask_bytes) if has_mask else 0)

    cb = struct.pack("<I", next_off)
    cb += name.encode("ascii").ljust(12, b"\x00")
    cb += struct.pack("<7I", width_words - 1, height - 1, first_bit, last_bit,
                       image_off, mask_off, mode)
    for (r, g, b) in palette:
        word = (b << 24) | (g << 16) | (r << 8)
        cb += struct.pack("<II", word, word)
    cb += bytes(image_bytes)
    if has_mask:
        cb += bytes(mask_bytes)

    # File header: count=1, offsets stored relative to the omitted 4-byte
    # "total size" word (see module docstring / docs/GRAPHICS_TOOLING.md).
    header = struct.pack("<3I", 1, 12 + 4, 12 + len(cb) + 4)
    Path(path).write_bytes(header + cb)


def cmd_from_png(args):
    bpp = args.bpp or {0: 1, 8: 2, 12: 4, 15: 8, 4: 1, 1: 2, 9: 4, 13: 8}.get(args.mode, 4)
    mode = args.mode if args.mode is not None else MODES_BY_BPP[bpp]
    image = Image.open(args.input).convert("RGBA")
    write_sprite_file(args.output, args.name, image, bpp, mode, args.mask_alpha_threshold)
    print(f"wrote {args.output}: {args.name!r} {image.width}x{image.height} {bpp}bpp mode={mode}")


def cmd_pack(args):
    """
    Function: cmd_pack
    Summary: Merge several single-sprite files (as produced by `from-png`)
             into one multi-sprite sprite file/area, matching how a real
             !Sprites file holds many named icons together.
    """
    all_sprite_blobs = []
    for f in args.inputs:
        data = Path(f).read_bytes()
        count, first_sprite_v, first_free_v = struct.unpack_from("<3I", data, 0)
        off = first_sprite_v - 4
        end = first_free_v - 4
        # A single-sprite file (from-png's output) has exactly one sprite,
        # running from `off` to the end of the file; re-point its
        # "offset to next sprite" field once concatenated below.
        assert count == 1, f"{f} contains {count} sprites, expected 1 from from-png"
        all_sprite_blobs.append(bytearray(data[off:end]))

    out = bytearray()
    for i, blob in enumerate(all_sprite_blobs):
        is_last = i == len(all_sprite_blobs) - 1
        struct.pack_into("<I", blob, 0, 0 if is_last else len(blob))
        out += blob

    header = struct.pack("<3I", len(all_sprite_blobs), 12 + 4, 12 + len(out) + 4)
    Path(args.output).write_bytes(header + bytes(out))
    print(f"wrote {args.output}: {len(all_sprite_blobs)} sprites")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("info", help="list the sprites in a sprite file")
    p.add_argument("spritefile")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("to-png", help="decode one sprite to a PNG (for visual review)")
    p.add_argument("spritefile")
    p.add_argument("name")
    p.add_argument("output")
    p.set_defaults(func=cmd_to_png)

    p = sub.add_parser("from-png", help="encode a PNG as a single-sprite sprite file")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--name", required=True)
    p.add_argument("--bpp", type=int, choices=[1, 2, 4, 8])
    p.add_argument("--mode", type=int)
    p.add_argument("--mask-alpha-threshold", type=int, default=128,
                    help="alpha below this is transparent in the mask; "
                         "pass a negative number to omit the mask")
    p.set_defaults(func=cmd_from_png)

    p = sub.add_parser("pack", help="merge single-sprite files into one sprite area")
    p.add_argument("output")
    p.add_argument("inputs", nargs="+")
    p.set_defaults(func=cmd_pack)

    args = parser.parse_args()
    if getattr(args, "mask_alpha_threshold", None) is not None and args.mask_alpha_threshold < 0:
        args.mask_alpha_threshold = None
    args.func(args)


if __name__ == "__main__":
    main()
