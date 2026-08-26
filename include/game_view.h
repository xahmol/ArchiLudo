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

/* Size in bytes of a saved-game file -- see game_view_save_to_path()'s
 * "Save/load" block comment in game_view.c for the layout. Exposed here
 * (rather than kept private to game_view.c) so src/save_view.c's
 * Message_DataSave drag-and-drop handshake can fill in an accurate
 * est_size field without duplicating this arithmetic. */
#define GAME_VIEW_SAVE_FILE_SIZE (4 + LUDO_PLAYERS * (GAME_VIEW_NAME_LEN + 1) + 7 \
                                 + LUDO_PLAYERS * LUDO_PAWNS * 3)

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
 *          move), force a redraw of the window if it's open, and mark
 *          the game as started (see game_view_has_started()). If player
 *          0 is AI-controlled (see game_view_configure_players()), their
 *          turn does *not* start automatically -- every AI action, even
 *          a fresh turn's first roll, waits for a Continue click (the
 *          Throw icon, relabelled -- per explicit user request).
 * Syntax:  void game_view_new_game(void);
 * Input:   none.
 * Output:  none.
 */
void game_view_new_game(void);

/*
 * Function: game_view_has_started
 * Summary: Whether a game has actually been started yet via
 *          game_view_new_game() (i.e. via src/setup_view.c's "New Game"
 *          dialogue's Start button). Lets main.c tell a first-ever
 *          iconbar click -- which must open src/setup_view.c to ask for
 *          player details first, per explicit user request -- apart from
 *          a later one, which just reopens/refocuses the game already in
 *          progress.
 * Syntax:  int game_view_has_started(void);
 * Input:   none.
 * Output:  1 if a game has been started, 0 otherwise.
 */
int game_view_has_started(void);

/*
 * Function: game_view_win_continue
 * Summary: Resume normal turn-based play after src/win_view.c's win-choice
 *          dialogue has been dismissed (either button -- "Continue" leaves
 *          the game exactly where it is, "New Game" calls this too before
 *          opening src/setup_view.c, so its defaults still reflect the
 *          just-finished game's live player configuration). Marks the
 *          current winner as acknowledged, so refresh_status()/
 *          game_view_click() stop treating game.winner != -1 as "paused,
 *          waiting for a choice" and resume ordinary per-turn behaviour --
 *          see game_view.c's win_acknowledged doc comment. Idempotent
 *          (safe to call when there's nothing to acknowledge).
 * Syntax:  void game_view_win_continue(void);
 * Input:   none.
 * Output:  none.
 */
void game_view_win_continue(void);

/*
 * Function: game_view_get_players
 * Summary: The current (or, for a just-finished game, most recent)
 *          player configuration -- display names and human/AI status --
 *          for src/setup_view.c's "New Game" dialogue to default to. Per
 *          explicit user request ("for new game dialogue, defaults always
 *          should be the in progress game, unless we just started a new
 *          one"): setup_view_open() calls this every time it's opened
 *          (skipped only if game_view_has_started() is still 0, i.e. no
 *          game has ever been configured yet, in which case setup_view.c
 *          keeps its own hardcoded first-run defaults).
 * Syntax:  void game_view_get_players(
 *              char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
 *              int is_ai[LUDO_PLAYERS]);
 * Input:   none.
 * Output:  names - filled with each player's current DISPLAY name (the
 *                  configured custom name if one was set, otherwise the
 *                  default colour name, e.g. "GREEN") -- never empty, so
 *                  the caller can copy it straight into a writable icon.
 *          is_ai - filled with each player's current human(0)/AI(nonzero)
 *                  status.
 */
void game_view_get_players(char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
                            int is_ai[LUDO_PLAYERS]);

/*
 * Function: game_view_poll_idle
 * Summary: Advance whichever animation is currently running (a dice-roll
 *          or pawn-move animation, see src/game_view.c's turn_step) and
 *          poll the pointer for the movable-pawn hover-destination
 *          highlight -- called by main.c on every Wimp_Poll
 *          Null_Reason_Code (idle) event. A no-op on most calls (each
 *          animation/poll step is throttled against the real-time clock
 *          internally), so safe to call unconditionally every idle poll.
 * Syntax:  void game_view_poll_idle(void);
 * Input:   none.
 * Output:  none.
 */
void game_view_poll_idle(void);

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
 *                  include/ai.h; picks its pawn automatically via
 *                  ludo_ai_choose_pawn() whenever it becomes their turn,
 *                  but still waits for a Continue click -- the Throw icon,
 *                  relabelled -- before each roll, per explicit user
 *                  request that AI turns never advance on their own).
 * Output:  none.
 */
void game_view_configure_players(const char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
                                  const int is_ai[LUDO_PLAYERS]);

/*
 * Function: game_view_app_dir
 * Summary: This program's own directory (see game_view_initialise()'s
 *          argv0 handling), for src/save_view.c to build a sensible
 *          default Save/Load pathname against, the same way
 *          game_view.c's own debug log already does.
 * Syntax:  const char *game_view_app_dir(void);
 * Input:   none.
 * Output:  the directory path (no trailing "."), or "" if
 *          game_view_initialise() hasn't been called yet.
 */
const char *game_view_app_dir(void);

/*
 * Function: game_view_save_to_path
 * Summary: Write the current game (player names/AI settings and full
 *          board state) to a file at the given path -- see
 *          src/game_view.c's "Save/load" block comment for the format.
 * Syntax:  int game_view_save_to_path(const char *path);
 * Input:   path - a full RISC OS pathname to write to.
 * Output:  1 on success, 0 on failure (see the debug Log for why).
 */
int game_view_save_to_path(const char *path);

/*
 * Function: game_view_load_from_path
 * Summary: Replace the current game with one loaded from a file
 *          previously written by game_view_save_to_path(), and redraw
 *          the game window if it's open. Marks the game as started (see
 *          game_view_has_started()) so a first-ever iconbar click after
 *          loading reopens the game rather than asking for setup again.
 * Syntax:  int game_view_load_from_path(const char *path);
 * Input:   path - a full RISC OS pathname to read from.
 * Output:  1 on success, 0 on failure (not a valid ArchiLudo save, or
 *          the file couldn't be read -- see the debug Log for which).
 */
int game_view_load_from_path(const char *path);

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
