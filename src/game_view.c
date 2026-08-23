/*
 * ArchiLudo game view -- implementation.
 * See include/game_view.h for the module overview and API docs.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "oslib/wimp.h"
#include "oslib/colourtrans.h"
#include "oslib/osspriteop.h"
#include "oslib/wimpspriteop.h"

#include "game_view.h"
#include "game_logic.h"
#include "board_layout.h"

#define CELL          32
#define MARGIN         8
#define THROW_HEIGHT  32
#define THROW_WIDTH  100
#define STATUS_HEIGHT 28
#define HEADER_GAP     4
#define HEADER_HEIGHT (THROW_HEIGHT + HEADER_GAP + STATUS_HEIGHT)
#define BOARD_PIXELS  (BOARD_GRID_SIZE * CELL)
/* The system font is a fixed 16 OS units per character (see
 * riscos_wimp_reference.md's Text section), and the longest status
 * message is ~28 characters -- so the window has to be wide enough for
 * that text, not just for the board. MIN_CONTENT_WIDTH is that floor;
 * the board is then centred within whichever of the two ends up wider. */
#define MIN_CONTENT_WIDTH 528
#define WINDOW_WIDTH  (MARGIN + (BOARD_PIXELS > (MIN_CONTENT_WIDTH - 2 * MARGIN) ? BOARD_PIXELS : (MIN_CONTENT_WIDTH - 2 * MARGIN)) + MARGIN)
#define BOARD_ORIGIN_X ((WINDOW_WIDTH - BOARD_PIXELS) / 2)
#define BOARD_ORIGIN_Y (-(MARGIN + HEADER_HEIGHT + MARGIN))
#define WINDOW_HEIGHT (MARGIN + HEADER_HEIGHT + MARGIN + BOARD_PIXELS + MARGIN)

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

/* Player order/colours match /home/xahmol/git/ludo/GEOS/src/main.c's
 * startfieldgraphics comments exactly (see docs/BOARD_LAYOUT.md) -- must
 * also match assets/generate_placeholder_art.py's PLAYER_COLOURS. */
static const int player_rgb[LUDO_PLAYERS][3] = {
	{ 30, 160, 60 },   /* 0: green */
	{ 220, 30, 30 },   /* 1: red */
	{ 30, 140, 220 },  /* 2: blue */
	{ 230, 200, 30 },  /* 3: yellow */
};
static const char *player_name[LUDO_PLAYERS] = { "GREEN", "RED", "BLUE", "YELLOW" };

static wimp_w window_handle = (wimp_w) -1;
static ludo_game game;
static char status_text[STATUS_TEXT_LEN] = "Click Throw to begin.";

static cell_kind cell_kinds[BOARD_GRID_SIZE][BOARD_GRID_SIZE];
static int cell_owner[BOARD_GRID_SIZE][BOARD_GRID_SIZE];

static char sprite_area_buffer[SPRITE_AREA_SIZE];
static osspriteop_area *sprite_area = (osspriteop_area *) sprite_area_buffer;
static int sprites_loaded = 0;

#define APP_DIR_LEN 200
static char app_dir[APP_DIR_LEN] = "";

/*
 * Function: set_app_dir
 * Summary: Derive this program's own directory from argv[0] (the full
 *          RISC OS pathname it was invoked as, e.g. "HostFS:$.ArchiLudo"
 *          -- see game_view_initialise()'s doc comment) by truncating at
 *          the last "." path separator. Round 3's diagnostic logging
 *          never actually landed anywhere the user could find it, and
 *          round 4's screenshot showed pawns rendering as flat squares
 *          (the sprite-missing fallback) rather than the loaded
 *          placeholder art -- both point at the same root cause: a bare
 *          relative filename ("Sprites", "Log") doesn't reliably resolve
 *          against this program's own directory the way it would need to.
 *          Building absolute paths from argv[0] instead is the standard
 *          RISC OS convention for a program to find its own resources.
 */
static void set_app_dir(const char *argv0)
{
	const char *last_dot = NULL;
	const char *p;
	size_t len;

	for (p = argv0; *p; p++)
		if (*p == '.')
			last_dot = p;

	len = last_dot ? (size_t) (last_dot - argv0) : 0;
	if (len >= APP_DIR_LEN)
		len = APP_DIR_LEN - 1;

	memcpy(app_dir, argv0, len);
	app_dir[len] = '\0';
}

/*
 * Function: resource_path
 * Summary: Build an absolute path to a file named `leaf` in this
 *          program's own directory (see set_app_dir()). Falls back to
 *          the bare leafname if argv[0] didn't yield a usable directory.
 */
static void resource_path(char *out, size_t out_size, const char *leaf)
{
	if (app_dir[0] != '\0')
		snprintf(out, out_size, "%s.%s", app_dir, leaf);
	else
		snprintf(out, out_size, "%s", leaf);
}

/*
 * Function: debug_log
 * Summary: Append one line to a plain text file "Log" in this program's
 *          own directory (see resource_path()). TEMPORARY diagnostic
 *          added while chasing a partial-board-render bug reported from
 *          Arculator screenshots that couldn't be explained by static
 *          code review alone -- see docs/ARCHITECTURE.md's Phase 1
 *          implementation notes and CLAUDE.md's documented file-logging
 *          fallback for non-interactive WIMP tracing. Remove once that
 *          bug is confirmed fixed.
 */
static void debug_log(const char *fmt, ...)
{
	char path[APP_DIR_LEN + 8];
	FILE *f;
	va_list args;

	resource_path(path, sizeof(path), "Log");
	f = fopen(path, "a");
	if (f == NULL)
		return;

	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	fclose(f);
}

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

	{
		int n_ring = 0, n_col = 0, n_base = 0, n_centre = 0;

		for (r = 0; r < BOARD_GRID_SIZE; r++)
			for (c = 0; c < BOARD_GRID_SIZE; c++)
				switch (cell_kinds[c][r]) {
				case CELL_RING:        n_ring++;  break;
				case CELL_HOME_COLUMN: n_col++;   break;
				case CELL_HOME_BASE:   n_base++;  break;
				case CELL_CENTRE:      n_centre++; break;
				default: break;
				}

		debug_log("build_cell_kinds: ring=%d home_column=%d home_base=%d centre=%d "
		          "(expect 40/16/16/1)\n", n_ring, n_col, n_base, n_centre);
	}

	/* Round 4: green/blue's home columns (row=5, the horizontal cross bar)
	 * rendered as plain ring grey instead of their tint, while red/yellow's
	 * (col=5, the vertical bar) rendered correctly -- data (this file's
	 * tables) and this function's code were both re-checked and found
	 * consistent, so dump the actual runtime classification of every cell
	 * on both bars directly rather than keep guessing from source. */
	debug_log("cross-bar cell_kinds (0=empty,1=ring,2=home_column,3=home_base,4=centre):\n");
	for (c = 0; c < BOARD_GRID_SIZE; c++)
		debug_log("  row5 col%d: kind=%d owner=%d\n", c, (int) cell_kinds[c][5], cell_owner[c][5]);
	for (r = 0; r < BOARD_GRID_SIZE; r++)
		debug_log("  col5 row%d: kind=%d owner=%d\n", r, (int) cell_kinds[5][r], cell_owner[5][r]);
}

/*
 * Function: set_gcol
 * Summary: Set the current graphics foreground colour for os_plot(), from
 *          plain RGB values (0..255 each).
 */
static void set_gcol(int r, int g, int b)
{
	/* Cast each component to unsigned before shifting -- r/g/b can be up
	 * to 255, and shifting a *signed* 255 left by 24 sets the sign bit,
	 * which is technically undefined behaviour for a plain int even
	 * though every compiler this project uses happens to wrap it as
	 * expected. Avoid relying on that. */
	os_colour colour = ((os_colour) b << 24) | ((os_colour) g << 16) | ((os_colour) r << 8);

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
		snprintf(status_text, STATUS_TEXT_LEN, "%s wins!", player_name[game.winner]);
	} else if (game.last_roll == 0) {
		snprintf(status_text, STATUS_TEXT_LEN, "%s: click Throw", player_name[game.current_player]);
	} else if (ludo_movable_pawns(&game) != 0) {
		snprintf(status_text, STATUS_TEXT_LEN, "%s rolled %d: pick a pawn",
		         player_name[game.current_player], game.last_roll);
	} else if (game.just_released) {
		/* A six with a home pawn available is a mandatory release, not a
		 * move -- the roll that released the pawn has nothing left to pick,
		 * and the player throws again next. Distinct wording from the
		 * generic "Throw again" below so this doesn't read as the same
		 * no-op repeating (see docs/ARCHITECTURE.md's Phase 1 notes on the
		 * "endless reroll" confusion this was reported as). Kept within the
		 * same ~32-char budget as the other status strings (see
		 * MIN_CONTENT_WIDTH above). */
		snprintf(status_text, STATUS_TEXT_LEN, "%s: pawn out, Throw again",
		         player_name[game.current_player]);
	} else {
		snprintf(status_text, STATUS_TEXT_LEN, "%s rolled %d: Throw again",
		         player_name[game.current_player], game.last_roll);
	}

	if (window_handle != (wimp_w) -1)
		wimp_set_icon_state(window_handle, ICON_STATUS, 0, 0);
}

void game_view_new_game(void)
{
	ludo_init(&game);
	snprintf(status_text, STATUS_TEXT_LEN, "%s: click Throw", player_name[0]);
	if (window_handle != (wimp_w) -1)
		wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
}

void game_view_initialise(const char *argv0)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	wimp_icon *icon;

	set_app_dir(argv0);
	build_cell_kinds();
	ludo_init(&game);

	def.visible.x0 = 200;
	def.visible.y0 = 150;
	def.visible.x1 = 200 + WINDOW_WIDTH;
	def.visible.y1 = 150 + WINDOW_HEIGHT;
	def.xscroll = 0;
	def.yscroll = 0;
	def.next = wimp_TOP;
	/* NOTE: deliberately NOT wimp_WINDOW_AUTO_REDRAW. It was tried (to
	 * address a "feels slow" report) and caused a worse regression: the
	 * board stopped drawing at all, while the window's own icons (which
	 * the Wimp redraws itself regardless of this flag) kept appearing --
	 * i.e. Redraw_Window_Request stopped reaching this app's custom
	 * drawing code for a freshly-opened window. Revisit performance a
	 * different way (fewer plot calls per redraw) rather than this flag. */
	def.flags = wimp_WINDOW_NEW_FORMAT | wimp_WINDOW_MOVEABLE |
	            wimp_WINDOW_BOUNDED_ONCE | wimp_WINDOW_BACK_ICON |
	            wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_TITLE_ICON |
	            wimp_WINDOW_TOGGLE_ICON | wimp_WINDOW_SIZE_ICON;
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
	/* Allow shrinking to about half size -- content beyond the visible
	 * area is simply clipped (no scrollbars yet), but the size icon needs
	 * genuine headroom below the natural size to do anything visible; a
	 * minimum equal to the natural size effectively disables it. */
	def.xmin = WINDOW_WIDTH / 2;
	def.ymin = WINDOW_HEIGHT / 2;
	strncpy(def.title_data.text, "ArchiLudo", 12);
	def.icon_count = WINDOW_ICON_COUNT;

	icon = &def.icons[ICON_THROW];
	icon->extent.x0 = MARGIN;
	icon->extent.y0 = -(MARGIN + THROW_HEIGHT);
	icon->extent.x1 = MARGIN + THROW_WIDTH;
	icon->extent.y1 = -MARGIN;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	              wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	strncpy(icon->data.text, "Throw", 12);

	icon = &def.icons[ICON_STATUS];
	icon->extent.x0 = MARGIN;
	icon->extent.y0 = -(MARGIN + THROW_HEIGHT + HEADER_GAP + STATUS_HEIGHT);
	icon->extent.x1 = WINDOW_WIDTH - MARGIN;
	icon->extent.y1 = -(MARGIN + THROW_HEIGHT + HEADER_GAP);
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = status_text;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = STATUS_TEXT_LEN;

	window_handle = wimp_create_window((wimp_window *) &def);

	sprite_area->size = SPRITE_AREA_SIZE;
	{
		char sprites_path[APP_DIR_LEN + 8];
		os_error *error;

		resource_path(sprites_path, sizeof(sprites_path), "Sprites");
		error = xosspriteop_clear_sprites(osspriteop_USER_AREA, sprite_area);
		if (error == NULL)
			error = xosspriteop_load_sprite_file(osspriteop_USER_AREA, sprite_area, sprites_path);
		sprites_loaded = (error == NULL);
		debug_log("game_view_initialise: app_dir=\"%s\" sprites_path=\"%s\" "
		          "sprites_loaded=%d%s%s\n", app_dir, sprites_path, sprites_loaded,
		          error ? " error=" : "", error ? error->errmess : "");
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
	int rect_index = 0;
	int drawn_ring = 0, drawn_col = 0, drawn_base = 0, drawn_centre = 0;

	more = wimp_redraw_window(redraw);
	debug_log("redraw: window box=(%d,%d,%d,%d) work_bg=%d sprites_loaded=%d\n",
	          redraw->box.x0, redraw->box.y0, redraw->box.x1, redraw->box.y1,
	          (int) wimp_COLOUR_VERY_LIGHT_GREY, sprites_loaded);

	while (more) {
		int origin_x = redraw->box.x0 - redraw->xscroll;
		int origin_y = redraw->box.y1 - redraw->yscroll;
		int col, row, player, pawn;

		debug_log("  rect[%d]: clip=(%d,%d,%d,%d) origin=(%d,%d)\n",
		          rect_index++, redraw->clip.x0, redraw->clip.y0,
		          redraw->clip.x1, redraw->clip.y1, origin_x, origin_y);

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
					set_gcol(150, 150, 150);
					drawn_ring++;
					break;
				case CELL_HOME_COLUMN:
					player = cell_owner[col][row];
					set_gcol((player_rgb[player][0] + 255 * 3) / 4,
					         (player_rgb[player][1] + 255 * 3) / 4,
					         (player_rgb[player][2] + 255 * 3) / 4);
					drawn_col++;
					break;
				case CELL_HOME_BASE:
					player = cell_owner[col][row];
					set_gcol(player_rgb[player][0] * 3 / 4,
					         player_rgb[player][1] * 3 / 4,
					         player_rgb[player][2] * 3 / 4);
					drawn_base++;
					break;
				case CELL_CENTRE:
				default:
					set_gcol(255, 215, 0);
					drawn_centre++;
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

	debug_log("redraw done: %d rect(s), cells drawn ring=%d home_column=%d "
	          "home_base=%d centre=%d (expect 40/16/16/1 if 1 rect covered "
	          "the whole window)\n", rect_index, drawn_ring, drawn_col,
	          drawn_base, drawn_centre);
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

	debug_log("try_move_pawn: click at (%d,%d) player=%d movable_mask=0x%x\n",
	          col, row, game.current_player, movable);

	for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
		board_cell cell;

		if (!(movable & (1u << pawn)))
			continue;
		cell = board_pawn_cell(&game, game.current_player, pawn);
		debug_log("  candidate pawn %d at (%d,%d)\n", pawn, cell.col, cell.row);
		if (cell.col == col && cell.row == row) {
			ludo_move_pawn(&game, pawn);
			debug_log("  MOVED pawn %d\n", pawn);
			refresh_status();
			wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
			return;
		}
	}

	debug_log("  no match -- no pawn moved\n");
}

void game_view_click(wimp_pointer *pointer)
{
	if (pointer->i == ICON_THROW) {
		if (game.winner != -1) {
			game_view_new_game();
		} else {
			ludo_roll(&game, 0);
			/* Only a six that releases a home pawn changes anything on the
			 * board itself (the released pawn appears on the ring) -- an
			 * ordinary roll only changes the status text, which
			 * refresh_status() below already redraws via
			 * wimp_set_icon_state(). Forcing a full-window redraw on every
			 * single throw (the old behaviour) caused a visible flash each
			 * time for no visual benefit -- see docs/ARCHITECTURE.md's
			 * Phase 1 implementation notes, "Round 4". */
			if (game.just_released)
				wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
		}
		refresh_status();
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

		debug_log("click: pos=(%d,%d) visible.x0=%d visible.y1=%d work=(%d,%d) "
		          "cell=(%d,%d)\n", pointer->pos.x, pointer->pos.y,
		          state.visible.x0, state.visible.y1, work_x, work_y, col, row);

		if (col >= 0 && col < BOARD_GRID_SIZE && row >= 0 && row < BOARD_GRID_SIZE)
			try_move_pawn(col, row);
		else
			debug_log("  click outside board grid range\n");
	}
}
