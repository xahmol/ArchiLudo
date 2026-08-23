/*
 * ArchiLudo setup view -- implementation.
 * See include/setup_view.h for the module overview and API docs.
 */

#include <string.h>

#include "oslib/wimp.h"
#include "oslib/wimpspriteop.h" /* wimpspriteop_AREA, for def.sprite_area only --
                                  * this window plots no sprites either */

#include "setup_view.h"
#include "game_view.h"
#include "game_logic.h"

#define MARGIN        8
#define ROW_HEIGHT   40
#define ROW_GAP       8
#define SWATCH_SIZE  32
#define NAME_WIDTH  180
#define TYPE_WIDTH  100
#define COL_GAP       8
#define BUTTON_WIDTH 100
#define BUTTON_HEIGHT 40
#define BUTTON_GAP   16

#define ROWS_HEIGHT (LUDO_PLAYERS * ROW_HEIGHT + (LUDO_PLAYERS - 1) * ROW_GAP)
#define WINDOW_WIDTH (MARGIN + SWATCH_SIZE + COL_GAP + NAME_WIDTH + COL_GAP + TYPE_WIDTH + MARGIN)
#define WINDOW_HEIGHT (MARGIN + ROWS_HEIGHT + MARGIN + BUTTON_HEIGHT + MARGIN)

#define SWATCH_X0 MARGIN
#define NAME_X0   (SWATCH_X0 + SWATCH_SIZE + COL_GAP)
#define TYPE_X0   (NAME_X0 + NAME_WIDTH + COL_GAP)

#define ROW_Y1(row) (-(MARGIN + (row) * (ROW_HEIGHT + ROW_GAP)))
#define ROW_Y0(row) (ROW_Y1(row) - ROW_HEIGHT)

#define BUTTON_ROW_Y1 (-(MARGIN + ROWS_HEIGHT + MARGIN))
#define BUTTON_ROW_Y0 (BUTTON_ROW_Y1 - BUTTON_HEIGHT)
#define START_X0  MARGIN
#define CANCEL_X0 (START_X0 + BUTTON_WIDTH + BUTTON_GAP)

/* One swatch + name + type icon per player, then Start and Cancel. */
#define ICON_SWATCH(player) ((player) * 3)
#define ICON_NAME(player)   ((player) * 3 + 1)
#define ICON_TYPE(player)   ((player) * 3 + 2)
#define ICON_START  (LUDO_PLAYERS * 3)
#define ICON_CANCEL (LUDO_PLAYERS * 3 + 1)
#define WINDOW_ICON_COUNT (LUDO_PLAYERS * 3 + 2)

/* Standard 16-colour Wimp desktop palette approximations of this
 * project's actual (full-RGB) player colours -- plain Wimp icons can
 * only use this fixed palette for their fill colour, unlike the custom
 * os_plot/colourtrans drawing game_view.c's board uses. Must stay in the
 * same green/red/blue/yellow order as game_view.c's player_rgb and
 * assets/generate_placeholder_art.py's PLAYER_COLOURS. */
static const wimp_colour swatch_colour[LUDO_PLAYERS] = {
	wimp_COLOUR_DARK_GREEN,
	wimp_COLOUR_RED,
	wimp_COLOUR_DARK_BLUE,
	wimp_COLOUR_YELLOW,
};
static const char *default_name[LUDO_PLAYERS] = { "GREEN", "RED", "BLUE", "YELLOW" };

static char name_buffer[LUDO_PLAYERS][GAME_VIEW_NAME_LEN];
static char type_text[LUDO_PLAYERS][6]; /* "Human" or "AI", plus terminator */
static int type_is_ai[LUDO_PLAYERS];
static char start_validation[4] = "R1";
static char cancel_validation[4] = "R1";

static wimp_w window_handle = (wimp_w) -1;

/*
 * Function: set_type
 * Summary: Set one player's Human/AI toggle state (both the tracked flag
 *          and its displayed text) and, if the window already exists,
 *          ask the Wimp to redraw that one icon.
 */
static void set_type(int player, int is_ai)
{
	type_is_ai[player] = is_ai;
	/* type_text[] is sized to exactly fit "Human\0", the longer of the
	 * two -- a plain strcpy() is safe (and, unlike strncpy() for a
	 * same-length copy, doesn't trip a "may not null-terminate" warning). */
	strcpy(type_text[player], is_ai ? "AI" : "Human");

	if (window_handle != (wimp_w) -1)
		wimp_set_icon_state(window_handle, ICON_TYPE(player), 0, 0);
}

void setup_view_initialise(void)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	int player;

	def.visible.x0 = 150;
	def.visible.y0 = 150;
	def.visible.x1 = 150 + WINDOW_WIDTH;
	def.visible.y1 = 150 + WINDOW_HEIGHT;
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
	/* No custom drawing in this window at all (unlike game_view.c's
	 * board) -- every row is plain Wimp icons, so plain background click
	 * behaviour (BUTTON_NEVER) is correct here, not the
	 * work_flags-must-be-BUTTON_CLICK fix game_view.c needed (see that
	 * file's game_view_initialise(), "Round 6.4" -- that was specifically
	 * about detecting clicks on custom-plotted background content, which
	 * this window has none of). */
	def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	def.sprite_area = wimpspriteop_AREA;
	def.xmin = WINDOW_WIDTH;
	def.ymin = WINDOW_HEIGHT;
	strncpy(def.title_data.text, "New Game", 12);
	def.icon_count = WINDOW_ICON_COUNT;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		wimp_icon *icon;

		strncpy(name_buffer[player], default_name[player], GAME_VIEW_NAME_LEN - 1);
		name_buffer[player][GAME_VIEW_NAME_LEN - 1] = '\0';
		type_is_ai[player] = 0;
		strcpy(type_text[player], "Human");

		icon = &def.icons[ICON_SWATCH(player)];
		icon->extent.x0 = SWATCH_X0;
		icon->extent.x1 = SWATCH_X0 + SWATCH_SIZE;
		icon->extent.y1 = ROW_Y1(player);
		icon->extent.y0 = ROW_Y0(player);
		icon->flags = wimp_ICON_BORDER | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (swatch_colour[player] << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.text[0] = '\0';

		icon = &def.icons[ICON_NAME(player)];
		icon->extent.x0 = NAME_X0;
		icon->extent.x1 = NAME_X0 + NAME_WIDTH;
		icon->extent.y1 = ROW_Y1(player);
		icon->extent.y0 = ROW_Y0(player);
		/* Writable: the user types a name directly into this icon. No
		 * validation string restriction ("A..." command) -- the Wimp's
		 * own default for a writable icon accepts ordinary printable
		 * characters with no allow-list needed; see the RISC OS 3 PRM's
		 * Wimp chapter, "Writable icons". */
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_FILLED | wimp_ICON_VCENTRED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_WRITABLE << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = name_buffer[player];
		icon->data.indirected_text.validation = "";
		icon->data.indirected_text.size = GAME_VIEW_NAME_LEN;

		icon = &def.icons[ICON_TYPE(player)];
		icon->extent.x0 = TYPE_X0;
		icon->extent.x1 = TYPE_X0 + TYPE_WIDTH;
		icon->extent.y1 = ROW_Y1(player);
		icon->extent.y0 = ROW_Y0(player);
		/* Click-to-toggle Human/AI, same runtime-mutable-validation-
		 * string technique as game_view.c's flash_throw_button() (R1
		 * "slab out" throughout -- no press animation needed here, this
		 * one's persistent state rather than a momentary flash) -- see
		 * riscos_wimp_reference.md's Icons section. */
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = type_text[player];
		icon->data.indirected_text.validation = "R1";
		icon->data.indirected_text.size = sizeof(type_text[player]);
	}

	{
		wimp_icon *icon = &def.icons[ICON_START];

		icon->extent.x0 = START_X0;
		icon->extent.x1 = START_X0 + BUTTON_WIDTH;
		icon->extent.y1 = BUTTON_ROW_Y1;
		icon->extent.y0 = BUTTON_ROW_Y0;
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = "Start";
		icon->data.indirected_text.validation = start_validation;
		icon->data.indirected_text.size = 6;

		icon = &def.icons[ICON_CANCEL];
		icon->extent.x0 = CANCEL_X0;
		icon->extent.x1 = CANCEL_X0 + BUTTON_WIDTH;
		icon->extent.y1 = BUTTON_ROW_Y1;
		icon->extent.y0 = BUTTON_ROW_Y0;
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = "Cancel";
		icon->data.indirected_text.validation = cancel_validation;
		icon->data.indirected_text.size = 7;
	}

	window_handle = wimp_create_window((wimp_window *) &def);
}

void setup_view_open(void)
{
	wimp_window_state state;

	if (window_handle == (wimp_w) -1)
		return;

	state.w = window_handle;
	wimp_get_window_state(&state);
	state.next = wimp_TOP;
	wimp_open_window((wimp_open *) &state);

	/* Caret in the first name field, positioned at the end of its
	 * existing text -- "when moving to a new writable icon, place the
	 * caret at the end of the existing text" (RISC OS 3 PRM's Wimp
	 * chapter). height=-1 asks the Wimp to use the icon's own natural
	 * caret height rather than specifying one explicitly. No need to
	 * force-redraw anything else here: every icon's indirected buffer
	 * (names, Human/AI text) already holds whatever it was last set to
	 * -- by setup_view_initialise()'s defaults, or by the user's own
	 * previous edits/toggles in this same window -- so simply opening
	 * the window already shows the right thing. */
	wimp_set_caret_position(window_handle, ICON_NAME(0), 0, 0, -1,
	                         (int) strlen(name_buffer[0]));
}

wimp_w setup_view_window_handle(void)
{
	return window_handle;
}

void setup_view_redraw(wimp_draw *redraw)
{
	osbool more;

	more = wimp_redraw_window(redraw);
	while (more)
		more = wimp_get_rectangle(redraw);
}

void setup_view_click(wimp_pointer *pointer)
{
	int player;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		if (pointer->i == ICON_TYPE(player)) {
			set_type(player, !type_is_ai[player]);
			return;
		}
	}

	if (pointer->i == ICON_CANCEL) {
		wimp_close_window(window_handle);
		return;
	}

	if (pointer->i == ICON_START) {
		char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN];

		for (player = 0; player < LUDO_PLAYERS; player++) {
			strncpy(names[player], name_buffer[player], GAME_VIEW_NAME_LEN - 1);
			names[player][GAME_VIEW_NAME_LEN - 1] = '\0';
		}
		game_view_configure_players(names, type_is_ai);

		wimp_close_window(window_handle);
		game_view_open();
		game_view_new_game();
		return;
	}
}

void setup_view_key_pressed(wimp_key *key)
{
	wimp_process_key(key->c);
}
