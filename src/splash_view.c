/*
 * ArchiLudo splash view -- implementation.
 * See include/splash_view.h for the module overview and API docs.
 *
 * The "idi8b" (I Dream In 8 Bits) logo below is a hand-extracted
 * pixel-art reproduction of the real logo, not the logo file itself --
 * see logo_rects[]'s own comment for exactly where the data came from
 * and how it was derived.
 */

#include <string.h>
#include <stdio.h>

#include "oslib/wimp.h"
#include "oslib/colourtrans.h"
#include "oslib/wimpspriteop.h"

#include "splash_view.h"
#include "archiludo.h"
#include "setup_view.h"

#define MARGIN         16
#define LOGO_CELL       4
/* Dark-grey card the logo sits on -- per explicit user request, since the
 * pastel brand colours read poorly straight against a plain white/very
 * light grey window background. Padding is how far the card extends
 * beyond the logo's own tight bounding box on every side. */
#define LOGO_BG_PAD    12
#define TEXT_WIDTH    336
#define TEXT_LINE_HEIGHT 28
/* Vertical gap between text lines -- per explicit user request ("text is
 * too cramped without any pixel whiteline"): the lines used to sit
 * directly flush against each other with zero breathing room. */
#define TEXT_LINE_GAP   10
#define TEXT_LINES       5
#define TEXT_BLOCK_HEIGHT (TEXT_LINES * TEXT_LINE_HEIGHT + (TEXT_LINES - 1) * TEXT_LINE_GAP)
#define BUTTON_GAP      16
#define BUTTON_WIDTH   100
#define BUTTON_HEIGHT   40

/*
 * The logo, as a grid of coloured rectangles -- (x0, y0, x1, y1, colour),
 * grid cells, colour indexed into logo_palette[] below. Pixel-EXACT,
 * decoded from the real source art rather than approximated from a
 * screenshot of it: `idi8b-logo-lowercase.petmate`
 * (`/home/xahmol/git/idreamtin8bits-astro/src/assets/`, a private repo,
 * checked out locally) is a "Petmate" PETSCII-editor save -- a JSON grid
 * of (C64 screen code, C64 colour index) per character cell (40x25 here,
 * 27 columns x 11 rows actually used before a trailing "IDreamtIn8Bits.com"
 * byline row) -- and `idi8b-logo-lowercase.png` (the source used by two
 * earlier, less accurate attempts at this table) turned out to be a
 * direct screen capture of that same PETSCII art, not independent
 * artwork of its own; its apparent "curves" were just PNG antialiasing
 * of underlying blocky character-cell pixels, and downsampling it
 * (or the equivalent but coarser ANSI-art export, `idi8b-logo-ansi.ans`)
 * lost the design's distinctive thin vertical-bar strokes -- every
 * letter in this logo is literally built from vertical bars, which only
 * became obvious once decoded at the source's true resolution. Decoded
 * with a one-off Python script against a real C64 character ROM dump
 * (`chargen-901225-01.bin`, found in a local VICE SVN checkout at
 * `~/svn-mirror/vice/data/C64/` -- VICE itself doesn't bundle ROMs, they
 * being Commodore's copyrighted property, so this was the only verified
 * source available rather than hand-guessing PETSCII glyph bit patterns
 * from memory) to turn each cell's screen code into its real 8x8 pixel
 * bitmap, at the "lower" charset offset the .petmate file specifies;
 * same-colour pixel runs merged into rectangles (55 of them), then
 * halved in resolution (every coordinate came out exactly even, an
 * artifact of this particular font's glyphs, so no precision was lost)
 * to keep the table compact. Colour indices below are remapped from the
 * five raw C64 colours actually used to the real idi8b brand pastels
 * (`logo_palette[]`), in the same left-to-right "i-d-i-8-b" order as the
 * two earlier attempts used.
 */
typedef struct { signed char x0, y0, x1, y1, colour; } logo_rect;

#define LOGO_GRID_W 108
#define LOGO_GRID_H 40
#define LOGO_RECT_COUNT 55
static const logo_rect logo_rects[LOGO_RECT_COUNT] = {
	{  29,  0,  32, 40, 1 },  /* teal */
	{  33,  0,  36, 40, 1 },  /* teal */
	{  57,  0,  60, 40, 3 },  /* coral */
	{  61,  0,  64,  8, 3 },  /* coral */
	{  65,  0,  68,  8, 3 },  /* coral */
	{  69,  0,  72,  8, 3 },  /* coral */
	{  73,  0,  76,  2, 3 },  /* coral */
	{  84,  0,  87, 40, 4 },  /* purple */
	{  88,  0,  91, 40, 4 },  /* purple */
	{  54,  2,  56,  4, 3 },  /* coral */
	{  73,  2,  78,  4, 3 },  /* coral */
	{   0,  4,   3, 12, 0 },  /* green */
	{   4,  4,   7, 12, 0 },  /* green */
	{  40,  4,  43, 12, 2 },  /* blue */
	{  44,  4,  47, 12, 2 },  /* blue */
	{  53,  4,  56, 16, 3 },  /* coral */
	{  73,  4,  76, 16, 3 },  /* coral */
	{  77,  4,  80, 16, 3 },  /* coral */
	{   0, 16,   3, 40, 0 },  /* green */
	{   4, 16,   7, 40, 0 },  /* green */
	{  17, 16,  20, 40, 1 },  /* teal */
	{  21, 16,  24, 24, 1 },  /* teal */
	{  25, 16,  28, 24, 1 },  /* teal */
	{  40, 16,  43, 40, 2 },  /* blue */
	{  44, 16,  47, 40, 2 },  /* blue */
	{  54, 16,  56, 18, 3 },  /* coral */
	{  61, 16,  64, 24, 3 },  /* coral */
	{  65, 16,  68, 24, 3 },  /* coral */
	{  69, 16,  72, 24, 3 },  /* coral */
	{  73, 16,  78, 18, 3 },  /* coral */
	{  92, 16,  95, 24, 4 },  /* purple */
	{  96, 16,  99, 24, 4 },  /* purple */
	{ 100, 16, 103, 40, 4 },  /* purple */
	{  14, 18,  16, 20, 1 },  /* teal */
	{  73, 18,  76, 22, 3 },  /* coral */
	{ 104, 18, 106, 20, 4 },  /* purple */
	{  12, 20,  16, 36, 1 },  /* teal */
	{ 104, 20, 107, 36, 4 },  /* purple */
	{  54, 22,  56, 24, 3 },  /* coral */
	{  73, 22,  78, 24, 3 },  /* coral */
	{  53, 24,  56, 36, 3 },  /* coral */
	{  73, 24,  76, 36, 3 },  /* coral */
	{  77, 24,  80, 36, 3 },  /* coral */
	{  21, 32,  24, 40, 1 },  /* teal */
	{  25, 32,  28, 40, 1 },  /* teal */
	{  61, 32,  64, 40, 3 },  /* coral */
	{  65, 32,  68, 40, 3 },  /* coral */
	{  69, 32,  72, 40, 3 },  /* coral */
	{  92, 32,  95, 40, 4 },  /* purple */
	{  96, 32,  99, 40, 4 },  /* purple */
	{  14, 36,  16, 38, 1 },  /* teal */
	{  54, 36,  56, 38, 3 },  /* coral */
	{  73, 36,  78, 38, 3 },  /* coral */
	{ 104, 36, 106, 38, 4 },  /* purple */
	{  73, 38,  76, 40, 3 },  /* coral */
};
/* The idi8b brand's actual pastel colours, in the order letters 1-5
 * ("i","d","i","8","b") use them -- must match logo_rects[]'s colour
 * indices above (0=green .. 4=purple). */
static const int logo_palette[5][3] = {
	{ 178, 236, 145 },  /* 0: green */
	{ 132, 197, 204 },  /* 1: teal */
	{ 134, 122, 222 },  /* 2: blue */
	{ 192, 129, 120 },  /* 3: coral */
	{ 147,  81, 182 },  /* 4: purple */
};

#define LOGO_WIDTH  (LOGO_GRID_W * LOGO_CELL)
#define LOGO_HEIGHT (LOGO_GRID_H * LOGO_CELL)
#define WINDOW_WIDTH (MARGIN + (LOGO_WIDTH > TEXT_WIDTH ? LOGO_WIDTH : TEXT_WIDTH) + MARGIN)
#define LOGO_X0 (MARGIN + ((WINDOW_WIDTH - MARGIN * 2 - LOGO_WIDTH) / 2))
#define LOGO_Y1 (-MARGIN)
#define TEXT_X0 (MARGIN + ((WINDOW_WIDTH - MARGIN * 2 - TEXT_WIDTH) / 2))
#define TEXT_Y1 (LOGO_Y1 - LOGO_HEIGHT - LOGO_BG_PAD - MARGIN)
#define BUTTON_Y1 (TEXT_Y1 - TEXT_BLOCK_HEIGHT - BUTTON_GAP)
#define WINDOW_HEIGHT (MARGIN - (BUTTON_Y1 - BUTTON_HEIGHT))
#define BUTTON_X0 (MARGIN + ((WINDOW_WIDTH - MARGIN * 2 - BUTTON_WIDTH) / 2))

#define ICON_TITLE   0
#define ICON_DESC    1
#define ICON_VERSION 2
#define ICON_AUTHOR  3
#define ICON_URL     4
#define ICON_OK      5
#define WINDOW_ICON_COUNT 6

/* Sized for the full VERSION string (e.g. "v0.1.0-20260823-2129") -- per
 * explicit user request to show the complete version including its
 * build timestamp, not the major.minor.patch-only prefix this used to
 * truncate to. */
#define TEXT_LINE_BUF_LEN 32

static wimp_w window_handle = (wimp_w) -1;
/* Set by the current splash_view_open() call -- see its own doc
 * comment in splash_view.h. Read once by splash_view_click() when the
 * player dismisses the window. */
static int go_to_new_game_on_dismiss = 0;
static char version_text[TEXT_LINE_BUF_LEN];
static char text_line_buf[TEXT_LINES][TEXT_LINE_BUF_LEN];
static char ok_validation[4] = "R1";

/*
 * Function: set_gcol
 * Summary: Set the current graphics foreground colour for os_plot(),
 *          from plain RGB values (0..255 each) -- same helper as
 *          src/game_view.c's, duplicated here rather than shared since
 *          the two windows have no other reason to depend on each other.
 */
static void set_gcol(int r, int g, int b)
{
	os_colour colour = ((os_colour) b << 24) | ((os_colour) g << 16) | ((os_colour) r << 8);

	colourtrans_set_gcol(colour, colourtrans_SET_FG_GCOL, os_ACTION_OVERWRITE, 0);
}

static void fill_rect(int x0, int y0, int x1, int y1)
{
	os_plot(os_MOVE_TO, x0, y0);
	os_plot(os_PLOT_RECTANGLE + os_PLOT_TO, x1, y1);
}

void splash_view_initialise(void)
{
	wimp_WINDOW(WINDOW_ICON_COUNT) def;
	wimp_icon *icon;
	int i;
	static const char *desc_line[TEXT_LINES] = {
		"ArchiLudo", "Ludo for RISC OS", NULL, "By Xander Mol", "idreamtin8bits.com",
	};

	/* VERSION is passed by the Makefile as a build-time -D define (see
	 * Makefile's VERSION/VERSION_TIMESTAMP) -- e.g. "v0.1.0-20260823-1230".
	 * Shown in full, including the build timestamp -- per explicit user
	 * request (an earlier version truncated at the first '-'). */
	{
		size_t len = strlen(VERSION);

		if (len >= sizeof(version_text))
			len = sizeof(version_text) - 1;
		memcpy(version_text, VERSION, len);
		version_text[len] = '\0';
	}

	def.visible.x0 = 200;
	def.visible.y0 = 200;
	def.visible.x1 = 200 + WINDOW_WIDTH;
	def.visible.y1 = 200 + WINDOW_HEIGHT;
	def.xscroll = 0;
	def.yscroll = 0;
	def.next = wimp_TOP;
	def.flags = wimp_WINDOW_NEW_FORMAT | wimp_WINDOW_MOVEABLE |
	            wimp_WINDOW_BOUNDED_ONCE | wimp_WINDOW_BACK_ICON |
	            wimp_WINDOW_CLOSE_ICON | wimp_WINDOW_TITLE_ICON;
	def.title_fg = wimp_COLOUR_BLACK;
	def.title_bg = wimp_COLOUR_LIGHT_GREY;
	def.work_fg = wimp_COLOUR_BLACK;
	def.work_bg = wimp_COLOUR_VERY_LIGHT_GREY;
	def.scroll_outer = wimp_COLOUR_MID_LIGHT_GREY;
	def.scroll_inner = wimp_COLOUR_VERY_LIGHT_GREY;
	def.highlight_bg = wimp_COLOUR_CREAM;
	def.extra_flags = 0;
	def.extent.x0 = 0;
	def.extent.y0 = -WINDOW_HEIGHT;
	def.extent.x1 = WINDOW_WIDTH;
	def.extent.y1 = 0;
	def.title_flags = wimp_ICON_TEXT | wimp_ICON_BORDER | wimp_ICON_HCENTRED |
	                   wimp_ICON_VCENTRED | wimp_ICON_FILLED;
	/* The logo is the only custom-plotted content, and it's well away
	 * from the icons below -- but a window's work_flags controls whether
	 * clicking its plain background generates a Mouse_Click at all, so
	 * set this to CLICK anyway so clicking directly on the logo also
	 * dismisses the splash, not just clicking OK or the surrounding
	 * margin. */
	def.work_flags = (wimp_icon_flags) (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	def.sprite_area = wimpspriteop_AREA;
	def.xmin = WINDOW_WIDTH;
	def.ymin = WINDOW_HEIGHT;
	/* Window title text is a fixed 12-byte inline buffer -- "About
	 * ArchiLudo" doesn't fit, hence the shorter "About". */
	strncpy(def.title_data.text, "About", 12);
	def.icon_count = WINDOW_ICON_COUNT;

	for (i = 0; i < TEXT_LINES; i++) {
		icon = &def.icons[ICON_TITLE + i];
		icon->extent.x0 = TEXT_X0;
		icon->extent.x1 = TEXT_X0 + TEXT_WIDTH;
		icon->extent.y1 = TEXT_Y1 - i * (TEXT_LINE_HEIGHT + TEXT_LINE_GAP);
		icon->extent.y0 = icon->extent.y1 - TEXT_LINE_HEIGHT;
		/* Indirected: the fixed 12-byte inline icon text buffer is too
		 * short for lines like "Ludo for RISC OS" or
		 * "idreamtin8bits.com". No validation string needed (plain
		 * display text, not a button). */
		icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_HCENTRED |
		              wimp_ICON_VCENTRED |
		              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
		              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
		              (wimp_BUTTON_NEVER << wimp_ICON_BUTTON_TYPE_SHIFT);
		strncpy(text_line_buf[i], desc_line[i] ? desc_line[i] : version_text, TEXT_LINE_BUF_LEN - 1);
		text_line_buf[i][TEXT_LINE_BUF_LEN - 1] = '\0';
		icon->data.indirected_text.text = text_line_buf[i];
		icon->data.indirected_text.validation = "";
		icon->data.indirected_text.size = TEXT_LINE_BUF_LEN;
	}

	icon = &def.icons[ICON_OK];
	icon->extent.x0 = BUTTON_X0;
	icon->extent.x1 = BUTTON_X0 + BUTTON_WIDTH;
	icon->extent.y1 = BUTTON_Y1;
	icon->extent.y0 = BUTTON_Y1 - BUTTON_HEIGHT;
	/* Indirected for the same R1 "slab out" real-button look as
	 * game_view.c's Throw and setup_view.c's Start/Cancel -- see
	 * riscos_wimp_reference.md's Icons section. No press-flash needed
	 * (this window closes immediately on click, there's nothing left to
	 * un-flash back to). */
	icon->flags = wimp_ICON_TEXT | wimp_ICON_INDIRECTED | wimp_ICON_BORDER |
	              wimp_ICON_HCENTRED | wimp_ICON_VCENTRED | wimp_ICON_FILLED |
	              (wimp_COLOUR_BLACK << wimp_ICON_FG_COLOUR_SHIFT) |
	              (wimp_COLOUR_VERY_LIGHT_GREY << wimp_ICON_BG_COLOUR_SHIFT) |
	              (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon->data.indirected_text.text = "OK";
	icon->data.indirected_text.validation = ok_validation;
	icon->data.indirected_text.size = 3;

	window_handle = wimp_create_window((wimp_window *) &def);
}

void splash_view_open(int new_go_to_new_game_on_dismiss)
{
	wimp_window_state state;

	if (window_handle == (wimp_w) -1)
		return;

	go_to_new_game_on_dismiss = new_go_to_new_game_on_dismiss;

	state.w = window_handle;
	wimp_get_window_state(&state);
	state.next = wimp_TOP;
	wimp_open_window((wimp_open *) &state);
}

wimp_w splash_view_window_handle(void)
{
	return window_handle;
}

void splash_view_redraw(wimp_draw *redraw)
{
	osbool more;

	more = wimp_redraw_window(redraw);
	while (more) {
		int origin_x = redraw->box.x0 - redraw->xscroll;
		int origin_y = redraw->box.y1 - redraw->yscroll;
		int i;

		/* Dark-grey card behind the logo -- per explicit user request,
		 * the pastel brand colours read poorly straight against the
		 * window's own light background. */
		set_gcol(64, 64, 64);
		fill_rect(origin_x + LOGO_X0 - LOGO_BG_PAD,
		          origin_y + LOGO_Y1 - LOGO_HEIGHT - LOGO_BG_PAD,
		          origin_x + LOGO_X0 + LOGO_WIDTH + LOGO_BG_PAD,
		          origin_y + LOGO_Y1 + LOGO_BG_PAD);

		for (i = 0; i < LOGO_RECT_COUNT; i++) {
			const logo_rect *r = &logo_rects[i];
			int x0 = origin_x + LOGO_X0 + r->x0 * LOGO_CELL;
			int x1 = origin_x + LOGO_X0 + r->x1 * LOGO_CELL;
			int y1 = origin_y + LOGO_Y1 - r->y0 * LOGO_CELL;
			int y0 = origin_y + LOGO_Y1 - r->y1 * LOGO_CELL;

			set_gcol(logo_palette[(int) r->colour][0],
			         logo_palette[(int) r->colour][1],
			         logo_palette[(int) r->colour][2]);
			fill_rect(x0, y0, x1, y1);
		}

		more = wimp_get_rectangle(redraw);
	}
}

void splash_view_click(wimp_pointer *pointer)
{
	/* Anything in this window dismisses it -- OK, or clicking the
	 * background/logo (work_flags is BUTTON_CLICK, see
	 * splash_view_initialise()). The always-BUTTON_NEVER text icons
	 * don't generate a click at all if clicked directly, which is a
	 * minor, acceptable inconsistency for a dismiss-anywhere splash. */
	if (pointer->i == ICON_OK || pointer->i == wimp_ICON_WINDOW) {
		wimp_close_window(window_handle);
		/* Only for the automatic launch splash (see splash_view_open()'s
		 * own doc comment) -- go straight into the New Game dialogue
		 * rather than making the player make a second, separate
		 * iconbar click for it, matching the iconbar click's own
		 * "first click ever asks for player details" behaviour (see
		 * main.c). A later, player-triggered reopen via the About menu
		 * entry never does this, regardless of whether a game happens
		 * to be underway. */
		if (go_to_new_game_on_dismiss) {
			go_to_new_game_on_dismiss = 0;
			setup_view_open();
		}
	}
}
