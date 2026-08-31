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
#include "ai.h"
#include "save_view.h"
#include "rules_view.h"

#define MARGIN        8
#define ROW_HEIGHT   40
#define ROW_GAP       8
#define SWATCH_SIZE  32
#define NAME_WIDTH  180
#define TYPE_WIDTH  100
#define DIFFICULTY_WIDTH 130
#define COL_GAP       8
#define BUTTON_WIDTH 110
#define BUTTON_HEIGHT 40
#define BUTTON_GAP   16

#define ROWS_HEIGHT (LUDO_PLAYERS * ROW_HEIGHT + (LUDO_PLAYERS - 1) * ROW_GAP)
#define ROWS_WIDTH (MARGIN + SWATCH_SIZE + COL_GAP + NAME_WIDTH + COL_GAP + TYPE_WIDTH \
                    + COL_GAP + DIFFICULTY_WIDTH + MARGIN)
/* Four buttons: Start/Rules/Load/Cancel -- widen the window if that row
 * would otherwise be wider than the name/type rows above it. */
#define BUTTONS_WIDTH (MARGIN + 4 * BUTTON_WIDTH + 3 * BUTTON_GAP + MARGIN)
#define WINDOW_WIDTH (ROWS_WIDTH > BUTTONS_WIDTH ? ROWS_WIDTH : BUTTONS_WIDTH)
#define WINDOW_HEIGHT (MARGIN + ROWS_HEIGHT + MARGIN + BUTTON_HEIGHT + MARGIN)

#define SWATCH_X0 MARGIN
#define NAME_X0   (SWATCH_X0 + SWATCH_SIZE + COL_GAP)
#define TYPE_X0   (NAME_X0 + NAME_WIDTH + COL_GAP)
#define DIFFICULTY_X0 (TYPE_X0 + TYPE_WIDTH + COL_GAP)

#define ROW_Y1(row) (-(MARGIN + (row) * (ROW_HEIGHT + ROW_GAP)))
#define ROW_Y0(row) (ROW_Y1(row) - ROW_HEIGHT)

#define BUTTON_ROW_Y1 (-(MARGIN + ROWS_HEIGHT + MARGIN))
#define BUTTON_ROW_Y0 (BUTTON_ROW_Y1 - BUTTON_HEIGHT)
#define START_X0  MARGIN
#define RULES_X0  (START_X0 + BUTTON_WIDTH + BUTTON_GAP)
#define LOAD_X0   (RULES_X0 + BUTTON_WIDTH + BUTTON_GAP)
#define CANCEL_X0 (LOAD_X0 + BUTTON_WIDTH + BUTTON_GAP)

/* One swatch + name + type + difficulty icon per player, then Start,
 * Rules, Load and Cancel. */
#define ICON_SWATCH(player)     ((player) * 4)
#define ICON_NAME(player)       ((player) * 4 + 1)
#define ICON_TYPE(player)       ((player) * 4 + 2)
#define ICON_DIFFICULTY(player) ((player) * 4 + 3)
#define ICON_START  (LUDO_PLAYERS * 4)
#define ICON_RULES  (LUDO_PLAYERS * 4 + 1)
#define ICON_LOAD   (LUDO_PLAYERS * 4 + 2)
#define ICON_CANCEL (LUDO_PLAYERS * 4 + 3)
#define WINDOW_ICON_COUNT (LUDO_PLAYERS * 4 + 4)

/* Standard 16-colour Wimp desktop palette approximations of this
 * project's actual (full-RGB) player colours -- plain Wimp icons can
 * only use this fixed palette for their fill colour, unlike the custom
 * os_plot/colourtrans drawing game_view.c's board uses. Must stay in the
 * same green/red/blue/yellow order as game_view.c's player_rgb and
 * assets/generate_icon_sprites.py's PLAYER_WIMP_COLOUR. */
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
static char difficulty_text[LUDO_PLAYERS][8]; /* "Low"/"Medium"/"High", plus terminator */
static ludo_ai_difficulty player_difficulty[LUDO_PLAYERS];
/* Tracks whether ICON_DIFFICULTY(player) is currently shaded, so
 * set_type() only issues a wimp_set_icon_state() EOR toggle when the
 * shaded state actually needs to change (same "only flip when the
 * desired state differs from what's tracked" bookkeeping
 * game_view.c's flash_throw_button() uses for its own icon). */
static int difficulty_shaded[LUDO_PLAYERS];
static char start_validation[4] = "R1";
static char rules_validation[4] = "R1";
static char load_validation[4] = "R1";
static char cancel_validation[4] = "R1";

static const char *difficulty_label[3] = { "Low", "Medium", "High" };

/* Pending rules for the next Start click -- see setup_view_configure_rules()
 * (called by src/rules_view.c's OK button) and game_view_configure_rules()
 * (applied to the actual game when Start is clicked). Defaults to
 * LUDO_VARIANT_MEJN until the Rules dialogue is ever touched. */
static ludo_rules pending_rules;

static wimp_w window_handle = (wimp_w) -1;

/*
 * Function: set_difficulty
 * Summary: Set one player's AI difficulty (both the tracked value and
 *          its displayed text) and, if the window already exists, ask
 *          the Wimp to redraw that one icon. Meaningful only while that
 *          player is AI-controlled -- see set_type()'s own shading of
 *          ICON_DIFFICULTY(player) -- but always tracked regardless, so
 *          a player's difficulty choice survives toggling Human/AI back
 *          and forth.
 */
static void set_difficulty(int player, ludo_ai_difficulty difficulty)
{
	player_difficulty[player] = difficulty;
	strcpy(difficulty_text[player], difficulty_label[difficulty]);

	if (window_handle != (wimp_w) -1)
		wimp_set_icon_state(window_handle, ICON_DIFFICULTY(player), 0, 0);
}

/*
 * Function: set_type
 * Summary: Set one player's Human/AI toggle state (both the tracked flag
 *          and its displayed text) and, if the window already exists,
 *          ask the Wimp to redraw that one icon. Also shades
 *          ICON_DIFFICULTY(player) while the player is Human -- the
 *          difficulty choice only matters for an AI-controlled seat.
 */
static void set_type(int player, int is_ai)
{
	type_is_ai[player] = is_ai;
	/* type_text[] is sized to exactly fit "Human\0", the longer of the
	 * two -- a plain strcpy() is safe (and, unlike strncpy() for a
	 * same-length copy, doesn't trip a "may not null-terminate" warning). */
	strcpy(type_text[player], is_ai ? "AI" : "Human");

	if (window_handle != (wimp_w) -1) {
		wimp_set_icon_state(window_handle, ICON_TYPE(player), 0, 0);

		if (difficulty_shaded[player] != !is_ai) {
			wimp_set_icon_state(window_handle, ICON_DIFFICULTY(player),
			                     wimp_ICON_SHADED, 0);
			difficulty_shaded[player] = !is_ai;
		}
	}
}

void setup_view_initialise(void)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	int player;

	pending_rules = ludo_default_rules(LUDO_VARIANT_MEJN);

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
	 * behaviour (BUTTON_NEVER) is correct here. game_view.c's own window
	 * needs BUTTON_CLICK instead, since it must detect clicks on its
	 * custom-plotted board background, which this window has none of --
	 * see docs/ARCHITECTURE.md's "WIMP conventions and gotchas" section. */
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
		player_difficulty[player] = LUDO_AI_NORMAL;
		strcpy(difficulty_text[player], difficulty_label[LUDO_AI_NORMAL]);
		/* Shaded from the start -- every player begins Human (above). */
		difficulty_shaded[player] = 1;

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

		icon = &def.icons[ICON_DIFFICULTY(player)];
		icon->extent.x0 = DIFFICULTY_X0;
		icon->extent.x1 = DIFFICULTY_X0 + DIFFICULTY_WIDTH;
		icon->extent.y1 = ROW_Y1(player);
		icon->extent.y0 = ROW_Y0(player);
		/* Click-to-cycle Low/Medium/High, same technique as ICON_TYPE(player)
		 * above -- shaded (wimp_ICON_SHADED) whenever this player is Human,
		 * since difficulty only applies to an AI-controlled seat; set_type()
		 * keeps that shading in sync as the player's Human/AI toggle changes. */
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              wimp_ICON_SHADED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = difficulty_text[player];
		icon->data.indirected_text.validation = "R1";
		icon->data.indirected_text.size = sizeof(difficulty_text[player]);
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

		icon = &def.icons[ICON_RULES];
		icon->extent.x0 = RULES_X0;
		icon->extent.x1 = RULES_X0 + BUTTON_WIDTH;
		icon->extent.y1 = BUTTON_ROW_Y1;
		icon->extent.y0 = BUTTON_ROW_Y0;
		/* Opens src/rules_view.c's "Rule Options" dialogue, seeded with
		 * this dialogue's own pending_rules -- see setup_view_click(). */
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = "Rules";
		icon->data.indirected_text.validation = rules_validation;
		icon->data.indirected_text.size = 6;

		icon = &def.icons[ICON_LOAD];
		icon->extent.x0 = LOAD_X0;
		icon->extent.x1 = LOAD_X0 + BUTTON_WIDTH;
		icon->extent.y1 = BUTTON_ROW_Y1;
		icon->extent.y0 = BUTTON_ROW_Y0;
		/* Skips this dialogue's own player setup entirely -- per explicit
		 * user request ("the new game dialogue needs a button to
		 * optionally load a previously saved game"), opens
		 * src/save_view.c's Load dialogue instead (which restores its own
		 * player names/AI settings from the save file, see
		 * game_view_load_from_path()). */
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = "Load";
		icon->data.indirected_text.validation = load_validation;
		icon->data.indirected_text.size = 5;

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

	/* Sync from the actual in-progress (or just-finished) game's own
	 * live player configuration every time this dialogue is opened, so
	 * its defaults always reflect the current game rather than
	 * whichever hardcoded/stale values were last left in these icons'
	 * indirected buffers. Nothing to sync from the very first time this
	 * window is opened (game_view_has_started() is still 0), in which
	 * case setup_view_initialise()'s own hardcoded defaults stand. */
	if (game_view_has_started()) {
		char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN];
		int is_ai[LUDO_PLAYERS];
		ludo_ai_difficulty difficulty[LUDO_PLAYERS];
		int player;

		game_view_get_players(names, is_ai, difficulty);
		for (player = 0; player < LUDO_PLAYERS; player++) {
			strncpy(name_buffer[player], names[player], GAME_VIEW_NAME_LEN - 1);
			name_buffer[player][GAME_VIEW_NAME_LEN - 1] = '\0';
			set_type(player, is_ai[player]);
			set_difficulty(player, difficulty[player]);
		}

		/* Same "always default to the in-progress game" convention,
		 * for rules too -- see game_view_get_rules(). */
		game_view_get_rules(&pending_rules);
	}

	state.w = window_handle;
	wimp_get_window_state(&state);
	state.next = wimp_TOP;
	wimp_open_window((wimp_open *) &state);

	/* Force-redraw every row now, not just rely on the Wimp's own next
	 * exposure -- the sync above can change indirected icon text while
	 * the window is already/about to be visible (set_type() already
	 * redraws its own icon; the name fields need the same here, since
	 * they're updated directly rather than through a setter). */
	{
		int player;

		for (player = 0; player < LUDO_PLAYERS; player++)
			wimp_set_icon_state(window_handle, ICON_NAME(player), 0, 0);
	}

	/* Caret in the first name field, positioned at the end of its
	 * existing text -- "when moving to a new writable icon, place the
	 * caret at the end of the existing text" (RISC OS 3 PRM's Wimp
	 * chapter). height=-1 asks the Wimp to use the icon's own natural
	 * caret height rather than specifying one explicitly. */
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
		if (pointer->i == ICON_DIFFICULTY(player)) {
			/* Shaded (and so, by Wimp convention, not clickable) whenever
			 * this player is Human -- guarded explicitly anyway rather
			 * than relying solely on that, since it costs nothing. */
			if (type_is_ai[player])
				set_difficulty(player, (player_difficulty[player] + 1) % 3);
			return;
		}
	}

	if (pointer->i == ICON_CANCEL) {
		wimp_close_window(window_handle);
		return;
	}

	if (pointer->i == ICON_RULES) {
		/* Deliberately does NOT close this window -- the Rules dialogue
		 * is meant to sit alongside New Game (its OK button calls
		 * setup_view_configure_rules() and closes only itself), matching
		 * how a real "sub-dialogue" is expected to layer, unlike Load
		 * (which fully replaces this dialogue with a different flow). */
		rules_view_open(&pending_rules);
		return;
	}

	if (pointer->i == ICON_LOAD) {
		wimp_close_window(window_handle);
		load_view_open();
		return;
	}

	if (pointer->i == ICON_START) {
		char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN];

		for (player = 0; player < LUDO_PLAYERS; player++) {
			strncpy(names[player], name_buffer[player], GAME_VIEW_NAME_LEN - 1);
			names[player][GAME_VIEW_NAME_LEN - 1] = '\0';
		}
		game_view_configure_players(names, type_is_ai, player_difficulty);
		game_view_configure_rules(&pending_rules);

		wimp_close_window(window_handle);
		game_view_open();
		game_view_new_game();
		return;
	}
}

void setup_view_configure_rules(const ludo_rules *rules)
{
	pending_rules = *rules;
}

void setup_view_key_pressed(wimp_key *key)
{
	wimp_process_key(key->c);
}
