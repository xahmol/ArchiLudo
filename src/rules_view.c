/*
 * ArchiLudo rules view -- implementation.
 * See include/rules_view.h for the module overview and API docs.
 */

#include <string.h>

#include "oslib/wimp.h"
#include "oslib/wimpspriteop.h"

#include "rules_view.h"
#include "setup_view.h"

/* Column/row sizing: a bordered/filled icon at this desktop font size
 * needs roughly 14 OS units per character plus ~16 units of padding
 * (e.g. "Cancel", 6 chars, just fits a 100-unit button elsewhere in
 * this project's setup_view.c; "Six-release", 11 chars, needs ~170).
 * These constants are sized generously against that per-character
 * estimate -- RISC OS clips icon redraw to the icon's own extent, so
 * an over-length HCENTRED string silently loses characters from both
 * ends rather than overflowing visibly, which makes an undersized box
 * easy to miss until checked live. */
#define MARGIN         16
#define LABEL_WIDTH   190
#define OPTION_WIDTH  190
#define OPTION_GAP     16
#define VALUE_WIDTH   (OPTION_WIDTH * 2 + OPTION_GAP)
/* The variant value is a genuine RISC OS pop-up FIELD, not a plain
 * writable-looking box -- a read-only display icon (FIELD_WIDTH) sat
 * immediately against a small square arrow-sprite button (POPUP_WIDTH)
 * that actually opens the menu. See ICON_VARIANT_POPUP below and its
 * doc comment for the sprite/validation detail. */
#define POPUP_WIDTH    36
#define FIELD_WIDTH   (VALUE_WIDTH - POPUP_WIDTH)
#define ROW_HEIGHT     36
#define ROW_GAP         8
#define BUTTON_WIDTH  130
#define BUTTON_HEIGHT  44
#define BUTTON_GAP     16

#define TOGGLE_COUNT 8

/* Row 0 is the variant picker. Rows 1-2 are the Pachisi-authenticity
 * caveat, split across two lines -- at these column widths one line
 * only holds ~40 characters, well short of the full caveat sentence,
 * so it is pre-wrapped by hand into two short static strings rather
 * than left to overflow (see the sizing comment above for why an
 * icon's own extent clips, rather than wraps, its text). Rows 3..9 are
 * the 7 house-rule toggles. */
#define ROW_VARIANT    0
#define ROW_CAVEAT_1   1
#define ROW_CAVEAT_2   2
#define ROW_TOGGLE(t) (3 + (t))
#define GRID_ROWS     (3 + TOGGLE_COUNT)

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

/* One label + two option icons per toggle, plus the variant label/
 * field/popup-button trio, the two caveat lines, and OK/Cancel. */
#define ICON_VARIANT_LABEL 0
#define ICON_VARIANT_VALUE 1
#define ICON_VARIANT_POPUP 2
#define ICON_CAVEAT_1       3
#define ICON_CAVEAT_2       4
#define TOGGLE_BASE         5
#define ICON_TOGGLE_LABEL(t) (TOGGLE_BASE + (t) * 3)
#define ICON_TOGGLE_OPT_A(t)  (TOGGLE_BASE + (t) * 3 + 1)
#define ICON_TOGGLE_OPT_B(t)  (TOGGLE_BASE + (t) * 3 + 2)
#define ICON_OK     (TOGGLE_BASE + TOGGLE_COUNT * 3)
#define ICON_CANCEL (ICON_OK + 1)
#define WINDOW_ICON_COUNT (ICON_CANCEL + 1)

/* Validation string for a genuine RISC OS round radio icon: the "S"
 * command names the deselected/selected sprite pair, and the Wimp
 * itself swaps between them as wimp_ICON_SELECTED changes -- no
 * application redraw code needed. "radiooff"/"radioon" (the round-dot
 * pair used for a group of mutually-exclusive choices) are standard
 * sprites always present in the Wimp Sprite Pool (def.sprite_area =
 * wimpspriteop_AREA below tells these icons to look there rather than
 * in a private sprite area) -- NOT "optoff"/"opton", which is actually
 * the square *tick-box* pair for an independent on/off option, not a
 * radio choice. Based on Steve Fryatt's "Wimp Programming In C", Chapter 18
 * ("Sprite Icons and Choosing Options", specifically its "Multiple
 * options" section and Listing 18.10) and Chapter 20 ("Radio Icons
 * Revisited") -- www.stevefryatt.org.uk/risc-os/wimp-prog/ -- which is
 * also where this project's ESG/radio-icon conventions in
 * riscos_wimp_reference.md originate. Adapted: paired with this file's
 * own toggle table/ESG scheme rather than the tutorial's 3-way example. */
#define RADIO_VALIDATION "Sradiooff,radioon"

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

/* Row label text stays inline/non-indirected (<=11 chars + terminator,
 * a plain icon's fixed 12-byte text buffer -- see src/main.c's
 * set_menu_entry() for the same limit on menu items) since it never
 * changes at runtime. The option texts below, in contrast, are now
 * copied into opt_text[] and shown via indirected text+sprite icons
 * (see rules_view_initialise()), because a radio icon's text and its
 * "S" sprite-validation command must live in an indirected buffer --
 * OSLib's non-indirected wimp_icon_data has no text_and_sprite variant
 * with its own validation string. Some names are intentionally
 * abbreviated from game_logic.h's own field names for row-label-space
 * reasons (e.g. "Own capture" for own_pawn_capture, "Last pawn" +
 * "Needs 6"/"Any roll" instead of literally spelling out
 * "no_six_needed_last_pawn"'s double-negative). */
static const toggle_info TOGGLES[TOGGLE_COUNT] = {
	{ "Six-release", "Mandatory", 1, "Optional", 0 },
	{ "Own capture",  "On",        1, "Off",      0 },
	{ "Overshoot",    "Blocked",   0, "Bounce",   1 },
	{ "Blockade",     "Off",       0, "On",       1 },
	{ "Backward",     "Off",       0, "On",       1 },
	{ "Free home",    "Off",       0, "On",       1 },
	{ "Last pawn",    "Needs 6",   0, "Any roll", 1 },
	{ "3 sixes",      "Chain",     0, "Forfeit",  1 },
};

/* Which toggles are hidden (shaded) for each variant -- bit t set means
 * toggle t is inapplicable and shaded for that variant. Indexed by
 * ludo_variant (LUDO_VARIANT_MEJN=0, _LUDO=1, _PACHISI=2). Matches the
 * approved multi-rule-set plan's own applicability matrix; see
 * docs/GAME_LOGIC.md's "Rule-set variants" section for the same table
 * in prose. Toggle indices, matching TOGGLES[] above: 0=six-release,
 * 1=own capture, 2=overshoot, 3=blockade, 4=backward, 5=free home,
 * 6=last pawn, 7=three sixes. Three-sixes-forfeit is hidden for MEJN
 * specifically, same reasoning as blockade/backward/free-home -- not
 * part of this project's traditional ruleset, which keeps its own
 * unlimited six-chaining regardless (see ludo_default_rules()'s own
 * doc comment in game_logic.c). */
static const unsigned char VARIANT_HIDDEN_MASK[3] = {
	(1u << 3) | (1u << 4) | (1u << 5) | (1u << 7), /* MEJN: blockade/backward/free-home/three-sixes hidden */
	(1u << 4) | (1u << 5),             /* Ludo: backward/free-home hidden */
	(1u << 0),                          /* Pachisi-style: six-release hidden */
};

/* Full display names -- shown both in the variant value icon and in
 * the pop-up menu's own entries. "Mens Erger Je Niet" (18 chars) is
 * well past a plain menu entry's 12-byte inline text buffer (see
 * src/main.c's set_menu_entry(), used for this project's OTHER,
 * shorter-worded menu), so both the value icon and the menu entries
 * use indirected text here -- see variant_text/variant_menu_text and
 * rules_view_click(). Names are written out in full (VALUE_WIDTH
 * comfortably fits even "Mens Erger Je Niet"), not abbreviated. */
static const char *VARIANT_NAMES[3] = { "Mens Erger Je Niet", "Ludo", "Pachisi-style" };

static wimp_w window_handle = (wimp_w) -1;
static ludo_rules pending;
static char variant_text[24];
/* Copies of TOGGLES[]'s option text, one buffer per option icon --
 * see the indirected-text-and-sprite comment on RADIO_VALIDATION above
 * for why these can't just point at TOGGLES[]'s own const char* (an
 * indirected icon's text buffer must be a plain writable char[], even
 * though these particular ones are never actually rewritten after
 * rules_view_initialise() fills them once). */
static char opt_text[TOGGLE_COUNT][2][12];
/* The full caveat sentence ("Pachisi-style is a curated preset, not
 * authentic Pachisi.") is too long for one row at any of these column
 * widths regardless of variant-name length, so it's pre-wrapped by
 * hand into two short lines (kept well under a conservative per-char
 * width estimate, not the file's original 14-units/char guess -- round
 * 7.46's first attempt at this undershot and still clipped a few
 * characters on a real screenshot, so these are trimmed shorter still,
 * with margin, rather than re-guessing the exact metric again). */
static char caveat_line_1[40] = "Note: Pachisi-style is a curated";
static char caveat_line_2[40] = "preset, not authentic Pachisi.";
/* Indirected copies of VARIANT_NAMES[], one per pop-up menu entry --
 * "Mens Erger Je Niet" is far past a plain wimp_menu_entry's own
 * 12-byte inline text buffer (wimp_menu_entry shares wimp_icon_data
 * with ordinary icons, so it supports wimp_ICON_INDIRECTED the same
 * way -- confirmed against ArchieSDK's own oslib/wimp.h). */
static char variant_menu_text[3][24];
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
 * Syntax:  static int *rule_field(ludo_rules *r, int toggle);
 * Input:   r      - the rules struct to index into.
 *          toggle - toggle index, 0..TOGGLE_COUNT-1, matching TOGGLES[].
 * Output:  pointer to the int field within `r` that toggle controls.
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
	case 6: return &r->no_six_needed_last_pawn;
	default: return &r->three_sixes_forfeit_turn;
	}
}

/*
 * Function: set_icon_selected (internal)
 * Summary: Set one icon's SELECTED flag to exactly `selected`, only
 *          issuing a Wimp_SetIconState call if that's an actual change --
 *          same "read current state, EOR only if it differs" pattern as
 *          this project's own established wimp_ICON_SHADED convention
 *          (see riscos_wimp_reference.md's Icons section). For a radio
 *          icon using RADIO_VALIDATION, this is also what flips the
 *          icon between its "optoff" and "opton" sprites -- the Wimp
 *          reads SELECTED to choose which of the two, no separate
 *          redraw call needed.
 * Syntax:  static void set_icon_selected(int icon, int selected);
 * Input:   icon     - icon handle within this window.
 *          selected - non-zero to select the icon, 0 to deselect it.
 * Output:  none. The icon's SELECTED flag is updated (via a
 *          Wimp_SetIconState only if it actually needs to change).
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
 * Syntax:  static void set_icon_shaded(int icon, int shaded);
 * Input:   icon   - icon handle within this window.
 *          shaded - non-zero to shade (grey out/disable) the icon, 0 to
 *                   unshade it.
 * Output:  none. The icon's SHADED flag is updated (via a
 *          Wimp_SetIconState only if it actually needs to change).
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
 * Syntax:  static void refresh_toggle_display(int t);
 * Input:   t - toggle index, 0..TOGGLE_COUNT-1.
 * Output:  none. Updates that toggle's icons on screen.
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

/*
 * Function: refresh_all_toggle_displays (internal)
 * Summary: Call refresh_toggle_display() for every toggle -- used after
 *          any bulk change to `pending` (e.g. picking a new variant,
 *          which resets every toggle's default).
 * Syntax:  static void refresh_all_toggle_displays(void);
 * Input:   none.
 * Output:  none. Updates every toggle's icons on screen.
 */
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
 * Syntax:  static void refresh_variant_display(void);
 * Input:   none.
 * Output:  none. Updates variant_text and forces a redraw of the icon.
 */
static void refresh_variant_display(void)
{
	strncpy(variant_text, VARIANT_NAMES[pending.variant], sizeof(variant_text) - 1);
	variant_text[sizeof(variant_text) - 1] = '\0';
	wimp_set_icon_state(window_handle, ICON_VARIANT_VALUE, 0, 0);
}

/*
 * Function: init_radio_icon (internal)
 * Summary: Fill in one toggle option's indirected text+sprite radio
 *          icon -- shared by both option columns in the icon-creation
 *          loop below so the sprite/flag setup is only written once.
 * Syntax:  static void init_radio_icon(wimp_icon *icon, int x0, int row,
 *              char *buffer, const char *text, unsigned esg);
 * Input:   icon   - the icon struct to fill in.
 *          x0     - left edge of the icon's extent, in OS units.
 *          row    - which toggle row this icon belongs to (see
 *                   ROW_Y0()/ROW_Y1()).
 *          buffer - indirected text storage for this icon (at least 12
 *                   bytes), written to by this call and read by the
 *                   Wimp thereafter.
 *          text   - the option's label, copied into `buffer` (truncated
 *                   to 11 characters plus terminator).
 *          esg    - the icon's ESG (sprite-selection group) flag bits,
 *                   OR'd into icon->flags.
 * Output:  none. `icon` (and `buffer`) are filled in on return.
 */
static void init_radio_icon(wimp_icon *icon, int x0, int row, char *buffer,
                             const char *text, unsigned esg)
{
	icon->extent.x0 = x0;
	icon->extent.x1 = x0 + OPTION_WIDTH;
	icon->extent.y1 = ROW_Y1(row);
	icon->extent.y0 = ROW_Y0(row);
	/* No BORDER/FILLED and no HCENTRED -- a real radio icon is a small
	 * sprite followed by its label, left-aligned within the icon, not a
	 * filled push-button look-alike. Initial SELECTED/
	 * SHADED state is left at 0 here -- rules_view_open() always calls
	 * refresh_all_toggle_displays() before the window is ever actually
	 * shown, so nothing meaningful is lost by not computing it twice. */
	icon->flags = (wimp_icon_flags) (wimp_ICON_TEXT | wimp_ICON_SPRITE |
	              wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_RADIO << wimp_ICON_BUTTON_TYPE_SHIFT) | esg);

	strncpy(buffer, text, 11);
	buffer[11] = '\0';
	icon->data.indirected_text_and_sprite.text = buffer;
	icon->data.indirected_text_and_sprite.validation = RADIO_VALIDATION;
	icon->data.indirected_text_and_sprite.size = 12;
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
	icon->extent.x1 = OPT_A_X0 + FIELD_WIDTH;
	icon->extent.y1 = ROW_Y1(ROW_VARIANT);
	icon->extent.y0 = ROW_Y0(ROW_VARIANT);
	/* A plain read-only display field -- BUTTON_NEVER, since round
	 * 7.47.3 moved the actual "open the menu" click target to the
	 * dedicated arrow icon right after it (ICON_VARIANT_POPUP below).
	 * This mirrors the genuine RISC OS "pop-up menu field" convention
	 * (a real screenshot showed this field alone, unbordered-arrow-less,
	 * reading as an ordinary writable text box with nothing marking it
	 * as a drop-down -- per direct user feedback, "cant we make it a
	 * visible recognisable pulldown?"). */
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	variant_text[0] = '\0';
	icon->data.indirected_text.text = variant_text;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = sizeof(variant_text);

	icon = &def.icons[ICON_VARIANT_POPUP];
	icon->extent.x0 = OPT_A_X0 + FIELD_WIDTH;
	icon->extent.x1 = OPT_A_X0 + VALUE_WIDTH;
	icon->extent.y1 = ROW_Y1(ROW_VARIANT);
	icon->extent.y0 = ROW_Y0(ROW_VARIANT);
	/* The genuine RISC OS pop-up-menu-field button: a sprite-only icon
	 * (its own text left empty -- indirected purely so the "S"
	 * validation command has somewhere to specify the sprite pair) using
	 * the standard "gright"/"pgright" ("grey right-pointing arrow",
	 * raised/pressed) Wimp Sprite Pool sprites and the "R5" validation
	 * command that marks it as this specific kind of 3D pop-up button --
	 * no wimp_ICON_BORDER (the sprites supply their own visual chrome).
	 * Click opens the 3-item variant pop-up menu (rules_view_click()),
	 * the same non-writable-indirected-plus-click-opens-a-wimp_menu
	 * pattern src/main.c's own iconbar/window menu already establishes.
	 * Based on Steve Fryatt's "Wimp Programming In C", Chapter 24
	 * ("Pop-up Menus and Other Features"), section "Creating the pop-up
	 * menu icon" and its Listing 24.2's exact validation string --
	 * www.stevefryatt.org.uk/risc-os/wimp-prog/popup-menus-and-other-
	 * features -- confirmed against that page's own screenshot
	 * (menus-popup-menu.png) that the arrow button sits immediately
	 * after the display field, not before it or overlapping it.
	 * Adapted: field only, no separate "current shape" preview icon
	 * (the tutorial's own WIN_ICON_SHAPE) since this dialogue has
	 * nothing analogous to show. */
	icon->flags = (wimp_icon_flags) (wimp_ICON_TEXT | wimp_ICON_SPRITE |
	              wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED | wimp_ICON_HCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT));
	icon->data.indirected_text_and_sprite.text = "";
	icon->data.indirected_text_and_sprite.validation = "R5;Sgright,pgright";
	icon->data.indirected_text_and_sprite.size = 1;

	icon = &def.icons[ICON_CAVEAT_1];
	icon->extent.x0 = LABEL_X0;
	icon->extent.x1 = OPT_B_X0 + OPTION_WIDTH;
	icon->extent.y1 = ROW_Y1(ROW_CAVEAT_1);
	icon->extent.y0 = ROW_Y0(ROW_CAVEAT_1);
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_MID_LIGHT_GREY << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = caveat_line_1;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = sizeof(caveat_line_1);

	icon = &def.icons[ICON_CAVEAT_2];
	icon->extent.x0 = LABEL_X0;
	icon->extent.x1 = OPT_B_X0 + OPTION_WIDTH;
	icon->extent.y1 = ROW_Y1(ROW_CAVEAT_2);
	icon->extent.y0 = ROW_Y0(ROW_CAVEAT_2);
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_MID_LIGHT_GREY << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = caveat_line_2;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = sizeof(caveat_line_2);

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

		/* Paired radio icons: button type 11 (auto-selects, no
		 * double-click semantics -- see riscos_wimp_reference.md's
		 * Icons section) with a shared non-zero ESG forces mutual
		 * exclusivity (selecting one automatically deselects its
		 * sibling -- handled entirely by the Wimp itself once both
		 * icons share the same ESG, per the PRM's "Radio icons"
		 * recipe), each showing the standard "optoff"/"opton" sprite
		 * pair via RADIO_VALIDATION -- see init_radio_icon(). */
		init_radio_icon(&def.icons[ICON_TOGGLE_OPT_A(t)], OPT_A_X0, ROW_TOGGLE(t),
		                 opt_text[t][0], TOGGLES[t].opt_a_text, esg);
		init_radio_icon(&def.icons[ICON_TOGGLE_OPT_B(t)], OPT_B_X0, ROW_TOGGLE(t),
		                 opt_text[t][1], TOGGLES[t].opt_b_text, esg);
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

	if (pointer->i == ICON_VARIANT_POPUP) {
		int i;

		variant_menu.title_fg = wimp_COLOUR_BLACK;
		variant_menu.title_bg = wimp_COLOUR_LIGHT_GREY;
		variant_menu.work_fg = wimp_COLOUR_BLACK;
		variant_menu.work_bg = wimp_COLOUR_WHITE;
		/* Wide enough for "Mens Erger Je Niet" (18 chars) at this
		 * dialogue's own ~14-units/char calibration plus margin. */
		variant_menu.width = 300;
		variant_menu.height = wimp_MENU_ITEM_HEIGHT;
		variant_menu.gap = wimp_MENU_ITEM_GAP;
		strncpy(variant_menu.title_data.text, "Variant", 12);

		for (i = 0; i < 3; i++) {
			wimp_menu_entry *entry = &variant_menu.entries[i];

			entry->menu_flags = (i == 2) ? wimp_MENU_LAST : 0;
			entry->sub_menu = wimp_NO_SUB_MENU;
			/* Indirected, not plain text -- see variant_menu_text's
			 * own doc comment for why. */
			entry->icon_flags = (wimp_icon_flags) (wimp_ICON_TEXT | wimp_ICON_INDIRECTED |
			                   wimp_ICON_FILLED | wimp_ICON_VCENTRED
			                   | (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT)
			                   | (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT));
			strncpy(variant_menu_text[i], VARIANT_NAMES[i], sizeof(variant_menu_text[i]) - 1);
			variant_menu_text[i][sizeof(variant_menu_text[i]) - 1] = '\0';
			entry->data.indirected_text.text = variant_menu_text[i];
			entry->data.indirected_text.validation = "";
			entry->data.indirected_text.size = sizeof(variant_menu_text[i]);
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
