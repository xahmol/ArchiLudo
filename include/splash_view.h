#ifndef SPLASH_VIEW_H
#define SPLASH_VIEW_H

#include "oslib/wimp.h"

/*
 * ArchiLudo splash view
 * =======================
 *
 * The startup/about window: the "idi8b" (I Dream In 8 Bits) logo, title,
 * version, author, and URL -- matching GeoLudo's own "About"-style splash
 * (`/home/xahmol/git/ludo/GEOS/screenshots/ludo-splash-c64.png`), shown
 * once automatically when the app starts and reachable again afterwards
 * from the iconbar menu.
 *
 * The logo itself is drawn as a small grid of coloured `os_plot`
 * rectangles (see splash_view.c's `logo_rects[]`), not a sprite --
 * unlike the pawn/dice/icon art elsewhere in this project (see
 * `docs/ARCHITECTURE.md`'s "Current rendering approach" section),
 * this logo is flat-colour blocky pixel art with no per-pixel detail a
 * primitive can't express, so there was no need to switch it over. The
 * rectangle data
 * is a pixel-exact decode of the real source art (a PETSCII "Petmate"
 * file, decoded against a real C64 character ROM dump -- there are no
 * genuine curves to approximate, every letter is built from flat
 * vertical bars) -- see that file's top-of-file comment for the full
 * extraction writeup.
 */

/*
 * Function: splash_view_initialise
 * Summary: Create (but do not open) the splash/About window. Call once
 *          during application startup.
 * Syntax:  void splash_view_initialise(void);
 * Input:   none.
 * Output:  none. The window exists (closed) and is ready for
 *          splash_view_open().
 */
void splash_view_initialise(void);

/*
 * Function: splash_view_open
 * Summary: Open the splash/About window. `go_to_new_game_on_dismiss`
 *          controls what happens when the player then dismisses it
 *          (splash_view_click()): the automatic launch splash should
 *          lead straight into the New Game dialogue, but a later,
 *          player-triggered reopen via the iconbar's About menu entry
 *          must never do that -- doing so could interrupt (or, if the
 *          player had already cancelled out of a first New Game
 *          attempt without starting, unexpectedly reopen) a session
 *          the player didn't ask to touch. Pass 1 only from the one
 *          startup call site; every other call (the About menu entry)
 *          passes 0.
 * Syntax:  void splash_view_open(int go_to_new_game_on_dismiss);
 * Input:   go_to_new_game_on_dismiss - 1 for the automatic launch
 *                                      splash, 0 for every other call.
 * Output:  none.
 */
void splash_view_open(int go_to_new_game_on_dismiss);

/*
 * Function: splash_view_window_handle
 * Summary: The splash/About window's handle, for src/main.c's
 *          Wimp_Poll dispatch to compare against.
 * Syntax:  wimp_w splash_view_window_handle(void);
 * Input:   none.
 * Output:  the window handle created by splash_view_initialise().
 */
wimp_w splash_view_window_handle(void);

/*
 * Function: splash_view_redraw
 * Summary: Handle a Redraw_Window_Request for this window: fills the
 *          background and plots the `logo_rects[]` PETSCII logo, one
 *          filled os_plot rectangle per redraw-list entry.
 * Syntax:  void splash_view_redraw(wimp_draw *redraw);
 * Input:   redraw - the Redraw_Window_Request event block, already
 *                   Wimp_RedrawWindow-filled by the caller.
 * Output:  none.
 */
void splash_view_redraw(wimp_draw *redraw);

/*
 * Function: splash_view_click
 * Summary: Handle a Mouse_Click reported for this window -- clicking
 *          anywhere in it (OK icon, background, or logo) dismisses it.
 *          If this was the automatic launch splash
 *          (splash_view_open()'s go_to_new_game_on_dismiss was 1), also
 *          opens the New Game dialogue.
 * Syntax:  void splash_view_click(wimp_pointer *pointer);
 * Input:   pointer - the Mouse_Click event block.
 * Output:  none.
 */
void splash_view_click(wimp_pointer *pointer);

#endif
