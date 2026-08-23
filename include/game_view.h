#ifndef GAME_VIEW_H
#define GAME_VIEW_H

#include "oslib/wimp.h"
#include "game_logic.h"

/* Max characters (including the terminator) in a configured player name
 * -- see game_view_configure_players(). Deliberately short: the name
 * line's panel width only comfortably fits about a dozen characters at
 * the system font's fixed 16-units/character width (see game_view.c's
 * PANEL_WIDTH). */
#define GAME_VIEW_NAME_LEN 12

/*
 * ArchiLudo game view
 * ====================
 *
 * The WIMP-side board window: creation, redraw (board cells + pawns),
 * and mouse click handling, all driven by src/game_logic.c's state via
 * src/board_layout.c's grid mapping. This is the Phase 1 placeholder
 * presentation (see docs/ARCHITECTURE.md's Roadmap) -- board cells are
 * plotted as flat colour-filled rectangles and pawns as the small
 * placeholder sprites from assets/Sprites (see
 * assets/generate_placeholder_art.py); Phase 2 replaces the art without
 * needing to change this module's structure.
 *
 * Kept separate from src/main.c so main.c stays the thin task-lifecycle
 * shell (Wimp_Initialise / Wimp_Poll loop / iconbar / Message_Quit) while
 * this module owns everything specific to the one game window.
 */

/*
 * Function: game_view_initialise
 * Summary: Create the game window (closed) and load the pawn sprites
 *          used to draw it. Call once during application startup.
 * Syntax:  void game_view_initialise(const char *argv0);
 * Input:   argv0 - main()'s argv[0], the full RISC OS pathname the
 *                  program was invoked as (e.g. "HostFS:$.ArchiLudo").
 *                  Used to build absolute paths for "Sprites" and the
 *                  debug log, since the current selected directory at
 *                  launch isn't reliable -- see docs/ARCHITECTURE.md's
 *                  Phase 1 implementation notes, "Round 4".
 * Output:  none.
 */
void game_view_initialise(const char *argv0);

/*
 * Function: game_view_open
 * Summary: Open (or bring to the front, if already open) the game
 *          window, starting a new game if one isn't already in progress.
 * Syntax:  void game_view_open(void);
 * Input:   none.
 * Output:  none.
 */
void game_view_open(void);

/*
 * Function: game_view_new_game
 * Summary: Reset the game to a fresh start (all pawns home, player 0 to
 *          move), force a redraw of the window if it's open, and play
 *          out any leading AI turns (see game_view_configure_players())
 *          immediately -- so if player 0 is AI-controlled, their first
 *          turn happens without the human needing to click anything.
 * Syntax:  void game_view_new_game(void);
 * Input:   none.
 * Output:  none.
 */
void game_view_new_game(void);

/*
 * Function: game_view_configure_players
 * Summary: Set each player's display name and whether they're
 *          human-controlled or AI-controlled, per src/setup_view.c's
 *          "New Game" dialogue. Takes effect immediately (the name/AI
 *          status is read live wherever it's needed -- the panel's name
 *          line, and whether a turn should be played automatically) but
 *          does *not* itself reset the game in progress -- call
 *          game_view_new_game() afterwards for that (setup_view.c's
 *          Start button does both).
 * Syntax:  void game_view_configure_players(
 *              const char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
 *              const int is_ai[LUDO_PLAYERS]);
 * Input:   names - one GAME_VIEW_NAME_LEN-byte buffer per player; an
 *                  empty string leaves that player's default colour name
 *                  (e.g. "GREEN") in place rather than showing blank.
 *          is_ai - one flag per player: 0 = human (waits for Throw/board
 *                  clicks as normal), non-zero = AI-controlled (see
 *                  include/ai.h; plays automatically via
 *                  ludo_ai_choose_pawn() whenever it becomes their turn).
 * Output:  none.
 */
void game_view_configure_players(const char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
                                  const int is_ai[LUDO_PLAYERS]);

/*
 * Function: game_view_window_handle
 * Summary: The game window's handle, for main.c to recognise Wimp_Poll
 *          events (redraw/click) that belong to this window.
 * Syntax:  wimp_w game_view_window_handle(void);
 * Input:   none.
 * Output:  the window handle, or wimp_w equivalent of -1 if
 *          game_view_initialise() hasn't been called yet.
 */
wimp_w game_view_window_handle(void);

/*
 * Function: game_view_redraw
 * Summary: Handle a Redraw_Window_Request for the game window: runs the
 *          full Wimp_RedrawWindow/Wimp_GetRectangle loop itself.
 * Syntax:  void game_view_redraw(wimp_draw *redraw);
 * Input:   redraw - the block from Wimp_Poll, with only the window
 *                   handle field (w) filled in, as required by
 *                   Wimp_RedrawWindow.
 * Output:  none.
 */
void game_view_redraw(wimp_draw *redraw);

/*
 * Function: game_view_click
 * Summary: Handle a Mouse_Click event that occurred in the game window:
 *          rolling the dice (Throw icon), or choosing which pawn to move
 *          if the player clicks a board cell containing one of their
 *          currently-movable pawns.
 * Syntax:  void game_view_click(wimp_pointer *pointer);
 * Input:   pointer - the block from Wimp_Poll for a Mouse_Click event.
 * Output:  none.
 */
void game_view_click(wimp_pointer *pointer);

#endif
