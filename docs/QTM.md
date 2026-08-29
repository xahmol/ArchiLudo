# QTM manual

`lib/qtm.c`/`include/qtm.h` wrap the QTM (QTheMusic) relocatable module's
SWIs -- background ProTracker-format music playback plus one-shot sample
effects -- per explicit user request for a music/SFX layer, round 7.60.
QTM itself is bundled directly in the app directory (`assets/audio/QTMModule`
-> `!ArchiLudo/QTMModule,ffa`, loaded by `app/!Run`'s `RMEnsure` line), not
assumed present on a stock machine -- freeware, see `CREDITS.md`.

## Why QTM

Confirmed (via pi-star.co.uk/phlamethrower.co.uk documentation, and a real,
working ArchieSDK example project) to play 4/6/8-channel ProTracker/
FastTracker/StarTrekker `.mod` files, and to work on RISC OS 2 upward
(26-bit), well within this project's actual RISC OS 3.10 target. Its
one-shot sample-playback SWI (`QTM_PlayRawSample`) covers the sound-effects
half of the request without needing a second, separate sound library.

## SWI reference (confirmed, not guessed)

QTM's SWIs are not part of OSLib (a third-party module, not core RISC OS),
so `lib/qtm.c` declares their numbers itself and calls them via
`_kernel_swi()` (`<kernel.h>`, ArchieSDK's own general-purpose SWI
mechanism -- used instead of the simpler GCC `__swi()` attribute OSLib uses
for 1-in/1-out calls, since `QTM_PlayRawSample` needs up to 9 registers
with R0 both an input and an output, which `__swi()` can't express).

| SWI | Number | Confirmed via |
|---|---|---|
| `QTM_Load` | `&47E40` | `examples/bydctc/main.c` (ArchieSDK), a real working demo |
| `QTM_Start` | `&47E41` | same |
| `QTM_Clear` | `&47E44` | same |
| `QTM_PlayRawSample` | `&47E57` | RISC OS Open forum, "QTM and QTM_PlayRawSample" thread (Peter Howkins quoting QTM's own Technical Docs) |

**`QTM_Load`** -- On entry: R0 = pointer to filename, R1 = 0 (matches the
working reference call; not independently documented what R1 means, but 0
is what actually works). Loads a module ready to play.

**`QTM_Start`** -- no parameters. Starts playback of whatever was last
loaded.

**`QTM_Clear`** -- no parameters (confirmed working call passes an all-zero
register block). Used as ArchiLudo's "stop/silence" call -- see below for
why this is the best-confirmed choice, not a certainty.

**`QTM_PlayRawSample`** (`&47E57`) -- On entry:
- R0 = channel number (1-8) or -1 for automatic
- R1 = sample address (8-bit logarithmic sample data) -- 0 silences the channel
- R2 = sample length in bytes
- R3 = repeat offset (bytes from start of sample)
- R4 = repeat length in bytes (**1**, not 0, for one-shot/not looped --
  see round 7.62 below; a genuine live crash, not just documentation
  guesswork)
- R5 = note and flags: bits 0-27 = note/period, bits 28-31 = flags (0 =
  3-octave/Amiga-period system: 1-36 = notes C-1..B-3, 37-1999 = Amiga
  period values directly)
- R6 = linear volume (0-64)
- If R0 = -1: R7 = 0, R8 = 255 (required "for future compatibility", per
  QTM's own docs, when using automatic channel selection)

On exit (R0 = -1 case): R0 = channel actually used, or -1 if none were
free. `lib/qtm.c` always uses R0 = -1 (automatic), R3 = 0, R4 = 0
(one-shot).

**Not used / not confirmed**: QTM has dedicated `QTM_Stop`/`QTM_Pause`/
`QTM_Info`/`QTM_Pos`/`QTM_SoundControl` SWIs per the pi-star/phlamethrower
documentation, but none of their exact SWI numbers were confirmed against
a real working call the way the four above were -- `lib/qtm.c` uses
`QTM_Clear` for "stop" instead (the reference example calls it once at
startup, before loading its own music, i.e. to reset to a known silent
state -- a reasonable, but not 100% certain, fit for "stop/mute" too).
**Worth checking against QTM's own bundled documentation if one is ever
found** (not located during this round's research) before relying on
`QTM_Clear` for anything beyond simple mute.

## Sample format

`QTM_PlayRawSample` requires "8-bit logarithmic format" sample data --
confirmed via the RISC OS Open forum thread to specifically mean **VIDC's
own 8-bit logarithmic audio format**, which Jon Abbott's reply in that
thread describes as close to but not identical to standard ITU-T mu-law
(same general "logarithmic chords, linear within a chord" structure,
different sign/offset convention) -- the thread's own author tried several
guesses (plain mu-law, a-law, various sample rates) that all played back
"with enough distortion [to] barely discern the original sound" before
finding the actual working answer, which is worth taking as a warning: **do
not hand-roll a VIDC-log encoder from a general mu-law formula** -- it
looks close enough to seem right and isn't.

The forum's own working solution used RISC OS's `!SoundCon` utility (a
desktop app, not available on this Linux dev machine) to do the
conversion. Rather than try to replicate VIDC's undocumented exact
encoding table, `lib/qtm.c` instead uses the **RISC OS Sound system's own
`Sound_SoundLog` SWI** (`oslib/sound.h`'s `sound_sound_log()`) to do the
real conversion, on the real target machine, at `qtm_initialise()` time --
this is the actual OS-documented mechanism for exactly this conversion
(RISC OS 3 PRM: *"maps a 32-bit signed integer to an 8-bit signed
logarithm in VIDC format"*), so it's guaranteed correct without needing to
know the table's internals at all.

Two things confirmed from the PRM specifically for this:
- `Sound_SoundLog`'s output **is scaled by the current system volume**
  setting (`Sound_Volume`) -- `load_all_samples()` pins volume to 127
  (PRM: valid range 1-127) for the conversion pass and restores whatever
  it was afterwards, so the resulting bytes are a consistent, full-range
  encoding regardless of the user's own volume at startup. Per-effect
  playback loudness is controlled separately, via `QTM_PlayRawSample`'s
  own R6 parameter (`lib/qtm.c` always passes 64, the maximum).
- `Sound_SoundLog` takes a **32-bit signed integer** input with no
  explicitly documented expected scale. `load_one_sample()` shifts each
  16-bit source sample left by 16 bits before calling it (the standard
  convention for feeding a 16-bit source into a 32-bit-input log-encode
  call) -- **this is the one part of the whole pipeline not verified
  against a real confirmed example**, only reasoned from the SWI's own
  documented input type. Worth an ear-check on real hardware/Arculator; if
  SFX come out too quiet or clipped/distorted, this shift amount is the
  first thing to reconsider.

Bundled SFX files (`assets/audio/Sfx*`) are therefore shipped as **raw
16-bit signed mono PCM at 11025Hz**, headerless -- not pre-converted to
VIDC log -- and converted in memory once at `qtm_initialise()` time.
Background music (`Music1`/`Music2`) needs none of this: QTM's own MOD
player reads each track's sample data directly from the `.mod` file in
whatever format the tracker that made it already used.

### Playback pitch

`QTM_PlayRawSample`'s R5 "note" parameter (in the 37-1999 range) is an
Amiga period value, and playback pitch follows the classic Amiga formula:
`frequency = 3546895 / period` (confirmed against the forum thread's own
empirically-verified example: period 80 for a sample stored at 44100Hz,
i.e. `3546895/80 ≈ 44336Hz`, matching their own "closest at 44336Hz"
finding). All 6 bundled SFX are stored at a uniform 11025Hz, so one
constant covers all of them: `period = 3546895/11025 ≈ 321.7`, rounded to
**322** (`QTM_SFX_PERIOD` in `lib/qtm.c`).

## Asset preparation (for reproducing/replacing any of these)

Music tracks were downloaded directly from The Mod Archive's own API
endpoint (`https://api.modarchive.org/downloads.php?moduleid=<id>`) --
note the ordinary web UI (`modarchive.org/index.php?...`) blocks
automated fetches (503), but this direct download endpoint doesn't.
Confirmed genuine 4-channel ProTracker (`M.K.` tag at file offset 1080)
via `file(1)` before bundling.

SFX were sourced as OGG/WAV (see `CREDITS.md`) and converted with
`ffmpeg -i <src> -ac 1 -ar 11025 -f s16le <name>.pcm` (mono, 11025Hz,
16-bit signed, headerless) -- `SfxWin` was additionally trimmed to 4
seconds with a 0.5s fade-out first (`ffmpeg -t 4.0 -af
"afade=t=out:st=3.5:d=0.5"`) since the original ~11-second fanfare would
have used ~330KB as an 8-bit runtime buffer, disproportionate for a short
game-won sting on the 1MB ARM2/`WimpSlot`-constrained target.

## API (`include/qtm.h`)

```c
void qtm_initialise(void);                 /* call once at startup, after
                                             * game_view_initialise() */
int  qtm_available(void);                  /* is QTM actually present? */

void qtm_set_music_enabled(int enabled);
int  qtm_music_enabled(void);
void qtm_set_music_track(int track);       /* 0..QTM_MUSIC_TRACK_COUNT-1 */
int  qtm_music_track(void);

void qtm_play_sfx(qtm_sfx sfx);            /* QTM_SFX_DICE/RELEASE/MOVE/
                                             * CAPTURE/HOME/WIN */
```

Every function is always safe to call regardless of `qtm_available()`'s
answer -- if QTM isn't loaded, everything is a silent no-op, matching this
project's established "the game must stay playable if an extra falls
through" principle (see `docs/ARCHITECTURE.md`'s round 6.3/6.4 notes on
the same convention for pawn sprites).

`src/game_view.c` calls `qtm_play_sfx()` at 6 points: `start_roll_animation()`
(dice), `start_move_animation()`'s zero-distance branch and
`resolve_roll()`'s `just_released` branch (both pawn release -- the
optional and mandatory release paths respectively), `start_move_animation()`'s
normal path (move), and capture/home/win are all detected around the
single `ludo_move_pawn()` call in `start_move_animation()` (its own return
value for capture; before/after comparison of the moved pawn's `finished`
flag and `game.winner` for home/win).

`src/main.c`'s iconbar/window menu gained a "Music" submenu (round 7.60,
per explicit user request that music be "selectable and optional") -- a
ticked "On" toggle plus one ticked "Track N" entry per
`QTM_MUSIC_TRACK_COUNT`, refreshed just before the menu opens
(`refresh_music_menu_ticks()`). Picking a track also turns music on if it
was off, so a track pick never silently does nothing.

## Round 7.61: first live-test fixes

Two real bugs found from the first live pass (music confirmed working;
the first `qtm_play_sfx()` call -- dice throw -- crashed with a genuine
ARM data abort, "Internal error: abort on data transfer"):

- **`qtm_play_sfx()`'s `_kernel_swi_regs` block was only partially
  initialised** -- `r[9]` was left as uninitialised stack garbage
  (`QTM_PlayRawSample` only documents using R0-R8, but `_kernel_swi()`'s
  underlying stub most likely loads the whole 10-register block into
  physical registers regardless of which ones the SWI itself reads,
  unlike `qtm_clear()`/`start_track()`, which already `memset()` their
  own register blocks first). Fixed by zeroing the whole struct before
  filling in R0-R8, matching the rest of the file. Confirmed fixed live
  -- the dice SFX now plays cleanly (`err=(none)`, channel auto-assigned)
  with debug logging capturing every register value beforehand as
  supporting evidence.
- **`SfxWin` (the largest bundled sample) failed to load** -- silently,
  since `load_one_sample()` didn't originally log its own failure paths
  (now fixed, logs fopen/empty-file/malloc failures too). Most likely a
  `malloc()` failure: converting it needs ~130KB of simultaneous
  temporary buffers (16-bit source + 8-bit converted, freed right after),
  tight against `app/!Run`'s original `WimpSlot -max 256K` on top of
  everything else already resident. Two complementary fixes: `-max`
  raised to 384K (only the ceiling, not `-min`, so a genuinely tight
  stock 1MB machine still launches -- `WimpSlot -max` is a growth
  ceiling, not memory reserved up front), and `SfxWin` itself trimmed
  from 4s to 2.5s (88200 -> 55104 bytes, keeping the same 11025Hz all
  other SFX use -- changing rate without also changing `QTM_SFX_PERIOD`
  would have played it back at the wrong pitch/speed, an early mistake
  caught before shipping).

## Round 7.62: the same crash, still happening

Round 7.61's register-zeroing fix turned out not to be the (whole) story
-- the same "Internal error: abort on data transfer" at the exact same
address recurred on a second live test, even though the debug log showed
`qtm_play_sfx()`'s own SWI call returning cleanly (`err=(none)`) both
times, with nothing further logged afterward. That "clean return, then
silence" pattern is the tell: `QTM_PlayRawSample` almost certainly starts
playback **asynchronously** (a background DMA/fill-request mechanism,
the same architecture every ProTracker-derived player uses for its own
instrument samples) rather than consuming the sample data during the SWI
call itself -- so a clean SWI return only proves playback *started*, not
that it can safely finish.

The actual bug: **R4 (repeat length) was set to 0, not 1**. Classic
Amiga/ProTracker-family sample headers use a repeat length of **1**, not
0, as the "don't loop" sentinel -- a 0 is a well-known trap in that
format family, since compliant loop/fill logic generally assumes the
loop length is never smaller than 1 (dividing or stepping by a length of
0 is exactly the kind of thing that walks off into invalid memory during
a background fill). QTM is fundamentally a ProTracker-family player, so
its raw-sample fill logic almost certainly inherited the same
convention -- explaining both the deferred timing (the fault only
surfaces once QTM's background code actually tries to loop/fill using
the bad length, after the initiating SWI call already returned) and
its perfect reproducibility (a real logic bug, not memory-pressure
flakiness). Fixed: R4 = 1.

**Not re-confirmed as fixing anything** -- see round 7.63 below: the R4
fix didn't actually resolve the crash either, though it's kept as the
documented-correct value regardless.

## Round 7.63: the real cause -- channel contention with the music

Round 7.62's fix didn't work: the identical crash, at the identical
address, recurred again on the very next live test. Rather than guess a
fourth register value, this round used Arculator's own debugger (its `t
enable dataabort` command traps data aborts directly, halting emulation
at the fault instead of letting RISC OS's own handler swallow it into a
friendly error box) to get real data instead of another hypothesis. With
the trap enabled, the SAME reproduction steps **did not crash at all** --
instead, the background music audibly hung on a single note the instant
the SFX played.

That's the real symptom, and it points at **channel contention**, not a
bad register value: `QTM_PlayRawSample`'s R0 = -1 ("automatic channel
selection") is documented as searching "for a free channel (with no
music or samples playing through it)" -- but the currently-playing
background music is a 4-channel ProTracker module using channels 1-4,
and if QTM's own "free channel" search is buggy, or considers a channel
"free" between individual note triggers even while a module is actively
using it, automatic selection can end up handing a raw sample the SAME
physical channel the module player is mid-way through driving --
corrupting its own channel bookkeeping. This explains both symptoms as
one root cause: a stuck note (this round, with the abort trapped and
suppressed) or a hard data abort (rounds 7.60-7.62, un-trapped) depending
on the exact channel picked and what state it was in.

**Fix**: R0 = a **fixed** channel (5) instead of -1. Channel 5 is always
outside the 1-4 range a 4-channel module uses, so a raw sample can never
land on a channel the currently-playing music also needs, regardless of
whatever automatic-selection's own internal logic does. R7/R8 (only
documented as required when R0 = -1) dropped along with the -1 value
they went with.

**Confirmed live**: round 7.63's fix worked -- the crash is gone, and
background music keeps playing normally through repeated SFX triggers.
But a new, different symptom appeared: **the SFX itself is inaudible**
-- `QTM_PlayRawSample` returns cleanly (no error, channel 5 echoed back
in R0 as expected) every time, yet nothing is heard.

## Round 7.64: silent-but-clean playback -- the Sound system didn't know
## channel 5 existed

Added one more diagnostic (`Sound_Configure` called with R0=0, the same
"inspect, don't change" convention already used for `Sound_Volume`) right
before the play call, to check how many channels the underlying RISC OS
Sound system itself thinks exist -- confirmed live: **`channel_count=4`**,
exactly matching the currently-playing 4-channel module.

This fully explains the symptom: QTM's own R0=1-8 channel numbering (used
by `QTM_PlayRawSample`) is a request into whatever channels the
underlying OS Sound system has actually been configured for --
requesting channel 5 when the Sound system was only ever told about 4 is
accepted by QTM without complaint (it's a valid channel *number* per
QTM's own documented 1-8 range), but the hardware/DMA mixing genuinely
never processes it, since `Sound_Configure` -- not QTM -- is what
determines how many channels physically exist.

**Fix**: `qtm_initialise()` now calls `Sound_Configure` once, early --
before `load_all_samples()`/`start_track()`, i.e. before anything is
actually playing yet -- to bump the channel count up to
`QTM_SFX_CHANNEL` (5) if it's currently lower. Read-modify-write: only
`channel_count` changes; `sample_size`/`sample_period`/`channel_handler`/
`scheduler` are read back from the current configuration and passed
through completely unchanged, since `Sound_Configure`'s own PRM entry
gives no indication it's safe to casually reconfigure while something is
already relying on the existing setup -- doing this once, before
playback starts, sidesteps that risk entirely rather than needing to
find out empirically whether a live reconfigure disrupts already-playing
music.

**Not yet re-confirmed live** -- built and deployed, but this specific
fix hasn't been tested against Arculator yet.

## Round 7.65: third track, and a real Track submenu

Two independent additions, per explicit user requests:

- **A third background music track** -- `Music3` ("on the run" by Anders
  Lundqvist, ModArchive module 157927, same download/verification
  process as the first two -- see CREDITS.md). `QTM_MUSIC_TRACK_COUNT`
  bumped 2 -> 3; `lib/qtm.c`'s `build_music_path()` generalised from a
  hardcoded `track==0 ? "Music1" : "Music2"` ternary to a proper
  `snprintf("Music%d", track+1)` covering any track count.
- **The Music submenu's "Track N" entries replaced with a real Track
  submenu showing each track's actual title** -- per explicit request
  ("Is it not possible to make track selection a sub sub menu with full
  title of the MODs?"). RISC OS menu entries have a 12-byte inline text
  field, too short for titles like "digital innovation1" (20
  characters), so this needed **indirected menu text**
  (`wimp_ICON_INDIRECTED`, `entry->data.indirected_text.text` pointing
  at the real string rather than a copied 12-byte buffer) -- the same
  underlying `wimp_icon_data` union window icons already use in this
  project, just applied to a `wimp_menu_entry` for the first time.
  `src/main.c`'s Music submenu shrank to 2 entries ("On", "Track" --
  the latter now opening its own further submenu, `track_menu`);
  `track_menu` holds one ticked entry per bundled track, its text
  pointing into a fixed `track_titles[]` array (the actual titles
  embedded in each `.mod` file, confirmed when each was sourced, not
  invented). `Menu_Selection`'s `items[]` path for a track pick is now 3
  deep (`ICONBAR_MENU_MUSIC` -> `MUSIC_MENU_TRACK` -> the chosen track
  index), one level deeper than the flat "Track N" entries it replaced.

**Confirmed live**: "Music submenu works" -- the nested Track submenu,
indirected full-title text, and tick refresh all function correctly.

## Round 7.66: still silent -- power-of-two channel counts

Round 7.64's channel-count bump didn't fix the silence either -- SFX
still isn't heard, even though `qtm_play_sfx()` keeps returning cleanly
(no error, no crash, no hang -- just never audible). Added a
verification log right after the `Sound_Configure` bump to check whether
requesting 5 channels actually stuck, rather than assume it did.

New hypothesis, not yet confirmed: classic Acorn/VIDC-era sound hardware
channel counts are commonly constrained to **powers of two** (1/2/4/8),
since each channel needs its own DMA/buffer slot in a typical
implementation. Requesting exactly 5 may have been silently rounded back
down to 4 (the nearest valid value not exceeding 5), leaving channel 5
exactly as unserviced as before round 7.64 appeared to fix it. Changed
the request to **8** (the next power-of-two step up from 4, and
guaranteed valid if this theory holds) instead of 5, with the echoed-back
channel count now logged so this is confirmed either way rather than
guessed a second time.

**Confirmed NOT fixed**: SFX still inaudible on the next live test, with
the echoed-back channel count logged this time -- and it read back as
**1**, not 4 or 8. That inconsistency (1 this time, 4 in round 7.64's
read, no change at all after explicitly requesting 8) was the signal
that `Sound_Configure` was never the right mechanism at all, prompting a
real research pass instead of a fifth guess -- see round 7.67.

## Round 7.67: the real answer -- QTM_SoundControl, from QTM's own author

Per direct user request ("Can you find any online resources on how to
do sound FX with QTM?"), found real, authoritative source code rather
than continuing to guess: a stardot.org.uk thread about porting
*PowerMonger* to RISC OS, where a user asked exactly this project's own
question (how to play one-shot game sound effects through QTM without
disturbing the music) -- answered directly by **Steve Harrison, QTM's
own author**, posting as "steve3000" (his own site, pi-star.co.uk,
confirms this is the same person). He describes converting samples to
"native Archie 8-bit log format" and posted a short BASIC program,
`lin2LOG` (a linear-to-VIDC-log converter with a working
`QTM_PlayRawSample` playback test built in), as an attachment later in
the thread. Detokenised (BBC BASIC's tokenised format, using
[gerph/riscos-basic-detokenise](https://github.com/gerph/riscos-basic-detokenise))
to read the actual source -- see CREDITS.md for the full citation.

This overturned two of this project's own earlier, wrong guesses:

- **`QTM_SoundControl` exists and must be called before
  `QTM_PlayRawSample`** -- something no documentation this project found
  anywhere else even mentioned by name. The author's own code does
  exactly this before ever playing a sample:
  ```basic
  SYS"QTM_SoundControl",-1,-1 TO ch
  IF ch=0 THEN SYS"QTM_SoundControl",4,-1
  ```
  i.e. query the current sample-channel reservation (R0=-1, R1=-1), and
  if none exist yet, request some (R0=4, R1=-1). This is a
  **QTM-specific** channel reservation mechanism for one-shot samples,
  entirely separate from anything the OS-level `Sound_Configure` (rounds
  7.64/7.66) could ever have touched -- which is exactly why those two
  rounds' fixes never worked: reconfiguring the OS Sound system's own
  channel count was simply the wrong layer. `lib/qtm.c` now resolves
  `QTM_SoundControl`'s SWI number by name (`xos_swi_number_from_string()`,
  the same mechanism `qtm_available()` already uses) rather than
  hardcoding it, since no source gives its actual number anywhere.
- **`QTM_PlayRawSample`'s repeat-length register (R4) should be 0, not
  1** -- round 7.62's "must be 1" theory (the classic Amiga/ProTracker
  sample-header convention) was a reasonable guess but wrong: the
  author's own code uses R4=0 in every real call it makes. Reverted.

One thing round 7.63's fix got right, now on firmer ground:
`QTM_PlayRawSample` should use a **fixed** channel number, not automatic
(-1) selection -- confirmed by the author's own code always using
channel 1. What round 7.63/7.64 misunderstood was *which* channel space
that number refers to: not a raw hardware/`Sound_Configure` channel
(hence why bumping the OS-level channel count never helped), but channel
1 **within QTM's own reserved sample-channel pool** that
`QTM_SoundControl` sets up. `QTM_SFX_CHANNEL` changed from 5 to 1 to
match.

**Confirmed live: still crashing** (a data abort, at a slightly different
address than every earlier attempt -- &0182A6BC rather than &0182A768,
consistent with the same general class of fault at a marginally
different point given the new QTM_SoundControl call ahead of it). QTM's
own `QTM_SoundControl` query/reserve calls themselves ran and returned
cleanly first (`r0=0` on query, meaning no channels were reserved yet,
then the reserve call), and `QTM_PlayRawSample` itself again returned
cleanly (`err=(none)`, `r0_out=1`) -- the crash is still happening
*after* a clean-looking return, the same asynchronous-fault pattern as
every attempt since round 7.61.

## Round 7.68: R7/R8 -- a real, concrete discrepancy against the author's own code

Comparing the logged register values against `lin2LOG`'s own two real
`QTM_PlayRawSample` calls line by line found a genuine difference this
project's code had introduced by itself: **R8 was 0 in every attempt so
far, but the author's own code passes R8=255 in both its calls** --
including its channel-1 (not -1) call, the same fixed-channel case this
project already uses. Round 7.63's own reasoning ("R7/R8 only documented
as needed when R0=-1") doesn't match the author's actual practice, which
passes 0,255 unconditionally regardless of R0. Fixed: R7=0, R8=255 set
explicitly and unconditionally, matching the reference exactly rather
than the SWI's own written documentation's narrower claim about when
they're needed.

**Confirmed live: still the same crash.** R7/R8 now matched the author's
own reference exactly, and the fault persisted regardless.

## Round 7.69: R5 -- the last register that still differed in kind, not just value

With R7/R8 now an exact match, every register in `lib/qtm.c`'s
`QTM_PlayRawSample` call was identical in *kind* of value to the
author's own two real calls -- except R5. `lin2LOG`'s calls both use
small "note" numbers (its playback loop computes
`((key+1) AND NOT 1)/2` per keypress, always landing in the 1-36
sub-range documented for 3-octave notes), never an Amiga *period*
(37-1999) the way this project's own R5=322 does. Neither of the
author's own values represents "correct natural pitch" for a specific
sample rate -- his demo is deliberately a multi-note piano, not a
natural-playback test -- but every value he exercised live falls in the
small-note range, never the period range, which the period-based
approach here has no confirmed-working precedent for at all beyond a
different, unrelated forum poster's own (separately reported, but not
verified against QTM's *current* version or this project's own build)
success with period 80.

**Fix, as an isolating test rather than a considered final value**:
R5 changed from `QTM_SFX_PERIOD` (322, an Amiga period) to
`QTM_SFX_NOTE` (18, an arbitrary mid-range 3-octave note) -- purely to
determine whether QTM's period-range support itself is what's
unreliable (e.g. pitch-shifted resampling reading past the sample
buffer's actual end -- exactly the kind of bug that would only surface
*after* a clean-looking SWI return, matching every crash in this
investigation). If this produces audible (even mis-pitched) sound, the
period encoding was the trigger, and getting the *correct* pitch back
becomes the next, much lower-risk problem. If it still crashes, R5
wasn't it either, and the remaining registers (all now byte-identical to
the confirmed-working reference) point toward something outside the
register block entirely -- the sample data or format itself, or
something QTM-version- or environment-specific this investigation
hasn't reached yet.

**Confirmed live: still crashing.** R5's value (note vs period) wasn't
it -- every register in the call was now byte-identical to the author's
own confirmed-working usage, yet the fault persisted, ruling out the
register block itself as the remaining cause.

## Round 7.70: the crash address itself is the clue -- a second, different fault

Per direct user question ("analyse also why the error came back with the
new initialisation call, while first we no longer had this error on
using channel 5"): the crash address had actually changed between round
7.63 (the original automatic-channel-selection crash, `&0182A768`) and
every crash since round 7.67 (`&0182A6BC`) -- two *different* faults,
not one bug persisting across rounds. Round 7.63's fixed-channel-5 fix
(no `QTM_SoundControl` at the time) was genuinely stable, just silent;
the crash only came back once round 7.67 introduced `QTM_SoundControl`
(reserving exactly 4 sample channels) alongside switching the play
channel from 5 to 1.

Most likely explanation: reserving exactly 4 sample channels -- matching
the bundled music's own 4-channel width -- happens in `qtm_initialise()`
*before* the module is even loaded, so the reservation itself succeeds
cleanly against genuinely free channels at that moment. But the module
player has no way to know channels 1-4 were just claimed for samples,
and grabs them for itself anyway once `QTM_Start` runs afterward --
recreating the exact channel-collision class of bug round 7.63 already
diagnosed once, just via `QTM_SoundControl`'s reservation instead of
automatic (`-1`) selection. This fits the observed timing exactly: the
crash only ever happens once music is already playing, when a raw sample
actually tries to use a channel the module is using at that instant.

**Fix**: reserve 8 channels (the documented max) instead of 4, and play
SFX on channel 5 again (within the reservation, but outside the
4-channel module's own 1-4 range) -- so the two pools can't overlap
regardless of how the module claims its own channels.

Separately, per the user's own suggestion, deployed QTM's author's own
`lin2LOG` test program directly to Arculator's hostfs
(`hostfs/QTMTest/`) alongside a converted 8-bit signed linear test
sample, as an independent check: if the author's own reference program
also fails the same way in this environment, that would point to
something Arculator/QTM-version-specific rather than this project's own
code -- see the session notes for the exact run steps.

**Confirmed live: still crashing.** But this round produced the
breakthrough: with Arculator's own data-abort trap already enabled from
an earlier round, the debugger caught the fault live and, at the user's
own initiative, was used to read the exact register state and
disassemble the real faulting code -- see round 7.71.

## Round 7.71: the real root cause, found via Arculator's own debugger

Per the user's own idea to cross-check against the reference program,
`lin2LOG` (QTM's author's own test program) was deployed to Arculator's
hostfs -- but before completing that test, the user drove Arculator's
debugger directly against an ArchiLudo crash instead, since the
data-abort trap from an earlier round was still armed. Two commands
told the whole story:

- `r` (registers) showed `PC` in an unrelated memory region and "In
  supervisor mode" rather than "In abort mode" -- a sign the debugger's
  own idle state, not the actual fault, so this branch of investigation
  was a dead end.
- `d 182a768` (disassembling at the address RISC OS's own error dialog
  had reported, not a derived/guessed one) was the real breakthrough:

  ```
  182A768 : ADD  R1, R1, R2            ; fixed-point position += pitch step
  182A76C : LDRB R6, [R0, R1 ASR #12]  ; read source byte at (R1>>12) -- faults HERE
  182A770 : STRB R6, [R12], R11        ; write to output, advance
  ```

  repeated four times (a small unrolled loop). This is a textbook
  **pitch-shifted resampling read**: `R0` is the sample buffer base,
  `R1` a fixed-point fractional playback position advanced by a
  per-step pitch increment (`R2`) each iteration, `R1 ASR #12`
  converting that to a byte offset. Reading slightly past a sample's
  nominal end is normal, *required* behaviour for this kind of
  interpolating resampler -- the same reason Amiga ProTracker's own
  sample format conventions exist -- not a bug in `QTM_PlayRawSample`
  or in how this project was calling it. **The actual bug was much
  simpler: `load_one_sample()`'s buffer was allocated to exactly the
  sample's own length, with no lookahead margin at all**, so the
  legitimate overshoot read walked straight into unmapped memory.

  This also retroactively explains why every register-value change
  across rounds 7.61-7.69 never fixed anything (R9 zeroing, repeat
  length, channel selection, R7/R8, note vs period) -- **none of them
  were the actual bug**. It's entirely possible round 7.63's
  channel-contention diagnosis (and 7.67/7.70's `QTM_SoundControl`
  work) was chasing a secondary or coincidental symptom of this same
  underlying overrun, not a separate issue -- left in place regardless
  (channel 5 + an 8-channel reservation is at minimum harmless, and
  there isn't clean evidence to justify removing it now).

**Fix**: `load_one_sample()` now allocates `n + QTM_SAMPLE_PAD` (64)
bytes instead of exactly `n`, appending genuine VIDC-log silence
(queried via `sound_sound_log(0)`, not assumed to be byte value 0 --
mu-law-family encodings don't necessarily map linear silence to an
all-zero byte) as safety padding past the sample's real, unchanged
length (`QTM_PlayRawSample`'s own R2 parameter still reports the true
length -- the padding is memory safety margin only, never played as
real audio). 64 bytes is a generous margin, not a computed exact
minimum -- the actual overshoot in the disassembly looked like only a
handful of bytes, but the precise worst case depends on internal
pitch-step math this investigation doesn't have visibility into.

**Confirmed live: still crashing at the same address**, 64 bytes of
padding nowhere near enough -- prompting a much deeper live debugging
session (the user continuing to drive Arculator's debugger directly,
disassembling forward through the whole resampling routine) that found
the actual mechanism in round 7.72.

## Round 7.72: the wraparound math, not a proximity overrun

Continued disassembling forward from `182a768` (`d` with no address,
which continues from the last position) through the whole fill
routine's structure. Two things settled it:

- The tight resampling loop (`182A760`-`182A7C4`) is bounded by
  `CMP R12, R10 / BLT 182a760` -- **R12 vs R10 is output progress vs an
  output target, not a source-length check at all**. It reads from the
  source for exactly as many iterations as needed to fill the current
  output chunk, with zero awareness of whether the source has enough
  data left. No amount of trailing padding fixes this in general --
  padding only helps if the overrun is small, and this loop has no
  upper bound on how far past the source it can read if the source is
  short relative to one output chunk.
- A second block (`182A954`-`182A998`) is the actual bounds mechanism:
  `ADDS R1,R1,R2` then `SUBGE R1,R1,R5 LSL #12`, repeated -- wrapping
  the fixed-point read position back by the sample's length (`R5`,
  shifted into the same fixed-point form as R1) every time it overruns.
  **If R5 is 0, that subtraction never fires**, and the read position
  just grows without bound instead of ever wrapping back to valid
  buffer territory -- explaining the crash far better than "reads a
  little past the end": with repeat length 0, there is no wraparound at
  all, and the position can run arbitrarily far past the buffer as long
  as the output chunk demands it.

QTM's own author's `lin2LOG` passes repeat length 0 successfully in his
own demo -- but his samples are large/long relative to a single output
fill chunk, so this path most likely never actually triggered for him.
ArchiLudo's own SFX are short (`SfxMove` is only 1071 bytes), exactly
the case where it does.

**Fix**: repeat length (R4) set to the sample's own real length
(matching R2), not 0 -- this makes the wraparound math correct
regardless of how short the sample is relative to a fill chunk, at a
real cost: **the sample will now genuinely loop** rather than stop
after one play, since QTM has no other way to know "stop entirely"
(this is a fill/mixing-level convention, not a client-controlled
option). The follow-up this needs -- explicitly silencing the channel
(`QTM_PlayRawSample` with R1=0, per its own documentation, "if address
is 0, the channel is silenced") once the sample's own natural duration
has elapsed -- is not yet implemented; this round is specifically an
isolating test to confirm the crash itself is gone before adding that.

**Confirmed live: still crashing, same address.** The wraparound fix
alone wasn't sufficient. A further live debugging session continued
disassembling forward from `182a768` through a large amount of
surrounding code, but most of it turned out to be adjacent, unrelated
QTM subroutines (channel setup, `Sound_Configure`/`Sound_Stereo` calls,
note-frequency tables) rather than code on the actual fault path --
static disassembly is reliable for reading fixed code, but scrolling
far enough eventually leaves the relevant function entirely. `r`
(registers) continued to show a different, unrelated PC each time
(confirming it isn't capturing the fault moment, as suspected).

## Round 7.73: period reinstated alongside the wraparound fix

One combination hadn't actually been tested: round 7.69's arbitrary
`QTM_SFX_NOTE=18` (an isolating test at the time, before the real bug
was known) was still in use when round 7.72's wraparound fix (repeat
length = real sample length) was tried -- an arbitrary note could
produce a wildly wrong pitch step, racing through the buffer far faster
than 1:1 in a way not necessarily fully bounded by wraparound handling
within a single output-fill chunk. Reinstated `QTM_SFX_PERIOD` (322, an
Amiga period computed for the bundled SFX's real 11025Hz storage rate --
the formula itself was already confirmed against the RISC OS Open
forum's own example) in place of the note value, on top of (not instead
of) round 7.72's wraparound fix -- a period matching the sample's own
real rate should keep the read position advancing at close to 1 source
byte per output byte, minimising how far it can ever run regardless of
wraparound correctness.

**Confirmed live: still crashing, same address as ever.** Combining a
real Amiga period with the wraparound fix didn't help either -- at this
point every plausible register-level explanation this investigation
could construct from documentation, a confirmed working reference
example, and direct live debugging had been tried and ruled out.

## Round 7.74: three more real codebases checked, SFX disabled

Per direct user request ("Before we drop SFX, does one of these links
shed any insight?" and two further links), checked three more real,
shipped Archimedes demo-scene codebases rather than continue guessing
`QTM_PlayRawSample`'s parameters:

- **[kieranhj/arc-django-2](https://github.com/kieranhj/arc-django-2)**
  (GitHub) -- a real shipped game, with `lib/swis.h.asm` giving a larger
  confirmed SWI table (including `QTM_Stop = 0x47e42`, never confirmed
  before) and two real, working `QTM_SoundControl` calls. This
  **corrected a real misunderstanding this project had held since round
  7.67**: `QTM_SoundControl`'s R1 is a flags bitmask controlling QTM's
  own behaviour (bit 2 = "retain sound system after Pause/Stop/Clear",
  per the game's own comment), with no relationship whatsoever to
  reserving channels for one-shot samples -- the "reserve N sample
  channels" mechanism rounds 7.67-7.73 built and tuned was never doing
  what this project believed it did. Comparing timelines, the real
  reason SFX went from silent-but-stable (rounds 7.63-66) to crashing
  (7.68 onward) was almost certainly round 7.68's R7/R8 fix, not
  `QTM_SoundControl` at all -- R8=0 (the original code) most likely made
  `QTM_PlayRawSample` silently no-op the request entirely, while R8=255
  (matching the author's reference) is what actually let real playback
  -- and the underlying bug -- get attempted for the first time.
  `QTM_SoundControl`'s misapplied "reservation" logic has been removed
  from `lib/qtm.c` entirely.
- **[bitshifters/aklang](https://github.com/bitshifters/aklang)**
  ("ArchieKlang Announcetro", a shipped demo that *generates* sample
  data at runtime -- as close to this project's own use case as
  anything found) -- gave the most complete SWI table yet, revealing
  **two sample-playing SWIs this project never knew existed**:
  `QTM_PlaySample` (`0x47e54`) and `QTM_RegisterSample` (`0x47e5a`),
  entirely separate from `QTM_PlayRawSample` (`0x47e57`). But the demo
  itself never calls any of the three -- its generated samples become
  embedded instruments *inside* the music module itself, played through
  ordinary `QTM_Start`.
- **[bitshifters/mikroreise](https://github.com/bitshifters/mikroreise)**
  -- another real Bitshifters production sharing the same demo
  toolchain, checked for completeness. Same result: only music-level
  QTM calls, no `PlayRawSample`/`PlaySample`/`RegisterSample` usage
  anywhere.

**None of the three gave a working reference for the exact call this
project needed** -- but their consistent pattern is itself the real
finding: **no real, shipped Archimedes production checked across this
entire investigation plays one-shot effects via a raw-pointer SWI call
at all.** Every one embeds extra sounds as instrument samples inside the
music module and triggers them by index -- exactly the *first* option
QTM's own author suggested in the PowerMonger thread that supplied
`lin2LOG` (round 7.67), which this project under-weighted at the time in
favour of the raw-pointer approach because it looked simpler (no need to
edit the `.mod` files). Fourteen rounds of a rawpointer-shaped dead end
later, "embed as MOD instruments, play via `QTM_PlaySample` by index"
is the clearly better-supported path for a future attempt -- see
"Recommended next approach" below.

**Decision**: per direct user agreement, one-shot SFX via
`QTM_PlayRawSample` is disabled. `qtm_play_sfx()` is now a no-op (see
its own doc comment in `lib/qtm.c`); sample loading/conversion stays in
place, unused but harmless, as groundwork for a future attempt. Music
(confirmed working live throughout) is unaffected.

## Recommended next approach, if SFX is revisited

Don't resume tuning `QTM_PlayRawSample`'s registers -- the mechanism is
confirmed (an internal resampler that reads unbounded past the sample
buffer once real playback is attempted), and this investigation
exhausted the reasonable parameter space, a confirmed working
reference, and three independent real codebases without finding a
combination that avoids it. Instead:

1. Embed each SFX as an extra instrument sample inside `Music1`/`Music2`
   (ProTracker `.mod` files support up to 31 sample slots; a typical
   4-channel track uses well under half that).
2. Trigger them via `QTM_PlaySample` (`0x47e54`) or possibly
   `QTM_RegisterSample` (`0x47e5a`) + `QTM_PlaySample`, by sample index
   -- NOT `QTM_PlayRawSample`. Neither call's exact register convention
   is confirmed by this investigation; that research would need to
   start fresh (no working example was found for either, only their SWI
   numbers, from `bitshifters/aklang`'s `lib/swis.h.asm`).
3. This does mean each SFX becomes tied to a specific loaded music
   track's own sample table, rather than a single set of assets shared
   across all tracks -- a real design constraint worth planning for
   up front, not a detail to discover partway through.

## Round 7.76: MOD-embedded samples, QTM_PlaySample tried cautiously

Per direct user request ("Can you adapt existing MODs to have our own
samples in it?") and the recommended approach above, actually
implemented the MOD-embedding path -- with the caution level that felt
warranted after 14 rounds of `QTM_PlayRawSample` pain: build the
embedding tool first (no crash risk of its own), then attempt
`QTM_PlaySample` on exactly one event, not all 6 at once.

**Re-reading the original PowerMonger thread (round 7.67's `lin2LOG`
source) more carefully surfaced a detail missed the first time**: it
directly confirms this exact approach in its own words -- "load these
into the music mod using ProTracker, as long as there are enough unused
sample entries in the mod's sample table (max 31 samples, most tracks
use about half that)... and use QTM_PlaySample to play the game sounds
as needed." No register-level detail, but a second independent
confirmation (on top of three real codebases all avoiding
`QTM_PlayRawSample`) that this is the intended, supported use of QTM,
not a workaround.

**Capacity check**: `Music1`/`Music2` (the ripped tracker modules used
for ArchiLudo's background music, see CREDITS.md) turned out to have 8
and 7 free sample slots respectively (24-31 and 4/24-29) -- room for
all 6 SFX. `Music3` only has 3 free slots (28/30/31); confirmed via a
pattern-data scan (not just the header) that its other 28 slots are all
genuinely triggered somewhere in the song, so there's no reclaimable
"defined but unused" slack. Rather than force identical slot indices
everywhere and drop 3 SFX project-wide, `lib/qtm.c`'s `sfx_slot[][]`
table is per-track: `Music1`/`Music2` embed all 6 at indices 24-29;
`Music3` keeps only Capture/Home/Win (28/30/31) -- the 3 rarer,
higher-impact events, over the 3 that retrigger constantly during a
turn (Dice, Move, Release).

**`tools/mod_embed_sfx.py`** (new): parses each MOD's sample header
table and pattern data, converts the 6 bundled `Sfx*` assets (raw
headerless 16-bit signed PCM at 11025Hz -- the same files
`load_one_sample()` already reads, this is a second, independent
consumer of them) to 8-bit signed PCM (ProTracker's own native sample
format -- ordinary linear PCM, NOT the VIDC-log format
`QTM_PlayRawSample`/`Sound_SoundLog` needed; unrelated formats despite
both being "8-bit"), and splices each into its assigned empty slot,
rewriting only that slot's header record (name/length/volume/loop) and
shifting the sample-data region to make room. Reads from a new
`assets/audio_pristine/` (untouched originals of `Music1`/`Music2`/
`Music3`, kept since `assets/audio/` itself isn't yet committed to git
-- see the tool's own docstring) and overwrites `assets/audio/` in
place; safe to re-run any time an `Sfx*` asset or the slot table
changes. Validated via `ffprobe`'s `libopenmpt` demuxer (confirmed
present locally) after every run -- a genuine third-party tracker
parser, not just "the file didn't throw in Python" -- checking the
rewritten files still parse cleanly and their song duration is
unchanged (confirming the pattern data itself wasn't disturbed, only
the sample table). All three passed on the first run.

**`lib/qtm.c` changes**: added `QTM_SWI_PLAY_SAMPLE` (`0x47e54`,
confirmed via `bitshifters/aklang`'s `swis.h.asm`, same as round 7.74),
the `sfx_slot[QTM_MUSIC_TRACK_COUNT][QTM_SFX_COUNT]` table (the C-side
mirror of `mod_embed_sfx.py`'s `SFX_SLOTS` -- kept in sync by hand,
there's no shared source of truth between a Python build tool and this
file), and rewrote `qtm_play_sfx()`. Its register convention is a
**guess** -- no working example was found anywhere in this
investigation's research, only `QTM_PlaySample`'s SWI number and the
PowerMonger thread's confirmation that the approach itself is right.
By analogy with `QTM_PlayRawSample`'s own confirmed convention: R0 =
channel (-1 = auto-select), R1 = sample slot number, R2 = Amiga period
(reusing `QTM_SFX_PERIOD` = 322, the same 11025Hz-derived constant from
round 7.73 -- these SFX weren't resampled to a different rate for
embedding, so the same period should still be correct), R3 = volume
(0-64). Fields `QTM_PlayRawSample` needed but `QTM_PlaySample`
shouldn't (sample address, length, repeat offset/length) are omitted,
since that data already lives in the loaded MOD's own sample table for
that slot.

**Wired cautiously, not all at once**: only `QTM_SFX_DICE` actually
calls `QTM_PlaySample` for now (`QTM_PLAY_SAMPLE_TEST_SFX` in
`lib/qtm.c`) -- chosen deliberately, since it's also where the original
14-round crash saga started, closing that loop with a different SWI.
Every other event (Release/Move/Capture/Home/Win) stays a silent no-op
until Dice is confirmed safe live, rather than repeating round 7.60's
mistake of wiring all 6 blind before confirming the mechanism works at
all. `qtm_play_sfx()` also now checks `music_enabled` first -- calling
`QTM_PlaySample` with no MOD loaded (music off) would have nothing to
play from.

Build clean (`make test`: 410433/410433 checks; `make all`: zero
warnings), deployed via `make deploy`. **Not yet re-confirmed live** --
worth stressing given `QTM_PlaySample`'s register convention is
unconfirmed guesswork, same risk profile as every prior round in this
file marked the same way.

## Round 7.77: QTM_PlaySample accepted but silent -- six guesses, no crash, no sound

Round 7.76's `QTM_PlaySample` call was confirmed live: **no crash**
(the big difference from the `QTM_PlayRawSample` saga), but also no
audible sound at all. What followed was a short, deliberately cautious
sequence of individually-cheap, safe experiments -- each one a single
changed variable, live-tested, logged, and reported back before trying
the next -- rather than another long blind spiral:

1. **Added real return-register logging.** `qtm_play_sfx()` previously
   only logged what it *sent*; it now logs `_kernel_swi()`'s error
   return and the actual out-registers too. Every experiment below was
   diagnosed from this.
2. **Auto-select channel (R0=-1)**: accepted, no error, returned
   `r0_out=4` -- one of the loaded 4-channel module's own music
   channels. Plausible theory: the module's own pattern data
   immediately re-triggers that channel on its next row, cutting off
   the one-shot before it's audible.
3. **Fixed channel 5** (reusing round 7.70's `QTM_PlayRawSample`-era
   constant, deliberately outside the module's own 1-4 range): accepted,
   echoed back exactly, still silent.
4. **Swapped the bundled `QTMModule` from v1.49b to v1.49c.** Checked
   the actual module version bundled in the three real reference
   codebases (`kieranhj/arc-django-2`, `bitshifters/aklang`,
   `bitshifters/mikroreise`) -- all three carry the byte-identical
   (MD5-confirmed) v1.49c (03 Apr 2023), two weeks newer than
   ArchiLudo's existing v1.49b (19 Mar 2023). Since the `QTM_PlaySample`
   SWI number was only ever confirmed against that newer version's own
   source, swapped it in (`assets/audio/QTMModule`, see CREDITS.md).
   Required a full Arculator reboot to actually take effect --
   `RMEnsure QTM 1.49 RMLoad ...` in `app/!Run` only checks the
   numeric "1.49" part, so an already-resident v1.49b from an earlier
   run silently satisfies it and the new module never loads. Same
   result either way: accepted, echoed, silent.
5. **`QTM_SampleVolume` (`&47e5d`)**, set to max (64) once at
   `qtm_initialise()` time -- theory being a separate sample-channel
   master volume, independent of each call's own per-note volume,
   defaulting to muted. Accepted, echoed back 64, no change.
6. **`QTM_RemoveChannel` (`&47e4b`)**, called with R0=5 before starting
   playback -- theory being the underlying engine may only genuinely
   mix as many simultaneous voices as the loaded module's own channel
   count (4), with channels 5-8 being logically valid but never
   actually routed to output unless first freed this way. This produced
   the **one genuinely different result of the whole round**: `in(r0=5)
   -> out(r0=1)` -- the only call in this entire investigation (this
   round or round 7.74's) where the output didn't simply echo the
   input. Followed up by explicitly setting `QTM_SFX_CHANNEL` to 1 (the
   apparent clamp target) for both `QTM_RemoveChannel` and
   `QTM_PlaySample` -- but this time `QTM_RemoveChannel(1)` echoed
   `out(r0=1)` cleanly (no anomaly), and `QTM_PlaySample` with channel 1
   was still silent, with **no audible glitch to the music either**,
   ruling out even a brief real channel-1 collision. The round-5
   anomaly did not reproduce and is unexplained -- noted here rather
   than chased further.

All six experiments are individually documented in `lib/qtm.c`'s own
comments (search `Round 7.77`) rather than only here, since the code
comments are what a future reader hits first. Every one of them stayed
safe -- no crash was ever seen this round, a real and meaningful
difference from `QTM_PlayRawSample`'s behaviour.

**Conclusion**: six independent, individually well-motivated changes
all produced the identical "accepted, echoed, silent" signature, with
only one unreproducible anomaly and no forward progress. No
documentation or working example of `QTM_PlaySample`'s actual register
convention exists anywhere this project's research reached -- not the
RISC OS 3 PRM, not any of the three real Archimedes codebases checked
(none of which actually call it, despite defining its SWI number), not
general web search. Continuing to vary registers blindly was judged
unlikely to converge; per direct user decision, this was raised as a
question on stardot.org.uk instead of continuing to guess:
`https://stardot.org.uk/forums/viewtopic.php?t=33515`. Referenced from
that post: the original lin2LOG/PowerMonger thread
(`https://stardot.org.uk/forums/viewtopic.php?t=27420`, round 7.67) and
the three GitHub repos above
(`https://github.com/kieranhj/arc-django-2`,
`https://github.com/bitshifters/aklang`,
`https://github.com/bitshifters/mikroreise`).

**Current state, pending a forum reply**: code is left exactly as
round 7.76 deployed it -- safe, silent, only `QTM_SFX_DICE` wired,
`sfx_slot[][]` and the MOD-embedding tool both intact and correct
regardless of how the trigger-side mystery resolves. If
`viewtopic.php?t=33515` gets a real answer, that's the next thing to
try -- not another guess.

## Known gaps
