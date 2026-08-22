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
  select, `6` = default action), `7` editable field.

Sprite+text icon positioning is controlled by the H/V/R flag bits (bits 4,
5, 9) — see the table in the full Pinknoise page if a non-default layout is
needed; the common cases (both centred, or sprite-left/text-right) need no
special validation string handling.

Button types worth knowing for a game UI: type 9 ("menu icon", flashes
continuously unless its ESG select-group is non-zero — good for
simulated-menu action buttons); type 11 (auto-selects and reports
click/drag without double-click semantics — good for toggle-style icons
like dice/throw buttons).

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
- 8bpp (256-colour) sprites are **not legal as Wimp icons** — keep dice/pawn
  sprites at 1/2/4 bpp if they're going to be window icons; sprites plotted
  directly in a redraw handler via `OS_SpriteOp` don't have this restriction.

## Colours / ColourTrans

(Pinknoise `Wimp/Colours.html`)

The Wimp's 16 logical "Wimp colours" are looked up through a translation
table so that a fixed palette convention (grey ramp 0-7, yellow 8, blue 9,
green 10, red 11, title-bar shades 12-14, desktop background 15) works
uniformly across every screen mode/depth — **applications should not set the
palette**, and should express colours as Wimp colours (`Wimp_SetColour`,
`Wimp_TextColour`) rather than raw GCOL wherever a value is going into a
window/icon definition. Sprites plotted with 1/2/4bpp are translated through
this same table automatically; GCOL-drawn graphics and sprites plotted
directly (not via an icon) are not. `Wimp_ReadPixTrans` gives the
scale/translation-table pair to pass to `OS_SpriteOp` `PutSpriteScaled` when
plotting board/dice sprites by hand in a redraw handler (handles the
differing pixel aspect ratio between screen modes automatically).

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
