#!/usr/bin/env python3
"""
ArchiLudo ADFS disc image builder
===================================

Round 7.91. Writes a single-file ADFS "D" format (800KB, old map, New
Directory) disc image from scratch -- no third-party disc-image tool
dependency, matching this project's other from-scratch RISC OS format
tools (tools/prepare_pibridge_deploy.py's .inf sidecars,
tools/riscos_readme.py).

Format choice: "D" (800KB) rather than "L" (640KB) or "E"/"F" -- the
built release zip (~654KB) does not fit in L's 655,360-byte capacity,
but comfortably fits D's 819,200 bytes. D uses ADFS's OLD map (the
simpler of the two map formats -- no zone/fragment bookkeeping) with
"New Directory" entries (77 slots, vs. Old Directory's 47) -- introduced
in Arthur, long predating and fully compatible with this project's
RISC OS 3.10 target. Unlike ADFS "L", the old map's default interleave
compensation (DiscAddrToIntOffset in DiscImage_Private.pas) only applies
to format &02 (L) specifically -- D uses a plain linear byte layout,
confirmed from source, which is what makes writing this format directly
tractable without porting sector-interleave math too.

Ground truth for every byte offset, field width, and checksum algorithm
below: Gerald Holdsworth's DiscImageManager (GPL-3.0,
https://github.com/geraldholdsworth/DiscImageManager), specifically
LazarusSource/DiscImage_ADFS.pas's FormatOldMapADFS, CreateADFSDirectory,
UpdateADFSCat, CalculateADFSDirCheck, ADFSAllocateFreeSpace, and
DiscImage_Private.pas's ByteChecksum/ROR13 -- read directly rather than
worked out from the PRM's prose, the same "read the real implementation"
approach this project used for the PiFS .inf format and the Info-Zip
-, flag (see docs/ARCHITECTURE.md's rounds 7.87/7.89). This is a
from-scratch Python port of that logic (no DiscImageManager code is
copied), released under this project's own GPLv3 licence as permitted
by DiscImageManager's own GPL-3.0 terms.

Disc layout (byte offsets, decimal comments in hex for PRM cross-
reference):
    0x000-0x0FF  Free space map, sector 0: FreeStart (3B) + disc title
                 (10 chars, even-indexed) + disc size (3B) + checksum
    0x100-0x1FF  Free space map, sector 1: FreeLen (3B) + disc title
                 (odd-indexed) + disc ID (3B) + free-entry count (1B)
                 + checksum
    0x400-0xBFF  Root directory (New Directory format, 2048 bytes):
                 header (StartSeq + "Nick"), 77x26-byte catalogue
                 entries, tail (parent sector, title, own name, EndSeq,
                 "Nick", directory check byte)
    0xC00-EOF    File data (only one file: the payload, sector-aligned)
"""

import struct
import sys
import time
from pathlib import Path

TOTAL_SIZE = 800 * 1024      # ADFS "D" format capacity
SECSIZE = 1024                # New Directory / D-format sector alignment unit
ROOT = 0x400                  # Root directory's byte offset
ROOT_SIZE = 0x800             # 2048 bytes -- fixed size of a New Directory
ENTRIES_OFFSET = 0x05         # First catalogue entry, relative to directory start
ENTRY_SIZE = 0x1A             # 26 bytes per catalogue entry
NUM_ENTRIES = 77              # New Directory has 77 entry slots (Old Directory: 47)
TAIL = ROOT_SIZE - 0x29       # Directory tail starts here (0x7D7 = 2007)
DISC_ID = 0x4077              # Arbitrary but fixed volume identifier (no correctness
                               # dependency on its value -- matches DiscImageManager's
                               # own reference format-time example)
RISCOS_EPOCH_OFFSET_SECONDS = 2208988800
"""Seconds between the RISC OS epoch (00:00:00 01-Jan-1900) and the Unix
epoch -- same constant as tools/prepare_pibridge_deploy.py."""


def riscos_load_exec(filetype: int, when: float) -> tuple[int, int]:
    """Same "stamped" load/exec formula as prepare_pibridge_deploy.py's
    function of the same name -- duplicated rather than imported to keep
    each tool in tools/ independently runnable."""
    centiseconds = int((when + RISCOS_EPOCH_OFFSET_SECONDS) * 100)
    top_byte = (centiseconds >> 32) & 0xFF
    low32 = centiseconds & 0xFFFFFFFF
    load = 0xFFF00000 | ((filetype & 0xFFF) << 8) | top_byte
    return load, low32


def byte_checksum(data: bytes, offset: int, size: int) -> int:
    """Port of DiscImage_Private.pas's ByteChecksum for the OLD map case
    (newmap=False): a rotating byte-sum over [offset, offset+size-2],
    seeded at 0xFF, skipping the final byte (which holds the checksum
    itself)."""
    acc = 0xFF
    for pointer in range(size - 2, -1, -1):
        carry = acc // 0x100
        acc &= 0xFF
        acc += data[offset + pointer] + carry
    return acc & 0xFF


def ror13(v: int) -> int:
    """Port of DiscImage_Private.pas's ROR13: rotate a 32-bit value right
    by 13 bits."""
    v &= 0xFFFFFFFF
    return ((v >> 13) | (v << 19)) & 0xFFFFFFFF


def directory_check(data: bytes, sector: int) -> int:
    """Port of DiscImage_ADFS.pas's CalculateADFSDirCheck for New
    Directory (dirsize=2048, tail=0x7D7): a rolling XOR-with-ROR13
    checksum over the whole directory except its own final byte."""
    dirsize = ROOT_SIZE
    tail = TAIL
    num_entries = 0
    while data[sector + 0x05 + num_entries * ENTRY_SIZE] != 0:
        num_entries += 1
    end_of_check = num_entries * ENTRY_SIZE + 0x05

    dircheck = 0
    amt = 0
    # Stage 1: whole words up to end_of_check
    while amt + 3 < end_of_check:
        word = struct.unpack_from("<I", data, sector + amt)[0]
        dircheck = word ^ ror13(dircheck)
        amt += 4
    # Stage 2: remaining bytes (<4) individually
    while amt < end_of_check:
        dircheck = data[sector + amt] ^ ror13(dircheck)
        amt += 1
    # Stage 3: skip the first byte of the tail (Old/New Directory only)
    amt = tail + 1
    # Stage 4: whole words in the tail, excluding the final word (checksum)
    while amt + 3 < dirsize - 4:
        word = struct.unpack_from("<I", data, sector + amt)[0]
        dircheck = word ^ ror13(dircheck)
        amt += 4
    # Stage 5: XOR the four bytes of the accumulated word together
    return (
        (dircheck & 0xFF)
        ^ ((dircheck >> 24) & 0xFF)
        ^ ((dircheck >> 16) & 0xFF)
        ^ ((dircheck >> 8) & 0xFF)
    )


def sector_align(length: int) -> int:
    """ADFSSectorAlignLength(length, bpmbalign=False) for old map: round
    up to a multiple of SECSIZE."""
    return -(-length // SECSIZE) * SECSIZE


def write_disc_title(data: bytearray, title: str) -> None:
    """FormatOldMapADFS's disc title encoding: 10 characters, split
    across the two map sectors at alternating byte positions -- even-
    indexed characters in sector 0 starting at 0x0F7, odd-indexed in
    sector 1 starting at 0x1F6. A real, documented old-map ADFS quirk,
    not a simplification."""
    padded = title[:10].ljust(10, "\x00")
    for t, ch in enumerate(padded):
        if t % 2 == 0:
            data[0x0F7 + t // 2] = ord(ch)
        else:
            data[0x1F6 + t // 2] = ord(ch)


def write_free_space_map(data: bytearray, free_start: int, free_len: int) -> None:
    """FormatOldMapADFS's/ADFSAllocateFreeSpace's free-space-map fields
    (both expressed in 256-byte units), plus the two boot-sector
    checksums. Assumes the single-fragment case (one file, or a blank
    disc) -- the free-entry count at 0x1FE stays 3 (one 3-byte entry)."""
    data[0x000:0x003] = free_start.to_bytes(3, "little")
    data[0x0FC:0x0FF] = (TOTAL_SIZE // 0x100).to_bytes(3, "little")
    data[0x0FF] = byte_checksum(bytes(data), 0x000, 0x100)

    data[0x100:0x103] = free_len.to_bytes(3, "little")
    data[0x1FB:0x1FE] = DISC_ID.to_bytes(3, "little")
    data[0x1FE] = 0x03  # one free-space entry (3 bytes) -- see module doc
    data[0x1FF] = byte_checksum(bytes(data), 0x100, 0x100)


def write_root_directory(
    data: bytearray,
    filename: str,
    filetype: int,
    file_offset: int,
    file_length: int,
    when: float,
) -> None:
    """CreateADFSDirectory's root-creation layout plus a single
    UpdateADFSCat-style catalogue entry, combined into one pass since
    this disc only ever has the one file."""
    dirbuf = bytearray(ROOT_SIZE)

    # Header: StartSeq (0, a fresh directory) + "Nick" (New Directory ID)
    dirbuf[0] = 0x00
    dirbuf[1:5] = b"Nick"

    # Single catalogue entry, slot 0
    name_cr = (filename + "\r").encode("ascii")
    entry_off = ENTRIES_OFFSET
    dirbuf[entry_off : entry_off + len(name_cr)] = name_cr
    load, exec_ = riscos_load_exec(filetype, when)
    struct.pack_into("<I", dirbuf, entry_off + 0x0A, load)
    struct.pack_into("<I", dirbuf, entry_off + 0x0E, exec_)
    struct.pack_into("<I", dirbuf, entry_off + 0x12, file_length)
    dirbuf[entry_off + 0x16 : entry_off + 0x19] = (file_offset // 0x100).to_bytes(
        3, "little"
    )
    # Attributes: R(0x01) + W(0x02) + public-r(0x10) = 0x13, "WR/r" --
    # same convention as prepare_pibridge_deploy.py's DEFAULT_PERM.
    dirbuf[entry_off + 0x19] = 0x13

    # Tail: parent sector (root is its own parent), title, own name,
    # EndSeq, "Nick", directory check byte.
    root_sector = ROOT // 0x100
    dirbuf[TAIL + 0x03 : TAIL + 0x06] = root_sector.to_bytes(3, "little")
    title = ("$\r").encode("ascii").ljust(19, b"\x00")
    dirbuf[TAIL + 0x06 : TAIL + 0x06 + 19] = title[:19]
    dirbuf[TAIL + 0x23] = 0x00  # EndSeq, matches StartSeq
    dirbuf[TAIL + 0x24 : TAIL + 0x28] = b"Nick"

    data[ROOT : ROOT + ROOT_SIZE] = dirbuf
    data[ROOT + ROOT_SIZE - 1] = directory_check(bytes(data), ROOT)


def build(src: Path, dest: Path, disc_title: str, riscos_filename: str, filetype: int) -> None:
    if len(riscos_filename) > 10:
        raise ValueError(
            f"'{riscos_filename}' is {len(riscos_filename)} characters -- "
            f"New Directory filenames (including the CR terminator) are "
            f"limited to 10 bytes, so 9 characters maximum"
        )

    payload = src.read_bytes()
    aligned_len = sector_align(len(payload))
    file_offset = ROOT + ROOT_SIZE
    if file_offset + aligned_len > TOTAL_SIZE:
        raise ValueError(
            f"{src}: {len(payload)} bytes (sector-aligned to {aligned_len}) "
            f"does not fit an ADFS D-format disc ({TOTAL_SIZE} bytes total, "
            f"{TOTAL_SIZE - file_offset} available for file data)"
        )

    data = bytearray(TOTAL_SIZE)
    when = time.time()

    write_root_directory(data, riscos_filename, filetype, file_offset, len(payload), when)

    free_start = (file_offset + aligned_len) // 0x100
    free_len = (TOTAL_SIZE - file_offset - aligned_len) // 0x100
    write_disc_title(data, disc_title)
    write_free_space_map(data, free_start, free_len)

    data[file_offset : file_offset + len(payload)] = payload

    dest.write_bytes(data)
    print(
        f"Wrote {dest}: ADFS D (800KB), disc title '{disc_title}', "
        f"'{riscos_filename}' filetype &{filetype:03X}, "
        f"{len(payload)} bytes at offset 0x{file_offset:X}"
    )


def main() -> None:
    if len(sys.argv) != 6:
        print(
            "Usage: build_adfs_disk.py <src_file> <dest.adf> <disc_title> "
            "<riscos_filename> <filetype_hex>",
            file=sys.stderr,
        )
        sys.exit(1)
    src, dest, disc_title, riscos_filename, filetype_hex = sys.argv[1:6]
    build(Path(src), Path(dest), disc_title, riscos_filename, int(filetype_hex, 16))


if __name__ == "__main__":
    main()
