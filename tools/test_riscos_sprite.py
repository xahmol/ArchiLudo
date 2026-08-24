#!/usr/bin/env python3
"""
ArchiLudo sprite tool test suite
=================================

Summary: Self-contained regression tests for riscos_sprite.py, run with the
host's own Python (no RISC OS/Arculator/ArchieSDK involved) -- same spirit
as tests/test_game_logic.c's host-only philosophy, just for the asset
pipeline instead of the game logic.

Two of these tests lock in bugs found and fixed by hand-validating this
tool against real, genuine RISC OS sprite files (Steve Fryatt's wimp-prog
tutorial example downloads, and ro-chess's actual Sprites,ff9), per
explicit user request to verify the tool bitwise-correctly before trusting
it for the sprite pivot -- see docs/GRAPHICS_TOOLING.md's write-up of that
session for the full story. Those external files aren't vendored into
this repo (third-party downloads), so the regressions are reproduced here
with small hand-built synthetic sprite files that exercise the exact same
structural pattern instead.

Syntax:  python3 tools/test_riscos_sprite.py
Output:  prints each test as it runs, a final pass/fail summary, and
         exits non-zero if any check failed.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from riscos_sprite import (  # noqa: E402
    SPRITE_CB_FIXED_SIZE, mode_to_bpp, read_sprite_file, sprite_to_image,
    write_sprite_file,
)

try:
    from PIL import Image
except ImportError:
    sys.exit("This test needs Pillow: pip install Pillow")

checks_run = 0
checks_failed = 0
tests_run = 0


def check(cond, msg):
    global checks_run, checks_failed
    checks_run += 1
    if not cond:
        checks_failed += 1
        print(f"  FAIL: {msg}")


def run(fn):
    global tests_run
    tests_run += 1
    print(f"- {fn.__name__}")
    fn()


def build_raw_sprite_file(name, width_words_m1, height_lines_m1, first_bit,
                           last_bit, mode, palette_entries, image_bytes,
                           mask_bytes=b""):
    """
    Function: build_raw_sprite_file (test helper)
    Summary: Hand-assemble a single-sprite sprite file byte-for-byte from
             its raw fields, bypassing write_sprite_file() entirely --
             used to reproduce the exact real-world byte patterns
             (a palette-less 8bpp sprite, a non-standard palette size)
             that read_sprite_file() must handle correctly, without
             depending on any external reference file being present.
    """
    has_mask = bool(mask_bytes)
    image_off = SPRITE_CB_FIXED_SIZE + len(palette_entries) * 8
    mask_off = image_off + len(image_bytes) if has_mask else image_off
    cb = struct.pack("<I", 0)  # next_offset filled in once total size known
    cb += name.encode("ascii").ljust(12, b"\x00")
    cb += struct.pack("<7I", width_words_m1, height_lines_m1, first_bit,
                       last_bit, image_off, mask_off, mode)
    for (r, g, b) in palette_entries:
        word = (b << 24) | (g << 16) | (r << 8)
        cb += struct.pack("<II", word, word)
    cb += image_bytes
    if has_mask:
        cb += mask_bytes
    header = struct.pack("<3I", 1, 12 + 4, 12 + len(cb) + 4)
    return header + cb


def with_tempfile(data, fn):
    import tempfile
    with tempfile.NamedTemporaryFile(suffix=".spr", delete=False) as f:
        f.write(data)
        path = f.name
    try:
        return fn(path)
    finally:
        Path(path).unlink()


def test_new_vga_square_pixel_modes_recognised():
    """Round trip's find: ro-chess's real Sprites,ff9 uses mode 27 for its
    masked/icon sprites (16-colour, 640x480/1280x960 OS units -- the
    genuinely square-pixel VGA family, PRM Table B) -- mode_to_bpp() used
    to raise ValueError for it."""
    check(mode_to_bpp(25) == 1, "mode 25 should be 1bpp")
    check(mode_to_bpp(26) == 2, "mode 26 should be 2bpp")
    check(mode_to_bpp(27) == 4, "mode 27 should be 4bpp")
    check(mode_to_bpp(28) == 8, "mode 28 should be 8bpp")


def test_palette_less_8bpp_sprite_does_not_crash():
    """Round trip's find: real-world 8bpp sprites very often have NO
    embedded palette at all (image_off immediately follows the fixed
    44-byte header) -- confirmed against several sprites in Steve
    Fryatt's own wimp-prog tutorial example downloads (WindowSpriteArea.
    zip, AppSprite.zip). The old code assumed a full 256-entry palette
    unconditionally for any 8bpp sprite, which either ran past the end
    of the file (a crash) or silently misread a *different* sprite's
    image bytes as bogus palette colours for files with enough trailing
    data to survive the over-read."""
    width, height = 4, 2  # exactly one word wide at 8bpp (4 px/word)
    image_bytes = bytes([1, 2, 3, 4] * height)
    data = build_raw_sprite_file(
        "noplt", width_words_m1=0, height_lines_m1=height - 1,
        first_bit=0, last_bit=31, mode=28,  # 8bpp, no palette
        palette_entries=[], image_bytes=image_bytes)

    def check_it(path):
        sprites = read_sprite_file(path)
        check(len(sprites) == 1, "expected exactly one sprite")
        s = sprites[0]
        check(s["bpp"] == 8, f"expected 8bpp, got {s['bpp']}")
        check(s["width_px"] == width, f"expected width {width}, got {s['width_px']}")
        check(s["height"] == height, f"expected height {height}, got {s['height']}")
        check(s["palette"] == [], f"expected an empty palette, got {len(s['palette'])} entries")
        # Must not raise, and every pixel falls back to a defined colour
        # (black) rather than reading garbage -- this is the actual
        # regression check: it used to crash constructing this dict.
        img = sprite_to_image(s)
        check(img.size == (width, height), "decoded image size mismatch")

    with_tempfile(data, check_it)


def test_nonstandard_palette_size_read_correctly():
    """A 4bpp sprite with fewer than 16 palette entries (also legal per
    the PRM -- palette size is "optional"/whatever fits before
    image_off, not fixed at 1<<bpp) must be read with exactly the
    entries present, not over-read into whatever follows in the file."""
    palette = [(10, 20, 30), (40, 50, 60)]  # only 2 entries, not 16
    width, height = 8, 1  # exactly one word wide at 4bpp (8 px/word)
    # Each nibble is one pixel (low nibble = leftmost) -- 0x11 packs two
    # index-1 pixels per byte, so every one of the 8 pixels is index 1.
    image_bytes = bytes([0x11, 0x11, 0x11, 0x11])  # all pixels = index 1

    data = build_raw_sprite_file(
        "small_pal", width_words_m1=0, height_lines_m1=height - 1,
        first_bit=0, last_bit=31, mode=27,  # 4bpp
        palette_entries=palette, image_bytes=image_bytes)

    def check_it(path):
        s = read_sprite_file(path)[0]
        check(s["palette"] == palette,
              f"expected exactly the 2 stored palette entries, got {s['palette']}")
        img = sprite_to_image(s)
        check(img.getpixel((0, 0)) == (40, 50, 60, 255),
              f"pixel should resolve to palette index 1's colour, got {img.getpixel((0, 0))}")

    with_tempfile(data, check_it)


def test_roundtrip_nonword_aligned_widths():
    """write_sprite_file() -> read_sprite_file() -> sprite_to_image() must
    reproduce a source image pixel-for-pixel, including widths that
    don't divide evenly into a word -- exactly the case (ro-chess's
    real 58x13 and 34x40 sprites, both 4bpp) checked by hand against
    real reference files this session; reproduced synthetically here so
    it runs without those external files present."""
    for bpp, mode, width, height in [(4, 27, 5, 3), (8, 28, 5, 3),
                                      (1, 25, 9, 4), (2, 26, 7, 5)]:
        # A small distinctive RGBA pattern -- every pixel a different
        # flat colour so a transposition/shift bug would change at
        # least one pixel's decoded colour.
        img = Image.new("RGBA", (width, height))
        for y in range(height):
            for x in range(width):
                v = (x * 37 + y * 91) % 200
                img.putpixel((x, y), (v, 255 - v, (v * 3) % 256, 255))

        def check_it(path, img=img, bpp=bpp, mode=mode, width=width, height=height):
            write_sprite_file(path, "rt", img, bpp, mode, mask_alpha_threshold=None)
            s = read_sprite_file(path)[0]
            check(s["width_px"] == width,
                  f"{bpp}bpp {width}x{height}: width mismatch, got {s['width_px']}")
            check(s["height"] == height,
                  f"{bpp}bpp {width}x{height}: height mismatch, got {s['height']}")
            decoded = sprite_to_image(s)
            n_colours = 1 << bpp
            quantised_src = img.convert("RGB").quantize(colors=n_colours, method=Image.MEDIANCUT).convert("RGB")
            mismatches = sum(
                1 for y in range(height) for x in range(width)
                if decoded.getpixel((x, y))[:3] != quantised_src.getpixel((x, y))
            )
            check(mismatches == 0,
                  f"{bpp}bpp {width}x{height}: {mismatches}/{width*height} pixels "
                  f"differ from the (quantised) source after round-trip")

        with_tempfile(b"", lambda path, f=check_it: f(path))


def main():
    run(test_new_vga_square_pixel_modes_recognised)
    run(test_palette_less_8bpp_sprite_does_not_crash)
    run(test_nonstandard_palette_size_read_correctly)
    run(test_roundtrip_nonword_aligned_widths)

    print(f"\n{checks_run - checks_failed}/{checks_run} checks passed "
          f"({tests_run} test{'s' if tests_run != 1 else ''})")
    return 1 if checks_failed else 0


if __name__ == "__main__":
    sys.exit(main())
