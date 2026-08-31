#ifndef GAME_VIEW_H
#define GAME_VIEW_H

#include <stddef.h>

#include "oslib/wimp.h"
#include "game_logic.h"
#include "ai.h"

/* Max characters (including the terminator) in a configured player name
 * -- see game_view_configure_players(). Deliberately short: the name
 * line's panel width only comfortably fits about a dozen characters at
 * the system font's fixed 16-units/character width (see game_view.c's
 * PANEL_WIDTH). */
#define GAME_VIEW_NAME_LEN 12

/* Max characters (including the terminator) in a save slot's own display
 * name -- see game_view_save_to_path()/game_view_peek_slot_name(). Part
 * of the save data itself, not just a filename, so a slot's
 * label survives being loaded back and is what src/save_view.c's Save/
 * Load dialogues actually display -- see their own doc comments for the
 * 5-slot design this replaced free-form drag-and-drop pathnames with. */
#define GAME_VIEW_SLOT_NAME_LEN 32

/* Size in bytes of a saved-game file -- see game_view_save_to_path()'s
 * "Save/load" block comment in game_view.c for the layout.
 *
 * Includes the rules block (magic "ALS5") --
 * one byte per ludo_rules field (variant + 8 house-rule booleans, see
 * game_logic.h), so the chosen ruleset (which variant, which of the 8
 * toggles) survives save/load rather than reverting to MEJN defaults --
 * and GAME_VIEW_SLOT_NAME_LEN for the slot's own display name, part of
 * the 5 fixed, renamable save slots inside the app directory (see
 * docs/ARCHITECTURE.md's "Decisions made and not revisited" section for
 * why this replaced an earlier free-form drag-and-drop design). Each
 * player's block is name + is_ai + AI difficulty (one byte each), so a
 * per-player difficulty (see game_view_configure_players()) also
 * survives save/load. The "+ 7" fixed fields are current_player,
 * last_roll, tries_remaining, forced_pawn, pending_forced_pawn,
 * just_released, and winner; the trailing "+ LUDO_PLAYERS" is
 * game.finish_order (one signed byte per place, see game_logic.h),
 * so which player finished in which place also survives save/load.
 * The magic is bumped whenever the layout changes, and an older-magic
 * save is deliberately rejected rather than partially loaded -- an
 * accepted trade-off for this hobby project. */
#define GAME_VIEW_SAVE_FILE_SIZE (4 + GAME_VIEW_SLOT_NAME_LEN + 9 \
                                 + LUDO_PLAYERS * (GAME_VIEW_NAME_LEN + 2) + 7 \
                                 + LUDO_PLAYERS \
                                 + LUDO_PLAYERS * LUDO_PAWNS * 3)

/*
 * ArchiLudo game view
 * ====================
 *
 * The WIMP-side board window: creation, redraw (board cells + pawns),
 * and mouse click handling, all driven by src/game_logic.c's state via
 * src/board_layout.c's grid mapping. Board cells are `os_plot`
 * primitives; pawns are real sprites (`assets/PawnSprite`, see
 * docs/ARCHITECTURE.md's "Current rendering approach" section).
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
 *                  Used to build absolute paths for "PawnSprite" and the
 *                  debug log, since the current selected directory at
 *                  launch isn't reliable -- see docs/ARCHITECTURE.md's
 *                  "WIMP conventions and gotchas" section.
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
 *          player configuration -- display names, human/AI status, and
 *          each AI-controlled player's difficulty -- for
 *          src/setup_view.c's "New Game" dialogue to default to. Per
 *          explicit user request ("for new game dialogue, defaults always
 *          should be the in progress game, unless we just started a new
 *          one"): setup_view_open() calls this every time it's opened
 *          (skipped only if game_view_has_started() is still 0, i.e. no
 *          game has ever been configured yet, in which case setup_view.c
 *          keeps its own hardcoded first-run defaults).
 * Syntax:  void game_view_get_players(
 *              char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
 *              int is_ai[LUDO_PLAYERS],
 *              ludo_ai_difficulty difficulty[LUDO_PLAYERS]);
 * Input:   none.
 * Output:  names      - filled with each player's current DISPLAY name
 *                        (the configured custom name if one was set,
 *                        otherwise the default colour name, e.g. "GREEN")
 *                        -- never empty, so the caller can copy it
 *                        straight into a writable icon.
 *          is_ai      - filled with each player's current
 *                        human(0)/AI(nonzero) status.
 *          difficulty - filled with each player's currently configured
 *                        AI difficulty (meaningful only for a player
 *                        whose is_ai is nonzero -- a Human player's
 *                        entry is whatever was last configured for them,
 *                        not a meaningful "current" value).
 */
void game_view_get_players(char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
                            int is_ai[LUDO_PLAYERS],
                            ludo_ai_difficulty difficulty[LUDO_PLAYERS]);

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
 * Summary: Set each player's display name, whether they're
 *          human-controlled or AI-controlled, and (for an AI-controlled
 *          player) which difficulty ludo_ai_choose_pawn() should use for
 *          them, per src/setup_view.c's "New Game" dialogue. Takes
 *          effect immediately (all three are read live wherever they're
 *          needed -- the panel's name line, whether a turn should be
 *          played automatically, and which scoring tier that turn uses)
 *          but does *not* itself reset the game in progress -- call
 *          game_view_new_game() afterwards for that (setup_view.c's
 *          Start button does both).
 * Syntax:  void game_view_configure_players(
 *              const char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
 *              const int is_ai[LUDO_PLAYERS],
 *              const ludo_ai_difficulty difficulty[LUDO_PLAYERS]);
 * Input:   names      - one GAME_VIEW_NAME_LEN-byte buffer per player; an
 *                        empty string leaves that player's default colour
 *                        name (e.g. "GREEN") in place rather than showing
 *                        blank.
 *          is_ai      - one flag per player: 0 = human (waits for
 *                        Throw/board clicks as normal), non-zero =
 *                        AI-controlled (see include/ai.h; picks its pawn
 *                        automatically via ludo_ai_choose_pawn() whenever
 *                        it becomes their turn, but still waits for a
 *                        Continue click -- the Throw icon, relabelled --
 *                        before each roll, per explicit user request that
 *                        AI turns never advance on their own).
 *          difficulty - one ludo_ai_difficulty per player, used for that
 *                        player's turns whenever is_ai is nonzero for
 *                        them; ignored for a Human player.
 * Output:  none.
 */
void game_view_configure_players(const char names[LUDO_PLAYERS][GAME_VIEW_NAME_LEN],
                                  const int is_ai[LUDO_PLAYERS],
                                  const ludo_ai_difficulty difficulty[LUDO_PLAYERS]);

/*
 * Function: game_view_configure_rules
 * Summary: Set the ludo_rules a fresh game should start with, per
 *          src/rules_view.c's "Rule Options" dialogue (reached via
 *          src/setup_view.c's "Rules..." button). Mirrors
 *          game_view_configure_players()'s own two-call pattern: takes
 *          effect only the next time game_view_new_game() is called
 *          (which applies it via ludo_set_rules()), not to a game
 *          already in progress.
 * Syntax:  void game_view_configure_rules(const ludo_rules *rules);
 * Input:   rules - the rules to adopt for the next new game. Copied by
 *                  value.
 * Output:  none.
 */
void game_view_configure_rules(const ludo_rules *rules);

/*
 * Function: game_view_get_rules
 * Summary: The rules the current (or, for a just-finished game, most
 *          recent) game was actually configured with -- for
 *          src/setup_view.c's "New Game" dialogue to default to, the
 *          same "always default to the in-progress game" convention
 *          game_view_get_players() already follows.
 * Syntax:  void game_view_get_rules(ludo_rules *rules);
 * Input:   none.
 * Output:  rules - filled with the currently configured rules.
 */
void game_view_get_rules(ludo_rules *rules);

/*
 * Function: game_view_app_dir
 * Summary: This program's own directory (see game_view_initialise()'s
 *          argv0 handling), for src/save_view.c to build its 5 fixed
 *          save-slot pathnames against, the same way game_view.c's own
 *          debug log already does.
 * Syntax:  const char *game_view_app_dir(void);
 * Input:   none.
 * Output:  the directory path (no trailing "."), or "" if
 *          game_view_initialise() hasn't been called yet.
 */
const char *game_view_app_dir(void);

/*
 * Function: game_view_save_to_path
 * Summary: Write the current game (player names/AI settings and full
 *          board state) to a file at the given path, embedding `name` as
 *          the slot's own display name -- see src/game_view.c's
 *          "Save/load" block comment for the format.
 * Syntax:  int game_view_save_to_path(const char *path, const char *name);
 * Input:   path - a full RISC OS pathname to write to.
 *          name - the slot's display name, truncated to
 *                 GAME_VIEW_SLOT_NAME_LEN-1 characters if longer.
 * Output:  1 on success, 0 on failure (see the debug Log for why).
 */
int game_view_save_to_path(const char *path, const char *name);

/*
 * Function: game_view_peek_slot_name
 * Summary: A cheap, read-only check of one save slot's own display name
 *          and whether it's occupied at all -- src/save_view.c's Save
 *          and Load dialogues call this for each of the 5 slots every
 *          time they open, to populate the slot list, without needing to
 *          fully deserialise (let alone apply) a whole game just to read
 *          a label. Reads only the first 4+GAME_VIEW_SLOT_NAME_LEN bytes
 *          of the file, not the whole thing.
 * Syntax:  int game_view_peek_slot_name(const char *path, char *out,
 *                                       size_t out_size);
 * Input:   path - a full RISC OS pathname to check.
 *          out_size - size of the out buffer.
 * Output:  out - filled with the slot's display name (NUL-terminated,
 *                truncated to out_size-1 if needed) if occupied, or an
 *                empty string if the slot doesn't exist / isn't a valid
 *                ArchiLudo save.
 *          Returns 1 if the slot is occupied (out is a real name), 0
 *          otherwise.
 */
int game_view_peek_slot_name(const char *path, char *out, size_t out_size);

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
