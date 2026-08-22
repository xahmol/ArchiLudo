# OSLib usage manual

How ArchiLudo uses OSLib, and where to look up anything it doesn't cover
yet. See [BUILDCHAIN.md](BUILDCHAIN.md) for how OSLib is linked
(`-lOSLib32`, bundled with ArchieSDK at `SDK/include/oslib/` /
`SDK/lib/libOSLib32.a`) and [LIBARCHIE.md](LIBARCHIE.md) for the
lower-level `libarchie`/raw-SWI alternative OSLib sits alongside.

## Why OSLib instead of raw `_swi()` calls

ArchieSDK's own bundled example (`examples/hello-wimp/main.c`) uses raw
`_swi()`/`_kernel_swi()` calls with manual register/bitmask packing (see
`LIBARCHIE.md`). ArchiLudo uses OSLib's typed wrappers instead wherever
one exists (`wimp_initialise()`, `wimp_create_icon()`, `wimp_poll()`, ...)
because the compiler catches struct-layout and argument-order mistakes
that raw register packing would only surface at runtime, as a WIMP-crash
or garbage-icon bug. This matches the OSLib project's own stated design
goal (`SDK/include/oslib/wimp.h` and upstream docs): "type safety,
efficiency, and obvious syntax" over hand-rolled SWI calls.

## What ArchiLudo currently uses (`src/main.c`)

```c
#include "oslib/wimp.h"

wimp_t task_handle = wimp_initialise(wimp_VERSION_RO30, APP_NAME, NULL, &version_out);

wimp_icon_create icon;
icon.w = wimp_ICON_BAR_RIGHT;
icon.icon.extent = ...;
icon.icon.flags = wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED
                | (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
icon.icon.data.text[0..] = ...;
wimp_create_icon(&icon);

wimp_block block;
wimp_event_no reason = wimp_poll(0, &block, NULL);
switch (reason) {
case wimp_MOUSE_CLICK:        /* block.pointer.w, block.pointer.i, block.pointer.buttons */
case wimp_USER_MESSAGE:
case wimp_USER_MESSAGE_RECORDED: /* block.message.action, compare against message_QUIT */
}

wimp_close_down(task_handle);
```

Key facts confirmed directly against `SDK/include/oslib/wimp.h` while
writing this (don't take these from memory -- grep the actual header,
it's authoritative over any recollection or web reference):

- `wimp_initialise(version, name, messages, version_out)` takes a plain
  `char const *name` -- OSLib handles the `R1 = "TASK"` packed-bytes
  convention internally, unlike the raw-SWI form.
- `wimp_icon_create { wimp_w w; wimp_icon icon; }`;
  `wimp_icon { os_box extent; wimp_icon_flags flags; wimp_icon_data data; }`;
  the icon data union's `char text[12]` member is used directly for a
  short non-indirected text icon (no validation string, no sprite).
- `wimp_ICON_BAR_RIGHT`/`wimp_ICON_BAR_LEFT` are `wimp_w` sentinel values
  for `Wimp_CreateIcon`'s window handle, not real window handles.
- `wimp_ICON_BAR` (`0xFFFFFFFE`) is the sentinel returned in
  `wimp_pointer.w` for a click on the icon bar itself -- compare against
  this to detect an iconbar click in `wimp_MOUSE_CLICK`.
- `message_QUIT` (`0x0`) is defined directly in `wimp.h`, no separate
  `messages.h` needed.
- `wimp_VERSION_RO30` (`0x12C` = 300) is the version constant matching
  RISC OS 3.0/3.10 -- what `wimp_initialise()` should be called with for
  this project's target.

## Where to look up anything not covered above

In priority order:

1. **`riscos_wimp_reference.md`** (project root) -- curated summary of
   the Wimp SWI/message/icon/menu/sprite conventions, built from the PRM
   and the Pinknoise archive. Check here first.
2. **Local PRM mirror** -- `~/riscos-dev/prm-mirror/` (144 pages, ~12MB,
   full offline copy of `https://www.riscos.com/support/developers/prm/`).
   `wimp.html` is Part 7 "The Window Manager"; `sprites.html` and
   `vdu.html` are Part 3. Grep this directory directly for anything the
   curated reference doesn't go into enough depth on -- it's the actual
   manual, not a summary of it.
3. **Local Fryatt `wimp-prog` mirror** -- `~/riscos-dev/wimp-prog-mirror/wimp-prog/`
   (29 pages, offline copy of `https://www.stevefryatt.org.uk/risc-os/wimp-prog/`).
   Tutorial-form walkthrough of the same material in application-building
   order (iconbar -> windows -> icons -> menus -> keyboard -> messages);
   useful for the *shape* of an implementation even though its own
   SFLib-based code doesn't run under ArchieSDK (see
   [BUILDCHAIN.md](BUILDCHAIN.md) for why) -- only the OSLib call
   patterns and architecture carry over, not the library.
4. **`SDK/include/oslib/*.h` directly** -- the bundled headers are the
   ground truth for exact struct layouts/constants/function signatures
   for *this* toolchain's OSLib build. When in doubt, grep here rather
   than trusting a remembered API shape (upstream OSLib has had ABI
   changes across versions).

## Updating this file

Add a fact here whenever a new OSLib area gets used for the first time
(windows/redraw, menus, sprites, messages, dialogue boxes) -- see the
"Keep docs updated" note in the top-level `CLAUDE.md`.
