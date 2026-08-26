/*
 * ArchiLudo game view -- implementation.
 * See include/game_view.h for the module overview and API docs.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "oslib/wimp.h"
#include "oslib/colourtrans.h"
#include "oslib/wimpspriteop.h" /* wimpspriteop_AREA, for def.sprite_area only --
                                  * see load_pawn_sprites()/plot_pawn() for the
                                  * project's OWN private sprite area, loaded
                                  * separately and unrelated to this one */
#include "oslib/osspriteop.h" /* xosspriteop_load_sprite_file() -- see
                                * load_pawn_sprites() (round 7.17, the
                                * Wimp_PlotIcon sprite pivot -- see
                                * docs/ARCHITECTURE.md's "Resume here"/Round
                                * 7.17 for the full background) */

#include "game_view.h"
#include "game_logic.h"
#include "board_layout.h"
#include "ai.h"

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
 * than the original guess -- 40 tall matches the system font's own
 * 32-unit height plus a modest margin, the same proportions those
 * buttons use. Width sized for "Continue" (8 characters, 128 OS units
 * at the system font's fixed 16 units/char), the longer of this one
 * button's two labels (see refresh_status()) -- 120 (sized only for
 * "Throw", 5 characters) clipped "Continue" per explicit user report. */
#define THROW_WIDTH  168
#define THROW_HEIGHT  40

/* Highlight ring drawn around a movable pawn's cell when the current
 * human player has more than one legal choice, and around the cell a
 * hovered movable pawn would land on -- per explicit user request
 * ("suggest a way to highlight possible moves ... on hover over
 * possible moves, highlight the destination"). Sized a little larger
 * than the plain track marker so it reads as a ring drawn over/around
 * existing content, not a replacement for it. */
#define MOVABLE_HIGHLIGHT_RADIUS (MARKER_RADIUS + 6)
#define HOVER_HIGHLIGHT_RADIUS   (MARKER_RADIUS + 10)

/* Dice-roll animation: cycles through cosmetic faces for a short beat
 * before settling on the real (already-determined) result -- see
 * start_roll_animation(). Applies uniformly to human and AI rolls. */
#define ROLL_ANIM_TICKS    8
#define ROLL_ANIM_TICK_CS  6

/* Pawn-move animation: follows the actual board track -- one board
 * square per die pip, not a straight line cutting across the board --
 * see start_move_animation()'s `move_anim_path[]`. Each square-to-square
 * segment gets its own MOVE_ANIM_TICKS_PER_CELL ticks of linear
 * interpolation, so a longer roll takes proportionally longer to animate
 * rather than covering more distance in the same fixed time. Applies
 * uniformly to human and AI moves. */
#define MOVE_ANIM_TICKS_PER_CELL 3
#define MOVE_ANIM_STEP_CS        4
/* A legal move is never more than a 6 pip roll, so at most 7 cells
 * (start cell + up to 6 steps) -- see start_move_animation(). */
#define MOVE_ANIM_MAX_PATH 7

/* How often game_view_poll_idle() re-checks the pointer position for
 * hover highlighting -- cheap, but no need on literally every single
 * Null_Reason_Code poll. */
#define HOVER_POLL_CS 5

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

/* Per-player configuration from src/setup_view.c's "New Game" dialogue --
 * see game_view_configure_players(). configured_name[n][0]=='\0' means
 * "no custom name set", falling back to player_name[n] above. */
static char configured_name[LUDO_PLAYERS][GAME_VIEW_NAME_LEN];
static int player_is_ai[LUDO_PLAYERS];

static wimp_w window_handle = (wimp_w) -1;
static ludo_game game;
static char name_text[NAME_TEXT_LEN] = "";
static char status_text[STATUS_TEXT_LEN] = "";
/* "Throw" or "Continue" -- see refresh_status(). Sized for "Continue\0",
 * the longer of the two. */
static char throw_text[10] = "Throw";
static char throw_validation[4] = "R1";
/* Current on-screen shaded state of the Throw/Continue icon, so
 * refresh_status() only toggles wimp_ICON_SHADED (an EOR flag) when it
 * actually needs to change -- see round 7.15. */
static int throw_shaded = 0;

/* True once a game has actually been started via game_view_new_game()
 * (i.e. via src/setup_view.c's "New Game" dialogue) -- lets main.c tell
 * a first-ever iconbar click (which must ask for player details first)
 * apart from a later one (which just reopens/refocuses the game already
 * in progress). See game_view_has_started(). */
static int game_started = 0;

/*
 * Turn/animation phase. STEP_IDLE is the normal interactive state
 * (a human player's turn, nothing animating); STEP_AWAIT_CONTINUE pauses
 * an AI-controlled player's turn until the Throw/Continue icon (see
 * refresh_status()) is clicked, so an AI turn never advances on its own
 * -- per explicit user request ("only continue after pressing that").
 * STEP_ROLLING/STEP_MOVING run a short cosmetic animation (see
 * start_roll_animation()/start_move_animation()) before the real,
 * already-determined result is revealed; both apply equally to human and
 * AI turns.
 */
typedef enum {
	STEP_IDLE,
	STEP_AWAIT_CONTINUE,
	STEP_ROLLING,
	STEP_MOVING
} turn_step;

static turn_step step = STEP_IDLE;

/* Roll animation state -- see start_roll_animation(), game_view_poll_idle().
 * roll_anim_player records who was actually rolling, captured *before*
 * calling ludo_roll() -- see resolve_roll()'s doc comment for why this
 * matters (ludo_roll() can silently pass the turn to a different player
 * internally, three failed tries in a row). */
static int roll_anim_ticks_done;
static int roll_anim_player;
static os_t roll_anim_next_tick;
/* The die face plot_dice() actually draws: game.last_roll once settled,
 * a cycling cosmetic value while STEP_ROLLING. */
static int dice_display_face = 0;

/* Move animation state -- see start_move_animation(), game_view_poll_idle().
 * move_anim_path[] is the sequence of board cells the pawn actually
 * passes through (its own current cell, then one entry per step of the
 * roll -- see cell_for_steps()), not just the two endpoints, so the
 * animation follows the real track instead of cutting across the board
 * in a straight line. move_anim_tick counts ticks across the *whole*
 * path; game_view_poll_idle() and plot_pawn() both derive which segment
 * that falls in from MOVE_ANIM_TICKS_PER_CELL. */
static int move_anim_player, move_anim_pawn_index;
static board_cell move_anim_path[MOVE_ANIM_MAX_PATH];
static int move_anim_path_len;
static int move_anim_tick;
static os_t move_anim_next_tick;

/* Round 7.15: a snapshot of every pawn's board cell/in_play state, taken
 * immediately before a state-changing ludo_roll()/ludo_move_pawn() call
 * -- see snapshot_pawn_positions()/update_settle_diff_area(). Lets the
 * post-animation "settle" redraw at turn's end be scoped to only the
 * pawns a capture or a six's mandatory release actually displaced,
 * instead of the whole board, per explicit user report that the
 * full-window redraw at every turn transition was itself visibly
 * flickering. */
static board_cell settle_prev_cell[LUDO_PLAYERS][LUDO_PAWNS];
static int settle_prev_in_play[LUDO_PLAYERS][LUDO_PAWNS];

/* Hover-preview state -- highlights the destination cell a movable pawn
 * under the pointer would land on, per explicit user request. Only
 * meaningful while STEP_IDLE, the current player is human, and there is
 * more than one legal choice (see game_view_poll_idle()). */
static int hover_active = 0;
static board_cell hover_destination;
static os_t hover_next_poll;

/* Highlight flash state -- the movable-pawn rings and hover-destination
 * ring pulse on/off rather than sitting static, matching the selected-
 * square flash technique in a real, shipped RISC OS board game
 * (github.com/marutan/ro-chess's hilite_do(), which flashes on the same
 * ~50-centisecond cadence used here) -- per explicit user request after
 * reviewing that project for inspiration. See draw_highlights(),
 * update_highlight_area(). */
static int highlight_flash_on = 1;
static os_t highlight_next_flash;
#define HIGHLIGHT_FLASH_CS 50

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

/* Round 7.17 pawn icon sprites -- see load_pawn_sprites()/plot_pawn().
 * pawn_sprite_area is this program's OWN private sprite area (loaded
 * from assets/PawnSprites, malloc()'d once at startup), entirely
 * separate from wimpspriteop_AREA (the Wimp's shared pool, still used
 * for def.sprite_area) -- one named sprite per player, "pawn0".."pawn3"
 * in game_logic.c's player-index order. pawn_sprites_loaded stays 0 if
 * the file can't be found/loaded, in which case plot_pawn() falls back
 * to the original os_plot circles -- per this project's established
 * "the game must stay playable if a sprite approach fails again"
 * caution (see docs/ARCHITECTURE.md's round 6.3/6.4 notes). */
static osspriteop_area *pawn_sprite_area = NULL;
static int pawn_sprites_loaded = 0;
static const char *pawn_sprite_names[LUDO_PLAYERS] = { "pawn0", "pawn1", "pawn2", "pawn3" };

/*
 * Function: load_pawn_sprites
 * Summary: Load assets/PawnSprites (see assets/generate_icon_sprites.py)
 *          into a freshly malloc()'d private sprite area, so plot_pawn()
 *          can plot each player's pawn via Wimp_PlotIcon. Called once
 *          from game_view_initialise(). Leaves pawn_sprites_loaded at 0
 *          (its safe default) on any failure -- a missing/corrupt/
 *          unreadable sprite file is not fatal, just falls back to the
 *          existing os_plot circles.
 */
static void load_pawn_sprites(void)
{
	char path[APP_DIR_LEN + 20];
	FILE *f;
	long size;
	os_error *err;

	resource_path(path, sizeof(path), "PawnSprites");
	f = fopen(path, "rb");
	if (f == NULL) {
		debug_log("load_pawn_sprites: fopen failed for \"%s\"\n", path);
		return;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fclose(f);
	if (size <= 0) {
		debug_log("load_pawn_sprites: \"%s\" is empty or unreadable (size=%ld)\n",
		          path, size);
		return;
	}

	/* +4: the in-memory area control block has a leading "total size"
	 * word the sprite FILE itself omits -- see tools/riscos_sprite.py's
	 * module docstring / docs/GRAPHICS_TOOLING.md for the same
	 * offset-minus-4 convention on the reading side. */
	pawn_sprite_area = malloc((size_t) size + 4);
	if (pawn_sprite_area == NULL) {
		debug_log("load_pawn_sprites: malloc(%ld) failed\n", size + 4);
		return;
	}
	pawn_sprite_area->size = (int) size + 4;
	pawn_sprite_area->sprite_count = 0;
	pawn_sprite_area->first = 16;
	pawn_sprite_area->used = 16;

	err = xosspriteop_load_sprite_file(osspriteop_USER_AREA, pawn_sprite_area, path);
	if (err != NULL) {
		debug_log("load_pawn_sprites: OS_SpriteOp LoadFile failed for \"%s\": %s\n",
		          path, err->errmess);
		free(pawn_sprite_area);
		pawn_sprite_area = NULL;
		return;
	}

	pawn_sprites_loaded = 1;
	debug_log("load_pawn_sprites: loaded \"%s\" ok, %d sprite(s)\n",
	          path, pawn_sprite_area->sprite_count);
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
 * Function: fill_window_background
 * Summary: Fill a rectangle (absolute screen coordinates) with the
 *          window's own background colour -- via Wimp_SetColour, not a
 *          hand-picked RGB approximation, so it stays correct regardless
 *          of the user's desktop colour scheme (matches
 *          def.work_bg = wimp_COLOUR_VERY_LIGHT_GREY in
 *          game_view_initialise()). Needed only by the Wimp_UpdateWindow
 *          redraw paths (update_move_animation_area(),
 *          update_highlight_area(), redraw_now()) -- unlike a genuine
 *          Redraw_Window_Request, handled via Wimp_RedrawWindow in
 *          game_view_redraw(), Wimp_UpdateWindow does not clear
 *          anything first (see docs/ARCHITECTURE.md's Round 7.10), so
 *          without this, a shape drawn between two grid points one tick
 *          (a pawn mid-slide, a ring at its full radius) is never
 *          actually erased before the next tick draws over it -- per
 *          explicit user report ("old pawn position is not restored to
 *          old state in interim frames").
 */
static void fill_window_background(int x0, int y0, int x1, int y1)
{
	wimp_set_colour(wimp_COLOUR_VERY_LIGHT_GREY);
	fill_rect(x0, y0, x1, y1);
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
/*
 * Function: player_display_name
 * Summary: A player's configured name (see game_view_configure_players()),
 *          falling back to their fixed colour name if none was set.
 */
static const char *player_display_name(int player)
{
	if (configured_name[player][0] != '\0')
		return configured_name[player];
	return player_name[player];
}

static void refresh_status(void)
{
	/* Only genuinely an "AI's turn" while the game isn't already won --
	 * once there's a winner, the Throw/Continue icon always reverts to
	 * "Throw" (its "play again" meaning, see game_view_click()),
	 * regardless of which player happened to win. */
	int ai_turn = (game.winner == -1) && player_is_ai[game.current_player];

	if (game.winner != -1) {
		snprintf(name_text, NAME_TEXT_LEN, "%s WINS!", player_display_name(game.winner));
		snprintf(status_text, STATUS_TEXT_LEN, "Click Throw");
	} else {
		snprintf(name_text, NAME_TEXT_LEN, "%s", player_display_name(game.current_player));

		/* Per explicit user request, AI turns get their own status
		 * wording throughout (not left blank/stale) -- mirrors the human
		 * wording below, but "Pick a pawn" doesn't apply (the AI always
		 * picks immediately, see resolve_roll()) and "Click Throw"
		 * becomes "Click Continue" to match the relabelled button. */
		if (game.last_roll == 0) {
			snprintf(status_text, STATUS_TEXT_LEN, ai_turn ? "Click Continue" : "Click Throw");
		} else if (ai_turn) {
			if (ludo_movable_pawns(&game) != 0)
				snprintf(status_text, STATUS_TEXT_LEN, "AI is moving...");
			else if (game.just_released)
				snprintf(status_text, STATUS_TEXT_LEN, "Pawn released!");
			else
				snprintf(status_text, STATUS_TEXT_LEN, "Throw again");
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

	/* The Throw/Continue icon is one single physical button, relabelled
	 * -- per explicit user request ("ensure only either throw or continue
	 * button is visible"), never two separate buttons shown/hidden. */
	strncpy(throw_text, ai_turn ? "Continue" : "Throw", sizeof(throw_text) - 1);
	throw_text[sizeof(throw_text) - 1] = '\0';

	/* Round 7.15: shade the button whenever clicking it wouldn't actually
	 * do anything -- mid-animation, an AI turn between its own automatic
	 * actions, or a human with a pawn to pick instead -- per explicit
	 * user request ("show the buttons only when user is supposed to
	 * click on it, not in between"). Mirrors game_view_click()'s own
	 * ICON_THROW guard exactly, so a shaded button and an ignored click
	 * always agree. */
	{
		int throw_active;

		if (game.winner != -1)
			throw_active = 1; /* "play again", always clickable once won */
		else if (step == STEP_AWAIT_CONTINUE)
			throw_active = 1;
		else if (step == STEP_IDLE && !ai_turn
		      && (game.last_roll == 0 || ludo_movable_pawns(&game) == 0))
			throw_active = 1;
		else
			throw_active = 0;

		if (window_handle != (wimp_w) -1) {
			wimp_icon_flags eor = (throw_active == throw_shaded) ? wimp_ICON_SHADED
			                                                      : (wimp_icon_flags) 0;

			wimp_set_icon_state(window_handle, ICON_NAME, 0, 0);
			wimp_set_icon_state(window_handle, ICON_STATUS, 0, 0);
			wimp_set_icon_state(window_handle, ICON_THROW, eor, 0);
			throw_shaded = !throw_active;
		}
	}
}

void game_view_configure_players(const char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
                                  const int is_ai[LUDO_PLAYERS])
{
	int player;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		strncpy(configured_name[player], names[player], GAME_VIEW_NAME_LEN - 1);
		configured_name[player][GAME_VIEW_NAME_LEN - 1] = '\0';
		player_is_ai[player] = is_ai[player];
	}
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
 * Function: cell_centre_work
 * Summary: Same as cell_centre(), but in WORK AREA coordinates (no
 *          origin_x/origin_y applied) -- what Wimp_PlotIcon needs for an
 *          icon's bounding box. Round 7.18 fix: Wimp_PlotIcon's icon
 *          block "is the same format as that used by Wimp_CreateIcon...
 *          this being implicitly the window which is currently being
 *          redrawn or updated" (PRM, wimp.html) -- i.e. work-area
 *          coordinates, exactly like any other icon's fixed extent,
 *          NOT the absolute screen coordinates os_plot calls need.
 *          Confirmed against ro-chess's own real, working code: its
 *          BOARD[]/icon_update() never applies any origin/scroll offset
 *          to an icon's `.box` before calling wimp_ploticon(), and even
 *          passes that same untranslated box straight to
 *          Wimp_UpdateWindow's own (also work-area) box parameter.
 *          plot_pawn() originally used cell_centre() (screen-absolute)
 *          for the icon's extent too, by mistake -- this placed every
 *          pawn icon at the wrong screen location entirely (off the
 *          visible window whenever the window wasn't at OS-unit
 *          position (0,0)), which is why pawns stopped rendering at all
 *          once the sprite pivot shipped.
 */
static void cell_centre_work(int col, int row, int *wx, int *wy)
{
	*wx = BOARD_ORIGIN_X + col * CELL + CELL / 2;
	*wy = BOARD_ORIGIN_Y - row * CELL - CELL / 2;
}

/*
 * Function: plot_pawn
 * Summary: Draw one pawn -- home base, ring, home column, or finished --
 *          wherever board_pawn_cell() says it currently is. Plots the
 *          real pawn icon sprite (see load_pawn_sprites(),
 *          assets/generate_icon_sprites.py) via Wimp_PlotIcon when it
 *          loaded successfully; falls back to two overlapping filled
 *          circles (a wider "body" below a narrower "head") otherwise.
 *
 *          Round 6.3 dropped sprite plotting entirely after three
 *          separate small sprites rendered wrong in Arculator in ways
 *          that never reproduced in any offline check (round 6.1's
 *          board-entry markers too narrow, round 6.3's dice cropped,
 *          this pawn sprite solid black regardless of player) -- see
 *          docs/GRAPHICS_TOOLING.md's "Round 6.4". Round 7.16/7.17
 *          research found the actual, specific, documented cause: the
 *          old code used `OS_SpriteOp 34`
 *          (xosspriteop_put_sprite_user_coords), which the PRM states
 *          outright is undefined for a sprite whose mode doesn't match
 *          the current screen mode -- not an unexplained platform
 *          mystery after all. `Wimp_PlotIcon` goes through
 *          `PutSpriteScaled` with a proper scale/translation table
 *          instead, confirmed against real, shipped example code
 *          (`github.com/marutan/ro-chess`'s `icon_update()`) -- see
 *          docs/ARCHITECTURE.md's "Resume here"/round history for the
 *          full writeup. The `os_plot` fallback stays in place
 *          regardless (this project's established "the game must stay
 *          playable" caution) for whenever load_pawn_sprites() didn't
 *          find/load assets/PawnSprites.
 */
static void plot_pawn(int player, int pawn_index, int origin_x, int origin_y)
{
	int wx, wy;  /* work-area coordinates -- see cell_centre_work(); the
	              * only ones Wimp_PlotIcon's icon extent should use. */
	int cx, cy;  /* absolute screen coordinates -- os_plot fallback only. */
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

	/* The one pawn currently mid-move (see start_move_animation()) is
	 * drawn part-way between its old and new cell rather than at
	 * board_pawn_cell()'s (already-updated) destination -- per explicit
	 * user request ("animate the pawns actually moving to the new
	 * placement location") rather than the previous instant jump. Every
	 * other pawn draws at its normal current cell as before. */
	if (step == STEP_MOVING && player == move_anim_player && pawn_index == move_anim_pawn_index) {
		int fx, fy, tx, ty;
		int segments = move_anim_path_len - 1;
		int tick = move_anim_tick;
		int seg, seg_progress;

		if (segments < 1)
			segments = 1;
		seg = tick / MOVE_ANIM_TICKS_PER_CELL;
		if (seg >= segments)
			seg = segments - 1;
		seg_progress = tick - seg * MOVE_ANIM_TICKS_PER_CELL;

		cell_centre_work(move_anim_path[seg].col, move_anim_path[seg].row, &fx, &fy);
		cell_centre_work(move_anim_path[seg + 1].col, move_anim_path[seg + 1].row, &tx, &ty);
		wx = fx + (tx - fx) * seg_progress / MOVE_ANIM_TICKS_PER_CELL;
		wy = fy + (ty - fy) * seg_progress / MOVE_ANIM_TICKS_PER_CELL;
	} else {
		board_cell cell = board_pawn_cell(&game, player, pawn_index);

		cell_centre_work(cell.col, cell.row, &wx, &wy);
	}
	cx = origin_x + wx;
	cy = origin_y + wy;

	if (pawn_sprites_loaded) {
		wimp_icon icon;
		int half = PAWN_SIZE / 2;

		icon.extent.x0 = wx - half;
		icon.extent.y0 = wy - half;
		icon.extent.x1 = wx + half;
		icon.extent.y1 = wy + half;
		icon.flags = wimp_ICON_SPRITE | wimp_ICON_INDIRECTED |
		             wimp_ICON_HCENTRED | wimp_ICON_VCENTRED;
		/* size=13: 12-character max sprite name + terminator, matching
		 * pawn_sprite_names[]'s own longest entry ("pawn0".."pawn3",
		 * well within that, but 13 is the conventional buffer size for
		 * an old-style sprite name regardless). */
		icon.data.indirected_sprite.id = (osspriteop_id) pawn_sprite_names[player];
		icon.data.indirected_sprite.area = pawn_sprite_area;
		icon.data.indirected_sprite.size = 13;
		wimp_plot_icon(&icon);
		return;
	}

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
	int face = dice_display_face;
	int cx, cy, x0, y0, x1, y1, i;
	/* Mode 15 is 2x4 OS units per physical pixel (non-square -- see
	 * CLAUDE.md's Testing section): any manually fill_rect()-drawn
	 * border/outline thickness under 4 OS units risks not rendering at
	 * all on some or all edges, since a pixel only paints when its
	 * centre (spaced 4 units apart vertically) falls inside the filled
	 * shape. DICE_SIZE/16 happens to floor to exactly 4 at the current
	 * DICE_SIZE (72), but that was luck of integer truncation, not a
	 * guarantee -- a different DICE_SIZE could silently drop below the
	 * safe minimum (see the swatch-outline bug this same rule explains,
	 * game_view_redraw()'s swatch block below). Floored explicitly here
	 * so this can't regress if DICE_SIZE ever changes again. */
	int border = DICE_SIZE / 16;
	if (border < 4)
		border = 4;
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

	/* The "last line of die does not show" report this diagnostic was
	 * added for is now understood and fixed (round 7.7's mode-15
	 * minimum-line-thickness finding, not a coordinate bug -- see
	 * docs/ARCHITECTURE.md). Removed rather than left logging: this ran
	 * on every single animation tick (dice-roll cycling, ~8 times per
	 * throw), which had come to dominate the debug Log's size and made
	 * genuinely useful entries (move/roll outcomes) hard to find --
	 * exactly the noise that made the Round 7.8 investigation slower
	 * than it needed to be. */
	/* Temporary diagnostic (round 7.21) -- see update_dice_area()'s own
	 * new logging; remove both once the top/right border crop is
	 * diagnosed. This one logs what plot_dice() itself computes and
	 * actually asks fill_rect() to paint. */
	debug_log("plot_dice: origin=(%d,%d) cx=%d cy=%d box=(%d,%d,%d,%d) border=%d\n",
	          origin_x, origin_y, cx, cy, x0, y0, x1, y1, border);
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

/*
 * Function: draw_board_region
 * Summary: Draw cell markers and pawns restricted to a rectangular range
 *          of board grid cells (inclusive) -- shared by game_view_redraw()
 *          (called with the whole board) and update_move_animation_area()
 *          (called with just the few cells a pawn-move animation's
 *          current frame touches), so a move animation doesn't have to
 *          re-plot the *entire* board -- roughly 150 os_plot primitives
 *          (121 cell markers, 16 pawns' worth of circles, swatch, dice)
 *          -- on every single animation tick. Per explicit user report
 *          ("pawn movement also seems to do redraw every frame... only
 *          local redraw?").
 */
static void draw_board_region(int origin_x, int origin_y, int col0, int row0, int col1, int row1)
{
	int col, row, player, pawn;

	for (row = row0; row <= row1; row++) {
		for (col = col0; col <= col1; col++) {
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

	for (player = 0; player < LUDO_PLAYERS; player++) {
		for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
			board_cell cell;

			/* The one pawn currently mid-move draws somewhere along its
			 * whole path (see plot_pawn()) -- included if any cell of
			 * that path falls in range, since that covers every cell its
			 * interpolated position could actually pass through. */
			if (step == STEP_MOVING && player == move_anim_player && pawn == move_anim_pawn_index) {
				int in_range = 0, k;

				for (k = 0; k < move_anim_path_len; k++) {
					if (move_anim_path[k].col >= col0 && move_anim_path[k].col <= col1
					 && move_anim_path[k].row >= row0 && move_anim_path[k].row <= row1) {
						in_range = 1;
						break;
					}
				}
				if (in_range)
					plot_pawn(player, pawn, origin_x, origin_y);
				continue;
			}

			cell = board_pawn_cell(&game, player, pawn);
			if (cell.col >= col0 && cell.col <= col1 && cell.row >= row0 && cell.row <= row1)
				plot_pawn(player, pawn, origin_x, origin_y);
		}
	}
}

/*
 * Function: cell_range_to_work_box
 * Summary: Convert a rectangular range of board grid cells (inclusive)
 *          into work-area OS-unit bounds (the coordinate space
 *          Wimp_ForceRedraw takes, and the same one the rest of this
 *          module's WINDOW_WIDTH/WINDOW_HEIGHT etc. already use) -- the
 *          inverse of cell_centre()'s per-cell math, but for a whole
 *          range at once rather than one cell's centre point.
 */
static void cell_range_to_work_box(int col0, int row0, int col1, int row1,
                                    int *x0, int *y0, int *x1, int *y1)
{
	*x0 = BOARD_ORIGIN_X + col0 * CELL;
	*x1 = BOARD_ORIGIN_X + (col1 + 1) * CELL;
	*y1 = BOARD_ORIGIN_Y - row0 * CELL;
	*y0 = BOARD_ORIGIN_Y - (row1 + 1) * CELL;
}

/*
 * Function: update_move_animation_area
 * Summary: Synchronously redraw just the board cells a pawn-move
 *          animation's current frame can touch (every cell of its path
 *          -- see move_anim_path[] -- plus a one-cell margin so
 *          outline/marker radii spilling slightly past a cell's own
 *          boundary aren't clipped) via Wimp_UpdateWindow -- used
 *          instead of a full redraw_now() on every STEP_MOVING tick.
 *
 *          Third attempt at this (see docs/ARCHITECTURE.md's Rounds
 *          7.3/7.5/7.10 for the full history). Round 7.3's direct
 *          synchronous Wimp_RedrawWindow call auto-cleared the *entire*
 *          exposed window to background colour every tick (a manually
 *          invoked RedrawWindow always reports the whole window, not a
 *          caller-supplied clip). Round 7.5's fix -- Wimp_ForceRedraw +
 *          letting a genuine Redraw_Window_Request arrive back through
 *          the normal Wimp_Poll loop -- was correct and is still what
 *          game_view_redraw() uses for real window-exposure redraws
 *          below, but Wimp_RedrawWindow's auto-clear-then-repaint is
 *          itself a visible two-step flash on real/emulated ARM2/ARM3
 *          speeds, seen on every single animation tick (per explicit
 *          user report, and confirmed by research citing the RISC OS 3
 *          PRM's Wimp_UpdateWindow entry, ~/riscos-dev/prm-mirror/wimp.html:
 *          "the rectangles to be updated are not cleared by the Wimp
 *          first... this can be called at any time, not just in
 *          response to a Redraw_Window_Request").
 *
 *          Wimp_UpdateWindow was tried once already (Round 7.3) and
 *          produced no visible frames at all -- the actual bug, found by
 *          this round's research: unlike Wimp_RedrawWindow (where only
 *          `.w` is meaningful on entry, and the Wimp computes the
 *          rectangle itself), Wimp_UpdateWindow takes the rectangle as
 *          *input* (`w, x0, y0, x1, y1`) -- the earlier attempt left
 *          `redraw.box` as uninitialised stack garbage before the call,
 *          so the Wimp had no valid area to report back, and the
 *          `while (more)` loop's drawing call never ran. Confirmed
 *          against real, shipped example code (not just the PRM
 *          description): `github.com/marutan/ro-chess`'s `icon_update()`
 *          helper sets its redraw block's box to the icon's own
 *          work-area bounds before calling Wimp_UpdateWindow, then plots
 *          inline in the same `while (more)` loop -- exactly the
 *          pattern used here. Direct screen plotting outside this
 *          protocol entirely was considered and rejected: the PRM
 *          explicitly warns that in-window dragging "must use
 *          Wimp_UpdateWindow... rather than drawing directly on the
 *          screen" (window occlusion/multitasking correctness).
 *
 *          Any *other* pawn's position change this same move triggered
 *          (a capture sent home, a six-release) is outside this
 *          animation's scope and only appears once resolve_move() calls
 *          update_settle_diff_area() after the animation finishes (round
 *          7.15) -- an acceptable, deliberate limit (nothing asked for
 *          those to animate too, only the moving pawn itself).
 */
static void update_move_animation_area(void)
{
	wimp_draw redraw;
	osbool more;
	int col0, row0, col1, row1, x0, y0, x1, y1;
	int segments, seg;
	board_cell from_cell, to_cell;

	if (window_handle == (wimp_w) -1)
		return;

	/* Only the *current segment's* two cells (where the pawn's
	 * interpolated position -- see plot_pawn() -- can possibly fall this
	 * tick) need to be touched, not the whole move's path. An earlier
	 * version of this function unioned every cell of move_anim_path[],
	 * which for a roll that crosses a ring corner could span most of one
	 * side of the board; erasing and repainting that whole span on every
	 * one of the animation's ticks was needless work and was visible as
	 * flicker across the entire span, not just around the pawn -- per
	 * explicit user report. Restricting to the segment the pawn is
	 * actually sliding across right now is sufficient: the previous
	 * tick's position (which this call's erase step must cover) is
	 * always within the same or immediately preceding segment, and at a
	 * segment boundary the previous tick's interpolated position sits
	 * right next to (not before) the new segment's own start cell -- see
	 * plot_pawn()'s seg/seg_progress maths -- so it already falls inside
	 * this box's one-cell margin. */
	segments = move_anim_path_len - 1;
	if (segments < 1)
		segments = 1;
	seg = move_anim_tick / MOVE_ANIM_TICKS_PER_CELL;
	if (seg >= segments)
		seg = segments - 1;

	from_cell = move_anim_path[seg];
	to_cell = move_anim_path[seg + 1];

	col0 = from_cell.col; if (to_cell.col < col0) col0 = to_cell.col;
	row0 = from_cell.row; if (to_cell.row < row0) row0 = to_cell.row;
	col1 = from_cell.col; if (to_cell.col > col1) col1 = to_cell.col;
	row1 = from_cell.row; if (to_cell.row > row1) row1 = to_cell.row;

	col0--; if (col0 < 0) col0 = 0;
	row0--; if (row0 < 0) row0 = 0;
	col1++; if (col1 >= BOARD_GRID_SIZE) col1 = BOARD_GRID_SIZE - 1;
	row1++; if (row1 >= BOARD_GRID_SIZE) row1 = BOARD_GRID_SIZE - 1;

	cell_range_to_work_box(col0, row0, col1, row1, &x0, &y0, &x1, &y1);

	redraw.w = window_handle;
	redraw.box.x0 = x0;
	redraw.box.y0 = y0;
	redraw.box.x1 = x1;
	redraw.box.y1 = y1;

	more = wimp_update_window(&redraw);
	while (more) {
		int origin_x = redraw.box.x0 - redraw.xscroll;
		int origin_y = redraw.box.y1 - redraw.yscroll;

		/* Erase whatever the previous tick left here first -- see
		 * fill_window_background()'s doc comment for why this is
		 * necessary with Wimp_UpdateWindow specifically. */
		fill_window_background(redraw.box.x0, redraw.box.y0, redraw.box.x1, redraw.box.y1);
		draw_board_region(origin_x, origin_y, col0, row0, col1, row1);
		more = wimp_get_rectangle(&redraw);
	}
}

/*
 * Function: update_dice_area
 * Summary: Synchronously redraw just the die face's box via
 *          Wimp_UpdateWindow -- used instead of a full redraw_now() on
 *          every STEP_ROLLING tick. See update_move_animation_area()'s
 *          doc comment for the full history of why Wimp_UpdateWindow
 *          (not Wimp_ForceRedraw, not a direct Wimp_RedrawWindow call)
 *          and exactly how it needs to be called.
 */
static void update_dice_area(void)
{
	wimp_draw redraw;
	osbool more;

	if (window_handle == (wimp_w) -1)
		return;

	redraw.w = window_handle;
	redraw.box.x0 = DICE_CENTRE_X - DICE_SIZE / 2;
	redraw.box.x1 = DICE_CENTRE_X + DICE_SIZE / 2;
	redraw.box.y0 = DICE_CENTRE_Y - DICE_SIZE / 2;
	redraw.box.y1 = DICE_CENTRE_Y + DICE_SIZE / 2;

	/* Temporary diagnostic (round 7.21) -- per explicit user report that
	 * the die's top and right border lines are cropped/halved on real
	 * Arculator rendering, remove once diagnosed. Logs both what was
	 * REQUESTED (before wimp_update_window()) and what the Wimp actually
	 * REPORTS back on each iteration -- if these differ, the Wimp itself
	 * is clipping the update to something smaller than the die's full
	 * box; if they match, the bug is downstream (plot_dice()'s own
	 * fill_rect() calls or a pixel-rounding effect), not this request. */
	debug_log("update_dice_area: requested box=(%d,%d,%d,%d)\n",
	          redraw.box.x0, redraw.box.y0, redraw.box.x1, redraw.box.y1);

	more = wimp_update_window(&redraw);
	while (more) {
		int origin_x = redraw.box.x0 - redraw.xscroll;
		int origin_y = redraw.box.y1 - redraw.yscroll;

		debug_log("update_dice_area: got box=(%d,%d,%d,%d) clip=(%d,%d,%d,%d) "
		          "xscroll=%d yscroll=%d origin=(%d,%d)\n",
		          redraw.box.x0, redraw.box.y0, redraw.box.x1, redraw.box.y1,
		          redraw.clip.x0, redraw.clip.y0, redraw.clip.x1, redraw.clip.y1,
		          redraw.xscroll, redraw.yscroll, origin_x, origin_y);
		plot_dice(origin_x, origin_y);
		more = wimp_get_rectangle(&redraw);
	}
}

/*
 * Function: draw_highlights
 * Summary: Draw whichever highlight rings currently apply -- the
 *          movable-pawn rings (only meaningful while genuinely waiting
 *          for the human player to pick among more than one legal
 *          choice; single-choice rolls auto-move immediately, see
 *          resolve_roll(), so there's nothing to highlight then) and the
 *          hover-destination ring (see game_view_poll_idle()) -- unless
 *          the flash is currently in its "off" phase, in which case this
 *          draws nothing at all. Shared by game_view_redraw() (the whole
 *          board) and update_highlight_area() (just the affected cells,
 *          on each flash toggle) so both stay in exact agreement about
 *          what "currently highlighted" means.
 */
static void draw_highlights(int origin_x, int origin_y)
{
	if (!highlight_flash_on)
		return;

	if (step == STEP_IDLE && game.winner == -1 && !player_is_ai[game.current_player]
	 && game.last_roll != 0) {
		unsigned movable = ludo_movable_pawns(&game);

		if (movable != 0 && (movable & (movable - 1)) != 0) {
			int pawn;

			for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
				board_cell cell;
				int cx, cy;

				if (!(movable & (1u << pawn)))
					continue;
				cell = board_pawn_cell(&game, game.current_player, pawn);
				cell_centre(cell.col, cell.row, origin_x, origin_y, &cx, &cy);
				set_gcol(player_rgb[game.current_player][0],
				         player_rgb[game.current_player][1],
				         player_rgb[game.current_player][2]);
				outline_circle(cx, cy, MOVABLE_HIGHLIGHT_RADIUS);
			}
		}
	}

	if (hover_active) {
		int cx, cy;

		cell_centre(hover_destination.col, hover_destination.row, origin_x, origin_y, &cx, &cy);
		set_gcol(0, 0, 0);
		outline_circle(cx, cy, HOVER_HIGHLIGHT_RADIUS);
	}
}

/*
 * Function: update_highlight_area
 * Summary: Synchronously redraw just the board cells any currently-shown
 *          highlight ring touches (every movable pawn's cell, plus the
 *          hover-destination cell if active, plus a one-cell margin) via
 *          Wimp_UpdateWindow -- called on each flash toggle
 *          (game_view_poll_idle()) so the pulse is visible without a
 *          full-board redraw. The board/pawn content underneath is
 *          redrawn every single call, flash on or off, since
 *          Wimp_UpdateWindow doesn't clear anything -- this is what
 *          erases the previous frame's ring when the flash switches off
 *          (see docs/ARCHITECTURE.md's Round 7.10 for why
 *          Wimp_UpdateWindow is used this way at all).
 */
static void update_highlight_area(void)
{
	wimp_draw redraw;
	osbool more;
	int col0, row0, col1, row1, x0, y0, x1, y1, have_any = 0;

	if (window_handle == (wimp_w) -1)
		return;

	col0 = row0 = BOARD_GRID_SIZE;
	col1 = row1 = -1;

	if (step == STEP_IDLE && game.winner == -1 && !player_is_ai[game.current_player]
	 && game.last_roll != 0) {
		unsigned movable = ludo_movable_pawns(&game);

		if (movable != 0 && (movable & (movable - 1)) != 0) {
			int pawn;

			for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
				board_cell cell;

				if (!(movable & (1u << pawn)))
					continue;
				cell = board_pawn_cell(&game, game.current_player, pawn);
				if (cell.col < col0) col0 = cell.col;
				if (cell.col > col1) col1 = cell.col;
				if (cell.row < row0) row0 = cell.row;
				if (cell.row > row1) row1 = cell.row;
				have_any = 1;
			}
		}
	}

	if (hover_active) {
		if (hover_destination.col < col0) col0 = hover_destination.col;
		if (hover_destination.col > col1) col1 = hover_destination.col;
		if (hover_destination.row < row0) row0 = hover_destination.row;
		if (hover_destination.row > row1) row1 = hover_destination.row;
		have_any = 1;
	}

	if (!have_any)
		return;

	col0--; if (col0 < 0) col0 = 0;
	row0--; if (row0 < 0) row0 = 0;
	col1++; if (col1 >= BOARD_GRID_SIZE) col1 = BOARD_GRID_SIZE - 1;
	row1++; if (row1 >= BOARD_GRID_SIZE) row1 = BOARD_GRID_SIZE - 1;

	cell_range_to_work_box(col0, row0, col1, row1, &x0, &y0, &x1, &y1);

	redraw.w = window_handle;
	redraw.box.x0 = x0;
	redraw.box.y0 = y0;
	redraw.box.x1 = x1;
	redraw.box.y1 = y1;

	more = wimp_update_window(&redraw);
	while (more) {
		int origin_x = redraw.box.x0 - redraw.xscroll;
		int origin_y = redraw.box.y1 - redraw.yscroll;

		/* See fill_window_background()'s doc comment -- the ring's own
		 * radius extends past the marker/pawn underneath it, so without
		 * this a thin remnant of the "on" phase ring could survive an
		 * "off" phase. */
		fill_window_background(redraw.box.x0, redraw.box.y0, redraw.box.x1, redraw.box.y1);
		draw_board_region(origin_x, origin_y, col0, row0, col1, row1);
		draw_highlights(origin_x, origin_y);
		more = wimp_get_rectangle(&redraw);
	}
}

/*
 * Function: snapshot_pawn_positions
 * Summary: Record every pawn's current board cell and in_play flag into
 *          settle_prev_cell[]/settle_prev_in_play[] -- called immediately
 *          before a state-changing ludo_roll() or ludo_move_pawn() call
 *          so update_settle_diff_area() can later tell exactly which
 *          pawns (if any) that call displaced as a side effect.
 */
static void snapshot_pawn_positions(void)
{
	int player, pawn;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
			settle_prev_in_play[player][pawn] = game.players[player].pawns[pawn].in_play;
			settle_prev_cell[player][pawn] = board_pawn_cell(&game, player, pawn);
		}
	}
}

/*
 * Function: update_settle_diff_area
 * Summary: Compare every pawn's current board cell/in_play state against
 *          the snapshot snapshot_pawn_positions() took just before the
 *          state-changing call, and redraw (via Wimp_UpdateWindow, scoped
 *          tightly -- see update_move_animation_area()'s doc comment for
 *          the general pattern) only the cells that actually changed as
 *          a side effect of that call: a captured pawn sent home, a own-
 *          pawn collision sent home, or a six's mandatory release from
 *          the home base. `skip_player`/`skip_pawn` excludes the one
 *          pawn whose own move animation (see start_move_animation())
 *          already painted its final position on the last tick -- pass
 *          -1/-1 when there is no such pawn (the roll/release path has
 *          no animated mover of its own).
 *
 *          If nothing else changed -- true for the overwhelming majority
 *          of ordinary moves, which involve exactly one pawn -- this
 *          does nothing at all: no board redraw of any kind, since the
 *          per-tick animation already left the board showing the
 *          correct final state, and status/name/Throw-button text is
 *          handled separately by refresh_status()'s own small per-icon
 *          redraws. Added in round 7.15 specifically so the common case
 *          needs no board redraw whatsoever -- per explicit user report
 *          that a full-window redraw on every single turn transition was
 *          itself visibly flickering, independent of whatever it was
 *          redrawing.
 */
static void update_settle_diff_area(int skip_player, int skip_pawn)
{
	wimp_draw redraw;
	osbool more;
	int col0, row0, col1, row1, x0, y0, x1, y1, have_any = 0;
	int player, pawn;

	if (window_handle == (wimp_w) -1)
		return;

	col0 = row0 = BOARD_GRID_SIZE;
	col1 = row1 = -1;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
			board_cell now_cell;
			int now_in_play;

			if (player == skip_player && pawn == skip_pawn)
				continue;

			now_in_play = game.players[player].pawns[pawn].in_play;
			now_cell = board_pawn_cell(&game, player, pawn);

			if (now_in_play == settle_prev_in_play[player][pawn]
			 && now_cell.col == settle_prev_cell[player][pawn].col
			 && now_cell.row == settle_prev_cell[player][pawn].row)
				continue;

			/* Changed -- cover both where it used to be (needs erasing)
			 * and where it is now (needs drawing). */
			if (settle_prev_cell[player][pawn].col < col0) col0 = settle_prev_cell[player][pawn].col;
			if (settle_prev_cell[player][pawn].col > col1) col1 = settle_prev_cell[player][pawn].col;
			if (settle_prev_cell[player][pawn].row < row0) row0 = settle_prev_cell[player][pawn].row;
			if (settle_prev_cell[player][pawn].row > row1) row1 = settle_prev_cell[player][pawn].row;
			if (now_cell.col < col0) col0 = now_cell.col;
			if (now_cell.col > col1) col1 = now_cell.col;
			if (now_cell.row < row0) row0 = now_cell.row;
			if (now_cell.row > row1) row1 = now_cell.row;
			have_any = 1;
		}
	}

	if (!have_any)
		return;

	col0--; if (col0 < 0) col0 = 0;
	row0--; if (row0 < 0) row0 = 0;
	col1++; if (col1 >= BOARD_GRID_SIZE) col1 = BOARD_GRID_SIZE - 1;
	row1++; if (row1 >= BOARD_GRID_SIZE) row1 = BOARD_GRID_SIZE - 1;

	cell_range_to_work_box(col0, row0, col1, row1, &x0, &y0, &x1, &y1);

	redraw.w = window_handle;
	redraw.box.x0 = x0;
	redraw.box.y0 = y0;
	redraw.box.x1 = x1;
	redraw.box.y1 = y1;

	more = wimp_update_window(&redraw);
	while (more) {
		int origin_x = redraw.box.x0 - redraw.xscroll;
		int origin_y = redraw.box.y1 - redraw.yscroll;

		fill_window_background(redraw.box.x0, redraw.box.y0, redraw.box.x1, redraw.box.y1);
		draw_board_region(origin_x, origin_y, col0, row0, col1, row1);
		more = wimp_get_rectangle(&redraw);
	}
}

/*
 * Function: draw_full_window_content
 * Summary: Draw the entire game window's content -- the whole board,
 *          highlight rings, the player-colour swatch, and the die --
 *          for one already-clipped redraw rectangle. Shared by
 *          game_view_redraw() (a genuine Redraw_Window_Request, via
 *          Wimp_RedrawWindow) and redraw_now() (a manually-triggered
 *          full update, via Wimp_UpdateWindow) so both stay in exact
 *          agreement about what the window's content actually is.
 */
static void draw_full_window_content(int origin_x, int origin_y)
{
	draw_board_region(origin_x, origin_y, 0, 0, BOARD_GRID_SIZE - 1, BOARD_GRID_SIZE - 1);
	draw_highlights(origin_x, origin_y);

	/* Player-colour swatch next to the name line -- matches GEOS's own
	 * reference screenshot, which has a small coloured box beside the
	 * player name/status text. Black outline (same "larger shape
	 * behind the fill" technique as plot_pawn()'s pawn outline) per
	 * explicit user request -- yellow in particular read poorly
	 * against the panel's own light background with no border. */
	{
		int player = (game.winner != -1) ? game.winner : game.current_player;
		int x0 = origin_x + SWATCH_X0;
		int y1 = origin_y + SWATCH_Y1;
		/* Mode 15 is 2x4 OS units per physical pixel (non-square --
		 * see CLAUDE.md's Testing section), so a horizontal edge
		 * needs at least 4 OS units of thickness to guarantee even
		 * one physical pixel row; 2 was invisible on the top/bottom
		 * edges specifically (a vertical edge only needs 2, so those
		 * happened to still show) -- per explicit user report and
		 * correctly diagnosed cause ("perhaps as every two OS lines
		 * is one actual screen line?"). plot_pawn()'s own outline
		 * (PAWN_SIZE/12 = 4) already satisfied this by coincidence,
		 * which is why only the swatch showed the problem. */
		int outline = 4;

		set_gcol(0, 0, 0);
		fill_rect(x0 - outline, y1 - SWATCH_SIZE - outline, x0 + SWATCH_SIZE + outline, y1 + outline);
		set_gcol(player_rgb[player][0], player_rgb[player][1], player_rgb[player][2]);
		fill_rect(x0, y1 - SWATCH_SIZE, x0 + SWATCH_SIZE, y1);
	}

	plot_dice(origin_x, origin_y);
}

/*
 * Function: redraw_now
 * Summary: Redraw the whole game window immediately, synchronously --
 *          not via wimp_force_redraw(), which only *schedules* a
 *          Redraw_Window_Request for the next Wimp_Poll and so wouldn't
 *          show anything until this whole function returns. Needed
 *          whenever a known state change (a settled roll, a captured
 *          pawn, a fresh game) must become visible right away rather
 *          than waiting for the next genuine window exposure.
 *
 *          Uses Wimp_UpdateWindow, not Wimp_RedrawWindow (i.e. does
 *          *not* delegate to game_view_redraw(), despite drawing the
 *          exact same content via the same draw_full_window_content())
 *          -- per explicit user report ("after animation is done, there
 *          is still a full redraw, is that needed?"). It wasn't:
 *          Wimp_RedrawWindow's auto-clear-then-repaint is a visible
 *          flash regardless of how small or large the redrawn area is
 *          (see docs/ARCHITECTURE.md's Round 7.10), and this function
 *          runs at the end of essentially every game action, so that
 *          flash was happening constantly. Wimp_UpdateWindow doesn't
 *          clear, but draw_full_window_content() already repaints every
 *          marker/pawn/panel element unconditionally on every call
 *          regardless of what changed, so nothing is lost by skipping
 *          the clear -- the window's own extent is supplied as the
 *          input rectangle (Wimp_UpdateWindow, unlike Wimp_RedrawWindow,
 *          takes it as input, not output). Genuine window exposure
 *          (another window dragged away, first open) is unaffected --
 *          that still goes through main_dispatch()'s
 *          wimp_REDRAW_WINDOW_REQUEST case -> game_view_redraw() ->
 *          Wimp_RedrawWindow, completely unchanged, where the auto-clear
 *          is exactly what's wanted.
 */
static void redraw_now(void)
{
	wimp_draw redraw;
	osbool more;

	if (window_handle == (wimp_w) -1)
		return;

	redraw.w = window_handle;
	redraw.box.x0 = 0;
	redraw.box.y0 = -WINDOW_HEIGHT;
	redraw.box.x1 = WINDOW_WIDTH;
	redraw.box.y1 = 0;

	more = wimp_update_window(&redraw);
	while (more) {
		int origin_x = redraw.box.x0 - redraw.xscroll;
		int origin_y = redraw.box.y1 - redraw.yscroll;

		/* Erase first -- see fill_window_background()'s doc comment.
		 * Missed here originally (a direct regression, not just a
		 * theoretical risk): a captured pawn's *old* ring position is
		 * never revisited by draw_board_region() with real content (a
		 * pawn there is now gone -- board_pawn_cell() returns its new
		 * home-base cell instead), and the thin grey ring-track outline
		 * redrawn at that cell doesn't cover the much larger solid pawn
		 * circle that used to be there -- per explicit user report (a
		 * screenshot showing six red pawns/markers on screen for a
		 * four-pawn player). */
		fill_window_background(redraw.box.x0, redraw.box.y0, redraw.box.x1, redraw.box.y1);
		draw_full_window_content(origin_x, origin_y);
		more = wimp_get_rectangle(&redraw);
	}
}

/*
 * Function: single_movable_pawn
 * Summary: If exactly one bit is set in a ludo_movable_pawns() mask,
 *          return that pawn's index; otherwise (none, or more than one)
 *          return -1. Used so the player is only ever asked to pick a
 *          pawn when there's an actual choice to make -- see
 *          resolve_roll().
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
 * Function: cell_for_steps
 * Summary: The board cell for an arbitrary, explicit "steps travelled"
 *          value -- a read-only variant of board_pawn_cell() that isn't
 *          tied to a pawn's own *current* steps, mirroring its ring/
 *          home-column dispatch logic exactly (see board_layout.c's
 *          board_pawn_cell()/docs/BOARD_LAYOUT.md). Used to build a
 *          move's whole cell-by-cell path (start_move_animation()) and
 *          to preview a single hypothetical destination
 *          (preview_destination()), without mutating any game state.
 */
static board_cell cell_for_steps(int player, int steps)
{
	if (steps < LUDO_RING_LENGTH) {
		int entry = player * (LUDO_RING_LENGTH / LUDO_PLAYERS);

		return board_ring_cell((entry + steps) % LUDO_RING_LENGTH);
	} else {
		int column_index = steps - LUDO_RING_LENGTH;

		if (column_index >= LUDO_HOME_COLUMN_LENGTH)
			column_index = LUDO_HOME_COLUMN_LENGTH - 1;
		return board_home_column_cell(player, column_index);
	}
}

/*
 * Function: preview_destination
 * Summary: Where a pawn would land if moved right now with the current
 *          roll -- a read-only preview (no state change), used for the
 *          hover highlight (see game_view_poll_idle()). Safe to call for
 *          any pawn ludo_movable_pawns() reports movable: that already
 *          guarantees game.last_roll doesn't overshoot the home column.
 */
static board_cell preview_destination(int player, int pawn_index)
{
	const ludo_pawn *p = &game.players[player].pawns[pawn_index];

	return cell_for_steps(player, p->steps + game.last_roll);
}

/*
 * Function: after_settle
 * Summary: Decide the turn_step to leave things in once a roll or a move
 *          has fully resolved (no animation, no pending auto-move) --
 *          shared by resolve_roll() and resolve_move() so "whose turn is
 *          it now" is handled in exactly one place. STEP_AWAIT_CONTINUE
 *          if it's now an AI-controlled player's turn (their next action,
 *          even the very first roll of a fresh turn, waits for a
 *          Continue click -- per explicit user request), otherwise
 *          STEP_IDLE for ordinary human play (or a finished game).
 */
static void after_settle(void)
{
	if (game.winner == -1 && player_is_ai[game.current_player])
		step = STEP_AWAIT_CONTINUE;
	else
		step = STEP_IDLE;
	refresh_status();
	/* No board redraw here (round 7.15) -- unlike the name/status/Throw
	 * text refresh_status() just did, whether the *board* needs
	 * repainting varies by caller (an ordinary move's board content is
	 * already correct from the per-tick animation; a capture/release
	 * needs a small scoped redraw; a brand new/loaded game needs a full
	 * one) so each call site handles that itself -- see
	 * update_settle_diff_area(), resolve_move(), resolve_roll(),
	 * game_view_new_game(), game_view_load_from_path(). */
}

/*
 * Function: start_move_animation
 * Summary: Apply a pawn's move immediately (the rules/board state change
 *          instantly, exactly as before) but hold its on-screen position
 *          at the old cell and animate it sliding along the real board
 *          track, one square per die pip (move_anim_path[], built here
 *          from the pawn's steps count *before* the move via
 *          cell_for_steps()) rather than cutting straight across the
 *          board in one jump -- per explicit user request ("is it
 *          possible to follow the valid spaces track?"). Used for both a
 *          human's clicked/auto-moved pawn and an AI's chosen pawn.
 */
static void start_move_animation(int player, int pawn_index)
{
	int from_steps = game.players[player].pawns[pawn_index].steps;
	int roll = game.last_roll; /* captured before ludo_move_pawn() -- it may
	                             * itself reset last_roll (a six grants
	                             * another roll) before this can log it */
	int to_steps, i;

	/* Snapshot every pawn's position before the move so
	 * update_settle_diff_area() can later tell whether this move also
	 * captured/displaced some *other* pawn (see round 7.15). */
	snapshot_pawn_positions();

	ludo_move_pawn(&game, pawn_index);
	to_steps = game.players[player].pawns[pawn_index].steps;

	/* The single funnel every pawn move passes through (human click,
	 * human auto-move, and every AI move) -- unlike the old MOVED/MOVING
	 * log line in try_move_pawn() (human clicks only), this captures
	 * every move regardless of path, which is exactly what was missing
	 * when investigating the "moved 1 place instead of being rejected"
	 * report (see docs/ARCHITECTURE.md's Round 7.8 -- the actual move
	 * involved never went through a human click at all). */
	debug_log("start_move_animation: player=%d %s pawn=%d roll=%d steps %d -> %d\n",
	          player, player_is_ai[player] ? "AI" : "human", pawn_index,
	          roll, from_steps, to_steps);

	move_anim_path_len = to_steps - from_steps + 1;
	if (move_anim_path_len > MOVE_ANIM_MAX_PATH)
		move_anim_path_len = MOVE_ANIM_MAX_PATH; /* defensive -- a legal roll is never more than 6 */
	for (i = 0; i < move_anim_path_len; i++)
		move_anim_path[i] = cell_for_steps(player, from_steps + i);

	move_anim_player = player;
	move_anim_pawn_index = pawn_index;
	move_anim_tick = 0;
	move_anim_next_tick = os_read_monotonic_time() + MOVE_ANIM_STEP_CS;
	hover_active = 0;
	step = STEP_MOVING;
	update_move_animation_area();
}

/*
 * Function: resolve_move
 * Summary: Called once a pawn-move animation has finished -- the board
 *          state is already correct (start_move_animation() applied it
 *          up front), so this just redraws anything the move displaced
 *          besides the animated pawn itself (a capture, an own-pawn
 *          collision -- see update_settle_diff_area()) and settles the
 *          next turn_step.
 */
static void resolve_move(void)
{
	update_settle_diff_area(move_anim_player, move_anim_pawn_index);
	after_settle();
}

/*
 * Function: resolve_roll
 * Summary: Called once a die-roll animation has finished and the real
 *          result is showing -- runs the same "what happens after this
 *          roll" logic for both human and AI turns: a mandatory six-
 *          release needs nothing further; otherwise, with no legal move
 *          the turn's already been handled internally by ludo_roll()
 *          (see game_logic.h); with exactly one legal move it's played
 *          automatically for a human (per existing "don't ask which
 *          pawn" behaviour) and *always* automatically for an AI (via
 *          ludo_ai_choose_pawn()); with more than one legal move, a
 *          human is left to click a pawn (STEP_IDLE), highlighted per
 *          game_view_redraw()'s movable-pawn ring.
 *
 *          Checks first whether the turn actually *passed* during that
 *          roll: game_logic.h's ludo_roll() doc comment is explicit that
 *          a caller must "keep rolling while ludo_movable_pawns() is 0
 *          and the current_player has not changed" -- three consecutive
 *          failed tries (nobody released, nothing movable) makes
 *          ludo_roll() silently call ludo_end_turn() *internally*,
 *          which advances game.current_player to a genuinely different
 *          player and resets game.last_roll to 0 (a fresh, not-yet-
 *          thrown state for them) before returning. Found via
 *          tests/test_game_logic.c's headless full-game simulation
 *          (see docs/ARCHITECTURE.md's Round 7.8): without this check,
 *          ludo_movable_pawns() gets evaluated against the *new*
 *          player's board with last_roll==0 -- and since adding zero
 *          steps can never overshoot, every one of their in-play pawns
 *          reports as "movable", so this function would go on to
 *          auto-move (or, for a human with more than one choice, invite
 *          a click on) a pawn that player never actually threw a die
 *          for, moving it by zero net steps. If the roller and the
 *          current player no longer match, nothing was actually rolled
 *          for whoever it is now -- settle straight into their own
 *          fresh "click Throw"/"click Continue" state instead.
 */
static void resolve_roll(void)
{
	unsigned movable;

	if (game.current_player != roll_anim_player) {
		debug_log("resolve_roll: turn passed automatically during the roll "
		          "(roller=%d, now current_player=%d) -- nothing to resolve\n",
		          roll_anim_player, game.current_player);
		after_settle();
		return;
	}

	if (game.just_released) {
		/* The mandatory release moved a pawn from its home base onto the
		 * ring entry square, entirely inside ludo_roll() -- no move
		 * animation covers this, so it's the diff redraw's job alone
		 * (round 7.15). No pawn to skip: nothing here already painted
		 * its own final position the way an animated move does. */
		update_settle_diff_area(-1, -1);
		after_settle();
		return;
	}

	movable = ludo_movable_pawns(&game);
	debug_log("resolve_roll: player=%d roll=%d movable_mask=0x%x\n",
	          game.current_player, game.last_roll, movable);
	if (movable == 0) {
		after_settle();
		return;
	}

	if (player_is_ai[game.current_player]) {
		int pawn = ludo_ai_choose_pawn(&game, movable, LUDO_AI_NORMAL);

		start_move_animation(game.current_player, pawn);
		return;
	}

	{
		int auto_pawn = single_movable_pawn(movable);

		if (auto_pawn != -1) {
			start_move_animation(game.current_player, auto_pawn);
			return;
		}
	}

	/* Multiple choices -- a human clicks a pawn next; nothing more
	 * automatic happens this step. */
	step = STEP_IDLE;
	/* Start the movable-pawn ring flash from a fresh, fully-visible
	 * phase rather than whatever phase an unrelated previous flash
	 * cycle happened to be in -- otherwise the ring could flip off
	 * within a fraction of a second of first appearing. */
	highlight_flash_on = 1;
	highlight_next_flash = os_read_monotonic_time() + HIGHLIGHT_FLASH_CS;
	refresh_status();
	/* No pawn moved this step -- only the movable-pawn highlight rings
	 * are new content, so a scoped update covers it exactly (round
	 * 7.15), same as every later flash toggle already uses. */
	update_highlight_area();
}

/*
 * Function: start_roll_animation
 * Summary: Roll the die immediately (the real result is determined right
 *          away, exactly as before) but hold the displayed face on a
 *          cycling cosmetic animation for ROLL_ANIM_TICKS redraws (see
 *          game_view_poll_idle()) before revealing it and calling
 *          resolve_roll() -- per explicit user request ("AI play does
 *          not have any dice throw animation"). Used for both a human's
 *          Throw click and an AI's Continue-triggered roll.
 */
static void start_roll_animation(void)
{
	roll_anim_player = game.current_player;
	/* Snapshot every pawn's position before the roll so
	 * update_settle_diff_area() can later tell whether a six's mandatory
	 * release happened (see round 7.15). */
	snapshot_pawn_positions();
	ludo_roll(&game, 0);
	roll_anim_ticks_done = 0;
	dice_display_face = 1;
	roll_anim_next_tick = os_read_monotonic_time() + ROLL_ANIM_TICK_CS;
	hover_active = 0;
	step = STEP_ROLLING;
	update_dice_area();
}

/*
 * Function: game_view_poll_idle
 * Summary: Called by main.c on every Wimp_Poll Null_Reason_Code (idle)
 *          event. Advances whichever animation is currently running by
 *          comparing the real-time clock against a stored "next tick
 *          due" mark (never blocking/busy-waiting -- unlike the old
 *          pace_delay(), this must let Wimp_Poll keep running so the
 *          Continue button and pointer stay responsive throughout), and
 *          separately polls the pointer position for the hover-
 *          destination highlight (see preview_destination()) at a
 *          coarser interval, since that's cheap but pointless to redo on
 *          literally every idle poll.
 */
void game_view_poll_idle(void)
{
	os_t now;

	if (window_handle == (wimp_w) -1)
		return;

	now = os_read_monotonic_time();

	if (step == STEP_ROLLING) {
		if (now < roll_anim_next_tick)
			return;
		roll_anim_ticks_done++;
		if (roll_anim_ticks_done >= ROLL_ANIM_TICKS) {
			dice_display_face = game.last_roll;
			update_dice_area();
			resolve_roll();
		} else {
			dice_display_face = (dice_display_face % 6) + 1;
			update_dice_area();
			roll_anim_next_tick = now + ROLL_ANIM_TICK_CS;
		}
		return;
	}

	if (step == STEP_MOVING) {
		int total_ticks = (move_anim_path_len - 1) * MOVE_ANIM_TICKS_PER_CELL;

		if (now < move_anim_next_tick)
			return;
		move_anim_tick++;
		if (move_anim_tick >= total_ticks) {
			update_move_animation_area();
			resolve_move();
		} else {
			update_move_animation_area();
			move_anim_next_tick = now + MOVE_ANIM_STEP_CS;
		}
		return;
	}

	/* Hover-destination highlight: only meaningful in exactly the same
	 * "waiting for the human to pick among several pawns" situation the
	 * movable-pawn rings cover (see game_view_redraw()) -- checked here
	 * rather than unconditionally so an idle AI turn, a finished game, or
	 * a mid-animation frame never computes or shows a stale preview. */
	if (step != STEP_IDLE || game.winner != -1 || player_is_ai[game.current_player]
	 || game.last_roll == 0) {
		if (hover_active) {
			hover_active = 0;
			redraw_now();
		}
		return;
	}

	/* Flash the movable-pawn/hover rings on their own steady cadence,
	 * independent of the hover-poll throttle below -- see
	 * update_highlight_area(), which is itself a no-op if nothing is
	 * currently highlighted (matches how ro-chess's own hilite_do()
	 * keeps a single continuously-running flash toggle regardless of
	 * whether anything is currently shown). */
	if (now >= highlight_next_flash) {
		highlight_flash_on = !highlight_flash_on;
		highlight_next_flash = now + HIGHLIGHT_FLASH_CS;
		update_highlight_area();
	}

	if (now < hover_next_poll)
		return;
	hover_next_poll = now + HOVER_POLL_CS;

	{
		unsigned movable = ludo_movable_pawns(&game);
		wimp_pointer pointer;
		int was_active = hover_active;
		board_cell prev_dest = hover_destination;
		int new_active = 0;
		board_cell new_dest = { 0, 0 };

		if (movable != 0) {
			wimp_window_state state;
			int work_x, work_y, col, row, pawn;

			wimp_get_pointer_info(&pointer);
			if (pointer.w == window_handle) {
				state.w = window_handle;
				wimp_get_window_state(&state);
				work_x = pointer.pos.x - state.visible.x0 + state.xscroll;
				work_y = pointer.pos.y - state.visible.y1 + state.yscroll;
				col = (work_x - BOARD_ORIGIN_X) / CELL;
				row = (BOARD_ORIGIN_Y - work_y) / CELL;

				for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
					board_cell cell;

					if (!(movable & (1u << pawn)))
						continue;
					cell = board_pawn_cell(&game, game.current_player, pawn);
					if (cell.col == col && cell.row == row) {
						new_active = 1;
						new_dest = preview_destination(game.current_player, pawn);
						break;
					}
				}
			}
		}

		if (new_active != was_active || (new_active
		 && (new_dest.col != prev_dest.col || new_dest.row != prev_dest.row))) {
			hover_active = new_active;
			hover_destination = new_dest;
			redraw_now();
		}
	}
}

void game_view_new_game(void)
{
	ludo_init(&game);
	game_started = 1;
	step = STEP_IDLE;
	hover_active = 0;
	dice_display_face = 0;
	/* after_settle() itself calls refresh_status() -- see its doc comment.
	 * If the very first player is AI-controlled, their first action
	 * still waits for a Continue click. A brand new game is a genuinely
	 * full board reset (round 7.15's scoped diff redraw has no "before"
	 * state to compare against here), so this is one of the few places
	 * that still needs an explicit, unscoped redraw_now(). */
	after_settle();
	redraw_now();
}

int game_view_has_started(void)
{
	return game_started;
}

const char *game_view_app_dir(void)
{
	return app_dir;
}

/*
 * Save/load
 * =========
 *
 * A fixed-layout binary snapshot of everything needed to resume a game
 * exactly where it left off: each player's configured name and human/AI
 * setting (see game_view_configure_players()), plus every field of the
 * `ludo_game` struct itself (game_logic.h). Deliberately NOT a raw
 * `fwrite(&game, ...)` struct dump -- compiler struct padding isn't part
 * of any documented contract, so an explicit byte-by-byte layout (via
 * serialize_game()/deserialize_game()) is used instead, the same way
 * network/file formats normally are. See src/save_view.c for the
 * Save/Load dialogue windows and the drag-and-drop protocol built on top
 * of these functions -- per explicit user request for GEOS-parity
 * save/load, and docs/ARCHITECTURE.md's Round 7.1 notes on why drag-and-
 * drop rather than GEOS's own file picker.
 */
#define SAVE_FILE_SIZE GAME_VIEW_SAVE_FILE_SIZE

/*
 * Function: serialize_game
 * Summary: Pack the current game (players' names/AI flags, and every
 *          field of `game`) into a fixed-size byte buffer -- see the
 *          "Save/load" block comment above for the layout and why it's
 *          explicit rather than a raw struct dump.
 * Syntax:  static void serialize_game(unsigned char *buf);
 * Input:   buf - at least SAVE_FILE_SIZE bytes.
 * Output:  none. buf is filled with exactly SAVE_FILE_SIZE bytes.
 */
static void serialize_game(unsigned char *buf)
{
	int i = 0, player, pawn;

	buf[i++] = 'A'; buf[i++] = 'L'; buf[i++] = 'S'; buf[i++] = '1';

	for (player = 0; player < LUDO_PLAYERS; player++) {
		memcpy(&buf[i], configured_name[player], GAME_VIEW_NAME_LEN);
		i += GAME_VIEW_NAME_LEN;
		buf[i++] = (unsigned char) player_is_ai[player];
	}

	buf[i++] = (unsigned char) game.current_player;
	buf[i++] = (unsigned char) game.last_roll;
	buf[i++] = (unsigned char) game.tries_remaining;
	buf[i++] = (unsigned char) game.forced_pawn;
	buf[i++] = (unsigned char) game.pending_forced_pawn;
	buf[i++] = (unsigned char) game.just_released;
	buf[i++] = (unsigned char) game.winner;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
			buf[i++] = (unsigned char) game.players[player].pawns[pawn].in_play;
			buf[i++] = (unsigned char) game.players[player].pawns[pawn].finished;
			buf[i++] = (unsigned char) game.players[player].pawns[pawn].steps;
		}
	}
}

/*
 * Function: deserialize_game
 * Summary: Reverse of serialize_game() -- restores `configured_name`,
 *          `player_is_ai`, and `game` from a buffer produced by it.
 *          Caller must have already checked the 4-byte magic/version
 *          (see game_view_load_from_path()) before calling this.
 * Syntax:  static void deserialize_game(const unsigned char *buf);
 * Input:   buf - SAVE_FILE_SIZE bytes, magic already verified.
 * Output:  none.
 */
static void deserialize_game(const unsigned char *buf)
{
	int i = 4, player, pawn;

	for (player = 0; player < LUDO_PLAYERS; player++) {
		memcpy(configured_name[player], &buf[i], GAME_VIEW_NAME_LEN);
		configured_name[player][GAME_VIEW_NAME_LEN - 1] = '\0';
		i += GAME_VIEW_NAME_LEN;
		player_is_ai[player] = buf[i++];
	}

	game.current_player = buf[i++];
	game.last_roll = buf[i++];
	game.tries_remaining = buf[i++];
	game.forced_pawn = (signed char) buf[i++];
	game.pending_forced_pawn = (signed char) buf[i++];
	game.just_released = buf[i++];
	game.winner = (signed char) buf[i++];

	for (player = 0; player < LUDO_PLAYERS; player++) {
		for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
			game.players[player].pawns[pawn].in_play = buf[i++];
			game.players[player].pawns[pawn].finished = buf[i++];
			game.players[player].pawns[pawn].steps = buf[i++];
		}
	}
}

int game_view_save_to_path(const char *path)
{
	unsigned char buf[SAVE_FILE_SIZE];
	FILE *f;
	size_t written;

	serialize_game(buf);

	f = fopen(path, "wb");
	if (f == NULL) {
		debug_log("game_view_save_to_path: fopen failed for \"%s\"\n", path);
		return 0;
	}
	written = fwrite(buf, 1, SAVE_FILE_SIZE, f);
	fclose(f);

	if (written != SAVE_FILE_SIZE) {
		debug_log("game_view_save_to_path: short write to \"%s\" (%lu/%d bytes)\n",
		          path, (unsigned long) written, SAVE_FILE_SIZE);
		return 0;
	}
	return 1;
}

int game_view_load_from_path(const char *path)
{
	unsigned char buf[SAVE_FILE_SIZE];
	FILE *f;
	size_t got;

	f = fopen(path, "rb");
	if (f == NULL) {
		debug_log("game_view_load_from_path: fopen failed for \"%s\"\n", path);
		return 0;
	}
	got = fread(buf, 1, SAVE_FILE_SIZE, f);
	fclose(f);

	if (got != SAVE_FILE_SIZE || buf[0] != 'A' || buf[1] != 'L' || buf[2] != 'S' || buf[3] != '1') {
		debug_log("game_view_load_from_path: \"%s\" is not a valid ArchiLudo save "
		          "(%lu bytes read, expected %d)\n", path, (unsigned long) got, SAVE_FILE_SIZE);
		return 0;
	}

	deserialize_game(buf);
	game_started = 1;
	hover_active = 0;
	/* Show whatever die face the save was mid-turn on, if any, rather
	 * than a blank die until the next throw. */
	dice_display_face = game.last_roll;
	/* after_settle() sets the correct turn_step (STEP_AWAIT_CONTINUE if
	 * the loaded game's current player is AI-controlled, matching
	 * game_view_new_game()'s own "every AI action waits for Continue"
	 * rule) and refreshes the status text. A loaded save is a wholesale
	 * board replacement (round 7.15's scoped diff redraw has no "before"
	 * state to compare against here), so an explicit, unscoped
	 * redraw_now() is still needed, same as game_view_new_game(). */
	after_settle();
	redraw_now();
	return 1;
}

void game_view_initialise(const char *argv0)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	wimp_icon *icon;

	set_app_dir(argv0);
	build_cell_kinds();
	ludo_init(&game);
	load_pawn_sprites();

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



void game_view_redraw(wimp_draw *redraw)
{
	osbool more;

	more = wimp_redraw_window(redraw);
	while (more) {
		int origin_x = redraw->box.x0 - redraw->xscroll;
		int origin_y = redraw->box.y1 - redraw->yscroll;

		draw_full_window_content(origin_x, origin_y);
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
	unsigned movable;
	int pawn;

	/* Only the human player actually clicks pawns: not during an
	 * animation, an AI's turn (it always picks its own pawn via
	 * resolve_roll()), or before any roll this turn. */
	if (step != STEP_IDLE || game.winner != -1 || player_is_ai[game.current_player]
	 || game.last_roll == 0)
		return;

	movable = ludo_movable_pawns(&game);
	debug_log("try_move_pawn: click at (%d,%d) player=%d movable_mask=0x%x\n",
	          col, row, game.current_player, movable);

	for (pawn = 0; pawn < LUDO_PAWNS; pawn++) {
		board_cell cell;

		if (!(movable & (1u << pawn)))
			continue;
		cell = board_pawn_cell(&game, game.current_player, pawn);
		debug_log("  candidate pawn %d at (%d,%d)\n", pawn, cell.col, cell.row);
		if (cell.col == col && cell.row == row) {
			debug_log("  MOVING pawn %d\n", pawn);
			start_move_animation(game.current_player, pawn);
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
		/* Ignore extra clicks while an animation is already running, or
		 * outside the two situations this one button actually means
		 * something in (a human's own "Throw", or an AI turn paused on
		 * "Continue" -- see refresh_status()). */
		if (step != STEP_IDLE && step != STEP_AWAIT_CONTINUE)
			return;
		if (step == STEP_IDLE && game.winner == -1 && player_is_ai[game.current_player])
			return;

		flash_throw_button();

		if (game.winner != -1) {
			game_view_new_game();
		} else {
			start_roll_animation();
		}
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
