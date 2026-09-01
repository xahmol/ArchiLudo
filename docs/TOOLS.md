# Tools manual

Every standalone host-side (Python) command-line tool in this project's
`tools/` and `assets/` directories -- what each one does, exact usage,
requirements, and gotchas, so any of them can be run directly rather
than only via `make`. None of these run on RISC OS; they all prepare
things for it on the PC/Linux side. For the *why* behind the sprite
format and rendering choices these tools serve, see
[ARCHITECTURE.md](ARCHITECTURE.md)'s "Board and game rendering"
section; for how each fits into the overall build/`make` pipeline, see
[BUILDCHAIN.md](BUILDCHAIN.md).

## Contents

- [`tools/riscos_sprite.py` -- PNG <-> RISC OS Sprite converter](#toolsriscos_spritepy----png---risc-os-sprite-converter)
- [`tools/test_riscos_sprite.py` -- regression tests](#toolstest_riscos_spritepy----regression-tests)
- [`assets/generate_icon_sprites.py` -- pawn sprite generator](#assetsgenerate_icon_spritespy----pawn-sprite-generator)
- [`assets/generate_app_icon.py` -- app icon generator](#assetsgenerate_app_iconpy----app-icon-generator)
- [Editing sprites by hand](#editing-sprites-by-hand)
- [`tools/mod_embed_sfx.py` -- SFX-into-MOD embedder](#toolsmod_embed_sfxpy----sfx-into-mod-embedder)
- [`tools/build_adfs_disk.py` -- ADFS disc-image writer](#toolsbuild_adfs_diskpy----adfs-disc-image-writer)
- [`tools/riscos_readme.py` -- plain-text README generator](#toolsriscos_readmepy----plain-text-readme-generator)
- [`tools/prepare_pibridge_deploy.py` -- PiEconetBridge deploy stager](#toolsprepare_pibridge_deploypy----piecconetbridge-deploy-stager)
- [Experimental / not part of the pipeline](#experimental--not-part-of-the-pipeline)

## `tools/riscos_sprite.py` -- PNG <-> RISC OS Sprite converter

**Why it exists**: no suitable existing tool converts PNG to RISC OS
old-style (<=3.1) Sprite files on the PC/Linux side. RISC OS's own
`ConvertPNG` module (OSLib binds it) is a native RISC OS SWI authored
in 2002, long after RISC OS 3.10 (1992) -- PNG didn't exist yet when
3.10 shipped, and the module isn't present on real RISC OS 3.10 even
if it were runnable from the PC side, which it isn't (a module, not a
standalone host tool). No PC-native PNG-to-RISC-OS-sprite converter
turned up in a search of the Acorn/RISC OS open-source ecosystem
either. So: a from-scratch CLI, built and self-tested against the
actual byte layout rather than against prose alone -- see
[ARCHITECTURE.md](ARCHITECTURE.md)'s "Sprite file format" and "How the
format was verified" sections.

**Requires**: Python 3 + Pillow (`pip install Pillow`).

```
tools/riscos_sprite.py info <spritefile>
tools/riscos_sprite.py to-png <spritefile> <sprite-name> <output.png>
tools/riscos_sprite.py from-png <input.png> <output-spritefile> --name NAME
                        [--bpp {1,2,4,8}] [--mode N] [--mask-alpha-threshold N]
                        [--wimp-palette]
tools/riscos_sprite.py pack <output-spritefile> <input-spritefile>...
```

- `info` / `to-png`: inspect and preview an existing sprite file.
- `from-png`: quantises the PNG to a palette at the given bit depth and
  writes a single-sprite RISC OS sprite file. The alpha channel becomes
  the sprite's transparency mask (`--mask-alpha-threshold`, default
  128; pass a negative number to omit the mask). `--bpp` defaults to 4
  (16 colours) unless `--mode` implies a different one; pass `--bpp 8`
  for 256-colour assets. `--mode` overrides the bpp-implied default
  mode (`MODES_BY_BPP`) -- pass `--mode 27` for any new square-drawn
  sprite art (see [ARCHITECTURE.md](ARCHITECTURE.md)'s "Current
  rendering approach" section for why). `--wimp-palette` quantises
  against the fixed 16 standard Wimp colours instead of an adaptive
  per-sprite palette -- required for any sprite meant to be plotted via
  `Wimp_PlotIcon` as an indirected icon, since the Wimp auto-translates
  a 1/2/4bpp indirected sprite's colour indices onto those fixed 16
  regardless of what the sprite's own embedded palette says.
- `pack`: concatenates several single-sprite files into one multi-sprite
  sprite area/file, matching how a real `!Sprites` file holds many
  named icons together. Each input must itself hold exactly one sprite
  (i.e. `from-png` output, not an arbitrary multi-sprite file).

Sprite names longer than 12 ASCII characters are rejected. Pure
function of its inputs -- safe to re-run any time, no hidden state.

## `tools/test_riscos_sprite.py` -- regression tests

Self-contained regression suite for `riscos_sprite.py`, locking in two
real bugs found via hand-validation against genuine RISC OS sprite
files (a palette-less 8bpp sprite crash; a non-standard, smaller-than-
`1<<bpp` palette size being over-read) plus a general non-word-aligned-
width round-trip check. Not asset-pipeline tooling as such -- a
dev-facing test, but user-runnable standalone and not wired into any
`make` target.

```
python3 tools/test_riscos_sprite.py
```

Requires Python 3 + Pillow. No arguments, no external files needed --
builds small synthetic sprite files in a temp directory per test and
cleans up after itself. Prints each test as it runs, then a final
`N/M checks passed` summary; exits non-zero on any failure. Extend this
when a new format edge case is found rather than only checking it by
hand.

## `assets/generate_icon_sprites.py` -- pawn sprite generator

Generates ArchiLudo's actual shipped pawn sprites -- an original
chess-pawn-style silhouette (round head/finial, thin neck collar,
tapered body, flared two-level base), drawn square and tagged mode 27
so `Wimp_PlotIcon` handles aspect-scaling automatically across all 4
of ArchiLudo's supported screen modes. See
[ARCHITECTURE.md](ARCHITECTURE.md)'s "Current rendering approach" and
"Lasting gotchas for hand-drawn pixel art" sections for the full
rendering/anti-aliasing reasoning this script implements.

```
python3 assets/generate_icon_sprites.py
```

Requires Python 3 + Pillow; shells out to `tools/riscos_sprite.py`.
Invoked by `make assets`. Writes `assets/pawn_icon0.png` ..
`assets/pawn_icon3.png` (per player, kept for reference/regeneration)
and `assets/PawnSprite` (the shipped, packed sprite file). Safe/
idempotent to re-run -- always regenerates from its own hardcoded
design, overwriting prior output. **Note**: this overwrites the same
canonical native PNGs the hand-editing workflow below also writes to --
re-running this after hand-editing discards those edits (start over
deliberately, not a footgun -- see "Editing sprites by hand" below).

## `assets/generate_app_icon.py` -- app icon generator

Generates the `!ArchiLudo` application directory's Filer/iconbar icon
(a red pawn beside a die) as two sprite files -- a square-pixel
`!Sprites22` and rectangular-pixel `!Sprites`, per Steve Fryatt's
`wimp-prog` tutorial's own app-icon convention (see
[BUILDCHAIN.md](BUILDCHAIN.md)'s "Application directory" section).

```
python3 assets/generate_app_icon.py
```

Requires Python 3 + Pillow; shells out to `tools/riscos_sprite.py`.
Invoked by `make assets`. Writes `assets/app_icon_full.png`,
`assets/app_icon_half.png` (square-pixel reference renders),
`assets/app_icon_full_rect.png`, `assets/app_icon_half_rect.png`
(rectangular-pixel reference renders), and the shipped packed files
`assets/!Sprites`/`assets/!Sprites22`. Same idempotency and
overwrite-on-rerun behaviour as `generate_icon_sprites.py` above.

## Editing sprites by hand

`make export-sprites` (`assets/export_sprites_for_editing.py`) and
`make import-sprites` (`assets/import_edited_sprites.py`) round-trip
the shipped sprites to plain PNGs and back, at native resolution only
-- for touching up the generators' output by hand in a real pixel-art
editor rather than only by tweaking generator code.

```
python3 assets/export_sprites_for_editing.py   # make export-sprites
python3 assets/import_edited_sprites.py        # make import-sprites
```

Both require Python 3 + Pillow; `import_edited_sprites.py` also shells
out to `tools/riscos_sprite.py`. Neither takes arguments.

- **Export** reads `assets/pawn_icon0..3.png` and
  `assets/app_icon_full[_rect].png`/`app_icon_half[_rect].png` (i.e.
  the two generators above must have run at least once already --
  export just warns and skips per-file if a source is missing, rather
  than failing hard) and writes `assets/edit/pawn0..3.png`,
  `assets/edit/archiludo_{full,half}_{sq,rect}.png`, plus a generated
  `assets/edit/README.md` with the full hand-editing workflow
  (recommended editors, why not MS Paint, the exact file-to-sprite
  mapping table).
- **Import** is the reverse: rebuilds the 3 shipped packed sprite
  files (`assets/PawnSprite`, `assets/!Sprites`, `assets/!Sprites22`)
  from whatever's in `assets/edit/`, falling back per-sprite to the
  existing canonical native PNG for anything not touched there -- safe
  to run against a partially-edited `assets/edit/` (untouched sprites
  are rebuilt unchanged, not skipped or reset), and refreshes each
  sprite's own canonical native PNG in place to match whatever was
  actually used, so a later export has the edited result as its new
  baseline. Prints a note (not an error) and just rebuilds from the
  current canonical PNGs if `assets/edit/` doesn't exist at all.

**Recommended editors**: [Piskel](https://www.piskelapp.com/) (free,
purpose-built for pixel art, hard-edged pencil by default) or GIMP
with the Pencil tool and an alpha channel added to the layer.
**Not MS Paint** -- it has no real alpha-channel support at all
(silently flattens transparency to solid white) and no genuinely
hard-edged 1px pencil in every tool mode.

There is deliberately no upscaled "editing copy" of each sprite --
an earlier version of this workflow tried that and it caused edits to
the native-resolution file to be silently ignored (the import step
preferred the stale, unedited upscaled copy sitting next to it).
Editing is native-resolution only, with a **1px hard-edged pencil
tool**, no anti-aliasing, no resize/rotate/filters, no cropping the
canvas, and full (255) or zero alpha only (soft/partial alpha edges
get thresholded at import time regardless, so soft edges never
survive). See `assets/edit/README.md` (regenerated by every export)
for the complete step-by-step workflow once the directory exists.

## `tools/mod_embed_sfx.py` -- SFX-into-MOD embedder

Splices ArchiLudo's 6 bundled one-shot SFX (`assets/audio/Sfx*`, raw
headerless 16-bit signed mono PCM) into empty ProTracker sample slots
of `assets/audio/Music1/2/3`, loudness-normalized (RMS-targeted `tanh`
soft-clip, converted to 8-bit signed PCM) so `QTM_PlaySample` can
trigger them straight from the currently-loaded MOD's own sample table
-- see [QTM.md](QTM.md) for the full playback mechanism.

```
python3 tools/mod_embed_sfx.py
```

Requires Python 3 stdlib only, plus `ffprobe` (from FFmpeg) on `PATH`
for its own structural validation step. Re-splices from
`assets/audio_pristine/Music1/2/3` (the untouched originals) every
run -- never reads its own prior output -- so it's idempotent and safe
to re-run any time an `Sfx*` asset or the slot table changes. Requires
`assets/audio_pristine/` to exist; if it's ever lost, recreate it from
the ModArchive URLs in [`CREDITS.md`](../CREDITS.md) (the script's own
error message points here too). `SFX_SLOTS` (per-track slot
assignment) must be kept in sync **by hand** with `lib/qtm.c`'s own
`sfx_channel[]`/slot table -- there's no shared source of truth
between this Python build tool and the C code. Fails loudly (an
`assert`) if a target slot isn't actually empty in the pristine file.

## `tools/build_adfs_disk.py` -- ADFS disc-image writer

Writes a single-file ADFS "D" format (800KB, old map, New Directory)
disc image from scratch -- no third-party disc-image tool dependency.
Chosen over ADFS "L" (640KB) because the release zip doesn't fit L's
capacity. Ground-truthed against Gerald Holdsworth's DiscImageManager
source (`DiscImage_ADFS.pas`/`DiscImage_Private.pas`) for every byte
offset, field width, and checksum algorithm -- see
[`CREDITS.md`](../CREDITS.md).

```
python3 tools/build_adfs_disk.py <src_file> <dest.adf> <disc_title> <riscos_filename> <filetype_hex>
```

Requires Python 3 stdlib only. `<src_file>` is any single file (in
practice the release zip); `<filetype_hex>` is a bare hex string, no
`&`/`0x` prefix (e.g. `a91` for Zip). Raises if `<riscos_filename>` is
longer than 10 characters (New Directory's own filename limit
including the CR terminator, so 9 usable characters) or if the
sector-aligned payload doesn't fit the disc's available space. Pure
function of its inputs -- safe to re-run, always overwrites the
destination.

## `tools/riscos_readme.py` -- plain-text README generator

Converts `README.md` into a RISC-OS-readable plain-text file. A plain
`pandoc -t plain` produces Markdown tables 170+ characters wide (blows
past RISC OS's non-wrapping paged-text display) and UTF-8 "smart"
typography RISC OS's 8-bit text files can't represent.

```
python3 tools/riscos_readme.py <input.md> <output>
```

Requires Python 3 stdlib + `pandoc` on `PATH`. Flattens Markdown
pipe-tables into stacked "Header: value" records before handing off to
pandoc (rather than passing them through as tables), wraps everything
else to 78 columns, and writes LF line endings -- not the textbook
RISC OS CR-only convention, deliberately: confirmed via direct byte
inspection against real working RISC OS text files that LF is what
this project's actual target setup expects (see
[ARCHITECTURE.md](ARCHITECTURE.md)'s "Decisions made and not
revisited" section). Output is pure ASCII -- raises `ValueError`
listing any character it can't transliterate (see its own
`ASCII_TRANSLITERATIONS` table for what's handled: en/em dash, curly
quotes, ellipsis, nbsp); this is what has caught stray non-ASCII
characters slipping into README edits before.

## `tools/prepare_pibridge_deploy.py` -- PiEconetBridge deploy stager

Stages a built app directory for deployment to a real PiEconetBridge --
converts each `,xxx`-filetype-suffixed file into a plain filename plus
a `.inf` sidecar (PiFS's own filetype convention; it does NOT
understand the `,xxx` suffix Arculator's hostfs uses). See
[BUILDCHAIN.md](BUILDCHAIN.md)'s `deploy-pibridge` target description
for the full deploy pipeline this feeds into.

```
python3 tools/prepare_pibridge_deploy.py <src_appdir> <dest_stage_dir>
```

Requires Python 3 stdlib only. Reads every `NAME,xxx`-suffixed file
directly inside `<src_appdir>` (non-recursive). For each, writes
`<dest_stage_dir>/NAME` (a copy) and `<dest_stage_dir>/NAME.inf` (one
line: `owner load exec perm homeof`, lowercase hex). **Destroys and
recreates `<dest_stage_dir>` from scratch** (removes it first if it
already exists) -- do not point this at a directory with anything else
in it. Raises if any filename (excluding the `,xxx` suffix) exceeds 10
characters -- PiFS's default filename limit -- rather than silently
producing something that would fail to open on the real fileserver.

## `tools/gen_social_preview.py` -- GitHub social preview image generator

Composites `screenshots/social-preview.png` (1280x640): a bordered
game screenshot on the left, the idi8b logo, project name, version,
short description and repo URL on the right. Matches the layout
established across the author's other recent projects
(locifilemanager-v2, OricScreenEditorLOCI, oricdemo2026,
heartbeat-demo, vdcmaniac) -- the logo is reused directly from
locifilemanager-v2's own generated preview rather than redrawn.

```
python3 tools/gen_social_preview.py
```

Requires Python 3 + Pillow (same dependency as `tools/riscos_sprite.py`
-- see its own entry above) and, hardcoded in the script, a local
checkout of `locifilemanager-v2` at
`/home/xahmol/git/locifilemanager-v2` (logo source) plus the Ubuntu
Mono variable font at
`/usr/share/fonts/truetype/ubuntu/UbuntuMono[wght].ttf` -- this is a
one-off authoring tool for the maintainer's own machine, not part of
the build pipeline, so neither path is parameterised. Output is
committed directly (`screenshots/social-preview.png`); GitHub's own
Settings -> Social preview has no API, so uploading the generated
image there is always a manual step.

## Experimental / not part of the pipeline

`assets/experiments/gradient_preview.py` is a one-off, exploratory
preview script (smooth radial-gradient pawn shading, evaluated and
not adopted) kept only so a past design exploration survives a session
restart -- not wired into `make` anywhere, not a recommended tool for
general use, and not built or documented like the tools above. See its
own module docstring if it's ever revisited.
