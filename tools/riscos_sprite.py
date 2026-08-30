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
                     [--wimp-palette]
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
# selectable under the user's Arculator monitor-type setup). These modes
# all have non-square (2x4 OS units/pixel) geometry; a sprite tagged with
# one of them must either be drawn on a pre-squished canvas to compensate
# (see assets/generate_placeholder_art.py's MODE15_OS_UNITS_PER_PIXEL) or,
# this project's current preferred approach for new sprite art, be drawn
# square and tagged mode 27 instead, letting Wimp_PlotIcon's own scaling
# handle the aspect automatically -- see docs/GRAPHICS_TOOLING.md's
# "Current rendering approach" section.
MODES_BY_BPP = {
    1: 0,   # 2 colours,   2x4 OS units
    2: 8,   # 4 colours,   2x4 OS units
    4: 12,  # 16 colours,  2x4 OS units
    8: 15,  # 256 colours, 2x4 OS units
}

SPRITE_CB_FIXED_SIZE = 44  # bytes: next_offset, name(12), 6 more u32 fields

# The 16 standard "Wimp colours" (RISC OS 3 PRM, wimp.html's "Colour
# handling" section: 0-7 grey scale white->black, 8 dark blue, 9 yellow,
# 10 green, 11 red, 12 cream, 13 army green, 14 orange, 15 light blue --
# corrected here 2026-08-24 after finding riscos_wimp_reference.md's own
# paraphrase had 8/9 swapped; verified directly against the PRM table,
# not a paraphrase). Wimp_PlotIcon auto-translates a 1/2/4bpp indirected
# sprite icon's colour indices onto these fixed 16 regardless of the
# sprite's own embedded palette (see docs/ARCHITECTURE.md's "Resume
# here" point 4) -- so a sprite meant for icon plotting should be
# quantised against exactly these colours (see build_palette()'s
# `fixed_palette` parameter), not an adaptive per-sprite palette, or the
# two will disagree about what each index means.
#
# RGB values here are close approximations of RISC OS's well-known
# default desktop palette (not independently re-derived from a primary
# source in this project's local mirrors, which don't give the literal
# RGB triples) -- but exact precision doesn't matter for this tool's
# purpose: they're only used for *nearest-colour-match* assignment while
# quantising source art, and this project's own sprite art uses
# maximally distinct, fully-saturated target hues (a pure outline
# black, a pure highlight white, one clearly-player-coloured fill, one
# clearly grey shadow) that can't plausibly nearest-match the wrong Wimp
# colour even with an approximate reference RGB. The actual on-screen
# colour is always whatever RISC OS's real Wimp palette renders for
# that index at display time, regardless of this approximation.
WIMP_COLOURS = [
    (255, 255, 255),  # 0 white
    (221, 221, 221),  # 1
    (187, 187, 187),  # 2
    (153, 153, 153),  # 3
    (119, 119, 119),  # 4
    (85, 85, 85),     # 5
    (51, 51, 51),     # 6
    (0, 0, 0),        # 7 black
    (0, 0, 153),      # 8 dark blue
    (238, 238, 0),    # 9 yellow
    (0, 153, 0),      # 10 green
    (221, 0, 0),      # 11 red
    (255, 255, 187),  # 12 cream
    (85, 119, 0),     # 13 army green
    (255, 153, 0),    # 14 orange
    (0, 187, 255),    # 15 light blue
]


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
        # Palette data is OPTIONAL and its size is NOT reliably `1 << bpp`
        # (RISC OS 3 PRM, sprites.html: "256 colour modes may be an
        # exception... Most 256 colour sprites will have 16 palette
        # entries... some generated by programs will have a full 256
        # palette entries" -- and many sprites, especially small icon-bar
        # application sprites, have NO embedded palette at all, relying on
        # the mode's own default). The true entry count is however many
        # 8-byte entries actually fit between the fixed 44-byte header and
        # `image_off` -- derive it from that instead of assuming `1 << bpp`.
        # Confirmed against real sprite files (Steve Fryatt's wimp-prog
        # tutorial example downloads, e.g. WindowSpriteArea.zip's
        # `!ExamplApp/Sprites`): several genuine mode-15 (8bpp) sprites
        # there have `image_off == 44`, i.e. zero palette entries -- the
        # old `1 << bpp` assumption read 256 non-existent palette entries
        # for these, either running past the end of the file (a crash) or
        # silently misreading a *later* sprite's actual image bytes as
        # bogus palette colours for sprites with enough trailing data to
        # not crash.
        palette = []
        if bpp <= 8:
            n_colours = max(0, (image_off - SPRITE_CB_FIXED_SIZE) // 8)
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
    # currently use but that a sprite file found in the wild might contain
    # -- from the PRM's Table B (~/riscos-dev/prm-mirror/modes.html).
    # 25-28 (640x480, 1280x960 OS units, i.e. genuinely square 2x2
    # OS-units-per-pixel -- the classic VGA-monitor-type square-pixel
    # family) were found missing here, not hypothetically: ro-chess's
    # real `Sprites,ff9` uses mode 27 (16-colour/4bpp) for its board
    # sprites, discovered while validating this tool against real
    # reference sprite files per the user's explicit request.
    other_modes_bpp = {
        1: 2, 4: 1, 9: 4, 13: 8, 18: 1, 19: 2, 20: 4, 21: 8,
        25: 1, 26: 2, 27: 4, 28: 8,
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


def build_palette(image, n_colours, fixed_palette=None):
    """
    Function: build_palette
    Summary: Quantise an RGBA image to at most n_colours, either with
             Pillow's adaptive (median-cut) palette -- matching how real
             RISC OS sprite-creation tools embed a bespoke palette per
             sprite -- or, if `fixed_palette` is given, by nearest-colour
             matching against that exact fixed set instead (see
             WIMP_COLOURS -- needed for any sprite that will be plotted
             as a Wimp icon, since Wimp_PlotIcon ignores a 1/2/4bpp
             sprite's own embedded palette and always translates through
             the fixed 16 Wimp colours regardless of what's stored here;
             an adaptive palette would silently disagree with that
             translation and render the wrong colours).
    Syntax:  quantised, palette = build_palette(image, n_colours, fixed_palette=None)
    Input:   image         - a Pillow RGBA image.
             n_colours     - target palette size (2, 4, 16, or 256).
             fixed_palette - optional list of (r, g, b) tuples to
                             quantise against exactly (e.g. WIMP_COLOURS,
                             or a slice of it matching a lower bpp); the
                             first n_colours entries are used. None (the
                             default) uses Pillow's adaptive palette.
    Output:  (quantised, palette) -- quantised is a Pillow "P"-mode image
             indexed into palette, a list of (r, g, b) tuples of length
             n_colours (padded with black if the image used fewer, or if
             fixed_palette had fewer than n_colours entries).
    """
    rgb = image.convert("RGB")
    if fixed_palette is not None:
        target = fixed_palette[:n_colours]
        pal_img = Image.new("P", (1, 1))
        flat = [c for rgb_triple in target for c in rgb_triple]
        pal_img.putpalette(flat + [0, 0, 0] * (256 - len(target)))
        quantised = rgb.quantize(palette=pal_img, dither=Image.NONE)
        palette = list(target)
    else:
        quantised = rgb.quantize(colors=n_colours, method=Image.MEDIANCUT)
        raw_palette = quantised.getpalette()[:n_colours * 3]
        palette = [tuple(raw_palette[i:i + 3]) for i in range(0, len(raw_palette), 3)]
    while len(palette) < n_colours:
        palette.append((0, 0, 0))
    return quantised, palette


def write_sprite_file(path, name, image, bpp, mode, mask_alpha_threshold,
                       wimp_palette=False):
    """
    Function: write_sprite_file
    Summary: Write a single sprite (built from a Pillow image) out as a
             complete, standalone RISC OS old-style sprite file -- ready
             to *SLoad/Wimp_SpriteOp-merge on RISC OS, or to be combined
             with others via this tool's `pack` command.
    Syntax:  write_sprite_file(path, name, image, bpp, mode,
                                mask_alpha_threshold, wimp_palette=False)
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
             wimp_palette          - if True, quantise against the fixed
                                      16 Wimp colours (WIMP_COLOURS)
                                      instead of an adaptive per-sprite
                                      palette -- required for any sprite
                                      that will be plotted via
                                      Wimp_PlotIcon (see build_palette()).
                                      Only meaningful at bpp<=4 (8bpp
                                      icons don't go through the Wimp's
                                      automatic translation at all --
                                      see docs/ARCHITECTURE.md).
    Output:  none. Writes the file at `path`.
    """
    if len(name) > 12:
        sys.exit(f"sprite name {name!r} is longer than 12 characters")
    n_colours = 1 << bpp
    fixed_palette = WIMP_COLOURS if wimp_palette else None
    quantised, palette = build_palette(image, n_colours, fixed_palette)

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
    write_sprite_file(args.output, args.name, image, bpp, mode,
                       args.mask_alpha_threshold, wimp_palette=args.wimp_palette)
    palette_note = " (Wimp palette)" if args.wimp_palette else ""
    print(f"wrote {args.output}: {args.name!r} {image.width}x{image.height} "
          f"{bpp}bpp mode={mode}{palette_note}")


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
    p.add_argument("--wimp-palette", action="store_true",
                    help="quantise against the fixed 16 Wimp colours instead of "
                         "an adaptive per-sprite palette -- required for a sprite "
                         "that will be plotted via Wimp_PlotIcon (see WIMP_COLOURS)")
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
