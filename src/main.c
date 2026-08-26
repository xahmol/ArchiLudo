#include <stdbool.h>
#include <string.h>

#include "oslib/wimp.h"
#include "archiludo.h"
#include "game_view.h"
#include "setup_view.h"
#include "splash_view.h"
#include "save_view.h"
#include "win_view.h"

wimp_t task_handle;

#define ICONBAR_MENU_ITEMS      5
#define ICONBAR_MENU_NEW_GAME   0
#define ICONBAR_MENU_SAVE_GAME  1
#define ICONBAR_MENU_LOAD_GAME  2
#define ICONBAR_MENU_ABOUT      3
#define ICONBAR_MENU_QUIT       4

static wimp_MENU(ICONBAR_MENU_ITEMS) iconbar_menu;

/*
 * Function: create_iconbar_icon
 * Summary: Register ArchiLudo's icon on the right-hand side of the icon
 *          bar. SELECT click opens src/setup_view.c's "New Game" dialogue
 *          the first time, or reopens the game already in progress
 *          afterwards (see main_dispatch(), game_view_has_started());
 *          MENU click shows the shared app menu.
 *
 *          Round 7.37: plots the real "!ArchiLudo" sprite (a red pawn
 *          beside a die -- see assets/generate_app_icon.py,
 *          docs/BUILDCHAIN.md's "Application directory" section) instead
 *          of the placeholder "AL" text icon this used before the round
 *          7.36 application-directory work -- that work built and
 *          deployed the sprite but never actually wired the iconbar icon
 *          to use it, a plain oversight found via live user report ("task
 *          bar icon is still the old AL letter one"). Plain (non-
 *          indirected) sprite icon: `app/!Run`'s `IconSprites
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
 *          dialogues -- per explicit user request for GEOS-menu parity;
 *          see docs/ARCHITECTURE.md's Round 7.1 notes on why "Color" and
 *          "(Re)Start" from GEOS's own menu aren't included), "About"
 *          (reopens src/splash_view.c's splash/about window, shown
 *          automatically once at startup too), and "Quit".
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
	/* "About ArchiLudo" doesn't fit the fixed 12-byte inline menu-icon
	 * text buffer, hence the shorter "About". */
	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_ABOUT], "About", 0);
	set_menu_entry(&iconbar_menu.entries[ICONBAR_MENU_QUIT], "Quit", 1);
}

void archiludo_initialise(const char *argv0)
{
	wimp_version_no version_out;

	task_handle = wimp_initialise(wimp_VERSION_RO30, APP_NAME, NULL, &version_out);
	create_iconbar_icon();
	build_iconbar_menu();
	game_view_initialise(argv0);
	setup_view_initialise();
	win_view_initialise();
	splash_view_initialise();
	splash_view_open();
	/* After game_view_initialise() -- save_view.c's default pathname is
	 * built from game_view_app_dir(), which needs argv0 already
	 * processed. */
	save_view_initialise();
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
			win_view_click(&block->pointer);
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
		if (block->selection.items[0] == ICONBAR_MENU_NEW_GAME)
			setup_view_open();
		else if (block->selection.items[0] == ICONBAR_MENU_SAVE_GAME)
			save_view_open();
		else if (block->selection.items[0] == ICONBAR_MENU_LOAD_GAME)
			load_view_open();
		else if (block->selection.items[0] == ICONBAR_MENU_ABOUT)
			splash_view_open();
		else if (block->selection.items[0] == ICONBAR_MENU_QUIT)
			return true;
		break;

	case wimp_USER_DRAG_BOX:
		/* Only src/save_view.c's Save dialogue ever starts a drag (its
		 * draggable file icon, see save_view_click()) -- routed here
		 * unconditionally since there's nothing else in ArchiLudo a
		 * User_Drag_Box event could belong to. */
		save_view_drag_ended(&block->dragged);
		break;

	case wimp_USER_MESSAGE:
	case wimp_USER_MESSAGE_RECORDED:
		if (block->message.action == message_QUIT)
			return true;
		/* Message_DataSaveAck (continuing a save-drag) and an
		 * unsolicited Message_DataLoad (a file dragged in from Filer) --
		 * see save_view_message_received()'s doc comment. A no-op for
		 * any other message action. */
		save_view_message_received(&block->message);
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

int main(int argc, char *argv[])
{
	/* argv[0] is how a RISC OS program finds its own directory -- OS_GetEnv
	 * (see ArchieSDK's crt0.s) hands back the full pathname the program was
	 * invoked as (e.g. "HostFS:$.ArchiLudo"), unlike Unix where a bare
	 * relative command name is common. game_view_initialise() uses this to
	 * build absolute paths for "Sprites" and its debug log, rather than
	 * relying on the current selected directory (CSD) at launch time, which
	 * turned out not to be reliable here -- see docs/ARCHITECTURE.md's
	 * Phase 1 implementation notes, "Round 4". */
	archiludo_initialise(argc > 0 ? argv[0] : "");
	archiludo_poll_loop();

	wimp_close_down(task_handle);

	return 0;
}
