/*
 * ArchiLudo QTM audio wrapper -- implementation.
 * See include/qtm.h for the module overview and API docs, and
 * docs/QTM.md for the full research/design writeup this is built from
 * (SWI numbers confirmed against a real working ArchieSDK example and
 * the RISC OS Open forum).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <kernel.h>

#include "oslib/os.h"

#include "qtm.h"
#include "game_view.h"

/* QTM's own SWIs -- not part of OSLib (a third-party module, not core
 * RISC OS). Called via _kernel_swi() (kernel.h, ArchieSDK's own
 * general-purpose SWI mechanism) rather than the simpler GCC __swi()
 * attribute OSLib uses for 1-in/1-out calls -- QTM_PlayRawSample needs
 * up to 9 registers with R0 both an input AND an output, which __swi()
 * can't express.
 *
 * All confirmed against a real, shipped Archimedes game's own source
 * (kieranhj/arc-django-2 on GitHub, lib/swis.h.asm), not just a single
 * working-example call -- see docs/QTM.md's "SWI reference" section. */
#define QTM_SWI_LOAD             0x47e40
#define QTM_SWI_START            0x47e41
#define QTM_SWI_STOP             0x47e42
#define QTM_SWI_CLEAR            0x47e44
#define QTM_SWI_SOUND_CONTROL    0x47e58
/* QTM_PlayRawSample (0x47e57) is deliberately NOT defined here -- see
 * the "abandoned entirely" comment below; no code calls it. */
/* Plays a sample already embedded in the CURRENTLY LOADED MOD's own
 * sample table, by slot index -- see sfx_slot[][] below and
 * tools/mod_embed_sfx.py. Register convention confirmed against QTM's
 * own official SWI reference (Documents/Technical/API-SWIs in the
 * official QTM v1.49 distribution ZIP -- see qtm_play_sfx()'s own doc
 * comment and docs/QTM.md's "SWI reference" section). */
#define QTM_SWI_PLAY_SAMPLE      0x47e54
/* Separate master volume control for the sample channels, distinct
 * from QTM_MusicVolume. R0 = 0-64, confirmed against QTM's own official
 * SWI reference (see qtm_initialise()'s own QTM_SoundControl comment
 * and docs/QTM.md's "SWI reference" section) -- a genuine master
 * scaler for QTM_PlaySample output. */
#define QTM_SWI_SAMPLE_VOLUME    0x47e5d
/* Separate master volume for the MUSIC channels only (1-4), confirmed
 * against the official docs alongside QTM_SampleVolume above -- same
 * R0=0-64/-1-to-read convention. Used to mute the song while leaving it
 * loaded and playing, since its own sample table must stay populated
 * for QTM_PlaySample to keep working (see qtm_set_music_enabled()). */
#define QTM_SWI_MUSIC_VOLUME     0x47e5c

/* QTM_PlayRawSample-based one-shot SFX playback is abandoned entirely
 * -- see qtm_play_sfx()'s own doc comment for why, and docs/QTM.md.
 * Extensive live Arculator debugging (including catching the actual
 * fault live and disassembling the real resampling code, and cross-
 * checking every register value against QTM's own author's reference
 * program) traced the mechanism to a pitch-shifted resampling read
 * that runs unbounded past the sample buffer whenever real playback is
 * attempted, with no working parameter combination found. Do not
 * reintroduce this SWI for SFX -- the working, live mechanism is SFX
 * embedded as MOD instrument samples, triggered via QTM_PlaySample
 * (played from the currently-loaded MOD's own sample table, not a
 * standalone buffer at all). The sample loading/8-bit VIDC-log
 * conversion machinery this abandoned approach needed (reading
 * assets/audio/Sfx* at startup, Sound_SoundLog) has been removed
 * entirely, not just left unused -- see git history if it's ever
 * needed for reference. */
#define QTM_SFX_PERIOD 322 /* Amiga period for all 6 bundled SFX, which share
                             * a uniform 11025Hz sample rate:
                             * 3546895/11025 = 321.7, rounded to 322 */

static int qtm_ok = 0;
static int music_enabled = 1;
static int music_track = 0;
static int sfx_enabled = 1;

/* Per-track SFX->sample-slot table (1-based ProTracker slot index
 * within that track's own MOD sample table; 0 = not embedded in that
 * track). This is the C-side mirror of tools/mod_embed_sfx.py's
 * SFX_SLOTS dict -- keep the two in sync by hand if either changes,
 * there's no shared source of truth between a Python build-time asset
 * tool and this file. Music1/Music2 embed all 6 SFX at the same
 * indices (24-29); Music3 only has room for 3 (28/30/31), since its own
 * artist-authored sample table genuinely uses all but 3 of its 31
 * slots (confirmed against the pattern data itself, not just the
 * header) -- see docs/QTM.md. Indexed [music_track][qtm_sfx]. */
static const unsigned char sfx_slot[QTM_MUSIC_TRACK_COUNT][QTM_SFX_COUNT] = {
	/* Music1 */ { 24, 25, 26, 27, 28, 29 },
	/* Music2 */ { 24, 25, 26, 27, 28, 29 },
	/* Music3 */ {  0,  0,  0, 28, 30, 31 },
};

/* Which of QTM's free channels each qtm_sfx plays through -- confined to
 * channels 5-6 only, NOT the full 5-8 QTM_SoundControl's 8-channel mode
 * nominally makes available. QTM_SFX_CAPTURE (the only effect ever
 * assigned channel 8) was reported reliably inaudible in live testing --
 * the SWI call itself succeeds (confirmed via debug_log: QTM_PlaySample
 * returns no error, registers echoed back exactly as sent) and the
 * embedded sample data is genuinely present and loud (88% of 8-bit full
 * scale, comparable to the other SFX), so this isn't a data/loudness
 * problem -- channels 7/8 just don't reliably produce sound in
 * practice. Rather than chase exactly which of 7/8 is actually usable,
 * confined to the two channels (5, 6) every other SFX was already
 * proven working on.
 *
 * Only 2 channels for 6 events means real sharing, so which SFX share a
 * channel matters: QTM_SFX_MOVE is called unconditionally right after
 * QTM_SFX_CAPTURE/_HOME/_WIN in start_move_animation() (see
 * src/game_view.c) with zero delay between the two calls -- confirmed
 * via this project's own history that a shared channel lets the second
 * QTM_PlaySample call cut the first's sound off before it's audible
 * (the original reason DICE and MOVE were split across channels in the
 * first place, since a roll's resolution chains straight into a move).
 * QTM_SFX_RELEASE is the only event that's mutually exclusive with
 * QTM_SFX_MOVE (a move is either a release, handled in a separate
 * zero-distance branch that returns before ever reaching the MOVE
 * call, or an ordinary move -- never both), so it's the only safe
 * channel-mate for MOVE. DICE, CAPTURE, HOME, and WIN are never
 * triggered back-to-back with each other (DICE fires once per roll,
 * well before a pawn is even chosen; CAPTURE/HOME/WIN are mutually
 * exclusive alternatives of each other, chosen by start_move_
 * animation()'s own if/else-if chain), so all four safely share the
 * other channel. */
static const unsigned char sfx_channel[QTM_SFX_COUNT] = {
	6, /* QTM_SFX_DICE */
	5, /* QTM_SFX_RELEASE */
	5, /* QTM_SFX_MOVE */
	6, /* QTM_SFX_CAPTURE */
	6, /* QTM_SFX_HOME */
	6, /* QTM_SFX_WIN */
};

/*
 * Function: debug_log (internal)
 * Summary: Append one line to the same "Log" file game_view.c's own
 *          debug_log() writes to (this project's established
 *          non-interactive tracing convention) -- a separate copy since
 *          that one is static to game_view.c.
 *
 *          Compiled out entirely (to a no-op that never even evaluates
 *          its own arguments) unless built with `make DEBUG_LOG=1` --
 *          see the Makefile's own ARCHILUDO_DEBUG_LOG comment and
 *          game_view.c's own debug_log() doc comment.
 * Syntax:  static void debug_log(const char *fmt, ...);
 * Input:   fmt - printf-style format string, followed by its matching
 *                arguments.
 * Output:  none. Appends one line to the log file (or is silently
 *          skipped if it can't be opened); does nothing at all in a
 *          non-DEBUG_LOG build.
 */
#ifdef ARCHILUDO_DEBUG_LOG
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
#else
#define debug_log(...) ((void) 0)
#endif

/*
 * Function: build_asset_path (internal)
 * Summary: `<ArchiLudo$Dir>.<leaf>` -- every QTM-related asset (the
 *          module itself is loaded via app/!Run, not this) lives flat in
 *          the app directory, same convention src/save_view.c's
 *          build_slot_path() already established.
 * Syntax:  static void build_asset_path(const char *leaf, char *out,
 *                                       size_t out_size);
 * Input:   leaf     - the asset's own leafname (e.g. "Music1").
 *          out      - buffer to receive the full path.
 *          out_size - size of `out` in bytes.
 * Output:  none. `out` holds the full, nul-terminated path.
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
 * Function: build_music_path (internal)
 * Summary: `<ArchiLudo$Dir>.Music1` .. `.Music<QTM_MUSIC_TRACK_COUNT>` for
 *          track 0..QTM_MUSIC_TRACK_COUNT-1.
 * Syntax:  static void build_music_path(int track, char *out,
 *                                       size_t out_size);
 * Input:   track    - 0..QTM_MUSIC_TRACK_COUNT-1.
 *          out      - buffer to receive the full path.
 *          out_size - size of `out` in bytes.
 * Output:  none. `out` holds the full, nul-terminated path.
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
 * Syntax:  static void qtm_clear(void);
 * Input:   none.
 * Output:  none.
 */
static void qtm_clear(void)
{
	_kernel_swi_regs regs;

	/* QTM_SWI_STOP (0x47e42, confirmed via kieranhj/arc-django-2's own
	 * swis.h.asm) would be the more semantically correct call here, but
	 * QTM_Clear is what's actually been live-tested working for "stop
	 * music" (the Music menu's own "On" toggle) -- left as-is rather
	 * than swap in an untested call for a marginal correctness gain. */
	memset(&regs, 0, sizeof(regs));
	_kernel_swi(QTM_SWI_CLEAR, &regs, &regs);
}

/*
 * Function: start_track (internal)
 * Summary: QTM_Load the given track's file, then QTM_Start it. Does
 *          nothing if QTM isn't available.
 * Syntax:  static void start_track(int track);
 * Input:   track - 0..QTM_MUSIC_TRACK_COUNT-1.
 * Output:  none.
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
 *          64 (audible) or 0 (muted). Broken out into its own function
 *          so start_track() (which reloads the song, and per the
 *          official docs resets its volume) can re-apply the current
 *          mute state afterward too, from qtm_set_music_track(). Ducking
 *          the music volume while an SFX plays was tried here and
 *          reverted per direct user feedback ("really annoying") in
 *          favour of boosting the embedded sample data's own loudness
 *          instead (see tools/mod_embed_sfx.py's pcm16_to_pcm8()).
 * Syntax:  static void apply_music_volume(void);
 * Input:   none. Reads the current `music_enabled` flag.
 * Output:  none.
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

	/* R0 is a genuine channel-count switch (4 or 8), confirmed against
	 * QTM's own official SWI reference (Documents/Technical/API-SWIs in
	 * the official QTM v1.49 distribution ZIP) -- see docs/QTM.md's
	 * "SWI reference" section. ArchiLudo's MODs are all 4-channel (M.K.
	 * tag), which leaves QTM in 4-channel mode by default: channels 5-8
	 * are legal QTM_PlaySample parameter values but are never actually
	 * mixed by the DMA engine in that mode, so a call targeting one of
	 * them is accepted cleanly (a legal register value) but produces
	 * nothing audible until 8-channel mode is enabled here.
	 *
	 * R1=-1 preserves whatever flags are already set (leaving bit 1
	 * clear, so the 4-channel song keeps playing on channels 1-4, not
	 * 5-8 -- see QTM_SoundControl's own doc entry). R2 must be -1 per the
	 * docs ("reserved").
	 *
	 * Called AFTER start_track() below, not before -- QTM_Load appears
	 * to re-derive the *active* channel count from the loaded song's
	 * own format tag (all of ArchiLudo's MODs are 4-channel, "M.K."),
	 * overriding a channel-count setting made before that load; calling
	 * after start_track() avoids this. */

	/* The song is always loaded and started, regardless of
	 * music_enabled -- see qtm_set_music_enabled()'s own comment for
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
		(void) cerr; /* only read by debug_log() below, which compiles
		              * away entirely in a release build */
		debug_log("qtm_initialise: QTM_SoundControl in(r0=8 r1=-1 r2=-1) -> "
		          "err=%s out(r0=%d r1=%d)\n",
		          cerr ? cerr->errmess : "(none)", regs.r[0], regs.r[1]);
	}

	/* Set the sample-channel master volume to max -- confirmed correct
	 * against the official docs (R0 = 0-64, a master scaler separate
	 * from QTM_MusicVolume). */
	{
		_kernel_swi_regs regs;
		_kernel_oserror *verr;

		memset(&regs, 0, sizeof(regs));
		regs.r[0] = 64;
		verr = _kernel_swi(QTM_SWI_SAMPLE_VOLUME, &regs, &regs);
		(void) verr; /* only read by debug_log() below, which compiles
		              * away entirely in a release build */
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

	/* Mutes the music channels (QTM_MusicVolume) rather than fully
	 * stopping/clearing QTM (qtm_clear()) -- the song stays loaded and
	 * playing either way, so its sample table stays populated and
	 * qtm_play_sfx() keeps working regardless of whether the music
	 * itself is audible. This also allows testing SFX in isolation,
	 * without background music potentially masking a quiet one. True
	 * shutdown (actually stopping QTM, e.g. at application quit) is
	 * qtm_shutdown(), not this function -- see its own doc comment. */
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

	/* Unconditional, like qtm_initialise()'s own start_track() call --
	 * the song must stay loaded regardless of music_enabled so SFX keep
	 * working (see qtm_set_music_enabled()). Re-applies the mute state
	 * afterward since QTM_Load resets volume (per the official docs'
	 * note that a song load re-converts/resets its sample data --
	 * observed the same for volume in testing). */
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
 *          which only mutes -- see its own doc comment for why those
 *          are different). Call once at application quit, so background
 *          music doesn't keep playing after ArchiLudo itself has
 *          closed (QTM is a relocatable module independent of this
 *          task). A silent no-op if QTM isn't available.
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
 *          tools/mod_embed_sfx.py), NOT QTM_PlayRawSample (abandoned
 *          entirely -- see docs/QTM.md).
 *
 *          Register convention confirmed against QTM's own official
 *          SWI reference (Documents/Technical/API-SWIs, from the
 *          official QTM v1.49 distribution ZIP -- see docs/QTM.md's
 *          "SWI reference" section); the real missing piece for
 *          getting this to actually produce sound was
 *          QTM_SoundControl's 8-channel mode (see qtm_initialise()),
 *          not this call's own register layout.
 *
 *          A silent no-op if: QTM isn't available, SFX are disabled
 *          (see qtm_set_sfx_enabled()), or the current track has no
 *          slot for this sfx (sfx_slot[][] == 0, e.g. 3 of 6 on
 *          Music3). Not gated on music_enabled -- the song is always
 *          loaded regardless of whether the music itself is muted (see
 *          qtm_set_music_enabled()), so SFX keep working even with
 *          music off; SFX and music are independently switchable, one
 *          can be on with the other off.
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
	/* Fixed channels, confirmed correct per the official docs -- with
	 * QTM_SoundControl's 8-channel mode enabled (see qtm_initialise())
	 * and its flags left at their default (bit 1 clear), the 4-channel
	 * song plays on channels 1-4 and 5-8 are genuinely free, not just
	 * numerically valid. Per-sfx channel (sfx_channel[] above), not one
	 * shared fixed channel -- see that table's own comment for why.
	 * R1/R2/R3 all confirmed against
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
	(void) err; /* only read by debug_log() below, which compiles away
	             * entirely in a release build */
	debug_log("qtm_play_sfx: QTM_PlaySample sfx=%d track=%d slot=%d "
	          "in(r0=%d r1=%d r2=%d r3=64) -> err=%s "
	          "out(r0=%d r1=%d r2=%d r3=%d)\n",
	          (int) sfx, music_track, slot, sfx_channel[sfx], slot, QTM_SFX_PERIOD,
	          err ? err->errmess : "(none)",
	          regs.r[0], regs.r[1], regs.r[2], regs.r[3]);
}
