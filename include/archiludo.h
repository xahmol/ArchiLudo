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

#endif
