/*
 * ArchiLudo win view -- implementation.
 * See include/win_view.h for the module overview and API docs.
 */

#include <string.h>

#include "oslib/wimp.h"
#include "oslib/wimpspriteop.h"

#include "win_view.h"
#include "game_view.h"
#include "setup_view.h"

#define MARGIN          16
#define MESSAGE_WIDTH  288
#define MESSAGE_HEIGHT  40
#define BUTTON_GAP      16
#define BUTTON_WIDTH   128
#define BUTTON_HEIGHT   40

#define WINDOW_WIDTH  (MARGIN + MESSAGE_WIDTH + MARGIN)
#define MESSAGE_X0    (MARGIN + ((WINDOW_WIDTH - MARGIN * 2 - MESSAGE_WIDTH) / 2))
#define MESSAGE_Y1    (-MARGIN)
#define BUTTON_ROW_Y1 (MESSAGE_Y1 - MESSAGE_HEIGHT - MARGIN)
#define WINDOW_HEIGHT (MARGIN - (BUTTON_ROW_Y1 - BUTTON_HEIGHT))
#define BUTTON_ROW_WIDTH (BUTTON_WIDTH * 2 + BUTTON_GAP)
#define CONTINUE_X0   (MARGIN + ((WINDOW_WIDTH - MARGIN * 2 - BUTTON_ROW_WIDTH) / 2))
#define NEWGAME_X0    (CONTINUE_X0 + BUTTON_WIDTH + BUTTON_GAP)

#define ICON_MESSAGE  0
#define ICON_CONTINUE 1
#define ICON_NEWGAME  2
#define WINDOW_ICON_COUNT 3

/* Sized for "GREEN WINS!" plus a safety margin -- the longest realistic
 * message ("YELLOW WINS!") is 12 characters + terminator. */
#define MESSAGE_BUF_LEN 32

static wimp_w window_handle = (wimp_w) -1;
static char message_buf[MESSAGE_BUF_LEN];
static char continue_validation[4] = "R1";
static char newgame_validation[4] = "R1";

void win_view_initialise(void)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	wimp_icon *icon;

	def.visible.x0 = 300;
	def.visible.y0 = 400;
	def.visible.x1 = 300 + WINDOW_WIDTH;
	def.visible.y1 = 400 + WINDOW_HEIGHT;
	def.xscroll = 0;
	def.yscroll = 0;
	def.next = wimp_TOP;
	def.flags = wimp_WINDOW_NEW_FORMAT | wimp_WINDOW_MOVEABLE |
	            wimp_WINDOW_BOUNDED_ONCE | wimp_WINDOW_BACK_ICON |
	            wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_TITLE_ICON;
	def.title_fg = wimp_COLOUR_BLACK;
	def.title_bg = wimp_COLOUR_LIGHT_GREY;
	def.work_fg = wimp_COLOUR_BLACK;
	def.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
	def.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
	def.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
	def.highlight_bg = wimp_COLOUR_CREAM;
	def.extra_flags = 0;
	def.extent.x0 = 0;
	def.extent.y0 = -WINDOW_HEIGHT;
	def.extent.x1 = WINDOW_WIDTH;
	def.extent.y1 = 0;
	def.title_flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	                   wimp_ICON_VCENTRED | wimp_ICON_FILLED;
	/* No custom-plotted content -- plain Wimp icons only, same as
	 * setup_view.c, so background click behaviour doesn't matter here. */
	def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	def.sprite_area = wimpspriteop_AREA;
	def.xmin = WINDOW_WIDTH;
	def.ymin = WINDOW_HEIGHT;
	strncpy(def.title_data.text, "Game Won", 12);
	def.icon_count = WINDOW_ICON_COUNT;

	icon = &def.icons[ICON_MESSAGE];
	icon->extent.x0 = MESSAGE_X0;
	icon->extent.x1 = MESSAGE_X0 + MESSAGE_WIDTH;
	icon->extent.y1 = MESSAGE_Y1;
	icon->extent.y0 = MESSAGE_Y1 - MESSAGE_HEIGHT;
	/* Indirected, large-looking text -- just the plain system font
	 * centred; no need for anything fancier for a one-line message. */
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_HCENTRED |
	              wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	message_buf[0] = '\0';
	icon->data.indirected_text.text = message_buf;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = MESSAGE_BUF_LEN;

	icon = &def.icons[ICON_CONTINUE];
	icon->extent.x0 = CONTINUE_X0;
	icon->extent.x1 = CONTINUE_X0 + BUTTON_WIDTH;
	icon->extent.y1 = BUTTON_ROW_Y1;
	icon->extent.y0 = BUTTON_ROW_Y1 - BUTTON_HEIGHT;
	/* "R1" slab-out real-button look -- same convention as every other
	 * button in this project (setup_view.c's Start/Load/Cancel,
	 * splash_view.c's OK), see riscos_wimp_reference.md's Icons section. */
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "Continue";
	icon->data.indirected_text.validation = continue_validation;
	icon->data.indirected_text.size = 9;

	icon = &def.icons[ICON_NEWGAME];
	icon->extent.x0 = NEWGAME_X0;
	icon->extent.x1 = NEWGAME_X0 + BUTTON_WIDTH;
	icon->extent.y1 = BUTTON_ROW_Y1;
	icon->extent.y0 = BUTTON_ROW_Y1 - BUTTON_HEIGHT;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "New Game";
	icon->data.indirected_text.validation = newgame_validation;
	icon->data.indirected_text.size = 9;

	window_handle = wimp_create_window((wimp_window *) &def);
}

void win_view_open(const char *message)
{
	wimp_window_state state;

	if (window_handle == (wimp_w) -1)
		return;

	strncpy(message_buf, message, MESSAGE_BUF_LEN - 1);
	message_buf[MESSAGE_BUF_LEN - 1] = '\0';

	state.w = window_handle;
	wimp_get_window_state(&state);
	state.next = wimp_TOP;
	wimp_open_window((wimp_open *) &state);
	wimp_force_redraw(window_handle, MESSAGE_X0, MESSAGE_Y1 - MESSAGE_HEIGHT,
	                   MESSAGE_X0 + MESSAGE_WIDTH, MESSAGE_Y1);
}

wimp_w win_view_window_handle(void)
{
	return window_handle;
}

void win_view_redraw(wimp_draw *redraw)
{
	osbool more;

	more = wimp_redraw_window(redraw);
	while (more)
		more = wimp_get_rectangle(redraw);
}

void win_view_click(wimp_pointer *pointer)
{
	if (pointer->i == ICON_CONTINUE) {
		wimp_close_window(window_handle);
		game_view_win_continue();
		return;
	}

	if (pointer->i == ICON_NEWGAME) {
		wimp_close_window(window_handle);
		/* Deliberately game_view_win_continue() first, then
		 * setup_view_open() -- per explicit user request ("for new
		 * game dialogue, defaults always should be the in progress
		 * game"): setup_view_open() reads the CURRENT live player
		 * configuration (see game_view_get_players()), so the game
		 * just finished must still be "the current game" (names/AI
		 * settings intact) at the moment setup_view_open() runs, even
		 * though its actual board state is about to be discarded by
		 * whatever the user does next (Start, or Load). */
		game_view_win_continue();
		setup_view_open();
		return;
	}
}
