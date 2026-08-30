/*
 * ArchiLudo QTM audio wrapper -- implementation.
 * See include/qtm.h for the module overview and API docs, and
 * docs/QTM.md for the full research/design writeup this is built from
 * (SWI numbers confirmed against a real working ArchieSDK example and
 * the RISC OS Open forum; the sample-format handling confirmed against
 * the RISC OS 3 PRM's own Sound_SoundLog entry).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <kernel.h>

#include "oslib/os.h"
#include "oslib/sound.h"

#include "qtm.h"
#include "game_view.h"

/* QTM's own SWIs -- not part of OSLib (a third-party module, not core
 * RISC OS). Called via _kernel_swi() (kernel.h, ArchieSDK's own
 * general-purpose SWI mechanism) rather than the simpler GCC __swi()
 * attribute OSLib uses for 1-in/1-out calls -- QTM_PlayRawSample needs
 * up to 9 registers with R0 both an input AND an output, which __swi()
 * can't express.
 *
 * Round 7.74: all confirmed against a real, shipped Archimedes game's
 * own source (kieranhj/arc-django-2 on GitHub, lib/swis.h.asm), not
 * just single working-example calls -- see docs/QTM.md's "Round 7.74"
 * section for the full citation and what it corrected. */
#define QTM_SWI_LOAD             0x47e40
#define QTM_SWI_START            0x47e41
#define QTM_SWI_STOP             0x47e42
#define QTM_SWI_CLEAR            0x47e44
#define QTM_SWI_SOUND_CONTROL    0x47e58
#define QTM_SWI_PLAY_RAW_SAMPLE  0x47e57
/* Round 7.76: plays a sample already embedded in the CURRENTLY LOADED
 * MOD's own sample table, by slot index -- see sfx_slot[][] below and
 * tools/mod_embed_sfx.py. Round 7.78: register convention now CONFIRMED
 * against QTM's own official SWI reference (Documents/Technical/API-SWIs
 * in the official QTM v1.49 distribution ZIP -- see qtm_play_sfx()'s own
 * doc comment and docs/QTM.md's "Round 7.78" section). */
#define QTM_SWI_PLAY_SAMPLE      0x47e54
/* Round 7.77: separate master volume control for the sample channels,
 * distinct from QTM_MusicVolume. Round 7.78: R0 = 0-64 confirmed
 * against QTM's own official SWI reference (see qtm_initialise()'s own
 * QTM_SoundControl comment and docs/QTM.md's "Round 7.78" section) -- a
 * genuine master scaler for QTM_PlaySample/QTM_PlayRawSample output. */
#define QTM_SWI_SAMPLE_VOLUME    0x47e5d
/* Round 7.81: separate master volume for the MUSIC channels only (1-4),
 * confirmed against the official docs alongside QTM_SampleVolume above
 * -- same R0=0-64/-1-to-read convention. Used diagnostically to mute
 * the song while leaving it loaded and playing (so its sample table
 * stays populated for QTM_PlaySample), to test whether the background
 * music is simply masking otherwise-working SFX. */
#define QTM_SWI_MUSIC_VOLUME     0x47e5c

/* Round 7.74: one-shot SFX playback (QTM_PlayRawSample) is DISABLED --
 * see qtm_play_sfx()'s own doc comment for the full "why". Fourteen
 * rounds (7.60-7.73) of live Arculator debugging -- including catching
 * the actual fault live and disassembling the real resampling code, and
 * cross-checking every register value against QTM's own author's
 * reference program -- confirmed the mechanism (a pitch-shifted
 * resampling read that runs unbounded past the sample buffer whenever
 * real playback is actually attempted) but never found a working
 * parameter combination. Sample loading/conversion is left in place
 * (harmless, already working, and useful groundwork if this is revisited
 * with better information -- e.g. QTM's actual source, or a real raw-
 * sample reference from a shipped game, neither found despite research).
 * These constants are the LAST combination tried, kept for that future
 * attempt rather than deleted: */
#define QTM_SFX_NOTE   18  /* 3-octave note (1-36), round 7.69 */
#define QTM_SFX_PERIOD 322 /* Amiga period for 11025Hz, round 7.73;
                             * 3546895/11025 = 321.7 */
#define QTM_SAMPLE_PAD 64  /* trailing safety padding, bytes, round 7.71 */
typedef struct {
	unsigned char *data; /* malloc()'d 8-bit VIDC-log buffer, NULL if this
	                       * sample failed to load (missing file, out of
	                       * memory) -- qtm_play_sfx() silently skips it. */
	int length;           /* bytes */
} qtm_sample;

static qtm_sample samples[QTM_SFX_COUNT];
static int qtm_ok = 0;
static int music_enabled = 1;
static int music_track = 0;
static int sfx_enabled = 1;

/* Round 7.76: per-track SFX->sample-slot table (1-based ProTracker slot
 * index within that track's own MOD sample table; 0 = not embedded in
 * that track). This is the C-side mirror of tools/mod_embed_sfx.py's
 * SFX_SLOTS dict -- keep the two in sync by hand if either changes,
 * there's no shared source of truth between a Python build-time asset
 * tool and this file. Music1/Music2 embed all 6 SFX at the same
 * indices (24-29); Music3 only has room for 3 (28/30/31) after its own
 * artist-authored sample table -- see docs/QTM.md's "Round 7.76"
 * section for why (Music3 genuinely uses all but 3 of its 31 slots,
 * confirmed against the pattern data itself, not just the header).
 * Indexed [music_track][qtm_sfx]. */
static const unsigned char sfx_slot[QTM_MUSIC_TRACK_COUNT][QTM_SFX_COUNT] = {
	/* Music1 */ { 24, 25, 26, 27, 28, 29 },
	/* Music2 */ { 24, 25, 26, 27, 28, 29 },
	/* Music3 */ {  0,  0,  0, 28, 30, 31 },
};

/* Round 7.79: which of QTM's 4 genuinely-free channels (5-8, with
 * 8-channel mode enabled in qtm_initialise() and the 4-channel song on
 * 1-4) each qtm_sfx plays through. Live-tested with every SFX sharing a
 * single fixed channel (5) first -- Release was heard, but Dice never
 * was, because Dice is always immediately followed by a Move trigger
 * (the pawn starts moving right after the roll resolves) and both used
 * the same channel, so Move's own QTM_PlaySample call cut Dice's sound
 * off before it was audible. Spread across all 4 free channels instead
 * so no two SFX likely to fire close together (Dice+Move, in
 * particular) share one. Only 4 channels for 6 events, so Home/Win
 * reuse Dice/Release's channels -- acceptable since Home/Win are rare,
 * end-of-turn/end-of-game events unlikely to overlap with the per-step
 * Move sound. */
/* Round 7.80 diagnostic swap: Dice (channel 5) and Capture (channel 8)
 * both stayed silent even after spreading channels, while Release
 * (channel 6) is the only one confirmed audible so far, regardless of
 * its sample's own loudness (Capture's source recording is actually
 * louder than Release's, per RMS analysis, yet still silent) -- testing
 * whether channel 6 itself is the common factor by moving Dice onto it
 * (and Release off it, onto 5) rather than another sample-side theory. */
static const unsigned char sfx_channel[QTM_SFX_COUNT] = {
	6, /* QTM_SFX_DICE -- was 5 */
	5, /* QTM_SFX_RELEASE -- was 6 */
	7, /* QTM_SFX_MOVE */
	8, /* QTM_SFX_CAPTURE */
	5, /* QTM_SFX_HOME */
	6, /* QTM_SFX_WIN */
};

/* One filename per qtm_sfx value, in enum order -- see include/qtm.h's
 * own doc comment on qtm_sfx for what each maps to. */
static const char *const sfx_leafname[QTM_SFX_COUNT] = {
	"SfxDice", "SfxRelease", "SfxMove", "SfxCapture", "SfxHome", "SfxWin"
};

/*
 * Function: debug_log (internal)
 * Summary: Append one line to the same "Log" file game_view.c's own
 *          debug_log() writes to (this project's established
 *          non-interactive tracing convention) -- a separate copy since
 *          that one is static to game_view.c. Round 7.61: added
 *          temporarily to trace a live "Internal error: abort on data
 *          transfer" crash on the very first qtm_play_sfx() call (dice
 *          throw) -- music itself (QTM_Load/QTM_Start) is confirmed
 *          working live, narrowing this to QTM_PlayRawSample specifically.
 */
static void debug_log(const char *fmt, ...)
{
	const char *dir = game_view_app_dir();
	char path[300];
	FILE *f;
	va_list args;

	if (dir[0] != '\0')
		snprintf(path, sizeof(path), "%s.Log", dir);
	else
		snprintf(path, sizeof(path), "Log");

	f = fopen(path, "a");
	if (f == NULL)
		return;

	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	fclose(f);
}

/*
 * Function: build_asset_path (internal)
 * Summary: `<ArchiLudo$Dir>.<leaf>` -- every QTM-related asset (the
 *          module itself is loaded via app/!Run, not this) lives flat in
 *          the app directory, same convention src/save_view.c's
 *          build_slot_path() already established.
 */
static void build_asset_path(const char *leaf, char *out, size_t out_size)
{
	const char *dir = game_view_app_dir();

	if (dir[0] != '\0')
		snprintf(out, out_size, "%s.%s", dir, leaf);
	else
		snprintf(out, out_size, "%s", leaf);
}

/*
 * Function: load_one_sample (internal)
 * Summary: Read one bundled raw 16-bit signed mono PCM file and convert
 *          it, sample by sample, to the 8-bit VIDC-logarithmic format
 *          QTM_PlayRawSample requires, via the RISC OS Sound system's own
 *          Sound_SoundLog SWI (oslib/sound.h's sound_sound_log()) -- see
 *          docs/QTM.md's "Sample format" section for why this runs at
 *          startup rather than shipping pre-converted files. Leaves
 *          samples[sfx] untouched (data stays NULL) on any failure --
 *          qtm_play_sfx() then just silently skips that effect.
 */
static void load_one_sample(qtm_sfx sfx)
{
	char path[300];
	FILE *f;
	long size;
	int n, i;
	short *pcm16;
	unsigned char *log8;

	build_asset_path(sfx_leafname[sfx], path, sizeof(path));

	f = fopen(path, "rb");
	if (f == NULL) {
		debug_log("qtm load_one_sample: sfx=%d path=\"%s\" fopen failed\n",
		          (int) sfx, path);
		return;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	n = (int) (size / (long) sizeof(short));
	if (n <= 0) {
		fclose(f);
		debug_log("qtm load_one_sample: sfx=%d path=\"%s\" empty (size=%ld)\n",
		          (int) sfx, path, size);
		return;
	}

	pcm16 = malloc((size_t) n * sizeof(short));
	/* Round 7.71: +QTM_SAMPLE_PAD bytes of trailing padding -- confirmed
	 * live via Arculator's own debugger (breaking on the data abort and
	 * disassembling at the exact reported fault address) that the crash
	 * that has followed every SFX attempt since round 7.60 is a classic
	 * pitch-shifted resampling read past the end of the sample buffer:
	 *
	 *   ADD  R1, R1, R2            ; R1 += pitch step (fixed-point pos.)
	 *   LDRB R6, [R0, R1 ASR #12]  ; read source byte at (R1>>12) -- faults
	 *   STRB R6, [R12], R11        ; write to output
	 *
	 * QTM_PlayRawSample's own internal resampler needs to read slightly
	 * past the sample's logical end for interpolation, exactly like
	 * Amiga ProTracker's own sample format requires -- not a bug in the
	 * SWI call itself at all (every register value, channel, and
	 * QTM_SoundControl reservation this whole investigation tried was a
	 * red herring; the buffer was simply too short for what a
	 * pitch-shifted player legitimately needs to read). The padding is
	 * zero-filled below (silence) so any overshoot read stays harmless.
	 * R2 to QTM_PlayRawSample stays the real sample length `n` --
	 * padding is memory safety margin only, not extra audible data. */
	log8 = malloc((size_t) n + QTM_SAMPLE_PAD);
	if (pcm16 == NULL || log8 == NULL) {
		debug_log("qtm load_one_sample: sfx=%d path=\"%s\" malloc failed "
		          "(n=%d, pcm16=%s, log8=%s)\n",
		          (int) sfx, path, n, pcm16 ? "ok" : "NULL", log8 ? "ok" : "NULL");
		free(pcm16);
		free(log8);
		fclose(f);
		return;
	}

	fread(pcm16, sizeof(short), (size_t) n, f);
	fclose(f);

	/* Shift each 16-bit sample up into the top of Sound_SoundLog's own
	 * "32-bit signed integer" input range -- the SWI's PRM entry doesn't
	 * give an explicit expected input scale, so this uses the full
	 * available dynamic range rather than passing the raw 16-bit value
	 * (which would only exercise a tiny fraction of the log curve and
	 * come out far too quiet) -- the standard <<16 convention for
	 * feeding a 16-bit source into this call. Worth an ear-check on real
	 * hardware/Arculator; see docs/QTM.md. */
	for (i = 0; i < n; i++)
		log8[i] = (unsigned char) sound_sound_log(((int) pcm16[i]) << 16);

	/* Pad with genuine VIDC-log silence -- see the QTM_SAMPLE_PAD
	 * allocation comment above. Queried via sound_sound_log(0) rather
	 * than assumed to be byte value 0: standard mu-law-family encodings
	 * don't necessarily map linear silence to an all-zero byte, so any
	 * resampler overshoot into this region plays back as genuine
	 * silence, not a stray click from an unverified guess. */
	memset(log8 + n, (unsigned char) sound_sound_log(0), QTM_SAMPLE_PAD);

	free(pcm16);
	samples[sfx].data = log8;
	samples[sfx].length = n;

	debug_log("qtm load_one_sample: sfx=%d path=\"%s\" n=%d data=&%08x\n",
	          (int) sfx, path, n, (unsigned) (void *) log8);
}

/*
 * Function: load_all_samples (internal)
 * Summary: load_one_sample() for every qtm_sfx, bracketed by pinning the
 *          system sound volume to maximum and restoring it afterwards --
 *          the RISC OS 3 PRM's own Sound_SoundLog entry states its
 *          output "is scaled according to the current volume setting",
 *          so converting at a fixed, known volume keeps the resulting
 *          bytes consistent regardless of whatever the user's own system
 *          volume happens to be at startup. Per-effect playback loudness
 *          is controlled separately, via QTM_PlayRawSample's own R6
 *          parameter (qtm_play_sfx() always passes the maximum, 64).
 */
static void load_all_samples(void)
{
	int saved_volume;
	int i;

	saved_volume = sound_volume(0); /* R0=0 => inspect only, returns current */
	sound_volume(127);              /* PRM: valid range is 1-127 */

	for (i = 0; i < QTM_SFX_COUNT; i++)
		load_one_sample((qtm_sfx) i);

	sound_volume(saved_volume);
}

/*
 * Function: build_music_path (internal)
 * Summary: `<ArchiLudo$Dir>.Music1` .. `.Music<QTM_MUSIC_TRACK_COUNT>` for
 *          track 0..QTM_MUSIC_TRACK_COUNT-1.
 */
static void build_music_path(int track, char *out, size_t out_size)
{
	char leaf[16];

	snprintf(leaf, sizeof(leaf), "Music%d", track + 1);
	build_asset_path(leaf, out, out_size);
}

/*
 * Function: qtm_clear (internal)
 * Summary: Silence QTM (stop whatever is currently playing) via
 *          QTM_Clear -- the best-confirmed available SWI for this (it's
 *          what the reference ArchieSDK example calls once at startup,
 *          before loading its own music, i.e. "reset to a known silent
 *          state"); QTM's own dedicated Stop/Pause SWIs exist per the
 *          pi-star/phlamethrower documentation but their exact SWI
 *          numbers weren't confirmed against a working example, unlike
 *          this one -- see docs/QTM.md.
 */
static void qtm_clear(void)
{
	_kernel_swi_regs regs;

	/* Round 7.74: QTM_SWI_STOP is now confirmed (0x47e42, via
	 * kieranhj/arc-django-2's own swis.h.asm) and would be the more
	 * semantically correct call here, but QTM_Clear is what's actually
	 * been live-tested working for "stop music" throughout this
	 * project's whole audio investigation (the Music menu's own "On"
	 * toggle) -- left as-is rather than swap in an untested call for a
	 * marginal correctness gain. */
	memset(&regs, 0, sizeof(regs));
	_kernel_swi(QTM_SWI_CLEAR, &regs, &regs);
}

/*
 * Function: start_track (internal)
 * Summary: QTM_Load the given track's file, then QTM_Start it. Does
 *          nothing if QTM isn't available.
 */
static void start_track(int track)
{
	char path[300];
	_kernel_swi_regs regs;

	if (!qtm_ok)
		return;

	build_music_path(track, path, sizeof(path));

	memset(&regs, 0, sizeof(regs));
	regs.r[0] = (int) path;
	regs.r[1] = 0; /* matches the confirmed working reference call */
	_kernel_swi(QTM_SWI_LOAD, &regs, &regs);

	memset(&regs, 0, sizeof(regs));
	_kernel_swi(QTM_SWI_START, &regs, &regs);
}

/*
 * Function: apply_music_volume (internal)
 * Summary: QTM_MusicVolume, set from the current music_enabled flag --
 *          64 (audible) or 0 (muted). Round 7.81: broken out of
 *          qtm_set_music_enabled() so start_track() (which reloads the
 *          song, and per the official docs resets its volume) can
 *          re-apply the current mute state afterward too, from
 *          qtm_set_music_track(). Round 7.82 briefly added a duck (lower
 *          music volume while an SFX plays) here -- reverted per direct
 *          user feedback ("really annoying") in favour of boosting the
 *          embedded sample data's own loudness instead (see
 *          tools/mod_embed_sfx.py's pcm16_to_pcm8()).
 */
static void apply_music_volume(void)
{
	_kernel_swi_regs regs;

	if (!qtm_ok)
		return;

	memset(&regs, 0, sizeof(regs));
	regs.r[0] = music_enabled ? 64 : 0;
	_kernel_swi(QTM_SWI_MUSIC_VOLUME, &regs, &regs);
}

void qtm_initialise(void)
{
	os_error *err;
	int swi_no;

	/* Presence check via a live SWI-name lookup (not a hardcoded number
	 * guess) -- xos_swi_number_from_string() is the X-form, so a missing
	 * module returns an error here rather than throwing. Same principle
	 * this project already uses for pawn sprites: stay playable if the
	 * extra isn't there. */
	err = xos_swi_number_from_string("QTM_Load", &swi_no);
	qtm_ok = (err == NULL);
	if (!qtm_ok)
		return;

	/* Round 7.67-7.73 called QTM_SoundControl here believing R0 selected
	 * how many channels to reserve for one-shot samples -- round 7.74
	 * "corrected" that to just a flags bitmask, based on
	 * kieranhj/arc-django-2's own usage (which only ever touched R1) --
	 * round 7.78 found the ORIGINAL round 7.67 belief about R0 was right
	 * all along, confirmed against QTM's own official SWI reference
	 * (Documents/Technical/API-SWIs in the official QTM v1.49 distribution
	 * ZIP -- see docs/QTM.md's "Round 7.78" section for the full story).
	 * R0 genuinely is a channel-count switch (4 or 8); arc-django-2's own
	 * code never contradicted this, it just never happened to touch R0.
	 *
	 * ArchiLudo's MODs are all 4-channel (M.K. tag), which leaves QTM in
	 * 4-channel mode by default -- channels 5-8 are valid QTM_PlaySample
	 * parameter values but were never actually mixed by the DMA engine in
	 * that mode, which is exactly why every round 7.77 attempt on channel
	 * 5 was accepted cleanly (a legal register value) but produced
	 * nothing audible (nothing was actually driving that channel).
	 *
	 * R1=-1 preserves whatever flags are already set (leaving bit 1
	 * clear, so the 4-channel song keeps playing on channels 1-4, not
	 * 5-8 -- see QTM_SoundControl's own doc entry). R2 must be -1 per the
	 * docs ("reserved").
	 *
	 * Round 7.79: called AFTER load_all_samples()/start_track() below,
	 * not before -- the docs describe R0 as setting a "default" applied
	 * "whenever a song or sample causes the sound system to start up",
	 * which reads as safe to call either before or after, but round
	 * 7.78's first attempt (called before) was live-tested and still
	 * produced no sound despite QTM_SoundControl itself returning no
	 * error. Suspected: QTM_Load may re-derive the *active* channel count
	 * from the loaded song's own format tag (ArchiLudo's MODs are all
	 * 4-channel, "M.K."), overriding a "default" set before that load --
	 * calling after start_track() tests whether setting it once the song
	 * is already loaded and playing avoids that. */
	load_all_samples();

	/* Round 7.81: the song is now ALWAYS loaded and started, regardless
	 * of music_enabled -- see qtm_set_music_enabled()'s own comment for
	 * why (SFX need the loaded song's sample table regardless of whether
	 * the music itself is audible). Audibility is controlled purely via
	 * QTM_MusicVolume below. */
	start_track(music_track);

	{
		_kernel_swi_regs regs;
		_kernel_oserror *cerr;

		memset(&regs, 0, sizeof(regs));
		regs.r[0] = 8;
		regs.r[1] = -1;
		regs.r[2] = -1;
		cerr = _kernel_swi(QTM_SWI_SOUND_CONTROL, &regs, &regs);
		debug_log("qtm_initialise: QTM_SoundControl in(r0=8 r1=-1 r2=-1) -> "
		          "err=%s out(r0=%d r1=%d)\n",
		          cerr ? cerr->errmess : "(none)", regs.r[0], regs.r[1]);
	}

	/* Round 7.77: set the sample-channel master volume to max -- kept
	 * from round 7.77's diagnostic pass since it's confirmed correct
	 * against the official docs (R0 = 0-64, a master scaler separate from
	 * QTM_MusicVolume) even though it wasn't round 7.77's actual missing
	 * piece (QTM_SoundControl's channel count was). */
	{
		_kernel_swi_regs regs;
		_kernel_oserror *verr;

		memset(&regs, 0, sizeof(regs));
		regs.r[0] = 64;
		verr = _kernel_swi(QTM_SWI_SAMPLE_VOLUME, &regs, &regs);
		debug_log("qtm_initialise: QTM_SampleVolume in(r0=64) -> err=%s out(r0=%d)\n",
		          verr ? verr->errmess : "(none)", regs.r[0]);
	}

	apply_music_volume();
}

int qtm_available(void)
{
	return qtm_ok;
}

void qtm_set_music_enabled(int enabled)
{
	music_enabled = enabled ? 1 : 0;

	/* Round 7.81: per direct user request, this now mutes the music
	 * channels (QTM_MusicVolume) rather than fully stopping/clearing QTM
	 * (qtm_clear()) -- the song stays loaded and playing either way, so
	 * its sample table stays populated and qtm_play_sfx() keeps working
	 * regardless of whether the music itself is audible. This also
	 * doubles as a way to test SFX in isolation, without background
	 * music potentially masking a quiet one. True shutdown (actually
	 * stopping QTM, e.g. at application quit) is qtm_shutdown(), not
	 * this function -- see its own doc comment. */
	apply_music_volume();
}

int qtm_music_enabled(void)
{
	return music_enabled;
}

void qtm_set_music_track(int track)
{
	if (track < 0)
		track = 0;
	if (track >= QTM_MUSIC_TRACK_COUNT)
		track = QTM_MUSIC_TRACK_COUNT - 1;

	music_track = track;

	/* Round 7.81: unconditional, like qtm_initialise()'s own
	 * start_track() call -- the song must stay loaded regardless of
	 * music_enabled so SFX keep working (see qtm_set_music_enabled()).
	 * Re-applies the mute state afterward since QTM_Load resets volume
	 * (per the official docs' note that a song load re-converts/resets
	 * its sample data -- observed the same for volume in testing). */
	start_track(track);
	apply_music_volume();
}

int qtm_music_track(void)
{
	return music_track;
}

/*
 * Function: qtm_set_sfx_enabled / qtm_sfx_enabled
 * Summary: Turn one-shot SFX on or off, independently of background
 *          music (qtm_set_music_enabled()) -- per explicit user request
 *          to be able to have music with no SFX (or vice versa) rather
 *          than the two being tied together. Purely a flag qtm_play_sfx()
 *          checks; unlike music there's no separate QTM-level volume/mute
 *          SWI involved, since each SFX is only ever triggered momentarily
 *          by qtm_play_sfx() itself (nothing ongoing to mute).
 * Syntax:  void qtm_set_sfx_enabled(int enabled);
 *          int qtm_sfx_enabled(void);
 * Input:   enabled - 0 to disable, non-zero to enable.
 * Output:  qtm_sfx_enabled() returns the current setting (1 or 0).
 */
void qtm_set_sfx_enabled(int enabled)
{
	sfx_enabled = enabled ? 1 : 0;
}

int qtm_sfx_enabled(void)
{
	return sfx_enabled;
}

/*
 * Function: qtm_shutdown
 * Summary: Actually stop/release QTM (unlike qtm_set_music_enabled(0),
 *          which only mutes -- see its own doc comment for why those are
 *          now different). Call once at application quit (round 7.75's
 *          original fix for music continuing to play after ArchiLudo
 *          itself closed -- round 7.81 moved that responsibility to this
 *          dedicated function once qtm_set_music_enabled() stopped fully
 *          stopping QTM). A silent no-op if QTM isn't available.
 * Syntax:  void qtm_shutdown(void);
 * Input:   none.
 * Output:  none.
 */
void qtm_shutdown(void)
{
	if (!qtm_ok)
		return;

	qtm_clear();
}

/*
 * Function: qtm_play_sfx
 * Summary: Plays one bundled one-shot sample effect via QTM_PlaySample
 *          -- a sample already embedded in the CURRENTLY LOADED MOD's
 *          own sample table (see sfx_slot[][] above and
 *          tools/mod_embed_sfx.py), NOT QTM_PlayRawSample (permanently
 *          disabled, round 7.74 -- see docs/QTM.md).
 *
 *          Round 7.76/7.77 wired this cautiously (one event only,
 *          QTM_SFX_DICE) because QTM_PlaySample's register convention
 *          was unconfirmed guesswork at the time. Round 7.78 confirmed
 *          the real convention against QTM's own official SWI reference
 *          (Documents/Technical/API-SWIs, from the official QTM v1.49
 *          distribution ZIP -- see docs/QTM.md's "Round 7.78" section)
 *          and found the actual missing piece was QTM_SoundControl (see
 *          qtm_initialise()) rather than anything in this call itself --
 *          the register layout guessed in round 7.76 was already
 *          correct. Now widened to all QTM_SFX_COUNT events.
 *
 *          A silent no-op if: QTM isn't available, SFX are disabled (see
 *          qtm_set_sfx_enabled()), or the current track has no slot for
 *          this sfx (sfx_slot[][] == 0, e.g. 3 of 6 on Music3). Round
 *          7.81: no longer gated on music_enabled -- the song is now
 *          always loaded regardless of whether the music itself is
 *          muted (see qtm_set_music_enabled()), so SFX keep working
 *          even with music off, and (round 7.84) SFX and music are
 *          independently switchable -- one can be on with the other off.
 *
 * Syntax:  void qtm_play_sfx(qtm_sfx sfx);
 * Input:   sfx - which effect to play.
 * Output:  none.
 */
void qtm_play_sfx(qtm_sfx sfx)
{
	_kernel_swi_regs regs;
	_kernel_oserror *err;
	int slot;

	if (!qtm_ok || !sfx_enabled)
		return;

	slot = sfx_slot[music_track][sfx];
	if (slot == 0)
		return;

	memset(&regs, 0, sizeof(regs));
	/* Round 7.78: fixed channels, confirmed correct per the official docs
	 * -- with QTM_SoundControl's 8-channel mode enabled (see
	 * qtm_initialise()) and its flags left at their default (bit 1
	 * clear), the 4-channel song plays on channels 1-4 and 5-8 are
	 * genuinely free, not just numerically valid. Round 7.79: per-sfx
	 * channel (sfx_channel[] above), not one shared fixed channel -- see
	 * that table's own comment for why. R1/R2/R3 all confirmed against
	 * the official QTM_PlaySample doc entry: R1 = sample number (1-64,
	 * matches the MOD's own 1-31 sample table directly -- no
	 * QTM_RegisterSample step needed for samples already in that range),
	 * R2 = note/period (bits 28-31 clear + a raw value 37-1999 = Amiga
	 * period, matching QTM_SFX_PERIOD's 11025Hz-derived value), R3 =
	 * linear volume (0-64). R4/R5 are only required when R0=-1 (auto
	 * channel select); unused here since a fixed channel is used. */
	regs.r[0] = sfx_channel[sfx];
	regs.r[1] = slot;
	regs.r[2] = QTM_SFX_PERIOD;
	regs.r[3] = 64;
	err = _kernel_swi(QTM_SWI_PLAY_SAMPLE, &regs, &regs);
	debug_log("qtm_play_sfx: QTM_PlaySample sfx=%d track=%d slot=%d "
	          "in(r0=%d r1=%d r2=%d r3=64) -> err=%s "
	          "out(r0=%d r1=%d r2=%d r3=%d)\n",
	          (int) sfx, music_track, slot, sfx_channel[sfx], slot, QTM_SFX_PERIOD,
	          err ? err->errmess : "(none)",
	          regs.r[0], regs.r[1], regs.r[2], regs.r[3]);
}
