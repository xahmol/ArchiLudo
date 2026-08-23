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
#define FIELD_HEIGHT   40
/* Wide enough to actually fit the "Drag" label -- per explicit user
 * feedback that the draggable icon wasn't discoverable/didn't seem to
 * be there at all ("thought we were going to implement icon dragging
 * and dropping? It now asks just the full filepath"): it was there and
 * functional, but the original 40-unit-wide icon (sized only to be a
 * small square) both clipped its old "File" label and didn't read as
 * "drag me" the way a label saying "Drag" does. */
#define FILE_ICON_SIZE 64
#define COL_GAP         8
#define PATH_WIDTH    300
#define BUTTON_WIDTH  100
#define BUTTON_HEIGHT  40
#define BUTTON_GAP     16
#define ROW_GAP         8

#define PATH_BUF_LEN 256

/* Unregistered with the OS's file-type/application association system
 * (see save_view.h's "Not implemented" note on Message_DataOpen) --
 * &FFD ("Data") is the standard generic/no-special-meaning RISC OS
 * filetype, safe to use without a real registration. */
#define ARCHILUDO_SAVE_FILETYPE 0xFFDu

/* --- Save dialogue --- */

#define SAVE_FILE_X0 MARGIN
#define SAVE_PATH_X0 (SAVE_FILE_X0 + FILE_ICON_SIZE + COL_GAP)
#define SAVE_WINDOW_WIDTH (SAVE_PATH_X0 + PATH_WIDTH + MARGIN)
#define SAVE_ROW_Y1 (-MARGIN)
#define SAVE_ROW_Y0 (SAVE_ROW_Y1 - FIELD_HEIGHT)
#define SAVE_BUTTON_ROW_Y1 (SAVE_ROW_Y0 - ROW_GAP)
#define SAVE_BUTTON_ROW_Y0 (SAVE_BUTTON_ROW_Y1 - BUTTON_HEIGHT)
#define SAVE_WINDOW_HEIGHT (MARGIN - SAVE_BUTTON_ROW_Y0)
#define SAVE_GO_X0 MARGIN
#define SAVE_CANCEL_X0 (SAVE_GO_X0 + BUTTON_WIDTH + BUTTON_GAP)

#define ICON_SAVE_FILE   0
#define ICON_SAVE_PATH   1
#define ICON_SAVE_GO     2
#define ICON_SAVE_CANCEL 3
#define SAVE_ICON_COUNT  4

static wimp_w save_window_handle = (wimp_w) -1;
static char save_path[PATH_BUF_LEN];
static char save_go_validation[4] = "R1";
static char save_cancel_validation[4] = "R1";

/* Save-drag-in-progress state -- see save_view_drag_ended() and
 * save_view_message_received(). */
static int drag_pending = 0;
static int drag_my_ref = 0;

/* --- Load dialogue --- */

#define LOAD_PATH_X0 MARGIN
#define LOAD_WINDOW_WIDTH (LOAD_PATH_X0 + PATH_WIDTH + MARGIN)
#define LOAD_ROW_Y1 (-MARGIN)
#define LOAD_ROW_Y0 (LOAD_ROW_Y1 - FIELD_HEIGHT)
#define LOAD_BUTTON_ROW_Y1 (LOAD_ROW_Y0 - ROW_GAP)
#define LOAD_BUTTON_ROW_Y0 (LOAD_BUTTON_ROW_Y1 - BUTTON_HEIGHT)
#define LOAD_WINDOW_HEIGHT (MARGIN - LOAD_BUTTON_ROW_Y0)
#define LOAD_GO_X0 MARGIN
#define LOAD_CANCEL_X0 (LOAD_GO_X0 + BUTTON_WIDTH + BUTTON_GAP)

#define ICON_LOAD_PATH   0
#define ICON_LOAD_GO     1
#define ICON_LOAD_CANCEL 2
#define LOAD_ICON_COUNT  3

static wimp_w load_window_handle = (wimp_w) -1;
static char load_path[PATH_BUF_LEN];
static char load_go_validation[4] = "R1";
static char load_cancel_validation[4] = "R1";

/*
 * Function: build_default_path
 * Summary: A sensible starting pathname for either dialogue's writable
 *          icon -- this program's own directory (see
 *          game_view_app_dir()) plus a fixed leafname, the same
 *          resource_path()-style convention game_view.c's own debug Log
 *          already uses.
 */
static void build_default_path(char *out, size_t out_size)
{
	const char *dir = game_view_app_dir();

	if (dir[0] != '\0')
		snprintf(out, out_size, "%s.SaveGame", dir);
	else
		snprintf(out, out_size, "SaveGame");
}

void save_view_initialise(void)
{
	wimp_WINDOW(SAVE_ICON_COUNT) save_def;
	wimp_WINDOW(LOAD_ICON_COUNT) load_def;
	wimp_icon *icon;

	build_default_path(save_path, sizeof(save_path));
	build_default_path(load_path, sizeof(load_path));

	/* --- Save window --- */

	save_def.visible.x0 = 150;
	save_def.visible.y0 = 150;
	save_def.visible.x1 = 150 + SAVE_WINDOW_WIDTH;
	save_def.visible.y1 = 150 + SAVE_WINDOW_HEIGHT;
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
	save_def.extent.y0 = -SAVE_WINDOW_HEIGHT;
	save_def.extent.x1 = SAVE_WINDOW_WIDTH;
	save_def.extent.y1 = 0;
	save_def.title_flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	                        wimp_ICON_VCENTRED | wimp_ICON_FILLED;
	/* No custom drawing (unlike game_view.c's board) -- plain Wimp icons
	 * throughout, so BUTTON_NEVER is correct here (see setup_view.c's
	 * matching note). */
	save_def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	save_def.sprite_area = wimpspriteop_AREA;
	save_def.xmin = SAVE_WINDOW_WIDTH;
	save_def.ymin = SAVE_WINDOW_HEIGHT;
	strncpy(save_def.title_data.text, "Save Game", 12);
	save_def.icon_count = SAVE_ICON_COUNT;

	icon = &save_def.icons[ICON_SAVE_FILE];
	icon->extent.x0 = SAVE_FILE_X0;
	icon->extent.x1 = SAVE_FILE_X0 + FILE_ICON_SIZE;
	icon->extent.y1 = SAVE_ROW_Y1;
	icon->extent.y0 = SAVE_ROW_Y0;
	/* CLICK_DRAG (button type 6): a plain click still notifies (buttons
	 * == wimp_CLICK_SELECT), same as any button, but holding the button
	 * down long enough to become a drag reports buttons ==
	 * wimp_DRAG_SELECT instead -- see save_view_click()'s handling and
	 * the RISC OS 3 PRM's "Icon button types" table for exactly why type
	 * 6 (not the ordinary BUTTON_CLICK every other icon in this project
	 * uses) is needed for a draggable file icon. */
	icon->flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED |
	              wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK_DRAG << wimp_ICON_BUTTON_TYPE_SHIFT);
	strncpy(icon->data.text, "Drag", 12);

	icon = &save_def.icons[ICON_SAVE_PATH];
	icon->extent.x0 = SAVE_PATH_X0;
	icon->extent.x1 = SAVE_PATH_X0 + PATH_WIDTH;
	icon->extent.y1 = SAVE_ROW_Y1;
	icon->extent.y0 = SAVE_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_FILLED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_WRITABLE << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = save_path;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = PATH_BUF_LEN;

	icon = &save_def.icons[ICON_SAVE_GO];
	icon->extent.x0 = SAVE_GO_X0;
	icon->extent.x1 = SAVE_GO_X0 + BUTTON_WIDTH;
	icon->extent.y1 = SAVE_BUTTON_ROW_Y1;
	icon->extent.y0 = SAVE_BUTTON_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "Save";
	icon->data.indirected_text.validation = save_go_validation;
	icon->data.indirected_text.size = 6;

	icon = &save_def.icons[ICON_SAVE_CANCEL];
	icon->extent.x0 = SAVE_CANCEL_X0;
	icon->extent.x1 = SAVE_CANCEL_X0 + BUTTON_WIDTH;
	icon->extent.y1 = SAVE_BUTTON_ROW_Y1;
	icon->extent.y0 = SAVE_BUTTON_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "Cancel";
	icon->data.indirected_text.validation = save_cancel_validation;
	icon->data.indirected_text.size = 7;

	save_window_handle = wimp_create_window((wimp_window *) &save_def);

	/* --- Load window --- */

	load_def.visible.x0 = 150;
	load_def.visible.y0 = 150;
	load_def.visible.x1 = 150 + LOAD_WINDOW_WIDTH;
	load_def.visible.y1 = 150 + LOAD_WINDOW_HEIGHT;
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
	load_def.extent.y0 = -LOAD_WINDOW_HEIGHT;
	load_def.extent.x1 = LOAD_WINDOW_WIDTH;
	load_def.extent.y1 = 0;
	load_def.title_flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	                        wimp_ICON_VCENTRED | wimp_ICON_FILLED;
	load_def.work_flags = (wimp_icon_flags) (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
	load_def.sprite_area = wimpspriteop_AREA;
	load_def.xmin = LOAD_WINDOW_WIDTH;
	load_def.ymin = LOAD_WINDOW_HEIGHT;
	strncpy(load_def.title_data.text, "Load Game", 12);
	load_def.icon_count = LOAD_ICON_COUNT;

	icon = &load_def.icons[ICON_LOAD_PATH];
	icon->extent.x0 = LOAD_PATH_X0;
	icon->extent.x1 = LOAD_PATH_X0 + PATH_WIDTH;
	icon->extent.y1 = LOAD_ROW_Y1;
	icon->extent.y0 = LOAD_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_FILLED | wimp_ICON_VCENTRED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_WHITE << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_WRITABLE << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = load_path;
	icon->data.indirected_text.validation = "";
	icon->data.indirected_text.size = PATH_BUF_LEN;

	icon = &load_def.icons[ICON_LOAD_GO];
	icon->extent.x0 = LOAD_GO_X0;
	icon->extent.x1 = LOAD_GO_X0 + BUTTON_WIDTH;
	icon->extent.y1 = LOAD_BUTTON_ROW_Y1;
	icon->extent.y0 = LOAD_BUTTON_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "Load";
	icon->data.indirected_text.validation = load_go_validation;
	icon->data.indirected_text.size = 6;

	icon = &load_def.icons[ICON_LOAD_CANCEL];
	icon->extent.x0 = LOAD_CANCEL_X0;
	icon->extent.x1 = LOAD_CANCEL_X0 + BUTTON_WIDTH;
	icon->extent.y1 = LOAD_BUTTON_ROW_Y1;
	icon->extent.y0 = LOAD_BUTTON_ROW_Y0;
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "Cancel";
	icon->data.indirected_text.validation = load_cancel_validation;
	icon->data.indirected_text.size = 7;

	load_window_handle = wimp_create_window((wimp_window *) &load_def);
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

void save_view_open(void)
{
	open_window(save_window_handle);
	wimp_set_caret_position(save_window_handle, ICON_SAVE_PATH, 0, 0, -1,
	                         (int) strlen(save_path));
}

void load_view_open(void)
{
	open_window(load_window_handle);
	wimp_set_caret_position(load_window_handle, ICON_LOAD_PATH, 0, 0, -1,
	                         (int) strlen(load_path));
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
	if (pointer->i == ICON_SAVE_FILE) {
		/* Only a genuine drag (button held past the ~0.2s threshold,
		 * see include/save_view.h's doc comment) starts the Wimp_DragBox
		 * outline -- a plain click on this icon (buttons ==
		 * wimp_CLICK_SELECT) is simply ignored, it has no other purpose. */
		if (pointer->buttons == wimp_DRAG_SELECT) {
			wimp_window_state state;
			wimp_drag drag;
			int origin_x, origin_y;

			state.w = save_window_handle;
			wimp_get_window_state(&state);
			origin_x = state.visible.x0 - state.xscroll;
			origin_y = state.visible.y1 - state.yscroll;

			drag.w = save_window_handle;
			drag.type = wimp_DRAG_USER_FIXED;
			drag.initial.x0 = origin_x + SAVE_FILE_X0;
			drag.initial.x1 = origin_x + SAVE_FILE_X0 + FILE_ICON_SIZE;
			drag.initial.y0 = origin_y + SAVE_ROW_Y0;
			drag.initial.y1 = origin_y + SAVE_ROW_Y1;
			/* Generous fixed screen bounds -- real screen coordinates
			 * never come close to this, and Wimp_DragBox needs *some*
			 * bbox even though we don't want the drag meaningfully
			 * constrained. */
			drag.bbox.x0 = -16384;
			drag.bbox.y0 = -16384;
			drag.bbox.x1 = 16384;
			drag.bbox.y1 = 16384;

			wimp_drag_box(&drag);
		}
		return;
	}

	if (pointer->i == ICON_SAVE_GO) {
		if (game_view_save_to_path(save_path))
			wimp_close_window(save_window_handle);
		/* On failure, leave the dialogue open so the path can be
		 * corrected and retried -- see the debug Log for why it
		 * failed (game_view_save_to_path() logs there). */
		return;
	}

	if (pointer->i == ICON_SAVE_CANCEL) {
		wimp_close_window(save_window_handle);
		return;
	}
}

void load_view_click(wimp_pointer *pointer)
{
	if (pointer->i == ICON_LOAD_GO) {
		if (game_view_load_from_path(load_path))
			wimp_close_window(load_window_handle);
		return;
	}

	if (pointer->i == ICON_LOAD_CANCEL) {
		wimp_close_window(load_window_handle);
		return;
	}
}

void save_view_key_pressed(wimp_key *key)
{
	if (key->c == wimp_KEY_RETURN && key->i == ICON_SAVE_PATH) {
		if (game_view_save_to_path(save_path))
			wimp_close_window(save_window_handle);
		return;
	}
	wimp_process_key(key->c);
}

void load_view_key_pressed(wimp_key *key)
{
	if (key->c == wimp_KEY_RETURN && key->i == ICON_LOAD_PATH) {
		if (game_view_load_from_path(load_path))
			wimp_close_window(load_window_handle);
		return;
	}
	wimp_process_key(key->c);
}

void save_view_drag_ended(wimp_dragged *dragged)
{
	wimp_pointer pointer;
	wimp_message msg;

	(void) dragged; /* the final outline box itself isn't needed -- only
	                  * where the pointer ended up matters, via
	                  * Wimp_GetPointerInfo below. */

	wimp_get_pointer_info(&pointer);
	if (pointer.w == save_window_handle)
		return; /* dropped back on the Save dialogue itself -- no-op */

	msg.size = sizeof(wimp_message);
	msg.your_ref = 0;
	msg.action = message_DATA_SAVE;
	msg.data.data_xfer.w = pointer.w;
	msg.data.data_xfer.i = pointer.i;
	msg.data.data_xfer.pos = pointer.pos;
	msg.data.data_xfer.est_size = GAME_VIEW_SAVE_FILE_SIZE;
	msg.data.data_xfer.file_type = ARCHILUDO_SAVE_FILETYPE;
	strncpy(msg.data.data_xfer.file_name, "ArchiLudoGame",
	        sizeof(msg.data.data_xfer.file_name) - 1);
	msg.data.data_xfer.file_name[sizeof(msg.data.data_xfer.file_name) - 1] = '\0';

	/* Recorded (18, not 17) -- the reply (Message_DataSaveAck) is what
	 * actually tells us where to write the file, so it's required, not
	 * optional; see riscos_wimp_reference.md's Messages section. */
	wimp_send_message_to_window(wimp_USER_MESSAGE_RECORDED, &msg, pointer.w, pointer.i);

	drag_pending = 1;
	drag_my_ref = msg.my_ref;
}

void save_view_message_received(wimp_message *message)
{
	if (message->action == message_DATA_SAVE_ACK) {
		if (!drag_pending || message->your_ref != drag_my_ref)
			return; /* not a reply to a drag this module started */
		drag_pending = 0;

		if (game_view_save_to_path(message->data.data_xfer.file_name)) {
			/* Step 3 of the save protocol: acknowledge with
			 * Message_DataLoad so the receiver (Filer, typically) knows
			 * the file now genuinely exists at that path -- see
			 * riscos_wimp_reference.md's "Save protocol" section. */
			wimp_message reply = *message;

			reply.your_ref = message->my_ref;
			reply.action = message_DATA_LOAD;
			wimp_send_message_to_window(wimp_USER_MESSAGE, &reply,
			                             message->data.data_xfer.w,
			                             message->data.data_xfer.i);
		}
		return;
	}

	if (message->action == message_DATA_LOAD && message->your_ref == 0) {
		/* Unsolicited -- a file dragged in from Filer, per
		 * riscos_wimp_reference.md's "Load protocol" section. Only
		 * accepted if it landed on the game window itself; other
		 * windows of ours have no reason to receive a dropped file. */
		if (message->data.data_xfer.w != game_view_window_handle())
			return;

		if (game_view_load_from_path(message->data.data_xfer.file_name)) {
			wimp_message reply = *message;

			reply.your_ref = message->my_ref;
			reply.action = message_DATA_LOAD_ACK;
			wimp_send_message_to_window(wimp_USER_MESSAGE, &reply,
			                             message->data.data_xfer.w,
			                             message->data.data_xfer.i);
		}
		return;
	}
}
