/*
 * ArchiLudo game view -- implementation.
 * See include/game_view.h for the module overview and API docs.
 */

#include <string.h>
#include <stdio.h>

#include "oslib/wimp.h"
#include "oslib/colourtrans.h"
#include "oslib/osspriteop.h"
#include "oslib/wimpspriteop.h"

#include "game_view.h"
#include "game_logic.h"
#include "board_layout.h"

#define CELL          24
#define MARGIN         8
#define ICON_HEIGHT   24
#define BOARD_ORIGIN_X MARGIN
#define BOARD_ORIGIN_Y (-(MARGIN + ICON_HEIGHT + MARGIN))
#define BOARD_PIXELS  (BOARD_GRID_SIZE * CELL)
#define WINDOW_WIDTH  (MARGIN + BOARD_PIXELS + MARGIN)
#define WINDOW_HEIGHT (MARGIN + ICON_HEIGHT + MARGIN + BOARD_PIXELS + MARGIN)

#define ICON_THROW  0
#define ICON_STATUS 1
#define WINDOW_ICON_COUNT 2

#define STATUS_TEXT_LEN 80
#define SPRITE_AREA_SIZE 8192

/* Cell background categories, precomputed once from board_layout.c's
 * forward mapping (see build_cell_kinds()) so the redraw handler doesn't
 * need its own copy of the geometry rules. */
typedef enum {
	CELL_EMPTY,
	CELL_RING,
	CELL_HOME_COLUMN,
	CELL_HOME_BASE,
	CELL_CENTRE
} cell_kind;

static const int player_rgb[LUDO_PLAYERS][3] = {
	{ 220, 30, 30 },   /* 0: red   -- must match assets/generate_placeholder_art.py */
	{ 30, 140, 220 },  /* 1: blue */
	{ 230, 200, 30 },  /* 2: yellow */
	{ 30, 160, 60 },   /* 3: green */
};
static const char *player_name[LUDO_PLAYERS] = { "RED", "BLUE", "YELLOW", "GREEN" };

static wimp_w window_handle = (wimp_w) -1;
static ludo_game game;
static char status_text[STATUS_TEXT_LEN] = "Click Throw to begin.";

static cell_kind cell_kinds[BOARD_GRID_SIZE][BOARD_GRID_SIZE];
static int cell_owner[BOARD_GRID_SIZE][BOARD_GRID_SIZE];

static char sprite_area_buffer[SPRITE_AREA_SIZE];
static osspriteop_area *sprite_area = (osspriteop_area *) sprite_area_buffer;
static int sprites_loaded = 0;

/*
 * Function: build_cell_kinds
 * Summary: Precompute, once, which board_layout.c grid cell holds which
 *          kind of board feature (and which player owns it, for the
 *          home column/home base cells), by walking board_layout.c's
 *          forward mappings. Done once at startup rather than every
 *          redraw since the geometry never changes.
 */
static void build_cell_kinds(void)
{
	int c, r, i, player;

	for (r = 0; r < BOARD_GRID_SIZE; r++)
		for (c = 0; c < BOARD_GRID_SIZE; c++)
			cell_kinds[c][r] = CELL_EMPTY;

	for (i = 0; i < LUDO_RING_LENGTH; i++) {
		board_cell cell = board_ring_cell(i);
		cell_kinds[cell.col][cell.row] = CELL_RING;
	}

	for (player = 0; player < LUDO_PLAYERS; player++) {
		for (i = 0; i < LUDO_HOME_COLUMN_LENGTH; i++) {
			board_cell cell = board_home_column_cell(player, i);
			cell_kinds[cell.col][cell.row] = CELL_HOME_COLUMN;
			cell_owner[cell.col][cell.row] = player;
		}
		for (i = 0; i < LUDO_PAWNS; i++) {
			board_cell cell = board_home_base_cell(player, i);
			cell_kinds[cell.col][cell.row] = CELL_HOME_BASE;
			cell_owner[cell.col][cell.row] = player;
		}
	}

	{
		board_cell centre = board_finished_cell();
		cell_kinds[centre.col][centre.row] = CELL_CENTRE;
	}
}

/*
 * Function: set_gcol
 * Summary: Set the current graphics foreground colour for os_plot(), from
 *          plain RGB values (0..255 each).
 */
static void set_gcol(int r, int g, int b)
{
	os_colour colour = (os_colour) ((b << 24) | (g << 16) | (r << 8));

	colourtrans_set_gcol(colour, colourtrans_SET_FG_GCOL, os_ACTION_OVERWRITE, 0);
}

/*
 * Function: fill_rect
 * Summary: Plot a filled rectangle in the current foreground colour,
 *          given absolute screen coordinates (already offset by the
 *          redraw origin -- see game_view_redraw()).
 */
static void fill_rect(int x0, int y0, int x1, int y1)
{
	os_plot(os_MOVE_TO, x0, y0);
	os_plot(os_PLOT_RECTANGLE + os_PLOT_TO, x1, y1);
}

/*
 * Function: refresh_status
 * Summary: Rebuild the status line text from the current game state and
 *          ask the Wimp to redraw that one icon.
 */
static void refresh_status(void)
{
	if (game.winner != -1) {
		snprintf(status_text, STATUS_TEXT_LEN, "%s wins! Click Throw to play again.",
		         player_name[game.winner]);
	} else if (game.last_roll == 0) {
		snprintf(status_text, STATUS_TEXT_LEN, "%s to move -- click Throw.",
		         player_name[game.current_player]);
	} else if (ludo_movable_pawns(&game) != 0) {
		snprintf(status_text, STATUS_TEXT_LEN, "%s rolled %d -- click a pawn to move it.",
		         player_name[game.current_player], game.last_roll);
	} else {
		snprintf(status_text, STATUS_TEXT_LEN, "%s rolled %d -- click Throw again.",
		         player_name[game.current_player], game.last_roll);
	}

	if (window_handle != (wimp_w) -1)
		wimp_set_icon_state(window_handle, ICON_STATUS, 0, 0);
}

void game_view_new_game(void)
{
	ludo_init(&game);
	snprintf(status_text, STATUS_TEXT_LEN, "%s to move -- click Throw.", player_name[0]);
	if (window_handle != (wimp_w) -1)
		wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
}

void game_view_initialise(void)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	wimp_icon *icon;

	build_cell_kinds();
	ludo_init(&game);

	def.visible.x0 = 200;
	def.visible.y0 = 150;
	def.visible.x1 = 200 + WINDOW_WIDTH;
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
	def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	def.sprite_area = wimpspriteop_AREA;
	def.xmin = 0;
	def.ymin = 0;
	strncpy(def.title_data.text, "ArchiLudo", 12);
	def.icon_count = WINDOW_ICON_COUNT;

	icon = &def.icons[ICON_THROW];
	icon->extent.x0 = MARGIN;
	icon->extent.y0 = -(MARGIN + ICON_HEIGHT);
	icon->extent.x1 = MARGIN + 80;
	icon->extent.y1 = -MARGIN;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	              wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	strncpy(icon->data.text, "Throw", 12);

	icon = &def.icons[ICON_STATUS];
	icon->extent.x0 = MARGIN + 88;
	icon->extent.y0 = -(MARGIN + ICON_HEIGHT);
	icon->extent.x1 = WINDOW_WIDTH - MARGIN;
	icon->extent.y1 = -MARGIN;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = status_text;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = STATUS_TEXT_LEN;

	window_handle = wimp_create_window((wimp_window *) &def);

	sprite_area->size = SPRITE_AREA_SIZE;
	{
		os_error *error;

		error = xosspriteop_clear_sprites(osspriteop_USER_AREA, sprite_area);
		if (error == NULL)
			error = xosspriteop_load_sprite_file(osspriteop_USER_AREA, sprite_area, "Sprites");
		sprites_loaded = (error == NULL);
	}

	refresh_status();
}

void game_view_open(void)
{
	wimp_window_state state;

	state.w = window_handle;
	wimp_get_window_state(&state);
	state.next = wimp_TOP;
	wimp_open_window((wimp_open *) &state);
}

wimp_w game_view_window_handle(void)
{
	return window_handle;
}

/*
 * Function: plot_pawn
 * Summary: Draw one pawn, either as its loaded placeholder sprite or (if
 *          the sprite file failed to load) a plain filled circle-ish
 *          square in the player's colour, so the game stays playable
 *          even without assets/Sprites present.
 */
static void plot_pawn(int player, int pawn_index, int origin_x, int origin_y)
{
	board_cell cell = board_pawn_cell(&game, player, pawn_index);
	int x = origin_x + BOARD_ORIGIN_X + cell.col * CELL + (CELL - 20) / 2;
	int y = origin_y + BOARD_ORIGIN_Y - (cell.row + 1) * CELL + (CELL - 20) / 2;

	if (sprites_loaded) {
		char name[13];

		snprintf(name, sizeof(name), "pawn%d", player);
		xosspriteop_put_sprite_user_coords(osspriteop_USER_AREA, sprite_area,
		                                    (osspriteop_id) name, x, y,
		                                    os_ACTION_OVERWRITE + os_ACTION_USE_MASK);
	} else {
		set_gcol(player_rgb[player][0], player_rgb[player][1], player_rgb[player][2]);
		fill_rect(x, y, x + 18, y + 18);
	}
}

void game_view_redraw(wimp_draw *redraw)
{
	osbool more;

	more = wimp_redraw_window(redraw);
	while (more) {
		int origin_x = redraw->box.x0 - redraw->xscroll;
		int origin_y = redraw->box.y1 - redraw->yscroll;
		int col, row, player, pawn;

		for (row = 0; row < BOARD_GRID_SIZE; row++) {
			for (col = 0; col < BOARD_GRID_SIZE; col++) {
				cell_kind kind = cell_kinds[col][row];
				int x0, y1;

				if (kind == CELL_EMPTY)
					continue;

				x0 = origin_x + BOARD_ORIGIN_X + col * CELL;
				y1 = origin_y + BOARD_ORIGIN_Y - row * CELL;

				switch (kind) {
				case CELL_RING:
					set_gcol(220, 220, 220);
					break;
				case CELL_HOME_COLUMN:
					player = cell_owner[col][row];
					set_gcol((player_rgb[player][0] + 255 * 3) / 4,
					         (player_rgb[player][1] + 255 * 3) / 4,
					         (player_rgb[player][2] + 255 * 3) / 4);
					break;
				case CELL_HOME_BASE:
					player = cell_owner[col][row];
					set_gcol(player_rgb[player][0] * 3 / 4,
					         player_rgb[player][1] * 3 / 4,
					         player_rgb[player][2] * 3 / 4);
					break;
				case CELL_CENTRE:
				default:
					set_gcol(255, 215, 0);
					break;
				}

				fill_rect(x0, y1 - CELL + 2, x0 + CELL - 2, y1);
			}
		}

		for (player = 0; player < LUDO_PLAYERS; player++)
			for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
				plot_pawn(player, pawn, origin_x, origin_y);

		more = wimp_get_rectangle(redraw);
	}
}

/*
 * Function: try_move_pawn
 * Summary: If (col, row) matches one of the current player's currently
 *          movable pawns, move it and refresh the display.
 */
static void try_move_pawn(int col, int row)
{
	unsigned movable = ludo_movable_pawns(&game);
	int pawn;

	for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
		board_cell cell;

		if (!(movable & (1u << pawn)))
			continue;
		cell = board_pawn_cell(&game, game.current_player, pawn);
		if (cell.col == col && cell.row == row) {
			ludo_move_pawn(&game, pawn);
			refresh_status();
			wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
			return;
		}
	}
}

void game_view_click(wimp_pointer *pointer)
{
	if (pointer->i == ICON_THROW) {
		if (game.winner != -1)
			game_view_new_game();
		else
			ludo_roll(&game, 0);
		refresh_status();
		wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
		return;
	}

	if (pointer->i == wimp_ICON_WINDOW) {
		wimp_window_state state;
		int work_x, work_y, col, row;

		state.w = window_handle;
		wimp_get_window_state(&state);
		work_x = pointer->pos.x - state.visible.x0 + state.xscroll;
		work_y = pointer->pos.y - state.visible.y1 + state.yscroll;

		col = (work_x - BOARD_ORIGIN_X) / CELL;
		row = (BOARD_ORIGIN_Y - work_y) / CELL;

		if (col >= 0 && col < BOARD_GRID_SIZE && row >= 0 && row < BOARD_GRID_SIZE)
			try_move_pawn(col, row);
	}
}
