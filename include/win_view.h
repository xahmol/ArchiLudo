#ifndef WIN_VIEW_H
#define WIN_VIEW_H

#include "oslib/wimp.h"

/*
 * ArchiLudo win view
 * ====================
 *
 * The "a player has won" dialogue -- shown the moment any player finishes
 * all four pawns, offering a choice between continuing the game with the
 * remaining players (the game does not stop dead at the first winner --
 * the rules engine plays out full placement, see game_logic.c) or starting
 * a fresh game (opens src/setup_view.c's "New Game" dialogue, which always
 * defaults to whatever player configuration is/was actually in progress).
 *
 * Kept as its own module (its own window, own icons, own click/redraw
 * handling) rather than folded into game_view.c or main.c, matching how
 * this project already keeps one window per source file -- see
 * docs/ARCHITECTURE.md's Layering section.
 */

/*
 * Function: win_view_initialise
 * Summary: Create the win-choice window (closed). Call once during
 *          application startup, after game_view_initialise() and
 *          setup_view_initialise().
 * Syntax:  void win_view_initialise(void);
 * Input:   none.
 * Output:  none.
 */
void win_view_initialise(void);

/*
 * Function: win_view_open
 * Summary: Open (or bring to the front, if already open) the win-choice
 *          window, showing the given message. Safe to call again while
 *          already open (e.g. from a duplicate after_settle() call before
 *          the user has responded) -- just re-opens/refreshes in place.
 * Syntax:  void win_view_open(const char *message);
 * Input:   message - the text to show, e.g. "GREEN WINS!" -- copied into
 *                    this module's own buffer, so the caller's string
 *                    doesn't need to outlive the call.
 * Output:  none.
 */
void win_view_open(const char *message);

/*
 * Function: win_view_window_handle
 * Summary: The win-choice window's handle, for main.c to recognise
 *          Wimp_Poll events (redraw/click) that belong to this window.
 * Syntax:  wimp_w win_view_window_handle(void);
 * Input:   none.
 * Output:  the window handle, or wimp_w equivalent of -1 if
 *          win_view_initialise() hasn't been called yet.
 */
wimp_w win_view_window_handle(void);

/*
 * Function: win_view_redraw
 * Summary: Handle a Redraw_Window_Request for the win-choice window.
 * Syntax:  void win_view_redraw(wimp_draw *redraw);
 * Input:   redraw - the block from Wimp_Poll, with only the window
 *                   handle field (w) filled in, as required by
 *                   Wimp_RedrawWindow.
 * Output:  none.
 */
void win_view_redraw(wimp_draw *redraw);

/*
 * Function: win_view_click
 * Summary: Handle a Mouse_Click event in the win-choice window: Continue
 *          (closes the dialogue and lets game_view.c's own turn logic
 *          resume normally -- see game_view_win_continue()) or New Game
 *          (closes the dialogue and opens src/setup_view.c's "New Game"
 *          dialogue, per explicit user request that its defaults reflect
 *          the game just finished).
 * Syntax:  void win_view_click(wimp_pointer *pointer);
 * Input:   pointer - the block from Wimp_Poll for a Mouse_Click event.
 * Output:  none.
 */
void win_view_click(wimp_pointer *pointer);

#endif
