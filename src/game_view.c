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

/* Round 6: redesigned to match GeoLudo's own screen layout (board on the
 * left, a status/controls panel on the right -- see
 * /home/xahmol/git/ludo/GEOS/screenshots/ludo-game-c64.png, the reference
 * this was resized from) instead of ArchiLudo's earlier invented
 * top-header layout, per explicit user request. CELL doubled from the
 * original 32 in round 5 (board read as too small), kept here. */
#define CELL          64
#define BOARD_PIXELS  (BOARD_GRID_SIZE * CELL)
#define MARGIN         8

/* Board cell marker circles (ring/home-column backgrounds, and on-track
 * pawns -- see game_view_redraw()/plot_pawn()) share this radius, giving
 * the gapped, round-dot look of the GEOS reference screenshot rather than
 * ArchiLudo's earlier solid square grid. */
#define MARKER_RADIUS 22

/* On-screen size in OS units (square) for the reused-GEOS-art home base
 * pawn sprite -- must match assets/generate_placeholder_art.py's
 * PAWN_SIZE (kept in sync manually, one's Python and one's C). Board
 * entry markers (round 6.1) are drawn programmatically instead of from a
 * sprite -- see plot_start_marker(). */
#define PAWN_SIZE     40

/* Side panel: player name (+ a colour swatch, see game_view_redraw()),
 * action status, and the Throw button -- laid out top-to-bottom on the
 * right of the board, Throw positioned lower rather than at the very
 * top, again matching the GEOS reference. */
#define PANEL_GAP     16
#define PANEL_WIDTH  260
#define NAME_HEIGHT   40
#define SWATCH_SIZE   24
#define SWATCH_X0     (PANEL_X0 + PANEL_WIDTH - SWATCH_SIZE - MARGIN)
#define SWATCH_Y1     (-(MARGIN + (NAME_HEIGHT - SWATCH_SIZE) / 2))
#define STATUS_GAP     8
#define STATUS_HEIGHT 40
#define THROW_WIDTH  160
#define THROW_HEIGHT  48

#define BOARD_ORIGIN_X MARGIN
#define BOARD_ORIGIN_Y (-MARGIN)
#define PANEL_X0      (MARGIN + BOARD_PIXELS + PANEL_GAP)
#define WINDOW_WIDTH  (PANEL_X0 + PANEL_WIDTH + MARGIN)
#define WINDOW_HEIGHT (MARGIN + BOARD_PIXELS + MARGIN)
/* Throw sits at roughly the same proportion down the panel as GEOS's own
 * button (see the reference screenshot) -- not pixel-exact, since the
 * panel's overall proportions differ (RISC OS's fixed-width system font
 * needs more room per character than GEOS's own font did), but the same
 * "name/status near the top, Throw lower down" shape. */
#define THROW_Y1      (-(WINDOW_HEIGHT * 6 / 10))

#define ICON_NAME    0
#define ICON_STATUS  1
#define ICON_THROW   2
#define WINDOW_ICON_COUNT 3

#define STATUS_TEXT_LEN 40
#define NAME_TEXT_LEN   20
#define SPRITE_AREA_SIZE 8192

/* Cell background categories, precomputed once from board_layout.c's
 * forward mapping (see build_cell_kinds()) so the redraw handler doesn't
 * need its own copy of the geometry rules. CELL_RING_ENTRY is the one
 * ring cell per player (steps==0 for that player) where GEOS shows a
 * coloured direction-arrow marker instead of a plain track marker -- see
 * plot_start_marker(). Home base cells, and the centre "finished pawns"
 * cell, aren't tracked here at all: GEOS draws no background at the
 * home base (see the reference screenshot), and the centre cell isn't
 * part of any player's home stretch, so neither gets a permanent marker
 * -- both are left as plain window background, with only actual pawns
 * (drawn separately, see plot_pawn()) making them visible. */
typedef enum {
	CELL_EMPTY,
	CELL_RING,
	CELL_RING_ENTRY,
	CELL_HOME_COLUMN
} cell_kind;

/* Player order/colours match /home/xahmol/git/ludo/GEOS/src/main.c's
 * startfieldgraphics comments exactly (see docs/BOARD_LAYOUT.md) -- must
 * also match PLAYER_COLOURS in assets/generate_placeholder_art.py. */
static const int player_rgb[LUDO_PLAYERS][3] = {
	{ 30, 160, 60 },   /* 0: green */
	{ 220, 30, 30 },   /* 1: red */
	{ 30, 140, 220 },  /* 2: blue */
	{ 230, 200, 30 },  /* 3: yellow */
};
static const char *player_name[LUDO_PLAYERS] = { "GREEN", "RED", "BLUE", "YELLOW" };

static wimp_w window_handle = (wimp_w) -1;
static ludo_game game;
static char name_text[NAME_TEXT_LEN] = "";
static char status_text[STATUS_TEXT_LEN] = "";

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
 *          the last "." path separator. A bare relative filename
 *          ("Sprites", "Log") doesn't reliably resolve against this
 *          program's own directory the way it would need to when run
 *          this way. Building absolute paths from argv[0] instead is the
 *          standard RISC OS convention for a program to find its own
 *          resources.
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
 *          own directory (see resource_path()). Diagnostic left in place
 *          (kept lean -- just sprite-load status and pawn-click tracing)
 *          since Arculator has no other non-interactive tracing option;
 *          see CLAUDE.md's Testing section.
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
 *          home column / ring entry cells), by walking board_layout.c's
 *          forward mappings. Done once at startup rather than every
 *          redraw since the geometry never changes.
 */
static void build_cell_kinds(void)
{
	int c, r, i, player;
	const int ring_step = LUDO_RING_LENGTH / LUDO_PLAYERS;

	for (r = 0; r < BOARD_GRID_SIZE; r++)
		for (c = 0; c < BOARD_GRID_SIZE; c++)
			cell_kinds[c][r] = CELL_EMPTY;

	for (i = 0; i < LUDO_RING_LENGTH; i++) {
		board_cell cell = board_ring_cell(i);

		if (i % ring_step == 0) {
			cell_kinds[cell.col][cell.row] = CELL_RING_ENTRY;
			cell_owner[cell.col][cell.row] = i / ring_step;
		} else {
			cell_kinds[cell.col][cell.row] = CELL_RING;
		}
	}

	for (player = 0; player < LUDO_PLAYERS; player++) {
		for (i = 0; i < LUDO_HOME_COLUMN_LENGTH; i++) {
			board_cell cell = board_home_column_cell(player, i);
			cell_kinds[cell.col][cell.row] = CELL_HOME_COLUMN;
			cell_owner[cell.col][cell.row] = player;
		}
	}
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
 * Function: fill_circle / outline_circle
 * Summary: Plot a filled or outline circle in the current foreground
 *          colour, centred at (cx, cy) with the given radius, all in OS
 *          units -- mode-independent, unlike sprite plotting (see
 *          docs/GRAPHICS_TOOLING.md's "Round 6 correction"). Per the RISC
 *          OS 3 PRM's os_plot summary (~/riscos-dev/prm-mirror/vdu.html):
 *          "Move to centre. Plot circle to point on the circumference."
 */
static void fill_circle(int cx, int cy, int radius)
{
	os_plot(os_MOVE_TO, cx, cy);
	os_plot(os_PLOT_CIRCLE + os_PLOT_TO, cx + radius, cy);
}

static void outline_circle(int cx, int cy, int radius)
{
	os_plot(os_MOVE_TO, cx, cy);
	os_plot(os_PLOT_CIRCLE_OUTLINE + os_PLOT_TO, cx + radius, cy);
}

/*
 * Function: fill_triangle
 * Summary: Plot a filled triangle in the current foreground colour, given
 *          its three vertices in OS units. Per the RISC OS 3 PRM's
 *          os_plot summary: "Move to first vertex. Move to second
 *          vertex. Plot triangle to last vertex."
 */
static void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
	os_plot(os_MOVE_TO, x0, y0);
	os_plot(os_MOVE_TO, x1, y1);
	os_plot(os_PLOT_TRIANGLE + os_PLOT_TO, x2, y2);
}

/*
 * Function: refresh_status
 * Summary: Rebuild the player-name and action-status text from the
 *          current game state and ask the Wimp to redraw those icons.
 *          Split into two short lines (rather than one long sentence)
 *          both because it matches GEOS's own layout (see the reference
 *          screenshot) and because it keeps each line comfortably within
 *          PANEL_WIDTH at the system font's fixed 16-OS-units/character
 *          width.
 */
static void refresh_status(void)
{
	if (game.winner != -1) {
		snprintf(name_text, NAME_TEXT_LEN, "%s WINS!", player_name[game.winner]);
		snprintf(status_text, STATUS_TEXT_LEN, "Click Throw");
	} else {
		snprintf(name_text, NAME_TEXT_LEN, "%s", player_name[game.current_player]);

		if (game.last_roll == 0) {
			snprintf(status_text, STATUS_TEXT_LEN, "Click Throw");
		} else if (ludo_movable_pawns(&game) != 0) {
			snprintf(status_text, STATUS_TEXT_LEN, "Pick a pawn");
		} else if (game.just_released) {
			/* A six with a home pawn available is a mandatory release, not
			 * a move -- the roll that released the pawn has nothing left
			 * to pick, and the player throws again next. Distinct wording
			 * from "Throw again" below so this doesn't read as the same
			 * no-op repeating (see docs/ARCHITECTURE.md's Phase 1 notes on
			 * the "endless reroll" confusion this was originally reported
			 * as). */
			snprintf(status_text, STATUS_TEXT_LEN, "Pawn released!");
		} else {
			snprintf(status_text, STATUS_TEXT_LEN, "Throw again");
		}
	}

	if (window_handle != (wimp_w) -1) {
		wimp_set_icon_state(window_handle, ICON_NAME, 0, 0);
		wimp_set_icon_state(window_handle, ICON_STATUS, 0, 0);
	}
}

void game_view_new_game(void)
{
	ludo_init(&game);
	refresh_status();
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

	def.visible.x0 = 100;
	def.visible.y0 = 100;
	def.visible.x1 = 100 + WINDOW_WIDTH;
	def.visible.y1 = 100 + WINDOW_HEIGHT;
	def.xscroll = 0;
	def.yscroll = 0;
	def.next = wimp_TOP;
	/* NOTE: deliberately NOT wimp_WINDOW_AUTO_REDRAW -- see git history for
	 * why (caused the board to stop drawing entirely on a freshly-opened
	 * window). Performance is instead addressed by keeping the redraw
	 * loop cheap (os_plot circles instead of sprites for most of the
	 * board) rather than this flag. */
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

	icon = &def.icons[ICON_NAME];
	icon->extent.x0 = PANEL_X0;
	icon->extent.y1 = -MARGIN;
	icon->extent.y0 = -(MARGIN + NAME_HEIGHT);
	icon->extent.x1 = PANEL_X0 + PANEL_WIDTH;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = name_text;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = NAME_TEXT_LEN;

	icon = &def.icons[ICON_STATUS];
	icon->extent.x0 = PANEL_X0;
	icon->extent.y1 = -(MARGIN + NAME_HEIGHT + STATUS_GAP);
	icon->extent.y0 = icon->extent.y1 - STATUS_HEIGHT;
	icon->extent.x1 = PANEL_X0 + PANEL_WIDTH;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = status_text;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = STATUS_TEXT_LEN;

	icon = &def.icons[ICON_THROW];
	icon->extent.x0 = PANEL_X0;
	icon->extent.y1 = THROW_Y1;
	icon->extent.y0 = THROW_Y1 - THROW_HEIGHT;
	icon->extent.x1 = PANEL_X0 + THROW_WIDTH;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	              wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	strncpy(icon->data.text, "Throw", 12);

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
 * Function: cell_centre
 * Summary: The centre point, in the current redraw's absolute screen
 *          coordinates, of a board grid cell -- shared by the cell-kind
 *          loop and plot_pawn() so marker circles and pawns line up
 *          exactly.
 */
static void cell_centre(int col, int row, int origin_x, int origin_y, int *cx, int *cy)
{
	*cx = origin_x + BOARD_ORIGIN_X + col * CELL + CELL / 2;
	*cy = origin_y + BOARD_ORIGIN_Y - row * CELL - CELL / 2;
}

/*
 * Function: plot_pawn
 * Summary: Draw one pawn -- home base, ring, home column, or finished --
 *          with the detailed recoloured GEOS pawn sprite (see
 *          assets/generate_placeholder_art.py), wherever board_pawn_cell()
 *          says it currently is. Round 6 had this only for the home base,
 *          with on-track pawns drawn as a plain filled circle instead --
 *          based on a screenshot crop that turned out to be showing an
 *          empty home-column lane marker, not an actual on-track pawn;
 *          `ludo-playerwon.png` (a later state, with real pawns visible
 *          on the ring and in home columns) makes clear GEOS shows the
 *          detailed pawn shape everywhere a pawn actually is, not just
 *          the home base -- see docs/GRAPHICS_TOOLING.md's "Round 6.2"
 *          correction. Falls back to a plain filled square (no sprite
 *          lookup at all) if assets/Sprites failed to load, so the game
 *          stays playable regardless.
 */
static void plot_pawn(int player, int pawn_index, int origin_x, int origin_y)
{
	board_cell cell = board_pawn_cell(&game, player, pawn_index);
	int cx, cy, x, y;

	cell_centre(cell.col, cell.row, origin_x, origin_y, &cx, &cy);
	x = cx - PAWN_SIZE / 2;
	y = cy - PAWN_SIZE / 2;

	if (sprites_loaded) {
		char name[13];

		snprintf(name, sizeof(name), "pawn%d", player);
		xosspriteop_put_sprite_user_coords(osspriteop_USER_AREA, sprite_area,
		                                    (osspriteop_id) name, x, y,
		                                    os_ACTION_OVERWRITE + os_ACTION_USE_MASK);
	} else {
		set_gcol(player_rgb[player][0], player_rgb[player][1], player_rgb[player][2]);
		fill_rect(x, y, x + PAWN_SIZE - 2, y + PAWN_SIZE - 2);
	}
}

/*
 * Function: plot_start_marker
 * Summary: Draw one player's board-entry marker at a CELL_RING_ENTRY
 *          cell: a filled circle in the player's colour (same size as an
 *          ordinary marker) with a white arrow pointing in that player's
 *          direction of travel -- per explicit user request ("should
 *          look like a normal round but filled in the corresponding
 *          color and an arrow in it in direction of movement"). Round
 *          6's first attempt reused GEOS's own bm_gstart/rstart/bstart/
 *          ystart bitmaps as sprites here, but they rendered far too
 *          narrow in Arculator for reasons that didn't reproduce in any
 *          offline check (the packed sprite file's own metadata and a
 *          round-tripped/stretched preview both looked correct -- see
 *          docs/GRAPHICS_TOOLING.md's "Round 6.1"); drawn programmatically
 *          instead, sidestepping the whole sprite-scaling question and
 *          giving an exact, guaranteed-correct size and shape.
 */
static void plot_start_marker(int player, int cx, int cy)
{
	/* Direction each player's pawns travel at their own entry point, as
	 * OS-unit screen deltas (not row/col deltas -- board rows increase
	 * DOWNWARD on screen, i.e. toward more NEGATIVE os units, since
	 * cell_centre() subtracts row*CELL) -- matches board_layout.c's ring
	 * travel order exactly: green +col (right), red +row (down), blue
	 * -col (left), yellow -row (up); verified by comparing each player's
	 * entry ring cell against the very next one in travel order. */
	static const int dir_x[LUDO_PLAYERS] = {  1,  0, -1,  0 };
	static const int dir_y[LUDO_PLAYERS] = {  0, -1,  0,  1 };
	int dx = dir_x[player], dy = dir_y[player];
	int tip_len = MARKER_RADIUS * 6 / 10;
	int back_len = MARKER_RADIUS * 3 / 10;
	int half_base = MARKER_RADIUS * 4 / 10;
	int tip_x = cx + dx * tip_len,   tip_y = cy + dy * tip_len;
	int base_x = cx - dx * back_len, base_y = cy - dy * back_len;
	int perp_x = -dy, perp_y = dx;

	set_gcol(player_rgb[player][0], player_rgb[player][1], player_rgb[player][2]);
	fill_circle(cx, cy, MARKER_RADIUS);

	set_gcol(255, 255, 255);
	fill_triangle(tip_x, tip_y,
	              base_x + perp_x * half_base, base_y + perp_y * half_base,
	              base_x - perp_x * half_base, base_y - perp_y * half_base);
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
				int cx, cy, owner;

				if (kind == CELL_EMPTY)
					continue;

				cell_centre(col, row, origin_x, origin_y, &cx, &cy);

				switch (kind) {
				case CELL_RING:
					set_gcol(96, 96, 96);
					outline_circle(cx, cy, MARKER_RADIUS);
					break;
				case CELL_RING_ENTRY:
					plot_start_marker(cell_owner[col][row], cx, cy);
					break;
				case CELL_HOME_COLUMN:
					/* Always the owning player's full colour, whether or
					 * not a pawn currently sits there -- GEOS shows these
					 * as permanent "this lane belongs to X" markers, not
					 * an occupancy indicator (confirmed against the
					 * reference screenshot: the visible home-column dots
					 * are full-saturation player colour, not a paler
					 * background tint). */
					owner = cell_owner[col][row];
					set_gcol(player_rgb[owner][0], player_rgb[owner][1], player_rgb[owner][2]);
					fill_circle(cx, cy, MARKER_RADIUS);
					break;
				default:
					break;
				}
			}
		}

		for (player = 0; player < LUDO_PLAYERS; player++)
			for (pawn = 0; pawn < LUDO_PAWNS; pawn++)
				plot_pawn(player, pawn, origin_x, origin_y);

		/* Player-colour swatch next to the name line -- matches GEOS's own
		 * reference screenshot, which has a small coloured box beside the
		 * player name/status text. */
		{
			int player = (game.winner != -1) ? game.winner : game.current_player;
			int x0 = origin_x + SWATCH_X0;
			int y1 = origin_y + SWATCH_Y1;

			set_gcol(player_rgb[player][0], player_rgb[player][1], player_rgb[player][2]);
			fill_rect(x0, y1 - SWATCH_SIZE, x0 + SWATCH_SIZE, y1);
		}

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
			 * single throw caused a visible flash each time for no visual
			 * benefit. */
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
