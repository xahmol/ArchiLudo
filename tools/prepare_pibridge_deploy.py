#!/usr/bin/env python3
"""
ArchiLudo PiEconetBridge deploy staging tool
=============================================

Round 7.87. PiEconetBridge's fileserver (PiFS) does NOT use the ",xxx"
hex-suffix filetype convention Arculator's hostfs and this project's own
build output use -- confirmed by reading PiFS's actual source
(cr12925/PiEconetBridge, utilities/fs.c) after a live deploy showed
filetypes weren't preserved over rsync. PiFS instead expects one of:

  1. Linux extended attributes (user.econet_owner/_load/_exec/_perm/
     _homeof) on each file -- the default mode, but fragile to rely on
     for a deploy pipeline: it requires the LOCAL filesystem (WSL, in
     this project's case) to support xattrs, `rsync -X` to carry them
     over, and the remote filesystem PiFS itself sits on to support them
     too -- several links in a chain that could each silently drop the
     data with no error.
  2. A classic Acorn ".inf" sidecar file per stored file (e.g. `!Boot`
     alongside `!Boot.inf`), a plain text format PiFS falls back to
     per-file whenever one exists (utilities/fs.c's fs_read_xattr(),
     dotexists check) -- this doesn't depend on any filesystem feature
     at all, just ordinary files, so it survives rsync/scp/sftp/tar/
     anything without special handling. Used here for exactly that
     reason.

.inf format (confirmed from fs_write_attr_to_file()/fs_read_attr_from_file()
in utilities/fs.c): one line, space-separated lowercase hex, no leading
zero-padding beyond what %hx/%lx naturally produce:

    <owner> <load> <exec> <perm> [<homeof>]

`load`/`exec` encode the RISC OS filetype and timestamp using the
standard "stamped" scheme every RISC OS cross-dev tool uses: the top 12
bits of `load` are 0xFFF (marks this as a typed/stamped file, not a raw
load address), the next 12 bits are the filetype, and the remaining 8
bits of `load` plus all 32 bits of `exec` are a 40-bit centisecond count
since the RISC OS epoch (00:00:00 01-Jan-1900).

Reads each file's own filetype from its build-time ",xxx" suffix (see
project/game_view.h's `sfx_leafname[]`-style fixed conventions for
reference) rather than hardcoding a fixed set, so any future asset added
to the build just works without touching this script.
"""

import shutil
import sys
import time
from pathlib import Path

RISCOS_EPOCH_OFFSET_SECONDS = 2208988800
"""Seconds between the RISC OS epoch (00:00:00 01-Jan-1900) and the Unix
epoch (00:00:00 01-Jan-1970) -- the standard constant every RISC OS
cross-dev tool uses for this conversion (not counting leap seconds, per
RISC OS's own convention)."""

DEFAULT_OWNER = 0
DEFAULT_HOMEOF = 0
DEFAULT_PERM = 0x13
"""Owner read+write (0x01|0x02), public read (0x10) -- "WR/r" in Acorn
notation, the standard default for a shipped application's own files
(not locked, not public-writable). See utilities/fs.c's FS_PERM_OWN_R/
_OWN_W/_OTH_R/_OTH_W bit definitions in the PiEconetBridge source."""


def riscos_load_exec(filetype: int, when: float) -> tuple[int, int]:
    """Return (load, exec) for a "stamped" RISC OS file of the given
    filetype (0-0xFFF) and Unix timestamp `when`."""
    centiseconds = int((when + RISCOS_EPOCH_OFFSET_SECONDS) * 100)
    top_byte = (centiseconds >> 32) & 0xFF
    low32 = centiseconds & 0xFFFFFFFF
    load = 0xFFF00000 | ((filetype & 0xFFF) << 8) | top_byte
    return load, low32


def stage_file(src: Path, dest_dir: Path, when: float) -> None:
    """Copy one ",xxx"-suffixed build file into dest_dir as a plain-named
    file plus its matching .inf sidecar."""
    name, _, filetype_hex = src.name.rpartition(",")
    if not name or not filetype_hex:
        raise ValueError(f"{src.name}: expected a NAME,xxx filetype-suffixed "
                          f"filename (see app/!Run's own RMEnsure lines for "
                          f"the filetypes this project's build produces)")
    filetype = int(filetype_hex, 16)

    dest = dest_dir / name
    shutil.copy2(src, dest)

    load, exec_ = riscos_load_exec(filetype, when)
    inf_path = dest_dir / f"{name}.inf"
    inf_path.write_text(
        f"{DEFAULT_OWNER:x} {load:x} {exec_:x} {DEFAULT_PERM:x} {DEFAULT_HOMEOF:x}\n"
    )
    print(f"{name}: filetype &{filetype:03X} -> load=&{load:08X} exec=&{exec_:08X}")


def main() -> None:
    if len(sys.argv) != 3:
        print("Usage: prepare_pibridge_deploy.py <src_appdir> <dest_stage_dir>",
              file=sys.stderr)
        sys.exit(1)

    src_dir = Path(sys.argv[1])
    dest_dir = Path(sys.argv[2])

    if not src_dir.is_dir():
        print(f"error: {src_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    if dest_dir.exists():
        shutil.rmtree(dest_dir)
    dest_dir.mkdir(parents=True)

    when = time.time()
    staged = 0
    for src in sorted(src_dir.iterdir()):
        if not src.is_file():
            continue
        stage_file(src, dest_dir, when)
        staged += 1

    print(f"Staged {staged} file(s) for PiFS (plain names + .inf sidecars) "
          f"in {dest_dir}")


if __name__ == "__main__":
    main()
