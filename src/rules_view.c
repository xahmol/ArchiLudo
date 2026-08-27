/*
 * ArchiLudo rules view -- implementation.
 * See include/rules_view.h for the module overview and API docs.
 */

#include <string.h>

#include "oslib/wimp.h"
#include "oslib/wimpspriteop.h"

#include "rules_view.h"
#include "setup_view.h"

#define MARGIN         12
#define LABEL_WIDTH   150
#define OPTION_WIDTH  110
#define OPTION_GAP      6
#define VALUE_WIDTH   (OPTION_WIDTH * 2 + OPTION_GAP)
#define ROW_HEIGHT     32
#define ROW_GAP         6
#define BUTTON_WIDTH  120
#define BUTTON_HEIGHT  40
#define BUTTON_GAP     16

#define TOGGLE_COUNT 7

/* Row 0 is the variant picker, row 1 a static Pachisi-authenticity
 * caveat, rows 2..8 the 7 house-rule toggles. */
#define ROW_VARIANT   0
#define ROW_CAVEAT    1
#define ROW_TOGGLE(t) (2 + (t))
#define GRID_ROWS     (2 + TOGGLE_COUNT)

#define ROWS_WIDTH (MARGIN + LABEL_WIDTH + OPTION_GAP + OPTION_WIDTH + OPTION_GAP + OPTION_WIDTH + MARGIN)
#define ROWS_HEIGHT (GRID_ROWS * ROW_HEIGHT + (GRID_ROWS - 1) * ROW_GAP)
#define BUTTONS_WIDTH (MARGIN + 2 * BUTTON_WIDTH + BUTTON_GAP + MARGIN)
#define WINDOW_WIDTH (ROWS_WIDTH > BUTTONS_WIDTH ? ROWS_WIDTH : BUTTONS_WIDTH)
#define WINDOW_HEIGHT (MARGIN + ROWS_HEIGHT + MARGIN + BUTTON_HEIGHT + MARGIN)

#define LABEL_X0 MARGIN
#define OPT_A_X0 (LABEL_X0 + LABEL_WIDTH + OPTION_GAP)
#define OPT_B_X0 (OPT_A_X0 + OPTION_WIDTH + OPTION_GAP)

#define ROW_Y1(row) (-(MARGIN + (row) * (ROW_HEIGHT + ROW_GAP)))
#define ROW_Y0(row) (ROW_Y1(row) - ROW_HEIGHT)

#define BUTTON_ROW_Y1 (-(MARGIN + ROWS_HEIGHT + MARGIN))
#define BUTTON_ROW_Y0 (BUTTON_ROW_Y1 - BUTTON_HEIGHT)
#define OK_X0     MARGIN
#define CANCEL_X0 (OK_X0 + BUTTON_WIDTH + BUTTON_GAP)

/* One label + two option icons per toggle, plus the variant label/value
 * pair, the caveat, and OK/Cancel. */
#define ICON_VARIANT_LABEL 0
#define ICON_VARIANT_VALUE 1
#define ICON_CAVEAT         2
#define TOGGLE_BASE         3
#define ICON_TOGGLE_LABEL(t) (TOGGLE_BASE + (t) * 3)
#define ICON_TOGGLE_OPT_A(t)  (TOGGLE_BASE + (t) * 3 + 1)
#define ICON_TOGGLE_OPT_B(t)  (TOGGLE_BASE + (t) * 3 + 2)
#define ICON_OK     (TOGGLE_BASE + TOGGLE_COUNT * 3)
#define ICON_CANCEL (ICON_OK + 1)
#define WINDOW_ICON_COUNT (ICON_CANCEL + 1)

/*
 * Type: toggle_info (internal)
 * Summary: The static display text and underlying ludo_rules values for
 *          one house-rule toggle's pair of option icons -- see TOGGLES[]
 *          below and rule_field(), which together let every toggle be
 *          handled by the same generic code instead of 7 near-identical
 *          blocks.
 */
typedef struct {
	const char *label;
	const char *opt_a_text;
	int opt_a_value;
	const char *opt_b_text;
	int opt_b_value;
} toggle_info;

/* Labels/option text kept to <= 11 characters (+ terminator) throughout
 * -- these are plain, non-indirected icons (never change at runtime),
 * and a non-indirected icon's inline text buffer is a fixed 12 bytes
 * (see src/main.c's set_menu_entry() for the same limit on menu items).
 * Some names are intentionally abbreviated from game_logic.h's own
 * field names for this reason (e.g. "Own capture" for own_pawn_capture,
 * "Last pawn" + "Needs 6"/"Any roll" instead of literally spelling out
 * "no_six_needed_last_pawn"'s double-negative). */
static const toggle_info TOGGLES[TOGGLE_COUNT] = {
	{ "Six-release", "Mandatory", 1, "Optional", 0 },
	{ "Own capture",  "On",        1, "Off",      0 },
	{ "Overshoot",    "Blocked",   0, "Bounce",   1 },
	{ "Blockade",     "Off",       0, "On",       1 },
	{ "Backward",     "Off",       0, "On",       1 },
	{ "Free home",    "Off",       0, "On",       1 },
	{ "Last pawn",    "Needs 6",   0, "Any roll", 1 },
};

/* Which toggles are hidden (shaded) for each variant -- bit t set means
 * toggle t is inapplicable and shaded for that variant. Indexed by
 * ludo_variant (LUDO_VARIANT_MEJN=0, _LUDO=1, _PACHISI=2). Matches the
 * approved multi-rule-set plan's own applicability matrix; see
 * docs/GAME_LOGIC.md's "Rule-set variants" section for the same table
 * in prose. Toggle indices, matching TOGGLES[] above: 0=six-release,
 * 1=own capture, 2=overshoot, 3=blockade, 4=backward, 5=free home,
 * 6=last pawn. */
static const unsigned char VARIANT_HIDDEN_MASK[3] = {
	(1u << 3) | (1u << 4) | (1u << 5), /* MEJN: blockade/backward/free-home hidden */
	(1u << 4) | (1u << 5),             /* Ludo: backward/free-home hidden */
	(1u << 0),                          /* Pachisi-style: six-release hidden */
};

/* Kept short to fit a plain (non-indirected) menu entry's own 12-byte
 * inline text buffer (see src/main.c's set_menu_entry()) -- the fuller
 * "Pachisi-style" name lives in this dialogue's own caveat text and in
 * docs/GAME_LOGIC.md instead. */
static const char *VARIANT_NAMES[3] = { "MEJN", "Ludo", "Pachisi" };

static wimp_w window_handle = (wimp_w) -1;
static ludo_rules pending;
static char variant_text[16];
static char caveat_text[64] = "Note: Pachisi-style is a curated preset, not authentic Pachisi.";
static char ok_validation[4] = "R1";
static char cancel_validation[4] = "R1";

static int menu_open = 0;
static wimp_MENU(3) variant_menu;

/*
 * Function: rule_field (internal)
 * Summary: Address of the ludo_rules field a given toggle index
 *          controls -- lets every toggle be read/written by the same
 *          generic code (see TOGGLES[] above for the matching index
 *          order) instead of a 7-way repeated switch scattered through
 *          this file.
 */
static int *rule_field(ludo_rules *r, int toggle)
{
	switch (toggle) {
	case 0: return &r->mandatory_six_release;
	case 1: return &r->own_pawn_capture;
	case 2: return &r->overshoot_bounce;
	case 3: return &r->blockade;
	case 4: return &r->backward_movement;
	case 5: return &r->free_home_column;
	default: return &r->no_six_needed_last_pawn;
	}
}

/*
 * Function: set_icon_selected (internal)
 * Summary: Set one icon's SELECTED flag to exactly `selected`, only
 *          issuing a Wimp_SetIconState call if that's an actual change --
 *          same "read current state, EOR only if it differs" pattern as
 *          this project's own established wimp_ICON_SHADED convention
 *          (see riscos_wimp_reference.md's Icons section).
 */
static void set_icon_selected(int icon, int selected)
{
	wimp_icon_state state;

	state.w = window_handle;
	state.i = icon;
	wimp_get_icon_state(&state);

	if (((state.icon.flags & wimp_ICON_SELECTED) != 0) != (selected != 0))
		wimp_set_icon_state(window_handle, icon, wimp_ICON_SELECTED, 0);
}

/*
 * Function: set_icon_shaded (internal)
 * Summary: Same as set_icon_selected(), for the SHADED flag -- used to
 *          grey out (and disable clicking on) a toggle's label and both
 *          option icons when it doesn't apply to the currently selected
 *          variant (see VARIANT_HIDDEN_MASK).
 */
static void set_icon_shaded(int icon, int shaded)
{
	wimp_icon_state state;

	state.w = window_handle;
	state.i = icon;
	wimp_get_icon_state(&state);

	if (((state.icon.flags & wimp_ICON_SHADED) != 0) != (shaded != 0))
		wimp_set_icon_state(window_handle, icon, wimp_ICON_SHADED, 0);
}

/*
 * Function: refresh_toggle_display (internal)
 * Summary: Sync one toggle's two option icons' SELECTED state to the
 *          current `pending` value, and its label/both option icons'
 *          SHADED state to whether it applies to `pending.variant`.
 */
static void refresh_toggle_display(int t)
{
	int current = *rule_field(&pending, t);
	int hidden = (VARIANT_HIDDEN_MASK[pending.variant] >> t) & 1u;

	set_icon_selected(ICON_TOGGLE_OPT_A(t), current == TOGGLES[t].opt_a_value);
	set_icon_selected(ICON_TOGGLE_OPT_B(t), current == TOGGLES[t].opt_b_value);

	set_icon_shaded(ICON_TOGGLE_LABEL(t), hidden);
	set_icon_shaded(ICON_TOGGLE_OPT_A(t), hidden);
	set_icon_shaded(ICON_TOGGLE_OPT_B(t), hidden);
}

static void refresh_all_toggle_displays(void)
{
	int t;

	for (t = 0; t < TOGGLE_COUNT; t++)
		refresh_toggle_display(t);
}

/*
 * Function: refresh_variant_display (internal)
 * Summary: Update the variant value icon's indirected text to match
 *          pending.variant and ask the Wimp to redraw it.
 */
static void refresh_variant_display(void)
{
	strncpy(variant_text, VARIANT_NAMES[pending.variant], sizeof(variant_text) - 1);
	variant_text[sizeof(variant_text) - 1] = '\0';
	wimp_set_icon_state(window_handle, ICON_VARIANT_VALUE, 0, 0);
}

void rules_view_initialise(void)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	wimp_icon *icon;
	int t;

	pending = ludo_default_rules(LUDO_VARIANT_MEJN);

	def.visible.x0 = 150;
	def.visible.y0 = 100;
	def.visible.x1 = 150 + WINDOW_WIDTH;
	def.visible.y1 = 100 + WINDOW_HEIGHT;
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
	 * setup_view.c/win_view.c. */
	def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	def.sprite_area = wimpspriteop_AREA;
	def.xmin = WINDOW_WIDTH;
	def.ymin = WINDOW_HEIGHT;
	strncpy(def.title_data.text, "Rule Setup", 12);
	def.icon_count = WINDOW_ICON_COUNT;

	icon = &def.icons[ICON_VARIANT_LABEL];
	icon->extent.x0 = LABEL_X0;
	icon->extent.x1 = LABEL_X0 + LABEL_WIDTH;
	icon->extent.y1 = ROW_Y1(ROW_VARIANT);
	icon->extent.y0 = ROW_Y0(ROW_VARIANT);
	icon->flags = wimp_ICON_TEXT | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	strncpy(icon->data.text, "Variant:", 12);

	icon = &def.icons[ICON_VARIANT_VALUE];
	icon->extent.x0 = OPT_A_X0;
	icon->extent.x1 = OPT_A_X0 + VALUE_WIDTH;
	icon->extent.y1 = ROW_Y1(ROW_VARIANT);
	icon->extent.y0 = ROW_Y0(ROW_VARIANT);
	/* Click opens a 3-item pop-up menu (rules_view_click()) -- the same
	 * non-writable-indirected-text-plus-click-opens-a-wimp_menu pattern
	 * src/main.c's own iconbar/window menu already establishes, just
	 * triggered by clicking this specific icon rather than a MENU-button
	 * click on the window background. */
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	variant_text[0] = '\0';
	icon->data.indirected_text.text = variant_text;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = sizeof(variant_text);

	icon = &def.icons[ICON_CAVEAT];
	icon->extent.x0 = LABEL_X0;
	icon->extent.x1 = OPT_B_X0 + OPTION_WIDTH;
	icon->extent.y1 = ROW_Y1(ROW_CAVEAT);
	icon->extent.y0 = ROW_Y0(ROW_CAVEAT);
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_MID_LIGHT_GREY << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = caveat_text;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = sizeof(caveat_text);

	for (t = 0; t < TOGGLE_COUNT; t++) {
		unsigned esg = (unsigned) (t + 1) << wimp_ICON_ESG_SHIFT; /* ESG 0 has different semantics (see PRM) -- start numbering at 1 */

		icon = &def.icons[ICON_TOGGLE_LABEL(t)];
		icon->extent.x0 = LABEL_X0;
		icon->extent.x1 = LABEL_X0 + LABEL_WIDTH;
		icon->extent.y1 = ROW_Y1(ROW_TOGGLE(t));
		icon->extent.y0 = ROW_Y0(ROW_TOGGLE(t));
		icon->flags = wimp_ICON_TEXT | wimp_ICON_VCENTRED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
		strncpy(icon->data.text, TOGGLES[t].label, 12);

		/* Paired radio-style option icons: button type 11 (auto-selects,
		 * no double-click semantics -- see riscos_wimp_reference.md's
		 * Icons section) with a shared non-zero ESG forces mutual
		 * exclusivity (selecting one automatically deselects its sibling
		 * -- handled entirely by the Wimp itself once both icons share
		 * the same ESG, per the PRM's "Radio icons" recipe). Initial
		 * SELECTED/SHADED state is left at 0 here -- rules_view_open()
		 * always calls refresh_all_toggle_displays() before the window
		 * is ever actually shown, so nothing meaningful is lost by not
		 * computing it twice. */
		icon = &def.icons[ICON_TOGGLE_OPT_A(t)];
		icon->extent.x0 = OPT_A_X0;
		icon->extent.x1 = OPT_A_X0 + OPTION_WIDTH;
		icon->extent.y1 = ROW_Y1(ROW_TOGGLE(t));
		icon->extent.y0 = ROW_Y0(ROW_TOGGLE(t));
		icon->flags = (wimp_icon_flags) (wimp_ICON_TEXT | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_RADIO << wimp_ICON_BUTTON_TYPE_SHIFT) | esg);
		strncpy(icon->data.text, TOGGLES[t].opt_a_text, 12);

		icon = &def.icons[ICON_TOGGLE_OPT_B(t)];
		icon->extent.x0 = OPT_B_X0;
		icon->extent.x1 = OPT_B_X0 + OPTION_WIDTH;
		icon->extent.y1 = ROW_Y1(ROW_TOGGLE(t));
		icon->extent.y0 = ROW_Y0(ROW_TOGGLE(t));
		icon->flags = (wimp_icon_flags) (wimp_ICON_TEXT | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_RADIO << wimp_ICON_BUTTON_TYPE_SHIFT) | esg);
		strncpy(icon->data.text, TOGGLES[t].opt_b_text, 12);
	}

	icon = &def.icons[ICON_OK];
	icon->extent.x0 = OK_X0;
	icon->extent.x1 = OK_X0 + BUTTON_WIDTH;
	icon->extent.y1 = BUTTON_ROW_Y1;
	icon->extent.y0 = BUTTON_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "OK";
	icon->data.indirected_text.validation = ok_validation;
	icon->data.indirected_text.size = 3;

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

	window_handle = wimp_create_window((wimp_window *) &def);
}

void rules_view_open(const ludo_rules *rules)
{
	wimp_window_state state;

	if (window_handle == (wimp_w) -1)
		return;

	pending = *rules;

	state.w = window_handle;
	wimp_get_window_state(&state);
	state.next = wimp_TOP;
	wimp_open_window((wimp_open *) &state);

	refresh_variant_display();
	refresh_all_toggle_displays();
}

wimp_w rules_view_window_handle(void)
{
	return window_handle;
}

void rules_view_redraw(wimp_draw *redraw)
{
	osbool more;

	more = wimp_redraw_window(redraw);
	while (more)
		more = wimp_get_rectangle(redraw);
}

void rules_view_click(wimp_pointer *pointer)
{
	int t;

	if (pointer->i == ICON_VARIANT_VALUE) {
		int i;

		variant_menu.title_fg = wimp_COLOUR_BLACK;
		variant_menu.title_bg = wimp_COLOUR_LIGHT_GREY;
		variant_menu.work_fg = wimp_COLOUR_BLACK;
		variant_menu.work_bg = wimp_COLOUR_WHITE;
		variant_menu.width = 200;
		variant_menu.height = wimp_MENU_ITEM_HEIGHT;
		variant_menu.gap = wimp_MENU_ITEM_GAP;
		strncpy(variant_menu.title_data.text, "Variant", 12);

		for (i = 0; i < 3; i++) {
			wimp_menu_entry *entry = &variant_menu.entries[i];

			entry->menu_flags = (i == 2) ? wimp_MENU_LAST : 0;
			entry->sub_menu = wimp_NO_SUB_MENU;
			entry->icon_flags = wimp_ICON_TEXT | wimp_ICON_FILLED | wimp_ICON_VCENTRED
			                   | (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT)
			                   | (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT);
			strncpy(entry->data.text, VARIANT_NAMES[i], 12);
		}

		wimp_create_menu((wimp_menu *) &variant_menu, pointer->pos.x, pointer->pos.y);
		menu_open = 1;
		return;
	}

	for (t = 0; t < TOGGLE_COUNT; t++) {
		if (pointer->i == ICON_TOGGLE_OPT_A(t)) {
			*rule_field(&pending, t) = TOGGLES[t].opt_a_value;
			refresh_toggle_display(t);
			return;
		}
		if (pointer->i == ICON_TOGGLE_OPT_B(t)) {
			*rule_field(&pending, t) = TOGGLES[t].opt_b_value;
			refresh_toggle_display(t);
			return;
		}
	}

	if (pointer->i == ICON_CANCEL) {
		wimp_close_window(window_handle);
		return;
	}

	if (pointer->i == ICON_OK) {
		setup_view_configure_rules(&pending);
		wimp_close_window(window_handle);
		return;
	}
}

int rules_view_menu_open(void)
{
	return menu_open;
}

void rules_view_menu_selection(wimp_selection *selection)
{
	int item = selection->items[0];

	menu_open = 0;

	if (item < 0 || item >= 3)
		return; /* menu dismissed without a selection */

	/* Adopts the chosen variant's own defaults wholesale -- still
	 * overridable afterwards via the individual toggles, matching the
	 * plan's own "picking a variant is a preset action, not a lock". */
	pending = ludo_default_rules((ludo_variant) item);
	refresh_variant_display();
	refresh_all_toggle_displays();
}
