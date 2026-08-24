---
name: riscos-wimp-reference
description: RISC OS 3.10 WIMP C programming reference (SWI conventions, task lifecycle, windows/icons/menus, messages, drag, sprites, colours, filetypes) curated from the PRM, the Pinknoise/Acorn archive, and Steve Fryatt's wimp-prog guide
metadata:
  type: reference
---

Curated WIMP-C-programming reference for RISC OS 3.10, built for
[[archiludo-riscos-project]]-style projects (ArchieSDK, real Archimedes
hardware). Sourced from:

- **PRM** — `https://www.riscos.com/support/developers/prm/` (18 parts +
  appendices; Part 1 = kernel/SWIs, Part 7 = "The Window Manager"). Full
  offline mirror at `~/riscos-dev/prm-mirror/` (144 pages, ~12MB) — grep
  this directly for anything not covered in enough depth below; it's the
  actual manual, not a summary of it.
- **Pinknoise archive** — `https://wss.co.uk/pinknoise/Docs/index.html`
  (Acorn-internal docs collated by Robin Watts/Pelago; **serves an expired
  TLS cert**, fetch with `curl -k`, not WebFetch)
- **Steve Fryatt's `wimp-prog`** — `https://www.stevefryatt.org.uk/risc-os/wimp-prog`
  (event-driven architecture walkthrough — conceptual glue for everything
  below; his SFLib/GCCSDK code itself does NOT run under ArchieSDK, see
  [[archiludo-riscos-project]]). Full offline mirror at
  `~/riscos-dev/wimp-prog-mirror/wimp-prog/` (29 pages).

Scoped to what a WIMP C application needs — ADFS/CDFS internals, Econet,
podules, printer drivers and Psion docs from the source archives are
deliberately left out.

## SWI calling conventions

- SWI parameters/results pass in R0-R9; on error, the V flag is set and R0
  points to an `os_error` block: `{ int errnum; char errmess[252]; }`.
- ArchieSDK/OSLib code calls SWIs via `_swix()`/`_kernel_swi()` from
  `<kernel.h>`/`<swis.h>` — e.g. `_swix(OS_Byte, _INR(0,1), 0, 0)` (see
  ArchieSDK's own `examples/hello-world/main.c`). OSLib wraps most Wimp SWIs
  as typed C functions (`wimp_initialise()`, `wimp_poll()`, ...) plus `x`-
  prefixed variants that return an `os_error*` instead of raising it.
- Unknown `Wimp_Poll` reason codes and message actions **must be ignored** —
  this is a hard compatibility rule, not a suggestion (Pinknoise
  `Wimp/Guidelines.html`).
- Reserved fields must be zeroed. Wimp calls corrupt R0 even when R0 isn't a
  result register.

## Task lifecycle

`Wimp_Initialise` / `Wimp_CloseDown` (PRM Part 7 + Pinknoise `Wimp/Desktop.html`,
`Wimp/Guidelines.html`):

```
Wimp_Initialise
  Entry:  R0 = latest known Wimp version *100 (200 = version 2.00)
          R1 = "TASK"        (4 bytes: 'T','A','S','K', low-byte first —
                               NOT a pointer to the string "TASK")
          R2 --> task description string (used by Task Manager/Switcher)
  Exit:   R0 = actual Wimp version *100
          R1 = task handle

Wimp_CloseDown
  Entry:  R0 = task handle (from Wimp_Initialise)
          R1 = "TASK"
```

Setting `R1 = "TASK"` is what makes this a *new-style* (multitasking) task —
omit it and the Wimp gives the task the whole screen exclusively, which is
never what a normal app wants.

Basic loop shape (OSLib names): `wimp_initialise()` once, then loop calling
`wimp_poll()` and dispatching on the reason code, then `wimp_close_down()`
before exit.

### Message_Quit (action 0, broadcast)

On receipt, tidy up and call `Wimp_CloseDown` then exit — do **not** refuse
at this point. If the app has unsaved state, it must object earlier, at
**Message_PreQuit** (action 8, broadcast), by acknowledging that message
(`Wimp_SendMessage` R0=19) and popping a save/discard dialogue instead of
letting Quit arrive. See full closedown handshake (PreQuit → user dialogue →
CTRL-SHIFT-F12 replay → Quit) in Pinknoise `Wimp/Messages.html` — the
CTRL-SHIFT-F12 replay trick is only relevant if you also want to support the
Task Manager's global "Exit" option; a simple game can just handle Quit and
PreQuit directly.

### Escape handling

The Wimp maps Escape to ASCII 27 for whichever task owns the caret; no
escape *condition* is generated while polling normally. Only re-enable
`*FX 229,0` around a deliberately long operation that doesn't call
`Wimp_Poll`, and restore `*FX 229,1` + acknowledge (`*FX 124`) before the
next poll.

## Icon bar

(Pinknoise `Wimp/IconBar.html`, `IconBar3.html`)

Special window handles recognised by `Wimp_CreateIcon`/`Wimp_DeleteIcon`/
`Wimp_GetPointerInfo`:

| Handle | Meaning |
|---|---|
| `-1` | icon bar, right (utilities) side |
| `-2` | icon bar, left (filing systems) side |
| `-3` | RISC OS 3.10+: create to the **left** of icon R0 |
| `-4` | RISC OS 3.10+: create to the **right** of icon R0 |
| `-5`/`-6`/`-7`/`-8` | RISC OS 3.10+: priority-ordered placement, R0 = signed priority |

A resident/utility-style task should put a single icon on the bar with
`Wimp_CreateIcon` (window handle -1), and respond to a MENU click on it with
a small menu (`About <name>` … `Quit`). SELECT on the icon should open the
main application window. `Wimp_Poll` reason 6 (Mouse_Click) is delivered to
the icon's owner with the icon handle in the block.

Standard iconbar icon geometry: sprite-only icons vertically centred (or on
baseline y=0 if near 68 OS units tall); sprite+text icons sit on the
baseline (sprite y=20..84, text at y=-16..16). Icons are widened only in x —
the Wimp positions them, you don't.

## Windows, redraw, "pane" pattern

Core loop shape from the classic worked example (Pinknoise
`Wimp/pane.c.html`; this predates OSLib but the algorithm is unchanged):
`Wimp_Poll` → dispatch on reason (`Redraw_Window`, `Open_Window`,
`Close_Window`, `Mouse_Click`, ...). For a companion "pane" window that must
track a main window (e.g. a status/toolbar strip glued to a game window),
open the *pane* first if the tool window is moving toward it, then the tool
window, to avoid redundant redraws — and always re-read the tool window's
actual `bhandle` after opening it (its real stacking position isn't known
until after the call).

For a single simple game window with no pane, the essential per-poll
handling is just: `Redraw_Window` → `Wimp_RedrawWindow`/`Wimp_GetRectangle`
loop, replotting the board/sprites; `Open_Window_Request` →
`Wimp_OpenWindow`; `Close_Window_Request` → `Wimp_CloseWindow` (and quit if
it's the only window and the app has no iconbar icon to fall back on).

**Access rule**: `Wimp_CreateIcon`/`DeleteIcon`/`OpenWindow`/`CloseWindow`/
`RedrawWindow`/`UpdateWindow`/`GetRectangle`/`SetExtent`/`BlockCopy` only
work on windows the calling task owns (error "Access to window denied"
otherwise) — and only while the task is in the foreground (i.e. really
inside its `Wimp_Poll` turn, not from an interrupt).

## Animating a small region of a window without flicker

The naive way to redraw one small changed area of a window many times a
second (a die face, a moving token, a flashing highlight — anything that
isn't a genuine window-exposure `Redraw_Window_Request`) is to call
`Wimp_ForceRedraw` with a small rectangle and let the resulting
`Redraw_Window_Request` arrive back through the normal `Wimp_Poll` loop,
handled the ordinary way via `Wimp_RedrawWindow`/`Wimp_GetRectangle`.
This is *correct* (the standard, PRM-documented way to request a scoped
redraw — and the same technique any panel-only/partial redraw already
uses) but visibly flickers on real/emulated ARM2/ARM3 speeds: per the
PRM, `Wimp_RedrawWindow`/`Wimp_GetRectangle` **automatically clear every
rectangle they return to the window's background colour** before
handing control back to draw the real content — a genuine two-step
"flash background, then repaint" cycle each tick, and a manually
invoked `Wimp_RedrawWindow` always reports the window's *entire*
currently-exposed area regardless of how small a rectangle was forced
dirty, since there's no caller-supplied clip on that SWI.

**`Wimp_UpdateWindow` is the correct tool for repeated small-region
redraws instead** — same `wimp_draw`-shaped block and
`Wimp_GetRectangle`-continuation loop as `Wimp_RedrawWindow`, but "the
rectangles to be updated are **not** cleared by the Wimp first" and "this
can be called **at any time**, not just in response to a
`Redraw_Window_Request`." The critical difference from
`Wimp_RedrawWindow`, easy to get wrong: **`Wimp_UpdateWindow` takes the
rectangle as *input*** (`w, x0, y0, x1, y1`, all in work-area OS units),
not output — only `.w` is meaningful on entry to `Wimp_RedrawWindow`,
where the Wimp computes the redraw rectangle itself, but
`Wimp_UpdateWindow` needs the caller to supply `.box` (and, per OSLib's
`wimp_draw` struct, that's the same field `Wimp_RedrawWindow` treats as
output-only) *before* the call. Leaving it as uninitialised stack
garbage produces a nonsense "rectangle" and the SWI reports nothing to
redraw — `more` comes back false immediately, and the drawing call
inside the loop never runs at all, which reads exactly like "nothing
happens," not an obvious "wrong input" error. The correct shape:

```c
wimp_draw redraw;
osbool more;

redraw.w = window_handle;
redraw.box.x0 = ...; redraw.box.y0 = ...;  /* work-area coords, INPUT */
redraw.box.x1 = ...; redraw.box.y1 = ...;

more = wimp_update_window(&redraw);
while (more) {
    int origin_x = redraw.box.x0 - redraw.xscroll;
    int origin_y = redraw.box.y1 - redraw.yscroll;
    /* draw the small changed area here, inline, every iteration --
     * there is no later Redraw_Window_Request coming for this call */
    more = wimp_get_rectangle(&redraw);
}
```

Confirmed against real, shipped example code, not just the PRM's
one-line description: `github.com/marutan/ro-chess`'s `icon_update()`
helper (a general-purpose small-region updater used for every
piece/square change, including its own flashing-highlight animation
driven by a periodic timer) sets its redraw block's box to the changed
icon's own work-area bounds before calling `Wimp_UpdateWindow`, then
plots inline in the same `while (more)` loop — exactly the pattern
above — and never calls `Wimp_ForceRedraw` anywhere in its source at
all.

The PRM explicitly discourages the seemingly-simpler alternative (just
plotting directly to the screen whenever, bypassing the redraw protocol
entirely): in-window dragging "must use `Wimp_UpdateWindow`... rather
than drawing directly on the screen," for window-occlusion/
multitasking-correctness reasons — another window can legally be on top
of the animated area at any moment, and only the Wimp's own redraw
protocol (which `Wimp_UpdateWindow` is still part of, just without the
auto-clear) coordinates who's allowed to touch which pixels when.

A window's `work_bg` colour can also be set to `wimp_COLOUR_TRANSPARENT`
to disable the auto-clear for *all* redraws of that window (no other
code change needed) — tempting as an even simpler fix, but only safe if
the window's own redraw handler unconditionally repaints every pixel of
its extent on every call. If any code path relies on the Wimp's own
background clear for correctness (e.g. skipping cells/areas that have
no content of their own, expecting them to just show background) that
reliance breaks silently — those areas keep showing stale content from
whatever was there before on the next genuine exposure redraw (another
window dragged across and away). Worth doing only after checking the
redraw handler paints unconditionally everywhere, not just where the
current animation happens to touch.

**Two follow-up lessons from real use of this pattern (ArchiLudo round
7.13), both easy to miss even once the basic loop above is working:**

1. **The caller is responsible for erasing, every single call, not just
   the first.** Since `Wimp_UpdateWindow` never clears anything, any
   redraw handler wired onto it needs its own explicit erase step
   (a solid fill of `redraw.box` in the window's background colour)
   *before* drawing the real content, every time the loop's inner body
   runs — not just conceptually "once at the start of the animation."
   Forgetting this on even one of several `Wimp_UpdateWindow`-based
   redraw paths in the same codebase (having gotten it right on the
   others) produces a working-everywhere-except-here ghosting bug that
   only reveals itself once something moves through the one path that's
   missing it — in ArchiLudo's case, an old pawn position never being
   revisited by content that would naturally paint over it, once a
   converted `Wimp_RedrawWindow`-based function was switched to
   `Wimp_UpdateWindow` without also picking up the erase step its
   siblings already had.
2. **Scope the redraw rectangle to what this *frame* actually touches,
   not the full extent the animation could *ever* touch.** It's tempting
   to compute one bounding box up front that covers an entire
   multi-step animation's whole path (simpler code, computed once) and
   reuse it for every tick — but that means erasing and repainting the
   *entire* box on *every* tick, even though any single frame only
   changes a small part of it. This is both wasted work and, worse,
   visibly makes the flicker problem this whole pattern exists to solve
   proportional to the animation's *total* extent rather than to what's
   actually moving. Recompute the rectangle each tick from only the
   cells/pixels that frame's content can possibly occupy (current
   position plus enough margin to cover the *previous* tick's position,
   which this call's erase step must also clear) — see
   `update_move_animation_area()` in `src/game_view.c` for a worked
   example (scoped to the pawn's current path *segment*, not its whole
   multi-square route).
3. **For a discrete state change (not a continuous per-tick animation)
   whose side effects are data-dependent and scattered** — e.g. a game
   move that *might* also have captured an opponent's piece elsewhere on
   the board, or might not have — don't reach for a full-window redraw
   just because enumerating the exact affected region in advance is
   awkward. Snapshot whatever piece of state determines on-screen
   position immediately before the state-changing call, then diff it
   against the post-change state once the call (and anything animating
   *it* specifically) has finished; redraw only whatever changed. This
   finds exactly what needs repainting without hand-writing per-rule
   logic for every kind of side effect a move can have, and — critically
   — costs nothing at all in the common case where nothing else changed.
   See ArchiLudo's `snapshot_pawn_positions()`/`update_settle_diff_area()`
   (`src/game_view.c`, round 7.15) for a worked example: one generic
   diff against every game piece's position, used identically whether
   the triggering event was a capture, an own-piece collision, or a
   piece newly entering play.

## Icons: flags, validation strings, sprite+text

(Pinknoise `Wimp/Icons.html`, `Wimp/ValidStrs.html`)

Validation string grammar (RISC OS 3.10 feature set):

```
<validation> ::= <command> { ; <command> }*
<command>    ::= A<allow-spec> | D<char> | F<bg><fg> | L<n> | S<sprite>[,<sprite2>]
                | K{R|A|T|D|N}... | P<sprite>,<x>,<y> | R<type>,<highlight-colour>
```

- `A` — allowed characters for a writable icon (others are passed back as
  `Key_Pressed`); `~` toggles allow/exclude, `\` escapes `~-;\`.
- `D` — password-style display character (masks typed input).
- `F<bg><fg>` — anti-aliased font colours (2 hex digits, Wimp colours;
  default `07`).
- `L<n>` — line spacing for word-wrapped, non-writable, centred text icons.
- `S<sprite>[,<sprite2>]` — sprite name for a text+sprite icon (icon must be
  indirected); second name = sprite shown while selected.
- `K` + any of `R`/`A`/`T`/`D`/`N` — RISC OS 3.10 caret-navigation additions
  for chains of writable icons: `R` = Return moves to next icon, `A` =
  arrow-up/down moves between icons (wraps), `T` = Tab/Shift-Tab moves
  between icons (wraps), `D` = notify app of Copy/Delete-family keys, `N` =
  notify app of *every* keypress even if the Wimp handled it. Useful for a
  name-entry dialogue with several fields (mirrors what the GeoLudo ports
  did with repeated `DlgBoxGetString` calls — see [[archiludo-riscos-project]]).
- `P<sprite>,x,y` — custom pointer shape while over the icon.
- `R<type>,<highlight>` — border style: `0` plain, `1` slab-out, `2`
  slab-in, `3` ridge, `4` channel, `5`/`6` action button (highlights on
  select, `6` = default action), `7` editable field. **Changing this at
  runtime** (e.g. a manual "pressed" flash on a button icon, R1↔R2, as
  opposed to letting R5/R6 handle press/release automatically): the icon
  must be indirected with the validation string in a buffer the app owns
  — mutate that buffer's contents directly (e.g. `strncpy` "R1" over
  "R2"), then call `Wimp_SetIconState`/`wimp_set_icon_state(w, i, 0, 0)`
  (an actual-no-op flags EOR/clear) purely to make the Wimp re-read and
  redraw the icon's indirected data — the same pattern used to refresh
  indirected *text* after changing it (ArchiLudo's `refresh_status()`).
  Confirmed working this way in ArchiLudo's Throw button (see
  `src/game_view.c`'s `flash_throw_button()`).

Sprite+text icon positioning is controlled by the H/V/R flag bits (bits 4,
5, 9) — see the table in the full Pinknoise page if a non-default layout is
needed; the common cases (both centred, or sprite-left/text-right) need no
special validation string handling.

Button types worth knowing for a game UI: type 9 ("menu icon", flashes
continuously unless its ESG select-group is non-zero — good for
simulated-menu action buttons); type 11 (auto-selects and reports
click/drag without double-click semantics — good for toggle-style icons
like dice/throw buttons).

**Disabling a button visually and functionally: `wimp_ICON_SHADED`.**
Per the PRM (`wimp.html`): "When the icon's shaded bit is set, the Wimp
draws the icon in a 'subdued' way, to indicate that it can't be
selected. This also prevents selection by clicking" — a genuine
Wimp-level click guard, not just a greyed-out look, so it's safe to rely
on alongside (not just instead of) an app's own click-handler guard for
the same condition. Toggle it the same way as any other icon flag —
`wimp_set_icon_state(w, i, eor_bits, clear_bits)`, EOR-ing
`wimp_ICON_SHADED` only when the desired state actually differs from
what's currently on screen (track it in a local static so a per-poll
status refresh doesn't re-toggle every single call):

```c
int want_shaded = !some_condition_that_means_clickable;
wimp_icon_flags eor = (want_shaded != currently_shaded) ? wimp_ICON_SHADED : 0;

wimp_set_icon_state(w, i, eor, 0);
currently_shaded = want_shaded;
```

Worth reaching for whenever a UI has one button whose meaning changes
by state (a combined "Throw"/"Continue" action button, say) and isn't
always the actual next required action — shading it the rest of the
time (per ArchiLudo round 7.15) keeps the affordance honest about
what's clickable right now, without needing a second icon or hiding/
recreating the icon outright.

## Menus

(PRM Part 7 + Pinknoise `Wimp/Menus.html`)

`Wimp_CreateMenu` opens a menu tree; `Wimp_Poll` reason 9 (`Menu_Selection`)
returns the chosen path. MENU or SELECT closes the tree after a choice;
ADJUST keeps it open (the app must re-encode and re-call `Wimp_CreateMenu`
with the same tree pointer, after checking via `Wimp_GetPointerInfo` that
the right button is still down, or the tree is closed on the next poll).

Submenus can be resolved lazily: set bit 3 of a menu entry's flags with a
dummy submenu pointer, and the Wimp sends **Message_MenuWarning** (&400C0)
when the pointer reaches that entry's arrow, containing the proposed
submenu pointer/x/y and the selection path so far — respond with
`Wimp_CreateSubMenu` (optionally substituting a real submenu/dialogue window
handle at that point). This is the natural way to hook a dialogue window
(e.g. "Load game…") into a menu tree without building it up front.

## Messages

(Pinknoise `Wimp/Messages.html`, `Apps/DataTrans.html`, `Apps/DragDrop.html`)

`Wimp_Poll` reason codes 17/18/19 (`User_Message` / `User_Message_Recorded`
/ `User_Message_Acknowledge`); block layout for `Wimp_SendMessage`:

```
+0  size (20..256, multiple of 4)
+12 your_ref   (0 unless replying — then = my_ref of message being answered)
+16 message action
+20.. message data
```

18 guarantees the sender gets reason-19 back if nobody acknowledges it — use
18 whenever a reply is expected, 17 for fire-and-forget. A broadcast is
`Wimp_SendMessage` with the destination task handle = 0.

Message actions relevant here:

| Action | Value | Notes |
|---|---|---|
| `Message_Quit` | 0 | broadcast, see Task lifecycle above |
| `Message_DataSave` | 1 | start of save handshake |
| `Message_DataSaveAck` | 2 | receiver names a destination file |
| `Message_DataLoad` | 3 | "load from here" / drag-from-Filer |
| `Message_DataLoadAck` | 4 | confirms a successful load |
| `Message_DataOpen` | 5 | broadcast on double-click; app can claim to open in an existing instance |
| `Message_RAMFetch` / `RAMTransmit` | 6 / 7 | in-memory transfer via `Wimp_TransferBlock` (skip this for a simple save-game format — file-based DataSaveAck is enough) |
| `Message_PreQuit` | 8 | see Task lifecycle |
| `Message_PaletteChange` | 9 | broadcast; recompute cached colours if you cache any |
| `Message_MenuWarning` | &400C0 | see Menus above |
| `Message_ModeChange` | &400C1 | broadcast; re-derive anything mode-dependent via `OS_ReadVduVariables`, never assume a mode number |

### Save protocol (application → Filer or another app)

1. App sends `Message_DataSave` (dest window/icon/coords from
   `Wimp_GetPointerInfo` at end of the drag, estimated size, filetype,
   proposed leafname).
2. Receiver replies `Message_DataSaveAck` with a full pathname (directory
   viewers just want a file on disc; if the estimated-size field comes back
   negative, the destination is a scrap file — don't mark the document
   "saved" against that path).
3. App saves the file, then sends `Message_DataLoad` as acknowledgement
   (`your_ref` = the DataSaveAck's `my_ref`).
4. Receiver replies `Message_DataLoadAck` on success; if the app never gets
   this back, it must report "Bad Data Transfer, Receiver Dead" and delete
   the file it saved.

### Load protocol (Filer → application)

Filer sends `Message_DataLoad` (`your_ref = 0`) when a file is dragged onto
a window; the app loads it and replies `Message_DataLoadAck` on success.
`Message_DataOpen` is the broadcast-on-double-click variant — reply
`DataLoadAck` if an existing instance opens the file itself.

This is the idiomatic RISC OS save/load UX (drag-to-Filer, drag-from-Filer,
double-click-to-open) — see [[archiludo-riscos-project]] for why this
replaces GeoLudo's GEOS-native file-picker dialogue rather than porting it
literally.

## Sprites

(PRM sprite SWIs + Pinknoise `Wimp/Sprites.html`, `Sprites/*`)

- `Wimp_SpriteOp` mirrors `OS_SpriteOp` reason codes but only allows
  `SpriteReason_MergeSpriteFile` (11) to modify the shared pool — everything
  else is read-only against the Wimp's common sprite area (RMA sprites
  preferred, falls back to the ROM pool).
- A window's `wimp_window.sprite_area` (`areaCBptr`) field: `0` = system
  sprite area, `1` = Wimp's common pool, `>1` = pointer to a private sprite
  area you loaded yourself (e.g. board/dice/pawn art loaded once at
  startup with `OS_SpriteOp` `MergeSpriteFile`/`LoadSpriteFile` into your own
  area, then referenced from icons/redraw code).
- Icon data for a sprite icon: bit 1 of icon flags = "contains a sprite",
  bit 8 = "data is indirected"; indirected sprite icon data is
  `{ sprite-name-or-id, sprite-area-ptr, name-block-length }` — a non-zero
  name-block-length means "this is a sprite *name* pointer", zero means
  "this is a literal `os_sprite*` pointer" (useful for plotting one of
  several pre-decoded board-square sprites directly without a name lookup).
- 8bpp (256-colour) sprites don't work through `Wimp_PlotIcon`'s
  *automatic* colour translation — per the PRM's sprite-bpp table
  (`wimp.html`), that auto-translation (onto the 16 fixed Wimp colours)
  is only defined for 1/2/4bpp; for 8bpp it says outright "translation
  table is undefined." Keep dice/pawn/icon sprites at 1/2/4bpp if
  they're going through `Wimp_PlotIcon`. This is **not** a ceiling on
  sprites in general, though — `sprites.html` is explicit: "Use
  ColourTrans if you want to plot the sprite using the best
  approximation to its actual colours. This works for sprites in a
  256-colour mode as well." Full 256-colour sprites plotted *directly*
  in a redraw handler via `OS_SpriteOp 52` (PutSpriteScaled) with your
  own ColourTrans-generated pixel translation table (built once via
  `ColourTrans_ReturnColourNumber` per palette entry for a genuine
  256-entry-palette sprite, or `ColourTrans_SelectTable` for <=64-entry
  palettes) have no such restriction — this still runs inside the same
  `Wimp_UpdateWindow`/`Wimp_RedrawWindow` erase-and-redraw loop as any
  other scoped redraw (see "Animating a small region..." above), it's
  just a different `OS_SpriteOp` reason code than what `Wimp_PlotIcon`
  uses internally, not the "draw directly to the screen, bypassing the
  redraw protocol" case the PRM warns against elsewhere. For a single
  fixed target mode (as opposed to a portable app that must cope with
  the user's desktop mode changing), the translation table only needs
  building once, at sprite-load time.

## Colours / ColourTrans

(Pinknoise `Wimp/Colours.html`)

The Wimp's 16 logical "Wimp colours" are looked up through a translation
table so that a fixed palette convention (grey ramp 0-7 white->black, 8
dark blue, 9 yellow, 10 green, 11 red, 12 cream, 13 army green, 14
orange, 15 light blue — corrected 2026-08-24 against the PRM directly,
`wimp.html`'s own "Colour handling" table; an earlier version of this
paragraph had 8 and 9 swapped) works uniformly across every screen
mode/depth — **applications should not set the
palette**, and should express colours as Wimp colours (`Wimp_SetColour`,
`Wimp_TextColour`) rather than raw GCOL wherever a value is going into a
window/icon definition. Sprites plotted with 1/2/4bpp are translated through
this same table automatically; GCOL-drawn graphics and sprites plotted
directly (not via an icon) are not. `Wimp_ReadPixTrans` gives the
scale/translation-table pair to pass to `OS_SpriteOp` `PutSpriteScaled` when
plotting board/dice sprites by hand in a redraw handler (handles the
differing pixel aspect ratio between screen modes automatically).

## Screen modes: non-square pixels and thin manually-drawn lines

Several RISC OS screen modes (mode 15 among them: 640x256 pixels at
1280x1024 OS units) have **non-square pixels** — mode 15 specifically is
2 OS units per pixel horizontally, 4 OS units per pixel vertically.
`OS_ReadModeVariable`/`OS_ReadVduVariables` give the real per-mode
XEigFactor/YEigFactor to compute this exactly rather than hardcoding a
mode's numbers, but the consequence is the same in any non-square mode:
**a manually fill_rect()-drawn line/border/outline (the common
"plot a slightly larger shape in black behind, then the real content on
top" technique for a custom-drawn outline) needs to be at least one full
physical pixel thick in *both* directions to reliably render** — a
rasterizer that paints a pixel when the pixel's centre falls inside the
filled shape only guarantees a hit if the shape is at least as thick as
one pixel; a thinner one can fall entirely between two pixel-centre
sample points and paint nothing on that edge, and this isn't reliably
reproducible at a fixed source coordinate either, since a window's
on-screen position is OS-unit-granular, not quantised to the pixel grid
— the same border can vanish or reappear depending purely on where the
user happens to have dragged the window. Concretely for mode 15: any
such manually-drawn thickness must be **at least 4 OS units**, not just
"a couple of units that looks about right" — 4 clears both the 4-unit
vertical minimum and the (smaller) 2-unit horizontal one in one number,
so there's no need to track separate per-axis minimums.

Native stroke primitives (`os_PLOT_CIRCLE_OUTLINE` and similar "outline"
plot codes, as opposed to a filled shape used to fake a border) are
believed exempt — the standard assumption is that they rasterize at the
physical pixel level directly (like a Bresenham line algorithm) rather
than being subject to this OS-unit-vs-pixel-centre sampling issue — but
this is the conventional assumption, not something independently proven
against the PRM's `os_plot` reference, which documents the plot code's
existence without spelling out its rasterization guarantee. If a native
outline-stroked shape is ever reported as patchy or partially invisible,
don't assume it's automatically safe — check it the same way.

Found the hard way in ArchiLudo (see that project's `docs/ARCHITECTURE.md`
"Round 7.7" notes and its own auto-memory for the full incident
writeup): a player-colour swatch's outline using a 2-unit thickness was
invisible specifically on its top/bottom
edges (below the 4-unit vertical minimum) while its left/right edges
still showed (2 units meets the smaller horizontal minimum) — this
asymmetry, a border showing on some edges but not others, is the
signature symptom of this bug and a fast thing to check first before
assuming a coordinate-math error or an unrelated rendering mystery.

## Drag

(Pinknoise `Wimp/Drag.html`)

`Wimp_DragBox` reason codes 1-4 are canned window-move/size/scroll drags;
5-7 are simple fixed/rubber/invisible boxes; 8-11 take caller-supplied
assembler subroutines to draw/remove/move an arbitrary shape (needed if
dragging a game token needs a custom outline rather than a plain rectangle —
OSLib exposes these as the `wimp_drag_box`/`wimp_auto_scroll` family).
For simulating the ghost-caret/auto-scroll UX (Apps/DragDrop.html), the
`Message_Dragging`/`Message_DragClaim` pair lets another window claim an
in-progress drag — likely overkill for moving a pawn between fixed board
squares; a plain `Wimp_DragBox` type 6/7 plus `Mouse_Click`-driven pickup is
enough for that.

## Pointer / Hourglass

(Pinknoise `Wimp/Pointer.html`, `Wimp/Hourglass.html`)

- Change the pointer shape only while it's inside your window: pointer
  shape 2 on `Pointer_Entering_Window`, reset with `*Pointer` (shape 1) on
  `Pointer_Leaving_Window`. Track an "do I currently own the pointer" flag —
  don't blindly reset it from unrelated state changes.
- `Hourglass_On`/`Hourglass_Off` (nestable) for any operation that might
  take a noticeable moment (e.g. AI turn calculation, disc save) — there's a
  built-in 1/3s grace period before it actually becomes visible, so it's
  safe to bracket even fast operations. `Hourglass_Percentage` for a visible
  progress figure if a longer operation ever needs one.

## Filetypes / packaging

RISC OS filetypes are 12-bit values carried in a file's load address (native
filesystems) or, on a non-RISC-OS host filesystem (like this project's Linux
build box), encoded as a `,xxx` hex suffix on the filename — this is exactly
what ArchieSDK's `arm-archie-objcopy`/`arm-archie-zip` produce/preserve (see
[[archiludo-riscos-project]]). Filetypes relevant to this project:

| Filetype | Hex | Use |
|---|---|---|
| Absolute | `&FF8` | the built executable (`ArchiLudo,ff8`) |
| Obey | `&FEB` | `!Run`/`!Boot` script files inside an application directory |
| Sprite | `&FF9` | `!Sprites` (Filer icon) file |
| Data | `&FFD` | generic binary data (e.g. a save-game file) |
| Text | `&FFF` | plain text |

A full double-clickable application is a directory named `!AppName`
containing `!RunImage` (or a `!Run` Obey file that sets up
`Run <Obey$Dir>.!RunImage`), `!Boot`, and `!Sprites`/`!Sprites22` (Filer
icon, sprite named `!appname`). The build currently produces a bare
`ArchiLudo,ff8` runnable directly from a Filer window or command line; wrap
it in a proper `!ArchiLudo` application directory once double-click
launching/RAM-disc-friendly boot behaviour actually matters.

## Tying it together

Steve Fryatt's `wimp-prog` (`https://www.stevefryatt.org.uk/risc-os/wimp-prog`)
walks the same material in tutorial form and in the same order a real app
needs it: iconbar → windows → icons (incl. sprites/writable fields) → menus
(standard/pop-up/window) → keyboard → redraw → messages → internationalisation.
Read it for the narrative connective tissue between the SWI-level facts
above; write the actual C against plain OSLib calls (see
[[archiludo-riscos-project]] for why SFLib itself isn't usable with
ArchieSDK).
