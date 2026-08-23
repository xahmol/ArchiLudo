#ifndef SAVE_VIEW_H
#define SAVE_VIEW_H

#include "oslib/wimp.h"

/*
 * ArchiLudo save/load view
 * ==========================
 *
 * Two small dialogue windows -- Save and Load -- plus the RISC OS
 * drag-and-drop file-transfer protocol built on top of them, per
 * explicit user request for GEOS-parity save/load
 * (`GEOS/src/main.c`'s `fileLoad`/`fileSave`/`fileAutosaveToggle` menu
 * items) -- see docs/ARCHITECTURE.md's Round 7.1 notes for why this is
 * drag-and-drop rather than a literal port of GEOS's own directory-
 * listing file picker.
 *
 * Both dialogues also work without any dragging at all: each has a
 * writable pathname icon -- typing a full path and clicking Save/Load
 * (or pressing Return) saves/loads directly, exactly like the pathname
 * field in a standard RISC OS Save box. The Save dialogue additionally
 * has a small draggable "file" icon (button type CLICK_DRAG, see the
 * RISC OS 3 PRM's "Icon button types" table) for the idiomatic drag-to-
 * Filer flow: dragging it onto a Filer window (or any other task's
 * window) starts the standard Message_DataSave / DataSaveAck / DataLoad
 * handshake (see riscos_wimp_reference.md's "Save protocol" section).
 * Loading the other direction -- dragging an existing save file's Filer
 * icon onto ArchiLudo's game window -- is handled the same way, via an
 * unsolicited Message_DataLoad (your_ref == 0); this doesn't need the
 * Load dialogue open at all, matching ordinary RISC OS drag-in
 * conventions.
 *
 * The actual file format and read/write functions live in
 * src/game_view.c (game_view_save_to_path()/game_view_load_from_path())
 * -- this module only owns the two dialogue windows and the WIMP-level
 * drag/message plumbing around them, not the save format itself.
 *
 * Not implemented: Message_DataOpen (double-click a save file in Filer
 * to launch/resume ArchiLudo directly) -- that needs the save filetype
 * registered with the OS's file-type/application association system
 * (a !Boot-time `*Set Alias$@RunType_xxx` or Filer !Boot entry), which
 * this project doesn't set up. Dragging onto an already-running
 * ArchiLudo's game window (this module's `message_DATA_LOAD` handling)
 * works regardless.
 */

/*
 * Function: save_view_initialise
 * Summary: Create both the Save and Load dialogue windows (closed).
 *          Call once during application startup, after
 *          game_view_initialise() (the default pathname pre-filled into
 *          each dialogue is built from game_view_app_dir()).
 * Syntax:  void save_view_initialise(void);
 * Input:   none.
 * Output:  none.
 */
void save_view_initialise(void);

/*
 * Function: save_view_open / load_view_open
 * Summary: Open (or bring to the front, if already open) the Save/Load
 *          dialogue.
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
 * Summary: Handle a Mouse_Click event in the Save/Load dialogue: the
 *          Save/Load button (direct path-based save/load), Cancel
 *          (closes the window), or -- Save dialogue only -- a
 *          CLICK_DRAG-type click on the draggable file icon, which
 *          starts a Wimp_DragBox outline drag (completed later via
 *          save_view_drag_ended()).
 * Syntax:  void save_view_click(wimp_pointer *pointer);
 *          void load_view_click(wimp_pointer *pointer);
 * Input:   pointer - the block from Wimp_Poll for a Mouse_Click event.
 * Output:  none.
 */
void save_view_click(wimp_pointer *pointer);
void load_view_click(wimp_pointer *pointer);

/*
 * Function: save_view_key_pressed / load_view_key_pressed
 * Summary: Handle a Key_Pressed event in the Save/Load dialogue: Return
 *          in the pathname field triggers the same action as clicking
 *          Save/Load; anything else passes through to
 *          Wimp_ProcessKey() for ordinary text editing.
 * Syntax:  void save_view_key_pressed(wimp_key *key);
 *          void load_view_key_pressed(wimp_key *key);
 * Input:   key - the block from Wimp_Poll for a Key_Pressed event.
 * Output:  none.
 */
void save_view_key_pressed(wimp_key *key);
void load_view_key_pressed(wimp_key *key);

/*
 * Function: save_view_drag_ended
 * Summary: Handle a User_Drag_Box event -- completes a drag started by
 *          save_view_click() on the Save dialogue's file icon: finds
 *          what's under the pointer now (Wimp_GetPointerInfo) and, if
 *          it's not the Save dialogue itself, sends Message_DataSave to
 *          start the drag-and-drop handshake (see
 *          save_view_message_received()). A no-op if no drag from this
 *          module is actually in progress (e.g. some other User_Drag_Box
 *          this module didn't start -- shouldn't happen given ArchiLudo
 *          starts no other drags, but checked defensively).
 * Syntax:  void save_view_drag_ended(wimp_dragged *dragged);
 * Input:   dragged - the block from Wimp_Poll for a User_Drag_Box event.
 * Output:  none.
 */
void save_view_drag_ended(wimp_dragged *dragged);

/*
 * Function: save_view_message_received
 * Summary: Handle a User_Message/User_Message_Recorded whose action is
 *          Message_DataSaveAck (continuing a save-drag this module
 *          started -- writes the file, replies Message_DataLoad) or
 *          Message_DataLoad with your_ref == 0 (an unsolicited drag-in
 *          from Filer onto one of ArchiLudo's own windows -- loads the
 *          file, replies Message_DataLoadAck). main.c routes both here
 *          without needing to know the protocol detail itself; this
 *          function checks the action/your_ref and is a no-op for
 *          anything else.
 * Syntax:  void save_view_message_received(wimp_message *message);
 * Input:   message - the block from Wimp_Poll for the message event.
 * Output:  none.
 */
void save_view_message_received(wimp_message *message);

#endif
