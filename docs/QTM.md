# QTM manual

`lib/qtm.c`/`include/qtm.h` wrap the QTM (QTheMusic) relocatable
module's SWIs -- background ProTracker-format music playback plus
one-shot sample effects. QTM itself is bundled directly in the app
directory (`assets/audio/QTMModule` -> `!ArchiLudo/QTMModule,ffa`,
loaded by `app/!Run`'s `RMEnsure` line), not assumed present on a
stock machine -- freeware, see `CREDITS.md`.

## Contents

- [Playing: music and sound effects](#playing-music-and-sound-effects)
- [Why QTM](#why-qtm)
- [SWI reference](#swi-reference)
- [How SFX actually work: MOD-embedded samples](#how-sfx-actually-work-mod-embedded-samples)
- [Loudness normalization](#loudness-normalization)
- [Asset preparation](#asset-preparation)
- [API (`include/qtm.h`)](#api-includeqtmh)
- [Known gaps](#known-gaps)

## Playing: music and sound effects

ArchiLudo plays background music from one of 3 tracks, each an
authentic 4-channel tracker song. Open the **Music** menu (from the
iconbar icon or the game window's own menu) to:

- Turn music **On**/off.
- Turn **SFX** on/off, independently of music.
- Pick a **Track** from its own submenu, by the track's real name.
  Picking a track switches immediately and turns music on if it was
  off.

Six short sound effects play automatically during a game: a dice-roll
sound, a pawn-released-from-home sound, a per-step move sound, a
capture sound (an opponent sent home), a home-column-reached sound,
and a win fanfare. If **Track 3** is selected, only the Capture/Home/
Win sounds play -- Dice/Release/Move are silent on that track
specifically, because that track's own music data uses almost all of
its available sample slots, leaving no room to embed the other three
effects. This is a permanent limitation of that track, not a bug.

If QTM isn't present at all, every music/SFX menu item stays visible
but becomes an inert no-op -- the game is always fully playable
without it.

## Why QTM

Confirmed (via pi-star.co.uk/phlamethrower.co.uk documentation, and a
real, working ArchieSDK example project) to play 4/6/8-channel
ProTracker/FastTracker/StarTrekker `.mod` files, and to work on RISC OS
2 upward (26-bit), well within this project's actual RISC OS 3.10
target. `archieklang` (Kieran Connell's port of the Amiga "AmigaKlang"
soft synth) was considered and not used -- it's a more experimental
soft-synth aimed at full-screen demo contexts with tighter CPU control,
not needed here.

## SWI reference

QTM's SWIs are not part of OSLib (a third-party module, not core RISC
OS), so `lib/qtm.c` declares their numbers itself and calls them via
`_kernel_swi()` (`<kernel.h>`, ArchieSDK's own general-purpose SWI
mechanism).

| SWI | Number | Confirmed via |
|---|---|---|
| `QTM_Load` | `&47E40` | `examples/bydctc/main.c` (ArchieSDK), a real working demo |
| `QTM_Start` | `&47E41` | same |
| `QTM_Clear` | `&47E44` | same |
| `QTM_PlaySample` | `&47E54` | QTM's own official v1.49 distribution archive (full SWI reference + assembler source) |
| `QTM_SoundControl` | resolved by name at runtime | same |

**`QTM_Load`** -- On entry: R0 = pointer to filename, R1 = 0. Loads a
module ready to play.

**`QTM_Start`** -- no parameters. Starts playback of whatever was last
loaded.

**`QTM_Clear`** -- no parameters. Used as a "stop/silence" call.

**`QTM_SoundControl`** -- called once at `qtm_initialise()` time with
R0=8, R1=-1, R2=-1. R0 is a genuine 4/8-channel mode switch: without
this call, QTM stays in 4-channel mode by default, and channels 5-8
are numerically valid to address but are never actually mixed --
`QTM_PlaySample` calls targeting them are silently accepted and
produce no sound. This is the single most important, hardest-won
register value in this whole integration; do not remove or "simplify"
this call.

**`QTM_PlaySample`** -- On entry: R0 = channel (an out-of-range value
relative to the song's own 1-4 channels, e.g. 5-8 once 8-channel mode
is enabled), R1 = sample number within the currently-loaded MOD's own
sample table (1-64), R2 = period (pitch), R3 = volume (0-64). Plays one
sample from the loaded module's own sample table on the given channel,
without disturbing music playback on the song's own channels.

**Abandoned**: `QTM_PlayRawSample` (`&47E57`) was the original approach
for one-shot SFX -- playing a standalone, dynamically-supplied raw
sample rather than one already embedded in a MOD's sample table. After
extensive live debugging (including disassembling the actual fault in
Arculator's own debugger), the root cause was a genuine internal
resampler read pattern walking past the sample buffer with no lookahead
margin for short samples, worsened by a wraparound calculation that
never fires at repeat-length 0 -- no register-value combination avoids
it. Cross-checking three other real, shipped Archimedes codebases found
none of them use `QTM_PlayRawSample` for one-shot effects at all. **Do
not reintroduce this SWI for SFX** -- the working, final mechanism is
SFX embedded as MOD instrument samples, triggered via `QTM_PlaySample`
(see below).

## How SFX actually work: MOD-embedded samples

Each one-shot effect is pre-mixed at build time into the currently-
loaded track's own MOD sample table, as an extra instrument sample --
not loaded or converted at runtime. `tools/mod_embed_sfx.py` splices
each bundled SFX file into `Music1`/`Music2`/`Music3`'s own sample
table (raw 16-bit PCM truncated to the classic ProTracker 8-bit
format), and `lib/qtm.c` triggers one by calling `QTM_PlaySample` with
that sample's slot number and a fixed pitch/period.

Two lookup tables in `lib/qtm.c` must be kept in sync by hand with
`tools/mod_embed_sfx.py`'s own table (there is no shared source of
truth between the Python build tool and the C code):

- `sfx_slot[track][sfx]` -- which sample-table slot holds a given
  effect, per track (0 if that track doesn't carry that effect at all
  -- see Track 3's limitation above).
- `sfx_channel[sfx]` -- which channel plays a given effect. Confined to
  channels 5-6 only, not the full 5-8 range 8-channel mode nominally
  makes available -- channel 8 (the only one QTM_SFX_CAPTURE ever
  used) was reported reliably inaudible in live testing, despite the
  underlying QTM_PlaySample call succeeding and the embedded sample
  data being genuinely present and loud, so channels 7/8 are avoided
  entirely rather than chasing exactly which one is unusable. Current
  map: Dice=6, Release=5, Move=5, Capture=6, Home=6, Win=6 -- Move and
  Release share one channel (the only pair that's mutually exclusive,
  never triggered together), Dice/Capture/Home/Win share the other
  (never triggered back-to-back with each other). Move is called
  unconditionally right after Capture/Home/Win with zero delay, so it
  must never share their channel -- that's what would "cut itself off
  on the same channel" the way an earlier Dice/Move channel-sharing
  attempt did (see lib/qtm.c's own sfx_channel[] comment for the full
  reasoning).

**Playback pitch**: all 6 bundled SFX are stored at a uniform 11025Hz.
`QTM_PlaySample`'s period parameter follows the classic Amiga formula
(`frequency = 3546895 / period`), giving `period = 3546895/11025 ≈
321.7`, rounded to **322** (`QTM_SFX_PERIOD` in `lib/qtm.c`) -- a
single constant covers every bundled effect since they share a sample
rate.

**Vestigial code, not currently used**: `lib/qtm.c` still contains
`load_one_sample()`/`load_all_samples()`, which convert each SFX file
to VIDC's own 8-bit logarithmic audio format via the RISC OS Sound
system's `Sound_SoundLog` SWI -- this was the sample-format machinery
built for the abandoned `QTM_PlayRawSample` approach above. It still
runs at `qtm_initialise()` time and its output is harmless, but it is
no longer used by `qtm_play_sfx()`, which plays entirely from the
MOD-embedded samples instead. Left in place rather than removed as
part of this documentation pass; a future cleanup could delete it.

## Loudness normalization

The 6 bundled SFX had wildly inconsistent source recording levels (RMS
spread over 10x) -- a flat 16-to-8-bit truncation would leave most of
them far too quiet to hear over the music. `tools/mod_embed_sfx.py`
applies an RMS-targeted, `tanh` soft-clip loudness normalization at
build time (not a runtime volume setting) -- `QTM_PlaySample`'s own
volume parameter and `QTM_SampleVolume` are both left at their maximum
(64) always; boosting the embedded sample data itself is the only
lever. Ducking the background music's own volume during SFX playback
was tried and worked, but was rejected after direct user feedback that
it was "really annoying," and is not used.

## Asset preparation

Music tracks were downloaded directly from The Mod Archive's own API
endpoint (`https://api.modarchive.org/downloads.php?moduleid=<id>`) --
note the ordinary web UI (`modarchive.org/index.php?...`) blocks
automated fetches (503), but this direct download endpoint doesn't.
Confirmed genuine 4-channel ProTracker (`M.K.` tag at file offset 1080)
via `file(1)` before bundling.

SFX were sourced as OGG/WAV (see `CREDITS.md`) and converted with
`ffmpeg -i <src> -ac 1 -ar 11025 -f s16le <name>.pcm` (mono, 11025Hz,
16-bit signed, headerless). `SfxWin` was additionally trimmed to 2.5
seconds with a fade-out, since the original fanfare was too long for a
short game-won sting once embedded into a MOD's own sample table
alongside 5 other effects.

## API (`include/qtm.h`)

```c
void qtm_initialise(void);                 /* call once at startup, after
                                             * game_view_initialise() */
int  qtm_available(void);                  /* is QTM actually present? */

void qtm_set_music_enabled(int enabled);   /* mutes via QTM_MusicVolume --
                                             * the song stays loaded, so
                                             * qtm_play_sfx() keeps working
                                             * either way */
int  qtm_music_enabled(void);
void qtm_set_music_track(int track);       /* 0..QTM_MUSIC_TRACK_COUNT-1 */
int  qtm_music_track(void);

void qtm_set_sfx_enabled(int enabled);     /* independent of music */
int  qtm_sfx_enabled(void);

void qtm_play_sfx(qtm_sfx sfx);            /* QTM_SFX_DICE/RELEASE/MOVE/
                                             * CAPTURE/HOME/WIN */

void qtm_shutdown(void);                   /* actually stop/release QTM --
                                             * call once at application quit;
                                             * qtm_set_music_enabled(0) only
                                             * mutes, it does not shut down */
```

Every function is always safe to call regardless of `qtm_available()`'s
answer -- if QTM isn't loaded, everything is a silent no-op.

`src/game_view.c` calls `qtm_play_sfx()` at 6 points: the dice-roll
animation start (Dice), a pawn's release from home (Release, whether
via the mandatory-six path or an ordinary optional release),
`start_move_animation()`'s normal step path (Move), and capture/home/
win are all detected around the single `ludo_move_pawn()` call in
`start_move_animation()` (its own return value for capture; a
before/after comparison of the moved pawn's `finished` flag and
`game.winner` for home/win).

`src/main.c`'s iconbar/window menu has a "Music" submenu: a ticked
"On" toggle, a ticked "SFX" toggle, and one ticked "Track" entry per
`QTM_MUSIC_TRACK_COUNT`, refreshed just before the menu opens. Picking
a track also turns music on if it was off, so a track pick never
silently does nothing.

## Known gaps

- Track 3's missing 3 SFX (Dice/Release/Move) is a permanent scope
  limit of that track's own sample table, not something fixable
  without re-authoring its music data.
- `QTM_RegisterSample` (an alternative, dynamic-sample-loading SWI)
  was never tried -- not needed for ArchiLudo's fixed 6-SFX set, only
  relevant if the game ever needed runtime-loaded/user-added sounds.
