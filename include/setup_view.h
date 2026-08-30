#ifndef SETUP_VIEW_H
#define SETUP_VIEW_H

#include "oslib/wimp.h"
#include "game_logic.h"

/*
 * ArchiLudo setup view
 * =====================
 *
 * The "New Game" dialogue: one row per player with a colour swatch, a
 * writable name field, and a click-to-toggle Human/AI button, plus
 * Start, Rules..., Load and Cancel. Start applies the settings to
 * src/game_view.c (see game_view_configure_players()/
 * game_view_configure_rules()) and starts a fresh game; Rules... opens
 * src/rules_view.c's "Rule Options" dialogue to pick a rule-set variant
 * and override individual house-rule toggles; Load skips this
 * dialogue's own setup entirely and opens src/save_view.c's Load
 * dialogue instead; Cancel closes the dialogue without changing
 * anything.
 *
 * Kept as its own module (its own window, own icons, own click/redraw/
 * key handling) rather than folded into game_view.c or main.c, matching
 * how this project already keeps one window per source file -- see
 * docs/ARCHITECTURE.md's Layering section.
 */

/*
 * Function: setup_view_initialise
 * Summary: Create the setup window (closed). Call once during
 *          application startup, after game_view_initialise().
 * Syntax:  void setup_view_initialise(void);
 * Input:   none.
 * Output:  none.
 */
void setup_view_initialise(void);

/*
 * Function: setup_view_open
 * Summary: Open (or bring to the front, if already open) the setup
 *          window and place the caret in the first name field. Each
 *          field already shows whatever it was last set to -- this
 *          module's own defaults on first use, or the user's own
 *          previous edits/toggles from an earlier time this window was
 *          open -- since this is the only place player configuration is
 *          ever set (see game_view_configure_players()), there's nothing
 *          else to sync from.
 * Syntax:  void setup_view_open(void);
 * Input:   none.
 * Output:  none.
 */
void setup_view_open(void);

/*
 * Function: setup_view_window_handle
 * Summary: The setup window's handle, for main.c to recognise Wimp_Poll
 *          events (redraw/click/key) that belong to this window.
 * Syntax:  wimp_w setup_view_window_handle(void);
 * Input:   none.
 * Output:  the window handle, or wimp_w equivalent of -1 if
 *          setup_view_initialise() hasn't been called yet.
 */
wimp_w setup_view_window_handle(void);

/*
 * Function: setup_view_redraw
 * Summary: Handle a Redraw_Window_Request for the setup window.
 * Syntax:  void setup_view_redraw(wimp_draw *redraw);
 * Input:   redraw - the block from Wimp_Poll, with only the window
 *                   handle field (w) filled in, as required by
 *                   Wimp_RedrawWindow.
 * Output:  none.
 */
void setup_view_redraw(wimp_draw *redraw);

/*
 * Function: setup_view_click
 * Summary: Handle a Mouse_Click event in the setup window: toggling a
 *          Human/AI button, or Start/Rules.../Load/Cancel.
 * Syntax:  void setup_view_click(wimp_pointer *pointer);
 * Input:   pointer - the block from Wimp_Poll for a Mouse_Click event.
 * Output:  none.
 */
void setup_view_click(wimp_pointer *pointer);

/*
 * Function: setup_view_configure_rules
 * Summary: Called by src/rules_view.c when its OK button is clicked --
 *          stores the edited ludo_rules as this dialogue's own pending
 *          rules (the same way pending player names/AI settings are
 *          held directly in this module's own buffers), applied to the
 *          actual game only when Start is next clicked.
 * Syntax:  void setup_view_configure_rules(const ludo_rules *rules);
 * Input:   rules - the rules to adopt as pending. Copied by value.
 * Output:  none.
 */
void setup_view_configure_rules(const ludo_rules *rules);

/*
 * Function: setup_view_key_pressed
 * Summary: Handle a Key_Pressed event while the caret is in the setup
 *          window -- passes it straight to wimp_process_key() so the
 *          Wimp performs its own default text-entry handling for
 *          whichever writable name icon currently has the caret (this
 *          project doesn't intercept any keys specially here, e.g. no
 *          Return-moves-to-next-field chaining yet -- see
 *          riscos_wimp_reference.md's Icons section for the K-command
 *          validation-string alternative if that's wanted later).
 * Syntax:  void setup_view_key_pressed(wimp_key *key);
 * Input:   key - the block from Wimp_Poll for a Key_Pressed event.
 * Output:  none.
 */
void setup_view_key_pressed(wimp_key *key);

#endif
