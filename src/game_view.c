/*
 * ArchiLudo game view -- implementation.
 * See include/game_view.h for the module overview and API docs.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "oslib/wimp.h"
#include "oslib/colourtrans.h"
#include "oslib/wimpspriteop.h" /* wimpspriteop_AREA, for def.sprite_area only --
                                  * round 6.3 dropped sprite plotting itself, see
                                  * plot_pawn()'s doc comment */

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

/* Pawn's on-screen size in OS units (square) -- see plot_pawn(). Round
 * 6.3 dropped sprite-based pawn art (see that function's doc comment)
 * for a programmatically-drawn shape, same as board entry markers
 * (round 6.1, see plot_start_marker()) and dice (round 6.3, see
 * plot_dice()) -- this project no longer plots any sprites at all. */
#define PAWN_SIZE     48

/* Side panel: player name (+ a colour swatch, see game_view_redraw()),
 * action status, the current die face (round 6.3 -- GEOS's own
 * dice1..6.gbm, see plot_dice(); previously nothing showed the roll's
 * outcome at all, per repeated user request), and the Throw button --
 * laid out top-to-bottom on the right of the board, Throw positioned
 * lower rather than at the very top, again matching the GEOS reference.
 * The die sits in the gap between the status text and Throw, which is
 * otherwise empty. */
#define PANEL_GAP     16
#define PANEL_WIDTH  260
#define NAME_HEIGHT   40
#define SWATCH_SIZE   24
#define SWATCH_X0     (PANEL_X0 + PANEL_WIDTH - SWATCH_SIZE - MARGIN)
#define SWATCH_Y1     (-(MARGIN + (NAME_HEIGHT - SWATCH_SIZE) / 2))
#define STATUS_GAP     8
#define STATUS_HEIGHT 40
/* Round 6.7: bumped from 56 -- reported pip crowding on face 6 (two
 * columns of 3) with the old pip_radius/step ratio at that size; a
 * bigger die gives more room per pip regardless of the exact ratio. */
#define DICE_SIZE     72
#define DICE_CENTRE_X (PANEL_X0 + PANEL_WIDTH / 2)
#define DICE_CENTRE_Y (-260)
/* Sized like a genuine RISC OS dialogue button (Steve Fryatt's
 * introducing-icons tutorial and its reference screenshot,
 * https://www.stevefryatt.org.uk/risc-os/wimp-prog/introducing-icons --
 * real OK/Cancel/Close buttons there are compact, not oversized) rather
 * than the original guess -- "Throw" is 5 characters (80 OS units at the
 * system font's fixed 16 units/char), so 120 gives ~20 units padding
 * each side, and 40 tall matches the system font's own 32-unit height
 * plus a modest margin, the same proportions those buttons use. */
#define THROW_WIDTH  120
#define THROW_HEIGHT  40

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
static char throw_text[8] = "Throw";
static char throw_validation[4] = "R1";

static cell_kind cell_kinds[BOARD_GRID_SIZE][BOARD_GRID_SIZE];
static int cell_owner[BOARD_GRID_SIZE][BOARD_GRID_SIZE];

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
	/* THE pawn-click bug, finally found: a window definition's work_flags
	 * sets the work area's own button type, exactly like an icon's button
	 * type bits -- confirmed against the RISC OS 3 PRM
	 * (~/riscos-dev/prm-mirror/wimp.html: "A window definition uses the
	 * button type bits to determine its work area's button type"). This
	 * was wimp_BUTTON_NEVER, meaning a click anywhere on the board (which
	 * is custom-plotted directly onto the work area background, not made
	 * of icons) never generated a Mouse_Click event at all -- confirmed by
	 * a debug log showing every single click landing on the Throw icon
	 * and *never* on wimp_ICON_WINDOW, no matter where on the board the
	 * user actually clicked. wimp_BUTTON_CLICK matches ICON_THROW's own
	 * button type and is the standard choice for a work area that needs
	 * ordinary Select/Adjust click reporting. */
	def.work_flags = (wimp_icon_flags) (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
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
	/* Indirected (not a plain 12-byte inline string) so its validation
	 * string -- which is what controls the Bo(R)der 3D bevel type -- is a
	 * buffer this code can mutate at runtime for click feedback. R1
	 * ("slab out", a raised button look) at rest, briefly switched to R2
	 * ("slab in", sunken/pressed) on click then back -- per explicit user
	 * request and the RISC OS 3 PRM's Wimp chapter Bo(R)der command
	 * (~/riscos-dev/prm-mirror/wimp.html): "type 1 slab out / 2 slab in".
	 * See flash_throw_button(). */
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = throw_text;
	icon->data.indirected_text.validation = throw_validation;
	icon->data.indirected_text.size = sizeof(throw_text);

	window_handle = wimp_create_window((wimp_window *) &def);

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
 *          wherever board_pawn_cell() says it currently is: two
 *          overlapping filled circles (a wider "body" below a narrower
 *          "head"), giving a simple pawn-like silhouette.
 *
 *          Round 6.3: this used to plot the recoloured GEOS pawn sprite
 *          (see assets/generate_placeholder_art.py) via
 *          xosspriteop_put_sprite_user_coords(). Three separate small
 *          sprites in a row rendered wrong in Arculator in ways that
 *          never reproduced in any offline check (the packed sprite's
 *          own metadata, and a locally-simulated 2x/4x stretch, both
 *          looked correct every time): round 6.1's board-entry markers
 *          (too narrow), round 6.3's dice (cropped), and this pawn sprite
 *          itself (rendering solid black regardless of player, despite
 *          the packed sprite file's palette and pixel data both verified
 *          correct offline -- see docs/GRAPHICS_TOOLING.md's "Round
 *          6.4"). Given `os_plot` primitives (circles, rectangles,
 *          triangles) have been reliable in every single round so far
 *          with zero unexplained failures, standardised on them for both
 *          pawns and dice rather than keep chasing a sprite-rendering
 *          bug with no diagnosable cause -- correctness over the
 *          authentic GEOS silhouette shape for this Phase 1 placeholder
 *          pass. Revisit real sprite art in Phase 2 with more time to
 *          debug properly (or a different underlying mechanism, e.g. an
 *          explicit ColourTrans_GenerateTable translation table).
 */
static void plot_pawn(int player, int pawn_index, int origin_x, int origin_y)
{
	board_cell cell = board_pawn_cell(&game, player, pawn_index);
	int cx, cy;
	int body_radius = PAWN_SIZE * 5 / 16;
	int head_radius = PAWN_SIZE * 3 / 16;
	/* Outline thickness -- drawn as a slightly larger black circle behind
	 * each fill circle rather than an os_PLOT_CIRCLE_OUTLINE stroke, so its
	 * width is controllable (the outline plot code draws a fixed 1-pixel
	 * line). Needed so a pawn sitting on a same-coloured background marker
	 * (its own home column lane, or its own ring entry marker) is still
	 * visible against it -- per explicit user request. */
	int outline = PAWN_SIZE / 12;
	int body_y, head_y;

	cell_centre(cell.col, cell.row, origin_x, origin_y, &cx, &cy);
	body_y = cy - body_radius * 2 / 5;
	head_y = cy + body_radius * 4 / 5;

	set_gcol(0, 0, 0);
	fill_circle(cx, body_y, body_radius + outline);
	fill_circle(cx, head_y, head_radius + outline);

	set_gcol(player_rgb[player][0], player_rgb[player][1], player_rgb[player][2]);
	fill_circle(cx, body_y, body_radius);
	fill_circle(cx, head_y, head_radius);
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

/*
 * Function: plot_dice
 * Summary: Draw a die face for the current roll -- a white square with a
 *          thin black border and the standard pip layout -- in the panel
 *          gap between the status line and the Throw button. Added in
 *          round 6.3 since nothing previously showed the roll's actual
 *          outcome anywhere on screen (per repeated user report: "no dice
 *          are shown still, nor outcome of the dice throw"). Draws
 *          nothing before the first throw of a turn (`game.last_roll ==
 *          0`).
 *
 *          Round 6.3 first tried this via GEOS's own dice1..6.gbm
 *          sprites (see assets/generate_placeholder_art.py); like the
 *          pawn sprite (see plot_pawn()'s doc comment), it rendered
 *          wrong in Arculator (cropped) for reasons that never
 *          reproduced offline. Drawn with `os_plot` primitives instead,
 *          for the same reliability reason.
 */
static void plot_dice(int origin_x, int origin_y)
{
	/* Pip positions per face, as (col,row) on a 3x3 grid (0,0)=top-left
	 * .. (2,2)=bottom-right, standard die layout. {-1,-1} marks unused
	 * slots for faces with fewer than 6 pips. */
	static const signed char pips[6][6][2] = {
		{ {1, 1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1} },
		{ {0, 0}, {2, 2},   {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1} },
		{ {0, 0}, {1, 1},   {2, 2},   {-1, -1}, {-1, -1}, {-1, -1} },
		{ {0, 0}, {2, 0},   {0, 2},   {2, 2},   {-1, -1}, {-1, -1} },
		{ {0, 0}, {2, 0},   {1, 1},   {0, 2},   {2, 2},   {-1, -1} },
		{ {0, 0}, {2, 0},   {0, 1},   {2, 1},   {0, 2},   {2, 2} },
	};
	int face = game.last_roll;
	int cx, cy, x0, y0, x1, y1, i;
	int border = DICE_SIZE / 16;
	/* Smaller relative to `step` than a naive size/10 would give --
	 * reported pip crowding on face 6 (two columns of 3) at the old
	 * ratio, where adjacent same-column pips had very little gap between
	 * their edges. */
	int pip_radius = DICE_SIZE / 13;
	int step = DICE_SIZE / 4;

	if (face == 0)
		return;

	cx = origin_x + DICE_CENTRE_X;
	cy = origin_y + DICE_CENTRE_Y;
	x0 = cx - DICE_SIZE / 2;
	y0 = cy - DICE_SIZE / 2;
	x1 = cx + DICE_SIZE / 2;
	y1 = cy + DICE_SIZE / 2;

	/* Diagnostic for a reported "last line of die does not show" -- no
	 * code bug found by re-reading this geometry (it's a plain square,
	 * DICE_SIZE in both dimensions), so log the actual box each redraw
	 * computes to check against WINDOW_HEIGHT/the window's current state
	 * next round rather than guess further. */
	debug_log("plot_dice: face=%d box=(%d,%d,%d,%d) WINDOW_HEIGHT=%d\n",
	          face, x0, y0, x1, y1, WINDOW_HEIGHT);

	set_gcol(0, 0, 0);
	fill_rect(x0, y0, x1, y1);
	set_gcol(255, 255, 255);
	fill_rect(x0 + border, y0 + border, x1 - border, y1 - border);

	set_gcol(0, 0, 0);
	for (i = 0; i < 6; i++) {
		int col = pips[face - 1][i][0];
		int row = pips[face - 1][i][1];

		if (col < 0)
			break;
		fill_circle(x0 + DICE_SIZE / 2 + (col - 1) * step,
		            y0 + DICE_SIZE / 2 - (row - 1) * step,
		            pip_radius);
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

		plot_dice(origin_x, origin_y);

		more = wimp_get_rectangle(redraw);
	}
}

/*
 * Function: single_movable_pawn
 * Summary: If exactly one bit is set in a ludo_movable_pawns() mask,
 *          return that pawn's index; otherwise (none, or more than one)
 *          return -1. Used so the player is only ever asked to pick a
 *          pawn when there's an actual choice to make -- see
 *          game_view_click()'s ICON_THROW handler.
 */
static int single_movable_pawn(unsigned mask)
{
	int pawn, found = -1;

	for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
		if (!(mask & (1u << pawn)))
			continue;
		if (found != -1)
			return -1;
		found = pawn;
	}
	return found;
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

/*
 * Function: flash_throw_button
 * Summary: Briefly switch the Throw icon's border from R1 ("slab out", a
 *          raised button look, its resting state) to R2 ("slab in",
 *          sunken/pressed) and back, giving genuine RISC OS click
 *          feedback -- per explicit user request. The icon's validation
 *          string (throw_validation) is a buffer this code owns and can
 *          mutate directly; wimp_set_icon_state() with no actual flag
 *          change (0, 0) is the standard way to make the Wimp re-read and
 *          redraw an icon's indirected data after changing it in place
 *          (the same pattern refresh_status() already uses for the
 *          status/name text). The delay is a short, deliberate busy-wait
 *          (~0.1s) so the pressed look is actually visible -- there's no
 *          separate down/up event to synchronise against in this
 *          project's plain Mouse_Click handling, so this is timed rather
 *          than tied to the physical button release.
 */
static void flash_throw_button(void)
{
	os_t start;

	strncpy(throw_validation, "R2", sizeof(throw_validation));
	wimp_set_icon_state(window_handle, ICON_THROW, 0, 0);

	start = os_read_monotonic_time();
	while (os_read_monotonic_time() - start < 10)
		; /* ~10 centiseconds */

	strncpy(throw_validation, "R1", sizeof(throw_validation));
	wimp_set_icon_state(window_handle, ICON_THROW, 0, 0);
}

void game_view_click(wimp_pointer *pointer)
{
	/* Unconditional entry log -- the user reports clicks on the board not
	 * registering at all, and every prior round of click-side logging
	 * (inside the wimp_ICON_WINDOW branch below) has come back completely
	 * empty, which is only possible if either this function is never being
	 * reached for those clicks, or pointer->i isn't matching
	 * wimp_ICON_WINDOW (0xFFFFFFFF) the way it should for an ordinary
	 * background click. This line fires for literally every click in the
	 * window regardless of which icon (or none) it lands on, to settle
	 * that question definitively. */
	debug_log("game_view_click: entered, pos=(%d,%d) buttons=0x%x pointer->i=%d "
	          "(ICON_THROW=%d ICON_NAME=%d ICON_STATUS=%d wimp_ICON_WINDOW=%d)\n",
	          pointer->pos.x, pointer->pos.y, (unsigned) pointer->buttons, pointer->i,
	          ICON_THROW, ICON_NAME, ICON_STATUS, (int) wimp_ICON_WINDOW);

	if (pointer->i == ICON_THROW) {
		flash_throw_button();

		if (game.winner != -1) {
			game_view_new_game();
		} else {
			ludo_roll(&game, 0);
			/* A six that releases a home pawn changes the board itself
			 * (the released pawn appears on the ring), so needs a full
			 * redraw. Otherwise, if exactly one pawn can legally move
			 * (including the forced-pawn case, which is always exactly
			 * one), move it immediately rather than making the player
			 * click it -- per explicit user request ("if there is only
			 * one possible pawn that moves, don't ask which pawn should
			 * move"): the board changes either way, needing a full
			 * redraw. Only when nothing changed (no release, no
			 * auto-move -- just the status/die) is a panel-only redraw
			 * enough; forcing a full-window redraw on every single throw
			 * regardless caused a visible flash for no visual benefit. */
			if (game.just_released) {
				wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
			} else {
				int auto_pawn = single_movable_pawn(ludo_movable_pawns(&game));

				if (auto_pawn != -1) {
					ludo_move_pawn(&game, auto_pawn);
					wimp_force_redraw(window_handle, 0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
				} else {
					wimp_force_redraw(window_handle, PANEL_X0, -WINDOW_HEIGHT, WINDOW_WIDTH, 0);
				}
			}
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
