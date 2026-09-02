#ifndef ARCHILUDO_H
#define ARCHILUDO_H

#include "oslib/wimp.h"

#define APP_NAME "ArchiLudo"

extern wimp_t task_handle;

/*
 * Function: archiludo_initialise
 * Summary: One-time application startup: registers the task with the
 *          Wimp, builds the iconbar icon and its menus, initialises
 *          every view module (game/setup/win/rules/splash/save), opens
 *          the splash screen, and starts QTM (background music) if the
 *          module is present. Called once from main() before entering
 *          archiludo_poll_loop().
 * Syntax:  void archiludo_initialise(const char *argv0);
 * Input:   argv0 - the program's own pathname as RISC OS passed it in
 *                   argv[0] (e.g. "HostFS:$.ArchiLudo"), used to build
 *                   absolute paths to bundled resources -- see
 *                   game_view_initialise()'s own doc comment.
 * Output:  none. task_handle and every view module are ready for use.
 */
void archiludo_initialise(const char *argv0);

/*
 * Function: archiludo_poll_loop
 * Summary: The application's main event loop: repeatedly calls
 *          Wimp_Poll and dispatches each event until the user quits
 *          (iconbar Quit menu entry or a Message_Quit), then returns.
 * Syntax:  void archiludo_poll_loop(void);
 * Input:   none.
 * Output:  none. Does not return until the application is quitting.
 */
void archiludo_poll_loop(void);

/*
 * Function: archiludo_reseed_random
 * Summary: Mix fresh entropy into the C library's rand() state (see
 *          src/main.c's own definition and game_logic.c's
 *          ludo_roll()). Combines the CURRENT rand() state (so nothing
 *          already accumulated is lost) with a fresh time(NULL) and a
 *          fresh os_read_monotonic_time() reading -- called once at
 *          startup (before any dice can be rolled) and again every
 *          time the New Game dialogue's Start button is clicked. That
 *          second call point matters: the number of centiseconds
 *          between application launch and a player actually clicking
 *          Start (reading the splash screen, typing player names,
 *          picking difficulties) is driven by human reaction time and
 *          is not the same twice, unlike the mostly-fixed OS-boot-to-
 *          launch duration the startup call alone would capture.
 * Syntax:  void archiludo_reseed_random(void);
 * Input:   none.
 * Output:  none. Calls srand() once.
 */
void archiludo_reseed_random(void);

#endif
