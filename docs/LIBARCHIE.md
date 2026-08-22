# libarchie manual

`libarchie` is ArchieSDK's own small helper library (`SDK/lib/libarchie.a`,
headers under `SDK/include/archie/`) -- convenience wrappers around
low-level video/keyboard/SWI access that sit alongside OSLib in the same
toolchain. It's automatically available (no extra `-l` flag needed beyond
what `config.mk` already sets up) once a project links against ArchieSDK's
libc.

Canonical source: `~/riscos-dev/archiesdk/SDK/include/archie/*.h`. Update
this file if a future ArchieSDK version changes these headers.

## `archie/SDKTypes.h` -- fixed-width type aliases

```c
u64/i64, u32/i32, u16/i16, u8/i8   /* uint64_t/int64_t, ... via <stdint.h> */
```

Used throughout ArchieSDK's own headers and examples in place of
`<stdint.h>`'s longer names.

## `archie/video.h` -- screen mode and VSync helpers

```c
void  v_setMode(u32 mode);           /* switch screen mode (VDU 22, mode) */
void  v_disableTextCursor(void);     /* VDU 23,21,0,0,0,0,0,0,0,0 -- hide the text cursor */
void  v_enableVSync(void);           /* OS_Byte 14, event 4 (Vsync) */
void  v_disableVSync(void);          /* OS_Byte 13, event 4 */
void  v_waitForVSync(void);          /* OS_Byte 19 -- block until the next VSync */
void *v_getScreenAddress(void);      /* current screen memory base */
void  v_setBorderColourRGB(u8 r, u8 g, u8 b);
void  v_setBorderColour(u32 colour);
```

ArchieSDK's own `examples/oslib/main.c` and `examples/hello-world/main.c`
both use `v_setMode(13)` (mode 13, **256**-colour 320x256, 4x4 OS-unit
pixels -- confirmed against the RISC OS 3 PRM's sprite-modes table,
`~/riscos-dev/prm-mirror/sprites.html`; an earlier draft of this doc
mis-stated it as 16-colour) +
`v_disableTextCursor()` + `v_enableVSync()`/`atexit(quit)` as the standard
full-screen-game startup/teardown pattern. For a WIMP application this
isn't used -- the Wimp owns the screen mode -- but it's the natural choice
if ArchiLudo ever needs a non-desktop full-screen mode (e.g. a title/splash
screen shown before the Wimp task starts, matching what GeoLudo's splash
screen did).

## `archie/keyboard.h` -- keyboard/mouse polling

```c
bool k_checkKeypress(u32 key);
```

Polls the internal RISC OS keyboard matrix directly (not through the
Wimp's own key-event stream) -- appropriate for a full-screen non-WIMP
program, not for WIMP code (which must read keys via `Wimp_Poll`'s
`Key_Pressed` event so multitasking and the caret system keep working --
see `riscos_wimp_reference.md`'s Text/Escape section). `keyboard.h` defines
one `KEY_*` constant per physical key (`KEY_RETURN`, `KEY_SPACE`,
`KEY_LEFT`/`RIGHT`/`UP`/`DOWN`, `KEY_A`..`KEY_Z`, `KEY_0`..`KEY_9`,
function keys, keypad, mouse buttons via `KEY_LMOUSE`/`KEY_MMOUSE`/
`KEY_RMOUSE`) -- see the header itself for the full list, it's a flat
`#define` table with no further structure.

## `archie/utils.h`

```c
void p_clearConsole(void);
```

Clears the text-mode console (VDU-based). Not relevant to a WIMP app.

## `archie/SWI.h` -- raw SWI numbers and helpers

```c
#define OSByte_EventEnable   14
#define OSByte_EventDisable  13
#define OSByte_Vsync         19
#define OSByte_WriteVDUBank  112
#define OSByte_WriteDisplayBank 113
#define OSByte_ReadKey       129
#define OSWord_WritePalette  12
#define ErrorV   0x01
#define EventV   0x10
#define Event_VSync 4
#define VD_ScreenStart 148

#define swi(x) asm("SWI "#x : : : "cc", "memory")   /* inline-asm SWI call, no args/results */
```

Low-level constants backing `video.h`'s implementation; not normally
needed directly -- use `<swis.h>`'s `_swi`/`_swix` (below) for anything
`video.h`/`keyboard.h` don't already wrap.

## Raw SWI calls: `<swis.h>` / `<kernel.h>`

Not part of `libarchie` itself but the standard GCC-RISC-OS SWI-calling
convention that both `libarchie` and OSLib sit on top of, and what
ArchieSDK's own bundled examples (`examples/hello-wimp/main.c`) use
directly instead of OSLib:

```c
#include <swis.h>
#include <kernel.h>

/* register-mask helpers for _swi()/_swix() */
_IN(i)      /* mark register i as an input */
_INR(i,j)   /* mark registers i..j as inputs */
_OUT(i)     /* mark register i as an output */
_OUTR(i,j)  /* mark registers i..j as outputs */
_RETURN(i)  /* the SWI's function-result register */
_BLOCK(i)   /* an extra memory block of i bytes passed as a parameter block */

int result = _swi(Wimp_Initialise, _INR(0,2)|_RETURN(1), 300, *(int *)"TASK", "title");
```

or the lower-level explicit form used when you need every register back:

```c
_kernel_swi_regs regs;
regs.r[0] = 300;
regs.r[1] = *(int *)"TASK";
regs.r[2] = (int) "title";
_kernel_swi(Wimp_Initialise, &regs, &regs);
```

ArchiLudo's own `src/main.c` uses OSLib's typed wrappers
(`wimp_initialise()`, `wimp_create_icon()`, ...) instead of raw `_swi()`
calls wherever OSLib provides one, since the typed API catches struct/
register-packing mistakes at compile time -- see [OSLIB.md](OSLIB.md).
Raw `_swi()`/`_swix()` remains the right tool for any SWI OSLib doesn't
wrap (uncommon, but see `archie/SWI.h` above for the handful ArchieSDK
itself needed to write `video.h`/`keyboard.h`).
