#include <stdbool.h>
#include <string.h>

#include "oslib/wimp.h"
#include "archiludo.h"
#include "game_view.h"

wimp_t task_handle;

#define ICONBAR_MENU_ITEMS 1
#define ICONBAR_MENU_QUIT  0

static wimp_MENU(ICONBAR_MENU_ITEMS) iconbar_menu;

/*
 * Function: create_iconbar_icon
 * Summary: Register ArchiLudo's icon on the right-hand side of the icon
 *          bar. SELECT click opens the game window (see main_dispatch());
 *          MENU click shows the iconbar menu (see open_iconbar_menu()).
 */
static void create_iconbar_icon(void)
{
	wimp_icon_create iconbar_icon;

	iconbar_icon.w = wimp_ICON_BAR_RIGHT;
	iconbar_icon.icon.extent.x0 = 0;
	iconbar_icon.icon.extent.y0 = 0;
	iconbar_icon.icon.extent.x1 = 68;
	iconbar_icon.icon.extent.y1 = 68;
	iconbar_icon.icon.flags = wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED
	                        | (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	iconbar_icon.icon.data.text[0] = 'A';
	iconbar_icon.icon.data.text[1] = 'L';
	iconbar_icon.icon.data.text[2] = '\0';

	wimp_create_icon(&iconbar_icon);
}

/*
 * Function: build_iconbar_menu
 * Summary: Build the (fixed, never rebuilt) iconbar menu: just "Quit"
 *          for this Phase 1 milestone -- see docs/ARCHITECTURE.md's
 *          Roadmap for the credits/options entries planned later.
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

	iconbar_menu.entries[ICONBAR_MENU_QUIT].menu_flags = wimp_MENU_LAST;
	iconbar_menu.entries[ICONBAR_MENU_QUIT].sub_menu = wimp_NO_SUB_MENU;
	iconbar_menu.entries[ICONBAR_MENU_QUIT].icon_flags = wimp_ICON_TEXT | wimp_ICON_FILLED
	                                                    | wimp_ICON_VCENTRED
	                                                    | (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT)
	                                                    | (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);
	strncpy(iconbar_menu.entries[ICONBAR_MENU_QUIT].data.text, "Quit", 12);
}

void archiludo_initialise(void)
{
	wimp_version_no version_out;

	task_handle = wimp_initialise(wimp_VERSION_RO30, APP_NAME, NULL, &version_out);
	create_iconbar_icon();
	build_iconbar_menu();
	game_view_initialise();
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
		if (block->pointer.w == wimp_ICON_BAR) {
			if (block->pointer.buttons & wimp_CLICK_MENU)
				wimp_create_menu((wimp_menu *) &iconbar_menu,
				                  block->pointer.pos.x, block->pointer.pos.y);
			else
				game_view_open();
		} else if (block->pointer.w == game_view_window_handle()) {
			game_view_click(&block->pointer);
		}
		break;

	case wimp_MENU_SELECTION:
		if (block->selection.items[0] == ICONBAR_MENU_QUIT)
			return true;
		break;

	case wimp_USER_MESSAGE:
	case wimp_USER_MESSAGE_RECORDED:
		if (block->message.action == message_QUIT)
			return true;
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

int main(void)
{
	archiludo_initialise();
	archiludo_poll_loop();

	wimp_close_down(task_handle);

	return 0;
}
