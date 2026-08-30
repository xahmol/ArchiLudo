#!/usr/bin/env python3
"""
ArchiLudo MOD SFX embedding tool
=================================

Round 7.76 (see docs/QTM.md's "Round 7.74" section for why this exists:
QTM_PlayRawSample never worked after 14 rounds of investigation, and
every real Archimedes codebase checked embeds one-shot effects as MOD
instrument samples instead of playing them from a raw pointer).

Splices ArchiLudo's 6 bundled SFX (assets/audio/Sfx*, raw headerless
16-bit signed mono PCM at 11025Hz) into empty ProTracker sample slots of
assets/audio/Music1/Music2/Music3, converting to 8-bit signed PCM (the
native ProTracker sample format -- unrelated to the VIDC-log format
QTM_PlayRawSample needed, see docs/QTM.md's "Sample format" section).
QTM's own sample player reads a MOD's sample data directly, so no
Sound_SoundLog-style runtime conversion is needed for this path.

Each of Music1/Music2/Music3 is a real ripped tracker module with its
own artist-authored sample table (see docs/ARCHITECTURE.md/CREDITS.md);
ProTracker's format hard-caps a module at 31 sample slots, and how many
are actually free varies a lot per track (checked via a one-off
inspection script this round, not kept in the repo):

    Music1: 8 free slots (24-31) -- room for all 6 SFX
    Music2: 7 free slots (4, 24-29) -- room for all 6 SFX
    Music3: only 3 free slots (28, 30, 31) -- genuinely full otherwise,
            confirmed by cross-checking the pattern data itself (every
            "used" slot in the header is also actually triggered
            somewhere in the song -- no free lunch from an unused-but-
            defined sample)

So SFX-to-slot assignment is per-track, not a single global constant --
SFX_SLOTS below is a {track_index: {qtm_sfx_name: slot}} table, with a
track simply omitting an SFX it has no room for. Music3 keeps only the
3 highest-impact, least-frequent events (Capture/Home/Win) rather than
the high-frequency Dice/Release/Move ticks, since Dice and Move in
particular retrigger constantly during a turn -- see
docs/QTM.md's "Round 7.76" section for the reasoning. Music1 and Music2
deliberately use the SAME slot indices for all 6 SFX (24-29), so most of
the game (2 of 3 tracks) needs no per-track branching at the call site;
lib/qtm.c still needs the full per-track table for the Music3 case.

This tool is idempotent and re-splices from the pristine originals each
run (see PRISTINE_BACKUP) -- it does NOT read back its own previous
output -- so it's safe to re-run after changing an Sfx* asset or the
slot table below.

Usage: python3 tools/mod_embed_sfx.py
Requires: only the Python 3 standard library.
"""

import math
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUDIO = ROOT / "assets" / "audio"
PRISTINE_BACKUP = ROOT / "assets" / "audio_pristine"

# qtm_sfx enum order, matching include/qtm.h exactly (QTM_SFX_DICE=0 ..
# QTM_SFX_WIN=5) -- lib/qtm.c's generated slot table depends on this order.
SFX_NAMES = ["DICE", "RELEASE", "MOVE", "CAPTURE", "HOME", "WIN"]
SFX_FILES = {
    "DICE": "SfxDice",
    "RELEASE": "SfxRelease",
    "MOVE": "SfxMove",
    "CAPTURE": "SfxCapture",
    "HOME": "SfxHome",
    "WIN": "SfxWin",
}

# Per-track SFX->sample-slot assignment (1-based ProTracker slot index,
# matching every other slot number in this file/QTM's own convention).
# 0 = "not embedded in this track" (lib/qtm.c must treat this as
# unavailable, not attempt to play slot 0).
SFX_SLOTS = {
    "Music1": {"DICE": 24, "RELEASE": 25, "MOVE": 26, "CAPTURE": 27, "HOME": 28, "WIN": 29},
    "Music2": {"DICE": 24, "RELEASE": 25, "MOVE": 26, "CAPTURE": 27, "HOME": 28, "WIN": 29},
    "Music3": {"DICE": 0, "RELEASE": 0, "MOVE": 0, "CAPTURE": 28, "HOME": 30, "WIN": 31},
}

SAMPLE_HEADER_SIZE = 30
NUM_SAMPLE_SLOTS = 31
HEADER_SIZE = 20 + NUM_SAMPLE_SLOTS * SAMPLE_HEADER_SIZE + 2 + 128 + 4


SFX_TARGET_RMS = 6000.0
"""Target loudness (RMS, on a 16-bit scale) every SFX is normalized
towards before soft-clipping to 8-bit -- see pcm16_to_pcm8(). Chosen to
match SfxRelease's own original RMS (5901.6) almost exactly: Release is
the one SFX confirmed audible against the music at its ORIGINAL,
unmodified loudness during round 7.80/7.81's live testing, so it's used
here as the calibration reference for how loud "audible over the music"
actually needs to be, rather than an arbitrary number."""

SFX_MAX_GAIN = 12.0
"""Safety cap on how hard a near-silent sample can be driven -- without
this, a mostly-silent recording would get boosted into audible noise/
hiss rather than genuine signal. None of the 6 bundled SFX currently
need more than ~8x (SfxDice, the quietest), so this only guards against
a future addition with an unexpectedly quiet source recording."""


def pcm16_to_pcm8(raw: bytes) -> bytes:
    """16-bit signed LE PCM -> 8-bit signed PCM, loudness-normalized via
    a soft-clip (tanh) limiter, not just peak-scaled.

    Round 7.82's first attempt only peak-normalized (scaled so each
    sample's own loudest instant reached full scale) -- technically
    correct, but live-tested and found insufficient: a short, punchy
    sound like SfxDice has a high peak-to-average ratio (one brief loud
    "click", mostly quiet otherwise), so raising its PEAK to full scale
    barely raised its PERCEIVED loudness (average level barely moved).
    Round 7.82 also tried ducking the music volume instead -- live-tested
    working, but rejected by direct user feedback as "really annoying"
    (a noticeable, repeated dip on every SFX, especially the frequent
    per-step Move sound).

    This instead targets RMS (average power, the metric that actually
    drives perceived loudness) via SFX_TARGET_RMS, using tanh as a soft
    clipper rather than hard-clipping past full scale -- tanh(x) is
    almost exactly linear for small x (quiet passages get a clean,
    undistorted gain boost) and gracefully compresses towards +-1 for
    loud peaks (avoiding the harsh flat-topped distortion a hard clip
    would produce), which is the standard shape of a soft-knee limiter.
    QTM_PlaySample's own volume field and QTM_SampleVolume are both
    already at their documented maximum (64) -- see docs/QTM.md's "Round
    7.82" section -- so boosting the embedded data itself is the only
    remaining lever."""
    n = len(raw) // 2
    samples16 = struct.unpack("<%dh" % n, raw[: n * 2])
    if n == 0:
        return b""
    rms = math.sqrt(sum(s * s for s in samples16) / n)
    gain = min(SFX_TARGET_RMS / rms, SFX_MAX_GAIN) if rms > 0 else 1.0
    out = bytearray(n)
    for i, s in enumerate(samples16):
        y = math.tanh((s * gain) / 32768.0)
        s8 = int(round(y * 127))
        if s8 > 127:
            s8 = 127
        elif s8 < -128:
            s8 = -128
        out[i] = s8 & 0xFF
    if len(out) % 2 == 1:
        # ProTracker sample lengths are stored in WORDS -- pad to even.
        out.append(0)
    return bytes(out)


def parse_mod(data: bytes):
    """Return (sample_descs, pattern_data, sample_data_start) where
    sample_descs is a list of dicts (one per slot, 1-based 'idx') and
    sample_data_start is the byte offset where sample PCM data begins."""
    off = 20
    descs = []
    for i in range(NUM_SAMPLE_SLOTS):
        rec = data[off : off + SAMPLE_HEADER_SIZE]
        length_words = struct.unpack(">H", rec[22:24])[0]
        descs.append(
            {
                "idx": i + 1,
                "name": rec[0:22],
                "length": length_words * 2,
                "finetune": rec[24],
                "volume": rec[25],
                "repeat_off": struct.unpack(">H", rec[26:28])[0] * 2,
                "repeat_len": struct.unpack(">H", rec[28:30])[0] * 2,
            }
        )
        off += SAMPLE_HEADER_SIZE
    song_length = data[off]
    off += 2
    pattern_order = data[off : off + 128]
    off += 128
    off += 4  # tag ("M.K." etc)
    num_patterns = max(pattern_order[:song_length]) + 1
    pattern_data_len = num_patterns * 4 * 64 * 4
    pattern_data = data[off : off + pattern_data_len]
    sample_data_start = off + pattern_data_len
    return descs, pattern_data, sample_data_start


def write_sample_header(rec: bytearray, name: bytes, length: int, finetune: int, volume: int, repeat_off: int, repeat_len: int):
    rec[0:22] = (name + b"\0" * 22)[:22]
    struct.pack_into(">H", rec, 22, length // 2)
    rec[24] = finetune & 0x0F
    rec[25] = volume
    struct.pack_into(">H", rec, 26, repeat_off // 2)
    struct.pack_into(">H", rec, 28, max(1, repeat_len // 2))


def embed(track_filename: str, slots: dict):
    src = PRISTINE_BACKUP / track_filename
    data = bytearray(src.read_bytes())
    descs, pattern_data, sample_data_start = parse_mod(bytes(data))

    # Load+convert every SFX this track has a slot for, keyed by slot index.
    new_payloads = {}
    for sfx_name, slot in slots.items():
        if slot == 0:
            continue
        assert descs[slot - 1]["length"] == 0, (
            f"{track_filename} slot {slot} ({sfx_name}) is not actually "
            f"empty (has {descs[slot - 1]['length']} bytes) -- SFX_SLOTS "
            f"table doesn't match this track's real sample layout"
        )
        raw16 = (AUDIO / SFX_FILES[sfx_name]).read_bytes()
        new_payloads[slot] = pcm16_to_pcm8(raw16)

    # Rebuild the sample-data region in slot order, splicing in the new
    # payloads where the old (empty) slots contributed nothing.
    out_sample_data = bytearray()
    offsets = {}
    for d in descs:
        idx = d["idx"]
        offsets[idx] = len(out_sample_data)
        if idx in new_payloads:
            out_sample_data += new_payloads[idx]
        else:
            old_off = sample_data_start + sum(
                dd["length"] for dd in descs if dd["idx"] < idx
            )
            out_sample_data += data[old_off : old_off + d["length"]]

    # Rewrite the 31 sample-header records (only the embedded slots
    # actually change; the rest are re-emitted byte-identical).
    new_header = bytearray(data[20 : 20 + NUM_SAMPLE_SLOTS * SAMPLE_HEADER_SIZE])
    for d in descs:
        idx = d["idx"]
        rec = new_header[(idx - 1) * SAMPLE_HEADER_SIZE : idx * SAMPLE_HEADER_SIZE]
        rec = bytearray(rec)
        if idx in new_payloads:
            sfx_name = next(n for n, s in slots.items() if s == idx)
            write_sample_header(
                rec,
                name=("Sfx" + sfx_name.title()).encode("latin1"),
                length=len(new_payloads[idx]),
                finetune=0,
                volume=64,
                repeat_off=0,
                repeat_len=2,
            )
        new_header[(idx - 1) * SAMPLE_HEADER_SIZE : idx * SAMPLE_HEADER_SIZE] = rec

    out = bytearray()
    out += data[0:20]
    out += new_header
    out += data[20 + NUM_SAMPLE_SLOTS * SAMPLE_HEADER_SIZE : sample_data_start]
    out += out_sample_data

    dest = AUDIO / track_filename
    dest.write_bytes(bytes(out))
    print(f"{track_filename}: embedded {len(new_payloads)} SFX, "
          f"{len(data)} -> {len(out)} bytes")


def validate(track_filename: str):
    """Sanity-check the rewritten file still parses as a valid tracker
    module via libopenmpt (ffprobe) -- catches any structural mistake
    (bad header, offset error) as a hard parse failure rather than a
    silent corrupt file, without needing a real RISC OS to test on."""
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=format_name,duration",
         "-of", "default=noprint_wrappers=1", str(AUDIO / track_filename)],
        capture_output=True, text=True,
    )
    if result.returncode != 0 or "libopenmpt" not in result.stdout:
        print(f"VALIDATION FAILED for {track_filename}:\n{result.stdout}\n{result.stderr}",
              file=sys.stderr)
        sys.exit(1)
    print(f"{track_filename}: validated OK ({result.stdout.strip()})")


def main():
    if not PRISTINE_BACKUP.exists():
        print(f"error: {PRISTINE_BACKUP} not found -- see docs/QTM.md's "
              f"\"Round 7.76\" section for how to (re)create it from the "
              f"CREDITS.md ModArchive URLs if it's ever lost", file=sys.stderr)
        sys.exit(1)
    for track_filename, slots in SFX_SLOTS.items():
        embed(track_filename, slots)
        validate(track_filename)


if __name__ == "__main__":
    main()
