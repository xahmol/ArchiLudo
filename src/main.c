#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "oslib/wimp.h"
#include "archiludo.h"
#include "game_view.h"
#include "setup_view.h"
#include "splash_view.h"
#include "save_view.h"
#include "win_view.h"
#include "rules_view.h"
#include "qtm.h"

wimp_t task_handle;

#define ICONBAR_MENU_ITEMS      6
#define ICONBAR_MENU_NEW_GAME   0
#define ICONBAR_MENU_SAVE_GAME  1
#define ICONBAR_MENU_LOAD_GAME  2
#define ICONBAR_MENU_MUSIC      3
#define ICONBAR_MENU_ABOUT      4
#define ICONBAR_MENU_QUIT       5

static wimp_MENU(ICONBAR_MENU_ITEMS) iconbar_menu;

/* The "Music" iconbar/window menu entry's own submenu -- "On" (a
 * ticked toggle), "SFX" (a second, independent ticked toggle, so music
 * and SFX can be switched on/off separately), and "Track" (itself a
 * further submenu, see track_menu below, showing each track's own full
 * title rather than a generic "Track N"). A no-op menu (present, but
 * every click on it does nothing) if qtm_available() is false, e.g.
 * QTM isn't loaded -- the entries themselves stay visible rather than
 * disappearing, so it's obvious the feature exists even when silent. */
#define MUSIC_MENU_ON    0
#define MUSIC_MENU_SFX   1
#define MUSIC_MENU_TRACK 2
#define MUSIC_MENU_ITEMS 3

static wimp_MENU(MUSIC_MENU_ITEMS) music_menu;

/* "Track"'s own submenu -- one ticked entry per bundled track, showing
 * its real title (not "Track N"). Indirected text (wimp_ICON_INDIRECTED):
 * at up to ~20 characters, these titles don't fit the 12-byte inline
 * menu-entry text field every other menu in this project uses. Titles
 * are the bundled `.mod` files' own embedded song titles (confirmed via
 * `file`/hex inspection when each was sourced, see CREDITS.md), not
 * invented -- kept here as a parallel fixed array, the same convention
 * lib/qtm.c's own sfx_leafname[] already uses for a per-index fixed
 * string table. */
#define TRACK_MENU_ITEMS QTM_MUSIC_TRACK_COUNT

static wimp_MENU(TRACK_MENU_ITEMS) track_menu;

static const char *const track_titles[QTM_MUSIC_TRACK_COUNT] = {
	"digital innovation1", "lk's doskpop", "on the run"
};

/*
 * Function: create_iconbar_icon
 * Summary: Register ArchiLudo's icon on the right-hand side of the icon
 *          bar. SELECT click opens src/setup_view.c's "New Game" dialogue
 *          the first time, or reopens the game already in progress
 *          afterwards (see main_dispatch(), game_view_has_started());
 *          MENU click shows the shared app menu.
 *
 *          Plots the real "!ArchiLudo" sprite (a red pawn beside a
 *          die -- see assets/generate_app_icon.py,
 *          docs/BUILDCHAIN.md's "Application directory" section), not a
 *          placeholder text icon. Plain (non-indirected) sprite icon:
 *          `app/!Run`'s `IconSprites
 *          <ArchiLudo$Dir>.!Sprites` line (run before this task even
 *          starts, see main()'s own doc comment -- Wimp_Initialise
 *          happens after !Run's IconSprites line executes) has already
 *          loaded "!ArchiLudo"/"sm!ArchiLudo" into the Wimp's shared
 *          sprite pool by the time this runs, so no local sprite area is
 *          needed here (contrast game_view.c's pawn sprites, which use a
 *          private malloc()'d area precisely because THEY aren't meant to
 *          be globally shared). Falls back to the Wimp's own generic
 *          "application" pool sprite if "!ArchiLudo" isn't found for any
 *          reason (e.g. run directly as a bare file during development,
 *          bypassing !Run) -- this is Wimp_CreateIcon's own standard
 *          missing-sprite behaviour, nothing this code needs to handle
 *          explicitly.
 * Syntax:  static void create_iconbar_icon(void);
 * Input:   none.
 * Output:  none. Creates the iconbar icon as a side effect.
 */
static void create_iconbar_icon(void)
{
	wimp_icon_create iconbar_icon;

	iconbar_icon.w = wimp_ICON_BAR_RIGHT;
	iconbar_icon.icon.extent.x0 = 0;
	iconbar_icon.icon.extent.y0 = 0;
	iconbar_icon.icon.extent.x1 = 68;
	iconbar_icon.icon.extent.y1 = 68;
	iconbar_icon.icon.flags = wimp_ICON_SPRITE | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED
	                        | (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	strncpy(iconbar_icon.icon.data.sprite, "!ArchiLudo", 12);

	wimp_create_icon(&iconbar_icon);
}

/*
 * Function: set_menu_entry
 * Summary: Fill in one menu entry's flags/text -- every entry in
 *          iconbar_menu shares the same plain-text appearance, so this
 *          avoids repeating the same four-flag OR expression five times.
 * Syntax:  static void set_menu_entry(wimp_menu_entry *entry,
 *              const char *text, int is_last);
 * Input:   entry   - the menu entry to fill in.
 *          text    - up to 12 characters of plain entry text, copied
 *                    into the entry's inline (non-indirected) buffer.
 *          is_last - non-zero if this is the menu's final entry (sets
 *                    wimp_MENU_LAST).
 * Output:  none. `entry` is filled in on return.
 */
static void set_menu_entry(wimp_menu_entry *entry, const char *text, int is_last)
{
	entry->menu_flags = is_last ? wimp_MENU_LAST : 0;
	entry->sub_menu = wimp_NO_SUB_MENU;
	entry->icon_flags = wimp_ICON_TEXT | wimp_ICON_FILLED | wimp_ICON_VCENTRED
	                   | (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT)
	                   | (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);
	strncpy(entry->data.text, text, 12);
}

/*
 * Function: build_iconbar_menu
 * Summary: Build the (fixed, never rebuilt) iconbar/window menu: "New
 *          Game" (opens src/setup_view.c's player-configuration
 *          dialogue), "Save Game"/"Load Game" (open src/save_view.c's
 *          dialogues), "Music" (opens the Music submenu, see
 *          build_music_menu()), "About" (reopens src/splash_view.c's
 *          splash/about window, shown automatically once at startup
 *          too), and "Quit".
 * Syntax:  static void build_iconbar_menu(void);
 * Input:   none.
 * Output:  none. Fills in the module-scope iconbar_menu.
 */
static void build_iconbar_menu(void)
{
	strncpy(iconbar_menu.title_data.text, APP_NAME, 12);
	iconbar_menu.title_fg = wimp_COLOUR_BLACK;
	iconbar_menu.title_bg = wimp_COLOUR_LIGHT_GREY;
	iconbar_menu.work_fg = wimp_COLOUR_BLACK;
	iconbar_menu.work_bg = wimp_COLOUR_WHITE;
	iconbar_menu.width = 200;
	iconbar_menu.height = wimp_MENU_ITEM_HEIGHT;
	iconbar_menu.gap = wimp_MENU_ITEM_GAP;

	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_NEW_GAME], "New Game", 0);
	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_SAVE_GAME], "Save Game", 0);
	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_LOAD_GAME], "Load Game", 0);
	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_MUSIC], "Music", 0);
	iconbar_menu.entries[ICONBAR_MENU_MUSIC].sub_menu = (wimp_menu *) &music_menu;
	/* "About ArchiLudo" doesn't fit the fixed 12-byte inline menu-icon
	 * text buffer, hence the shorter "About". */
	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_ABOUT], "About", 0);
	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_QUIT], "Quit", 1);
}

/*
 * Function: build_music_menu
 * Summary: Build the (fixed, never rebuilt) Music submenu: "On" (a
 *          ticked toggle) and "Track" (opens track_menu, see
 *          build_track_menu()) -- ticks/sub_menu are set up here since
 *          neither changes after creation; refresh_music_menu_ticks()
 *          only ever updates "On"'s tick. Built and shown regardless of
 *          qtm_available() -- see include/qtm.h's own doc comment on why
 *          this stays visible (rather than being hidden) when QTM isn't
 *          actually present.
 * Syntax:  static void build_music_menu(void);
 * Input:   none.
 * Output:  none. Fills in the module-scope music_menu.
 */
static void build_music_menu(void)
{
	strncpy(music_menu.title_data.text, "Music", 12);
	music_menu.title_fg = wimp_COLOUR_BLACK;
	music_menu.title_bg = wimp_COLOUR_LIGHT_GREY;
	music_menu.work_fg = wimp_COLOUR_BLACK;
	music_menu.work_bg = wimp_COLOUR_WHITE;
	music_menu.width = 150;
	music_menu.height = wimp_MENU_ITEM_HEIGHT;
	music_menu.gap = wimp_MENU_ITEM_GAP;

	set_menu_entry(&music_menu.entries[MUSIC_MENU_ON], "On", 0);
	set_menu_entry(&music_menu.entries[MUSIC_MENU_SFX], "SFX", 0);
	set_menu_entry(&music_menu.entries[MUSIC_MENU_TRACK], "Track", 1);
	music_menu.entries[MUSIC_MENU_TRACK].sub_menu = (wimp_menu *) &track_menu;
}

/*
 * Function: build_track_menu
 * Summary: Build the (fixed, never rebuilt) Track submenu -- one entry
 *          per bundled track, showing its real title via indirected text
 *          (track_titles[], which the Wimp reads directly rather than
 *          copying into the entry's own 12-byte inline buffer -- these
 *          titles run well past 12 characters). Ticks reflect the
 *          current selection and are refreshed by
 *          refresh_track_menu_ticks() just before the menu opens.
 * Syntax:  static void build_track_menu(void);
 * Input:   none.
 * Output:  none. Fills in the module-scope track_menu.
 */
static void build_track_menu(void)
{
	int i;
	wimp_menu_entry *entry;

	strncpy(track_menu.title_data.text, "Track", 12);
	track_menu.title_fg = wimp_COLOUR_BLACK;
	track_menu.title_bg = wimp_COLOUR_LIGHT_GREY;
	track_menu.work_fg = wimp_COLOUR_BLACK;
	track_menu.work_bg = wimp_COLOUR_WHITE;
	/* Wide enough for "digital innovation1" (20 characters) at the
	 * system font's fixed-width menu rendering, plus margin -- see
	 * game_view.h's GAME_VIEW_NAME_LEN comment for the same
	 * 16-units/character convention this project already relies on
	 * elsewhere. */
	track_menu.width = 22 * 16;
	track_menu.height = wimp_MENU_ITEM_HEIGHT;
	track_menu.gap = wimp_MENU_ITEM_GAP;

	for (i = 0; i < QTM_MUSIC_TRACK_COUNT; i++) {
		entry = &track_menu.entries[i];
		entry->menu_flags = (i == QTM_MUSIC_TRACK_COUNT - 1) ? wimp_MENU_LAST : 0;
		entry->sub_menu = wimp_NO_SUB_MENU;
		entry->icon_flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_FILLED |
		                    wimp_ICON_VCENTRED
		                  | (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT)
		                  | (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);
		/* track_titles[] entries are string literals, never written to --
		 * the cast away from const matches how this project's other
		 * indirected-text icons already assign literals directly to this
		 * same (non-const, per OSLib's own struct) field, e.g.
		 * save_view.c's "Save"/"Cancel" button labels. */
		entry->data.indirected_text.text = (char *) track_titles[i];
	}
}

/*
 * Function: refresh_music_menu_ticks
 * Summary: Set the Music submenu's TICKED flag on "On" (per
 *          qtm_music_enabled()) and "SFX" (per qtm_sfx_enabled() --
 *          independent of "On") -- called right before the
 *          shared menu opens (see main_dispatch()'s wimp_CLICK_MENU
 *          handling), the same "just-in-time" approach as recomputing a
 *          menu's contents via Menu_Warning, but simpler since only tick
 *          state ever changes here, not the entries themselves. Track's
 *          own tick state is refreshed separately, by
 *          refresh_track_menu_ticks(), just before *that* submenu opens.
 * Syntax:  static void refresh_music_menu_ticks(void);
 * Input:   none.
 * Output:  none. Updates the module-scope music_menu's TICKED flags.
 */
static void refresh_music_menu_ticks(void)
{
	if (qtm_music_enabled())
		music_menu.entries[MUSIC_MENU_ON].menu_flags |= wimp_MENU_TICKED;
	else
		music_menu.entries[MUSIC_MENU_ON].menu_flags &= ~wimp_MENU_TICKED;

	if (qtm_sfx_enabled())
		music_menu.entries[MUSIC_MENU_SFX].menu_flags |= wimp_MENU_TICKED;
	else
		music_menu.entries[MUSIC_MENU_SFX].menu_flags &= ~wimp_MENU_TICKED;
}

/*
 * Function: refresh_track_menu_ticks
 * Summary: Set the Track submenu's TICKED flag on whichever entry
 *          matches qtm_music_track() -- called right before the Music
 *          submenu opens (same as refresh_music_menu_ticks()), since
 *          RISC OS pops a submenu open on hover with no separate event
 *          this project could hook just for Track specifically.
 * Syntax:  static void refresh_track_menu_ticks(void);
 * Input:   none.
 * Output:  none. Updates the module-scope track_menu's TICKED flags.
 */
static void refresh_track_menu_ticks(void)
{
	int i;

	for (i = 0; i < QTM_MUSIC_TRACK_COUNT; i++) {
		if (i == qtm_music_track())
			track_menu.entries[i].menu_flags |= wimp_MENU_TICKED;
		else
			track_menu.entries[i].menu_flags &= ~wimp_MENU_TICKED;
	}
}

/*
 * See include/archiludo.h's own doc comment for the public contract
 * (what this does and why it has two call sites). Implementation
 * detail that comment doesn't cover: the mix also folds in rand()'s
 * OWN current output, not just the two time sources, so a second (or
 * later) call never DISCARDS whatever entropy an earlier call already
 * added -- it only ever adds more. time(NULL) (RISC OS's real-time
 * clock via OS_Word 14, wrapped by ArchieSDK's own time.c) is
 * included despite reading a fixed default on real hardware whose
 * CMOS battery has died or whose clock was never set -- a real risk
 * on genuine 30+-year-old Archimedes machines -- because it costs
 * nothing to include and helps on any machine where it does work; it
 * is never relied on alone. Confirmed by reading ArchieSDK's actual
 * rand()/srand() (SDK/src/libc/stdlib/rand.c) that its static seed
 * otherwise defaults to a hardcoded 0, not the 1 a hosted libc
 * typically uses -- not that the exact default matters once this is
 * called, only that some call must happen at all.
 */
void archiludo_reseed_random(void)
{
	srand((unsigned int) rand() ^ (unsigned int) time(NULL) ^ (unsigned int) os_read_monotonic_time());
}

void archiludo_initialise(const char *argv0)
{
	wimp_version_no version_out;

	archiludo_reseed_random();
	task_handle = wimp_initialise(wimp_VERSION_RO30, APP_NAME, NULL, &version_out);
	create_iconbar_icon();
	build_track_menu();
	build_music_menu();
	build_iconbar_menu();
	game_view_initialise(argv0);
	setup_view_initialise();
	win_view_initialise();
	rules_view_initialise();
	splash_view_initialise();
	splash_view_open(1);
	/* After game_view_initialise() -- save_view.c's default pathname is
	 * built from game_view_app_dir(), which needs argv0 already
	 * processed. */
	save_view_initialise();
	/* After game_view_initialise() too -- qtm.c's bundled asset paths are
	 * also built from game_view_app_dir(). Starts background music
	 * immediately if QTM is available (default: enabled, track 1) --
	 * a silent no-op otherwise. */
	qtm_initialise();
}

/*
 * Function: main_dispatch
 * Summary: Handle one event returned from Wimp_Poll. Kept as a single
 *          switch here (rather than a per-event-type handler table) since
 *          the event set is still small -- see docs/ARCHITECTURE.md if
 *          this grows enough to be worth restructuring.
 * Syntax:  static bool main_dispatch(wimp_event_no reason, wimp_block *block);
 * Input:   reason - the event code from wimp_poll().
 *          block  - the associated event block.
 * Output:  true if the application should quit.
 */
static bool main_dispatch(wimp_event_no reason, wimp_block *block)
{
	switch (reason) {
	case wimp_REDRAW_WINDOW_REQUEST:
		if (block->redraw.w == game_view_window_handle())
			game_view_redraw(&block->redraw);
		else if (block->redraw.w == setup_view_window_handle())
			setup_view_redraw(&block->redraw);
		else if (block->redraw.w == win_view_window_handle())
			win_view_redraw(&block->redraw);
		else if (block->redraw.w == rules_view_window_handle())
			rules_view_redraw(&block->redraw);
		else if (block->redraw.w == splash_view_window_handle())
			splash_view_redraw(&block->redraw);
		else if (block->redraw.w == save_view_window_handle())
			save_view_redraw(&block->redraw);
		else if (block->redraw.w == load_view_window_handle())
			load_view_redraw(&block->redraw);
		break;

	case wimp_OPEN_WINDOW_REQUEST:
		/* Must be answered for ANY window, not just ours -- this is what
		 * makes dragging/resizing/scrolling actually take effect (the
		 * Wimp asks the owning task to confirm the new position; a
		 * missing handler here is why a window looks undraggable). */
		wimp_open_window(&block->open);
		break;

	case wimp_CLOSE_WINDOW_REQUEST:
		wimp_close_window(block->close.w);
		break;

	case wimp_MOUSE_CLICK:
		/* MENU click anywhere in one of our own windows (or the iconbar
		 * icon) opens the same shared menu -- per explicit user request
		 * ("do not see a menu bar in the main game screen yet"), matching
		 * the standard RISC OS convention that a window's own menu is
		 * reached by a MENU click inside it, not only via the iconbar.
		 * Wimp_Poll only ever reports a Mouse_Click for a window this
		 * task owns (or wimp_ICON_BAR for this task's own iconbar icon),
		 * so there's no risk of hijacking a MENU click meant for some
		 * other application's window. */
		if (block->pointer.buttons & wimp_CLICK_MENU) {
			refresh_music_menu_ticks();
			refresh_track_menu_ticks();
			wimp_create_menu((wimp_menu *) &iconbar_menu,
			                  block->pointer.pos.x, block->pointer.pos.y);
			break;
		}

		if (block->pointer.w == wimp_ICON_BAR) {
			/* First-ever click must ask for player details before any
			 * game starts -- per explicit user request ("ensure game
			 * after first start always asks that first"). Once a game
			 * has actually been started (see game_view_has_started()),
			 * later clicks just reopen/refocus it, matching ordinary
			 * iconbar-click behaviour. */
			if (game_view_has_started())
				game_view_open();
			else
				setup_view_open();
		} else if (block->pointer.w == game_view_window_handle()) {
			game_view_click(&block->pointer);
		} else if (block->pointer.w == setup_view_window_handle()) {
			setup_view_click(&block->pointer);
		} else if (block->pointer.w == win_view_window_handle()) {
			if (win_view_click(&block->pointer))
				return true;
		} else if (block->pointer.w == rules_view_window_handle()) {
			rules_view_click(&block->pointer);
		} else if (block->pointer.w == splash_view_window_handle()) {
			splash_view_click(&block->pointer);
		} else if (block->pointer.w == save_view_window_handle()) {
			save_view_click(&block->pointer);
		} else if (block->pointer.w == load_view_window_handle()) {
			load_view_click(&block->pointer);
		}
		break;

	case wimp_KEY_PRESSED:
		/* Writable icons exist in src/setup_view.c's "New Game" dialogue
		 * and src/save_view.c's Save/Load dialogues -- everywhere else,
		 * just pass the key straight through so the Wimp's own default
		 * key handling still runs (e.g. Tab/caret movement in whichever
		 * window currently has the input focus, even if it isn't one of
		 * ours). */
		if (block->key.w == setup_view_window_handle())
			setup_view_key_pressed(&block->key);
		else if (block->key.w == save_view_window_handle())
			save_view_key_pressed(&block->key);
		else if (block->key.w == load_view_window_handle())
			load_view_key_pressed(&block->key);
		else
			wimp_process_key(block->key.c);
		break;

	case wimp_MENU_SELECTION:
		/* rules_view.c's own variant pop-up menu (opened by clicking its
		 * variant display icon, not the shared iconbar/window menu) is
		 * the only OTHER menu this task ever creates -- RISC OS only
		 * ever has one menu open at a time, and a Menu_Selection event
		 * doesn't itself say which menu it belongs to, so this must be
		 * checked first rather than assuming it's always the iconbar
		 * menu below. */
		if (rules_view_menu_open()) {
			rules_view_menu_selection(&block->selection);
			break;
		}
		if (block->selection.items[0] == ICONBAR_MENU_NEW_GAME)
			setup_view_open();
		else if (block->selection.items[0] == ICONBAR_MENU_SAVE_GAME)
			save_view_open();
		else if (block->selection.items[0] == ICONBAR_MENU_LOAD_GAME)
			load_view_open();
		else if (block->selection.items[0] == ICONBAR_MENU_MUSIC) {
			/* items[1] is the Music submenu's own selected entry -- see
			 * build_music_menu()/refresh_music_menu_ticks(). "On" and
			 * "SFX" both toggle (rather than only ever turning on)
			 * since they're ticked toggles, not one-way actions, and
			 * independently of each other. items[1] == MUSIC_MENU_TRACK
			 * with items[2] set means a track was actually picked from
			 * Track's own submenu (see build_track_menu()) -- items[2]
			 * == -1 would mean Track was merely hovered/opened without
			 * picking anything, which shouldn't reach here at all
			 * (Menu_Selection only fires on an actual choice), but is
			 * still guarded defensively rather than assumed. */
			if (block->selection.items[1] == MUSIC_MENU_ON)
				qtm_set_music_enabled(!qtm_music_enabled());
			else if (block->selection.items[1] == MUSIC_MENU_SFX)
				qtm_set_sfx_enabled(!qtm_sfx_enabled());
			else if (block->selection.items[1] == MUSIC_MENU_TRACK
			      && block->selection.items[2] >= 0
			      && block->selection.items[2] < QTM_MUSIC_TRACK_COUNT) {
				/* Picking a track implies wanting to hear it, even if
				 * music was off -- per explicit user request that music
				 * be switchable from the menu, a track pick that
				 * silently does nothing because music happened to be
				 * off would read as broken, not "off". */
				qtm_set_music_track(block->selection.items[2]);
				qtm_set_music_enabled(1);
			}
		} else if (block->selection.items[0] == ICONBAR_MENU_ABOUT)
			splash_view_open(0);
		else if (block->selection.items[0] == ICONBAR_MENU_QUIT)
			return true;
		break;

	case wimp_USER_MESSAGE:
	case wimp_USER_MESSAGE_RECORDED:
		/* Save/load uses 5 fixed save slots (see src/save_view.c), not
		 * drag-and-drop -- Message_Quit is the only message ArchiLudo
		 * has any reason to care about. */
		if (block->message.action == message_QUIT)
			return true;
		break;

	case wimp_NULL_REASON_CODE:
		/* Idle poll -- drives the game window's dice-roll/pawn-move
		 * animations and hover-destination highlight (see
		 * game_view_poll_idle()'s doc comment). A cheap no-op whenever
		 * nothing is actually animating. */
		game_view_poll_idle();
		break;

	default:
		break;
	}

	return false;
}

void archiludo_poll_loop(void)
{
	bool quit = false;
	wimp_block block;

	while (!quit) {
		wimp_event_no reason = wimp_poll(0, &block, NULL);

		quit = main_dispatch(reason, &block);
	}
}

/*
 * Function: main
 * Summary: ArchiLudo's real process entry point -- initialises the WIMP
 *          task (archiludo_initialise()), runs the Wimp_Poll event loop
 *          until the user quits (archiludo_poll_loop()), then shuts down
 *          QTM and the task cleanly.
 * Syntax:  int main(int argc, char *argv[]);
 * Input:   argc - argument count, as passed by ArchieSDK's crt0.s.
 *          argv - argument vector; argv[0] is the full RISC OS pathname
 *                 this program was run as (see the comment below), used
 *                 to locate the application directory.
 * Output:  0 on a clean exit (the only path this function takes -- there
 *          is no error-exit case).
 */
int main(int argc, char *argv[])
{
	/* argv[0] is how a RISC OS program finds its own directory -- OS_GetEnv
	 * (see ArchieSDK's crt0.s) hands back the full pathname the program was
	 * invoked as (e.g. "HostFS:$.ArchiLudo"), unlike Unix where a bare
	 * relative command name is common. game_view_initialise() uses this to
	 * build absolute paths for "PawnSprite" and its debug log, rather than
	 * relying on the current selected directory (CSD) at launch time,
	 * which is not reliable here -- see docs/ARCHITECTURE.md's "WIMP
	 * conventions and gotchas" section. */
	archiludo_initialise(argc > 0 ? argv[0] : "");
	archiludo_poll_loop();

	/* QTM is a relocatable module, independent of this task -- without an
	 * explicit stop, background music keeps playing after ArchiLudo itself
	 * quits (both the Quit menu and Message_Quit converge on the poll loop
	 * ending here, so this one call covers both). qtm_set_music_enabled(0)
	 * does not stop QTM (it only mutes, leaving the song loaded so SFX
	 * keep working with music off) -- qtm_shutdown() is the dedicated
	 * call for real shutdown, see lib/qtm.c. */
	qtm_shutdown();

	wimp_close_down(task_handle);

	return 0;
}
