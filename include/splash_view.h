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
 * consistent with every other piece of ArchiLudo art since round 6.4 of
 * `docs/GRAPHICS_TOOLING.md` (repeated unexplained sprite-plotting
 * failures in this project's toolchain/environment). The rectangle data
 * is a pixel-exact decode of the real source art (a PETSCII "Petmate"
 * file, decoded against a real C64 character ROM dump -- there are no
 * genuine curves to approximate, every letter is built from flat
 * vertical bars) -- see that file's top-of-file comment for the full
 * extraction writeup.
 */

void splash_view_initialise(void);
void splash_view_open(void);
wimp_w splash_view_window_handle(void);
void splash_view_redraw(wimp_draw *redraw);
void splash_view_click(wimp_pointer *pointer);

#endif
