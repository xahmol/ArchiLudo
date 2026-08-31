#ifndef RULES_VIEW_H
#define RULES_VIEW_H

#include "oslib/wimp.h"
#include "game_logic.h"

/*
 * ArchiLudo rules view
 * ====================
 *
 * The "Rule Options" dialogue -- lets the player pick a main rule-set
 * variant (Mens Erger Je Niet / Ludo / Pachisi-style, see
 * include/game_logic.h's ludo_variant) via a pop-up menu, and override
 * any of the 7 individual house-rule toggles (ludo_rules) via paired
 * exclusive-selection-group (ESG) icons, one pair per toggle. Reached
 * from src/setup_view.c's "New Game" dialogue via a "Rules..." button;
 * OK hands the assembled ludo_rules back to setup_view.c (via
 * setup_view_configure_rules()) for use when Start is next clicked;
 * Cancel discards any changes made in this dialogue.
 *
 * Modelled closely on src/win_view.c's simple dialogue shape (plain Wimp
 * icons, no custom-plotted content) -- the two genuinely new patterns
 * for this project are the paired-ESG-icon "radio button" groups (see
 * riscos_wimp_reference.md's Icons section, "ESG" / "Radio icons") and
 * the click-to-open pop-up menu acting as a drop-down (the same
 * wimp_create_menu() pattern src/main.c's own iconbar/window menu
 * already uses, just triggered by clicking a specific icon in this
 * window rather than a MENU-button click on the background).
 */

/*
 * Function: rules_view_initialise
 * Summary: Create (but do not open) the Rule Options window. Call once
 *          during application startup, after game_logic.h's types are
 *          available (no other module dependency).
 * Syntax:  void rules_view_initialise(void);
 * Input:   none.
 * Output:  none. The window exists (closed) and is ready for
 *          rules_view_open().
 */
void rules_view_initialise(void);

/*
 * Function: rules_view_open
 * Summary: Open the Rule Options dialogue, seeded with `rules` as the
 *          starting point for every toggle and the variant menu -- the
 *          caller (src/setup_view.c) is responsible for choosing what
 *          this should be (its own pending rules, so reopening the
 *          dialogue mid-edit shows what was left there, matching this
 *          project's "New Game dialogue always defaults to the
 *          in-progress game" convention).
 * Syntax:  void rules_view_open(const ludo_rules *rules);
 * Input:   rules - the rules to display/edit. Copied by value; the
 *                  caller's own storage is not aliased or modified.
 * Output:  none.
 */
void rules_view_open(const ludo_rules *rules);

/*
 * Function: rules_view_window_handle
 * Summary: The Rule Options window's handle, for src/main.c's
 *          Wimp_Poll dispatch to compare against.
 * Syntax:  wimp_w rules_view_window_handle(void);
 * Input:   none.
 * Output:  the window handle created by rules_view_initialise().
 */
wimp_w rules_view_window_handle(void);

/*
 * Function: rules_view_redraw
 * Summary: Handle a Redraw_Window_Request for this window. Plain Wimp
 *          icons only, no custom-plotted content -- same trivial
 *          redraw-loop shape as every other dialogue module.
 * Syntax:  void rules_view_redraw(wimp_draw *redraw);
 * Input:   redraw - the Redraw_Window_Request event block, already
 *                   Wimp_RedrawWindow-filled by the caller.
 * Output:  none.
 */
void rules_view_redraw(wimp_draw *redraw);

/*
 * Function: rules_view_click
 * Summary: Handle a Mouse_Click reported for this window -- a toggle's
 *          option icon (updates the pending rules and, per this
 *          project's own established pattern, relies on the Wimp's own
 *          ESG mechanism to visually deselect its sibling), the variant
 *          display icon (opens the variant pop-up menu), or OK/Cancel.
 * Syntax:  void rules_view_click(wimp_pointer *pointer);
 * Input:   pointer - the Mouse_Click event block.
 * Output:  none. OK/Cancel close the window; OK also hands the
 *          assembled rules back to setup_view.c.
 */
void rules_view_click(wimp_pointer *pointer);

/*
 * Function: rules_view_menu_open
 * Summary: Whether this module's own variant-picker pop-up menu is
 *          currently the menu displayed on screen -- consulted by
 *          src/main.c's Menu_Selection dispatch to route the selection
 *          here instead of assuming it's always the shared iconbar/
 *          window menu (RISC OS only ever has one menu open at a time,
 *          and a Menu_Selection event doesn't itself say which menu it
 *          belongs to).
 * Syntax:  int rules_view_menu_open(void);
 * Input:   none.
 * Output:  1 if this module's variant menu is the one currently open,
 *          0 otherwise.
 */
int rules_view_menu_open(void);

/*
 * Function: rules_view_menu_selection
 * Summary: Handle a Menu_Selection event for this module's own variant
 *          pop-up menu (only ever meaningful when rules_view_menu_open()
 *          was true just before this fires) -- applies the chosen
 *          variant's own defaults (still overridable afterwards) to the
 *          pending rules, refreshes every toggle's display, and clears
 *          the "menu open" flag.
 * Syntax:  void rules_view_menu_selection(wimp_selection *selection);
 * Input:   selection - the Menu_Selection event block's own selection
 *                      field (a single-level item index into the
 *                      3-entry variant menu).
 * Output:  none.
 */
void rules_view_menu_selection(wimp_selection *selection);

#endif
