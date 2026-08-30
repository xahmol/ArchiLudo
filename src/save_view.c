/*
 * ArchiLudo save/load view -- implementation.
 * See include/save_view.h for the module overview and API docs.
 */

#include <string.h>
#include <stdio.h>

#include "oslib/wimp.h"
#include "oslib/wimpspriteop.h" /* wimpspriteop_AREA, for def.sprite_area only --
                                  * these windows plot no sprites either */

#include "save_view.h"
#include "game_view.h"

#define MARGIN          8
#define SLOT_ROW_HEIGHT 40
#define SLOT_ROW_GAP     8
#define SLOT_NAME_WIDTH 260
#define SLOT_BUTTON_WIDTH 90
#define COL_GAP          8
#define BUTTON_WIDTH   100 /* Cancel button only -- per-slot action buttons
                             * use SLOT_BUTTON_WIDTH instead. */
#define BUTTON_HEIGHT   40
#define ROW_GAP          8 /* gap between the last slot row and Cancel */

/* 5 fixed, renamable save slots inside the app directory -- see
 * save_view.h's own doc comment for why this replaced an earlier
 * free-form pathname/drag-and-drop design. */
#define SLOT_COUNT 5

/* Row `row` (0 = topmost), same downward-growing-negative-Y convention
 * this project's other dialogues already use. */
#define SLOT_ROW_Y1(row) (-MARGIN - (row) * (SLOT_ROW_HEIGHT + SLOT_ROW_GAP))
#define SLOT_ROW_Y0(row) (SLOT_ROW_Y1(row) - SLOT_ROW_HEIGHT)

#define SLOT_NAME_X0   MARGIN
#define SLOT_ACTION_X0 (SLOT_NAME_X0 + SLOT_NAME_WIDTH + COL_GAP)
#define WINDOW_WIDTH   (SLOT_ACTION_X0 + SLOT_BUTTON_WIDTH + MARGIN)

#define CANCEL_ROW_Y1 (SLOT_ROW_Y0(SLOT_COUNT - 1) - ROW_GAP)
#define CANCEL_ROW_Y0 (CANCEL_ROW_Y1 - BUTTON_HEIGHT)
#define CANCEL_X0     MARGIN
#define WINDOW_HEIGHT (MARGIN - CANCEL_ROW_Y0)

/* Icon layout shared by both the Save and Load windows (structurally
 * identical -- 5 name+action row pairs plus one Cancel button -- only
 * the name field's writability and the action button's label differ,
 * see save_view_initialise()). */
#define ICON_NAME(n)      (n)
#define ICON_ACTION(n)    (SLOT_COUNT + (n))
#define ICON_CANCEL       (SLOT_COUNT * 2)
#define WINDOW_ICON_COUNT (ICON_CANCEL + 1)

static wimp_w save_window_handle = (wimp_w) -1;
static wimp_w load_window_handle = (wimp_w) -1;

/* Save dialogue: writable backing buffer for each row's name field --
 * the Wimp writes directly into these as the user types, so whatever is
 * in here at the moment Save is clicked is exactly what gets embedded in
 * the save data (see game_view_save_to_path()). */
static char slot_names[SLOT_COUNT][GAME_VIEW_SLOT_NAME_LEN];

/* Load dialogue: read-only display buffer for each row's name field,
 * refreshed from disk every time the dialogue opens (see
 * load_view_open()) -- never written to by the Wimp itself, since these
 * icons use BUTTON_NEVER, not BUTTON_WRITABLE. */
static char load_slot_display[SLOT_COUNT][GAME_VIEW_SLOT_NAME_LEN];
static int load_slot_occupied[SLOT_COUNT];

/* Shared validation-string buffers for the icons that need one -- a
 * writable icon needs a (possibly empty) validation string, and a
 * BUTTON_CLICK icon's border/fill comes from "R1" the same way every
 * other button in this project's dialogues already does. One buffer per
 * *kind*, not one per icon, since the Wimp only ever reads these. */
static char empty_validation[1] = "";
static char button_validation[4] = "R1";

/*
 * Function: build_slot_path
 * Summary: The fixed pathname for save slot `slot` (0-based) --
 *          `<ArchiLudo$Dir>.Slot1` .. `.Slot5`. Every slot always has
 *          exactly one possible path; there is no user-chosen pathname
 *          anywhere in this module.
 */
static void build_slot_path(int slot, char *out, size_t out_size)
{
	const char *dir = game_view_app_dir();

	if (dir[0] != '\0')
		snprintf(out, out_size, "%s.Slot%d", dir, slot + 1);
	else
		snprintf(out, out_size, "Slot%d", slot + 1);
}

static void open_window(wimp_w w)
{
	wimp_window_state state;

	if (w == (wimp_w) -1)
		return;

	state.w = w;
	wimp_get_window_state(&state);
	state.next = wimp_TOP;
	wimp_open_window((wimp_open *) &state);
}

/*
 * Function: set_icon_shaded (internal)
 * Summary: Set one icon's SHADED flag to exactly `shaded`, only issuing
 *          a Wimp_SetIconState call if that's an actual change -- same
 *          "read current state, EOR only if it differs" pattern
 *          src/rules_view.c already established for this project (see
 *          riscos_wimp_reference.md's Icons section). Used to grey out
 *          (and disable clicking on) an empty slot's Load button.
 */
static void set_icon_shaded(wimp_w w, int icon, int shaded)
{
	wimp_icon_state state;

	state.w = w;
	state.i = icon;
	wimp_get_icon_state(&state);

	if (((state.icon.flags & wimp_ICON_SHADED) != 0) != (shaded != 0))
		wimp_set_icon_state(w, icon, wimp_ICON_SHADED, 0);
}

void save_view_initialise(void)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) save_def;
	wimp_WINDOW(WINDOW_ICON_COUNT) load_def;
	wimp_icon *icon;
	int i;

	/* --- Save window: 5 writable name fields + "Save" buttons --- */

	save_def.visible.x0 = 150;
	save_def.visible.y0 = 150;
	save_def.visible.x1 = 150 + WINDOW_WIDTH;
	save_def.visible.y1 = 150 + WINDOW_HEIGHT;
	save_def.xscroll = 0;
	save_def.yscroll = 0;
	save_def.next = wimp_TOP;
	save_def.flags = wimp_WINDOW_NEW_FORMAT | wimp_WINDOW_MOVEABLE |
	                  wimp_WINDOW_BOUNDED_ONCE | wimp_WINDOW_BACK_ICON |
	                  wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_TITLE_ICON;
	save_def.title_fg = wimp_COLOUR_BLACK;
	save_def.title_bg = wimp_COLOUR_LIGHT_GREY;
	save_def.work_fg = wimp_COLOUR_BLACK;
	save_def.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
	save_def.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
	save_def.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
	save_def.highlight_bg = wimp_COLOUR_CREAM;
	save_def.extra_flags = 0;
	save_def.extent.x0 = 0;
	save_def.extent.y0 = -WINDOW_HEIGHT;
	save_def.extent.x1 = WINDOW_WIDTH;
	save_def.extent.y1 = 0;
	save_def.title_flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	                        wimp_ICON_VCENTRED | wimp_ICON_FILLED;
	/* No custom drawing -- plain Wimp icons throughout, so BUTTON_NEVER
	 * is correct here (see setup_view.c's matching note). */
	save_def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	save_def.sprite_area = wimpspriteop_AREA;
	save_def.xmin = WINDOW_WIDTH;
	save_def.ymin = WINDOW_HEIGHT;
	strncpy(save_def.title_data.text, "Save Game", 12);
	save_def.icon_count = WINDOW_ICON_COUNT;

	for (i = 0; i < SLOT_COUNT; i++) {
		icon = &save_def.icons[ICON_NAME(i)];
		icon->extent.x0 = SLOT_NAME_X0;
		icon->extent.x1 = SLOT_NAME_X0 + SLOT_NAME_WIDTH;
		icon->extent.y1 = SLOT_ROW_Y1(i);
		icon->extent.y0 = SLOT_ROW_Y0(i);
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_FILLED | wimp_ICON_VCENTRED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_WRITABLE << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = slot_names[i];
		icon->data.indirected_text.validation = empty_validation;
		icon->data.indirected_text.size = GAME_VIEW_SLOT_NAME_LEN;

		icon = &save_def.icons[ICON_ACTION(i)];
		icon->extent.x0 = SLOT_ACTION_X0;
		icon->extent.x1 = SLOT_ACTION_X0 + SLOT_BUTTON_WIDTH;
		icon->extent.y1 = SLOT_ROW_Y1(i);
		icon->extent.y0 = SLOT_ROW_Y0(i);
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = "Save";
		icon->data.indirected_text.validation = button_validation;
		icon->data.indirected_text.size = 5;
	}

	icon = &save_def.icons[ICON_CANCEL];
	icon->extent.x0 = CANCEL_X0;
	icon->extent.x1 = CANCEL_X0 + BUTTON_WIDTH;
	icon->extent.y1 = CANCEL_ROW_Y1;
	icon->extent.y0 = CANCEL_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "Cancel";
	icon->data.indirected_text.validation = button_validation;
	icon->data.indirected_text.size = 7;

	save_window_handle = wimp_create_window((wimp_window *) &save_def);

	/* --- Load window: 5 read-only name fields + "Load" buttons --- */

	load_def.visible.x0 = 150;
	load_def.visible.y0 = 150;
	load_def.visible.x1 = 150 + WINDOW_WIDTH;
	load_def.visible.y1 = 150 + WINDOW_HEIGHT;
	load_def.xscroll = 0;
	load_def.yscroll = 0;
	load_def.next = wimp_TOP;
	load_def.flags = wimp_WINDOW_NEW_FORMAT | wimp_WINDOW_MOVEABLE |
	                  wimp_WINDOW_BOUNDED_ONCE | wimp_WINDOW_BACK_ICON |
	                  wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_TITLE_ICON;
	load_def.title_fg = wimp_COLOUR_BLACK;
	load_def.title_bg = wimp_COLOUR_LIGHT_GREY;
	load_def.work_fg = wimp_COLOUR_BLACK;
	load_def.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
	load_def.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
	load_def.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
	load_def.highlight_bg = wimp_COLOUR_CREAM;
	load_def.extra_flags = 0;
	load_def.extent.x0 = 0;
	load_def.extent.y0 = -WINDOW_HEIGHT;
	load_def.extent.x1 = WINDOW_WIDTH;
	load_def.extent.y1 = 0;
	load_def.title_flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	                        wimp_ICON_VCENTRED | wimp_ICON_FILLED;
	load_def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	load_def.sprite_area = wimpspriteop_AREA;
	load_def.xmin = WINDOW_WIDTH;
	load_def.ymin = WINDOW_HEIGHT;
	strncpy(load_def.title_data.text, "Load Game", 12);
	load_def.icon_count = WINDOW_ICON_COUNT;

	for (i = 0; i < SLOT_COUNT; i++) {
		icon = &load_def.icons[ICON_NAME(i)];
		icon->extent.x0 = SLOT_NAME_X0;
		icon->extent.x1 = SLOT_NAME_X0 + SLOT_NAME_WIDTH;
		icon->extent.y1 = SLOT_ROW_Y1(i);
		icon->extent.y0 = SLOT_ROW_Y0(i);
		/* Read-only: BUTTON_NEVER, not WRITABLE -- this is a display
		 * label, refreshed from disk on every load_view_open(), not
		 * something the player types into. */
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_FILLED | wimp_ICON_VCENTRED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = load_slot_display[i];
		icon->data.indirected_text.validation = empty_validation;
		icon->data.indirected_text.size = GAME_VIEW_SLOT_NAME_LEN;

		icon = &load_def.icons[ICON_ACTION(i)];
		icon->extent.x0 = SLOT_ACTION_X0;
		icon->extent.x1 = SLOT_ACTION_X0 + SLOT_BUTTON_WIDTH;
		icon->extent.y1 = SLOT_ROW_Y1(i);
		icon->extent.y0 = SLOT_ROW_Y0(i);
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
		              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
		icon->data.indirected_text.text = "Load";
		icon->data.indirected_text.validation = button_validation;
		icon->data.indirected_text.size = 5;
	}

	icon = &load_def.icons[ICON_CANCEL];
	icon->extent.x0 = CANCEL_X0;
	icon->extent.x1 = CANCEL_X0 + BUTTON_WIDTH;
	icon->extent.y1 = CANCEL_ROW_Y1;
	icon->extent.y0 = CANCEL_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "Cancel";
	icon->data.indirected_text.validation = button_validation;
	icon->data.indirected_text.size = 7;

	load_window_handle = wimp_create_window((wimp_window *) &load_def);
}

/*
 * Function: save_view_open
 * Summary: Refresh all 5 slot rows from whatever is actually on disc
 *          right now (occupied slots show their real saved name,
 *          otherwise "Slot N"), then open the window. Always re-reads
 *          on every open -- see the module's own note on the
 *          one accepted edge case this causes (an in-progress, unsaved
 *          rename gets discarded if the dialogue is somehow reopened
 *          without being closed first; not reachable through this
 *          project's own menu, which only ever opens this dialogue via a
 *          single iconbar/window-menu entry).
 */
void save_view_open(void)
{
	int i;
	char path[300];
	char peeked[GAME_VIEW_SLOT_NAME_LEN];

	for (i = 0; i < SLOT_COUNT; i++) {
		build_slot_path(i, path, sizeof(path));
		if (game_view_peek_slot_name(path, peeked, sizeof(peeked)) && peeked[0] != '\0')
			snprintf(slot_names[i], sizeof(slot_names[i]), "%s", peeked);
		else
			snprintf(slot_names[i], sizeof(slot_names[i]), "Slot %d", i + 1);

		if (save_window_handle != (wimp_w) -1)
			wimp_set_icon_state(save_window_handle, ICON_NAME(i), 0, 0);
	}

	open_window(save_window_handle);
}

/*
 * Function: load_view_open
 * Summary: Refresh all 5 slot rows from disc (real name if occupied,
 *          "(empty)" and a shaded Load button otherwise), then open the
 *          window.
 */
void load_view_open(void)
{
	int i;
	char path[300];

	for (i = 0; i < SLOT_COUNT; i++) {
		build_slot_path(i, path, sizeof(path));
		load_slot_occupied[i] = game_view_peek_slot_name(path, load_slot_display[i],
		                                                  sizeof(load_slot_display[i]));
		if (!load_slot_occupied[i] || load_slot_display[i][0] == '\0')
			snprintf(load_slot_display[i], sizeof(load_slot_display[i]), "(empty)");

		if (load_window_handle != (wimp_w) -1) {
			wimp_set_icon_state(load_window_handle, ICON_NAME(i), 0, 0);
			set_icon_shaded(load_window_handle, ICON_ACTION(i), !load_slot_occupied[i]);
		}
	}

	open_window(load_window_handle);
}

wimp_w save_view_window_handle(void)
{
	return save_window_handle;
}

wimp_w load_view_window_handle(void)
{
	return load_window_handle;
}

static void redraw_plain(wimp_draw *redraw)
{
	osbool more;

	more = wimp_redraw_window(redraw);
	while (more)
		more = wimp_get_rectangle(redraw);
}

void save_view_redraw(wimp_draw *redraw)
{
	redraw_plain(redraw);
}

void load_view_redraw(wimp_draw *redraw)
{
	redraw_plain(redraw);
}

void save_view_click(wimp_pointer *pointer)
{
	int slot;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		if (pointer->i == ICON_ACTION(slot)) {
			char path[300];

			/* An entirely blanked-out name field (user selected all,
			 * deleted) still needs a real label -- default back to
			 * "Slot N" rather than saving with an empty name. */
			if (slot_names[slot][0] == '\0')
				snprintf(slot_names[slot], sizeof(slot_names[slot]), "Slot %d", slot + 1);

			build_slot_path(slot, path, sizeof(path));
			if (game_view_save_to_path(path, slot_names[slot]))
				wimp_close_window(save_window_handle);
			/* On failure, leave the dialogue open so the user can see
			 * what's there and retry -- see the debug Log for why it
			 * failed (game_view_save_to_path() logs there). */
			return;
		}
	}

	if (pointer->i == ICON_CANCEL) {
		wimp_close_window(save_window_handle);
		return;
	}
}

void load_view_click(wimp_pointer *pointer)
{
	int slot;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		if (pointer->i == ICON_ACTION(slot)) {
			char path[300];

			if (!load_slot_occupied[slot])
				return; /* shaded/empty slot -- no-op */

			build_slot_path(slot, path, sizeof(path));
			/* game_view_open() must be called here, matching
			 * setup_view.c's ICON_START handler (which explicitly opens
			 * the game window before game_view_new_game()) -- loading a
			 * game must open the window immediately rather than only
			 * setting game_started/loading the board and leaving the
			 * window to appear on some later, separate iconbar click.
			 * Matches game_view_new_game()'s own pattern: open first,
			 * so a load that's about to succeed is
			 * immediately visible. */
			if (game_view_load_from_path(path)) {
				game_view_open();
				wimp_close_window(load_window_handle);
			}
			return;
		}
	}

	if (pointer->i == ICON_CANCEL) {
		wimp_close_window(load_window_handle);
		return;
	}
}

void save_view_key_pressed(wimp_key *key)
{
	wimp_process_key(key->c);
}

void load_view_key_pressed(wimp_key *key)
{
	wimp_process_key(key->c);
}
