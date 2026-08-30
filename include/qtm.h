#ifndef QTM_H
#define QTM_H

/*
 * ArchiLudo QTM audio wrapper
 * ============================
 *
 * Wraps the QTM (QTheMusic) relocatable module's own SWIs -- background
 * ProTracker-format music playback plus one-shot sample effects -- see
 * docs/QTM.md for the full writeup (SWI numbers/registers, why SFX are
 * embedded as MOD instrument samples rather than played via a raw-sample
 * SWI, and the player-facing manual).
 *
 * This library, not the caller, owns whether QTM is actually present:
 * every function here is always safe to call regardless of
 * qtm_available()'s answer -- if QTM isn't loaded, everything is a silent
 * no-op, matching this project's established "the game must stay
 * playable if an extra falls through" principle.
 *
 * QTM itself is bundled in the app directory (`QTMModule`, see app/!Run)
 * -- freeware, see CREDITS.md -- rather than assumed present on a stock
 * machine.
 */

/* One-shot sample effects -- see qtm_play_sfx(). Each maps to one sample
 * pre-embedded into every bundled MOD's own sample table at build time
 * (tools/mod_embed_sfx.py) -- see sfx_slot[][]/sfx_channel[] in lib/qtm.c
 * and docs/QTM.md's "How SFX actually work" section for the full
 * mechanism (QTM_PlaySample against that sample table, not a raw-sample
 * SWI). */
typedef enum {
	QTM_SFX_DICE = 0,    /* dice thrown */
	QTM_SFX_RELEASE,     /* a pawn released from its home base */
	QTM_SFX_MOVE,        /* a pawn moves one step (per animation tick) */
	QTM_SFX_CAPTURE,     /* an opponent's pawn sent home */
	QTM_SFX_HOME,        /* a pawn reaches its own home column/base */
	QTM_SFX_WIN,         /* the game is won */
	QTM_SFX_COUNT
} qtm_sfx;

/* Number of bundled background music tracks -- see qtm_set_music_track().
 * Matches the `Music1`/`Music2`/`Music3` files bundled in the app
 * directory. src/main.c's Music submenu tracks this automatically (its
 * own MUSIC_MENU_ITEMS/build_music_menu() loop is driven entirely by
 * this constant, not a hardcoded track count). */
#define QTM_MUSIC_TRACK_COUNT 3

/*
 * Function: qtm_initialise
 * Summary: Check whether QTM is actually loaded (see qtm_available()),
 *          and if so, load+convert all QTM_SFX_COUNT sample effects into
 *          memory and start background music at whatever track/enabled
 *          state is current (defaults: track 0, enabled). Call once
 *          during application startup, after game_view_initialise() (the
 *          bundled asset paths are built from game_view_app_dir()).
 * Syntax:  void qtm_initialise(void);
 * Input:   none.
 * Output:  none.
 */
void qtm_initialise(void);

/*
 * Function: qtm_available
 * Summary: Whether QTM is actually present and usable (checked once at
 *          qtm_initialise() time via a live SWI-name lookup, not
 *          assumed) -- src/main.c's Music submenu uses this only to
 *          decide whether the feature is worth surfacing at all; every
 *          other function here is already safe to call regardless.
 * Syntax:  int qtm_available(void);
 * Input:   none.
 * Output:  1 if QTM is loaded and ready, 0 otherwise.
 */
int qtm_available(void);

/*
 * Function: qtm_set_music_enabled / qtm_music_enabled
 * Summary: Turn background music on (audible) or off (muted), and read
 *          the current setting back. Per explicit user request that
 *          music be "selectable and optional" -- src/main.c's Music
 *          submenu "On" entry is a ticked toggle backed by this.
 *
 *          Round 7.81: this MUTES (QTM_MusicVolume) rather than actually
 *          stopping QTM -- the song stays loaded and playing either way,
 *          so qtm_play_sfx() keeps working regardless of this setting
 *          (per direct user request, to be able to test SFX in isolation
 *          without background music potentially masking a quiet one).
 *          This is NOT the same as application shutdown -- see
 *          qtm_shutdown() for actually stopping/releasing QTM.
 * Syntax:  void qtm_set_music_enabled(int enabled);
 *          int qtm_music_enabled(void);
 * Input:   enabled - 0 to mute, non-zero to make audible.
 * Output:  qtm_music_enabled() returns the current setting (1 or 0).
 */
void qtm_set_music_enabled(int enabled);
int qtm_music_enabled(void);

/*
 * Function: qtm_set_music_track / qtm_music_track
 * Summary: Choose which of the QTM_MUSIC_TRACK_COUNT bundled tracks
 *          plays -- always switches immediately (stop, load, start),
 *          regardless of qtm_music_enabled(); the song is always loaded
 *          so its sample table stays available for qtm_play_sfx() (round
 *          7.81), with the current mute state re-applied afterward.
 *          src/main.c's Music submenu "Track 1"/"Track 2" entries are
 *          backed by this, per explicit user request that the track be
 *          switchable from a menu.
 * Syntax:  void qtm_set_music_track(int track);
 *          int qtm_music_track(void);
 * Input:   track - 0..QTM_MUSIC_TRACK_COUNT-1 (clamped if out of range).
 * Output:  qtm_music_track() returns the current track index.
 */
void qtm_set_music_track(int track);
int qtm_music_track(void);

/*
 * Function: qtm_set_sfx_enabled / qtm_sfx_enabled
 * Summary: Turn one-shot SFX on or off, independently of background
 *          music (qtm_set_music_enabled()) -- per explicit user request
 *          to be able to have music with no SFX (or vice versa) rather
 *          than the two being tied together. src/main.c's Music submenu
 *          "SFX" entry is a ticked toggle backed by this.
 * Syntax:  void qtm_set_sfx_enabled(int enabled);
 *          int qtm_sfx_enabled(void);
 * Input:   enabled - 0 to disable, non-zero to enable.
 * Output:  qtm_sfx_enabled() returns the current setting (1 or 0).
 */
void qtm_set_sfx_enabled(int enabled);
int qtm_sfx_enabled(void);

/*
 * Function: qtm_play_sfx
 * Summary: Play one bundled one-shot sample effect immediately (does not
 *          interrupt or otherwise affect background music -- QTM plays
 *          samples and the module song through separate channels). A
 *          silent no-op if QTM isn't available or the given sample
 *          failed to load (e.g. its file was missing).
 * Syntax:  void qtm_play_sfx(qtm_sfx sfx);
 * Input:   sfx - which effect to play.
 * Output:  none.
 */
void qtm_play_sfx(qtm_sfx sfx);

/*
 * Function: qtm_shutdown
 * Summary: Actually stop/release QTM -- unlike qtm_set_music_enabled(0),
 *          which only mutes (round 7.81, see its own doc comment for
 *          why those are now different calls). Call once at application
 *          quit (round 7.75's fix for background music otherwise
 *          continuing to play after ArchiLudo itself closed, since QTM
 *          is a relocatable module independent of this task). A silent
 *          no-op if QTM isn't available.
 * Syntax:  void qtm_shutdown(void);
 * Input:   none.
 * Output:  none.
 */
void qtm_shutdown(void);

#endif
