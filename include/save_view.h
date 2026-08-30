#ifndef SAVE_VIEW_H
#define SAVE_VIEW_H

#include "oslib/wimp.h"

/*
 * ArchiLudo save/load view
 * ==========================
 *
 * Two small dialogue windows -- Save and Load -- each listing 5 fixed
 * save slots, replacing an earlier free-form pathname/drag-and-drop
 * design: drag-and-drop never reliably completed a single live
 * save/load round-trip across extensive Arculator testing (see
 * docs/ARCHITECTURE.md's "Decisions made and not revisited" section --
 * the message send always appeared to succeed at the SWI level,
 * including a direct icon-bar drop, but no DataSaveAck reply ever
 * arrived, most likely because Arculator's HostFS bridge doesn't
 * implement the Filer-side of that protocol at all), and typing a full
 * RISC OS pathname by hand is its own real friction for a simple board
 * game. Slots remove both problems: every slot's file lives at a fixed,
 * predictable path inside the app directory (`<ArchiLudo$Dir>.Slot1`
 * .. `.Slot5`), and each slot carries its own user-editable display name
 * as part of the save data itself (see game_view.h's
 * GAME_VIEW_SLOT_NAME_LEN / game_view_peek_slot_name()) -- so what the
 * dialogues show is never just a bare filename.
 *
 * Save dialogue: each row has a WRITABLE name field (pre-filled with the
 * slot's existing name if occupied, or "Slot N" if empty) and a Save
 * button -- click Save on any row to write the current game into that
 * slot under whatever name is currently in its field, overwriting
 * whatever was there before. Load dialogue: each row shows the slot's
 * name as plain read-only text ("(empty)" if unoccupied, with its Load
 * button shaded/disabled) and a Load button.
 *
 * The actual file format and read/write functions live in
 * src/game_view.c (game_view_save_to_path()/game_view_load_from_path()/
 * game_view_peek_slot_name()) -- this module only owns the two dialogue
 * windows and the fixed slot pathnames, not the save format itself.
 */

/*
 * Function: save_view_initialise
 * Summary: Create both the Save and Load dialogue windows (closed).
 *          Call once during application startup, after
 *          game_view_initialise() (the slot pathnames are built from
 *          game_view_app_dir()).
 * Syntax:  void save_view_initialise(void);
 * Input:   none.
 * Output:  none.
 */
void save_view_initialise(void);

/*
 * Function: save_view_open / load_view_open
 * Summary: Open (or bring to the front, if already open) the Save/Load
 *          dialogue, first refreshing all 5 slot rows from whatever is
 *          actually on disc right now (game_view_peek_slot_name() for
 *          each of the 5 fixed slot paths) -- so the list is never stale
 *          even if a slot was written by an earlier Save since this
 *          dialogue last opened.
 * Syntax:  void save_view_open(void); void load_view_open(void);
 * Input:   none.
 * Output:  none.
 */
void save_view_open(void);
void load_view_open(void);

/*
 * Function: save_view_window_handle / load_view_window_handle
 * Summary: The two dialogues' window handles, for main.c to recognise
 *          Wimp_Poll events (redraw/click/key) that belong to them.
 * Syntax:  wimp_w save_view_window_handle(void);
 *          wimp_w load_view_window_handle(void);
 * Input:   none.
 * Output:  the window handle, or wimp_w equivalent of -1 if
 *          save_view_initialise() hasn't been called yet.
 */
wimp_w save_view_window_handle(void);
wimp_w load_view_window_handle(void);

/*
 * Function: save_view_redraw / load_view_redraw
 * Summary: Handle a Redraw_Window_Request for the Save/Load dialogue:
 *          runs the full Wimp_RedrawWindow/Wimp_GetRectangle loop
 *          itself (both windows are plain Wimp icons, no custom
 *          drawing, so this just drains the rectangle list).
 * Syntax:  void save_view_redraw(wimp_draw *redraw);
 *          void load_view_redraw(wimp_draw *redraw);
 * Input:   redraw - the block from Wimp_Poll, with only the window
 *                   handle field (w) filled in.
 * Output:  none.
 */
void save_view_redraw(wimp_draw *redraw);
void load_view_redraw(wimp_draw *redraw);

/*
 * Function: save_view_click / load_view_click
 * Summary: Handle a Mouse_Click event in the Save/Load dialogue: a
 *          per-slot Save/Load button (the writable name field itself,
 *          Save dialogue only, needs no click handling -- typing just
 *          works), or Cancel (closes the window). Save always succeeds
 *          for any slot (occupied or empty -- overwrites); Load on an
 *          empty slot's (shaded) button is a no-op.
 * Syntax:  void save_view_click(wimp_pointer *pointer);
 *          void load_view_click(wimp_pointer *pointer);
 * Input:   pointer - the block from Wimp_Poll for a Mouse_Click event.
 * Output:  none.
 */
void save_view_click(wimp_pointer *pointer);
void load_view_click(wimp_pointer *pointer);

/*
 * Function: save_view_key_pressed / load_view_key_pressed
 * Summary: Handle a Key_Pressed event in the Save/Load dialogue --
 *          passes through to Wimp_ProcessKey() for ordinary text editing
 *          in the Save dialogue's writable name fields. Return has no
 *          special action (unlike the previous single-pathname design)
 *          since there's no longer one obvious "the" field it should
 *          trigger -- use the per-row Save/Load button instead.
 * Syntax:  void save_view_key_pressed(wimp_key *key);
 *          void load_view_key_pressed(wimp_key *key);
 * Input:   key - the block from Wimp_Poll for a Key_Pressed event.
 * Output:  none.
 */
void save_view_key_pressed(wimp_key *key);
void load_view_key_pressed(wimp_key *key);

#endif
