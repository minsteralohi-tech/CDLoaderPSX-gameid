#include <stdint-gcc.h>
#include <stdarg.h>
#include <stdbool.h>
#include "screen.h"
#include "gpu.h"
#include "bios.h"
#include "str.h"

/* Two pre-baked font atlases (see font.c): a small one for the scrolling log
 * and a 2x pre-scaled one for the title, so the GPU only ever needs to blit
 * 1:1 sprites - no runtime scaling required. Both cover the same glyph range
 * and only the left 6 (small) / 12 (big) texture columns are ever drawn,
 * which keeps the rendered text compact without needing narrower bitmaps. */
extern const unsigned char font8x8[];
extern const unsigned char font16x16[];
extern const unsigned int font_first;
extern const unsigned int font_last;

#define SCREEN_W 320
#define SCREEN_H 240

/* Small font: 8x8 cell, only 6 columns drawn. */
#define GLYPH_W   6
#define GLYPH_H   8
/* Big (title) font: 16x16 cell, only 12 columns drawn. */
#define GLYPH_W_BIG 12
#define GLYPH_H_BIG 16

/* Font atlases in VRAM, placed below the visible framebuffer. This codebase's
 * gpu_draw_tex_rect (from tonyhax's gpu.c) takes texcoord as an absolute VRAM
 * offset within the selected page, confirmed working on hardware for the
 * single-font version of this console. Keep both fonts' absolute Y values
 * inside the same 256-tall texture page (Y base = 256, selected below). */
#define FONT_VRAM_X   0
#define FONT_VRAM_Y   256
#define FONT_TEX_W    128 /* 16 glyphs/row, 8px cells */
#define FONT_TEX_H    48  /* ceil(91/16)=6 rows * 8px  */

#define FONT2_VRAM_X  0
#define FONT2_VRAM_Y  310 /* must stay within the same 256-tall page as FONT_VRAM_Y */
#define FONT2_TEX_W   256 /* 16 glyphs/row, 16px cells */
#define FONT2_TEX_H   96  /* 6 rows * 16px             */

/* Layout: fixed title bar, then a scrolling log area below a divider line.
 * Margins are generous to stay clear of CRT overscan. */
#define MARGIN_X    20
#define TITLE_Y     10
#define SUBTITLE_Y  (TITLE_Y + GLYPH_H_BIG + 2)
#define DIVIDER_Y   (SUBTITLE_Y + GLYPH_H + 6)
#define LOG_TOP     (DIVIDER_Y + 6)
#define LOG_BOTTOM  (SCREEN_H - 12)

#define LOG_COLS ((SCREEN_W - 2 * MARGIN_X) / GLYPH_W)
#define LOG_ROWS ((LOG_BOTTOM - LOG_TOP) / GLYPH_H)

static char text_buffer[LOG_ROWS][LOG_COLS + 1];
static int  line_count;

static bool is_pal;

static const char * title_text = "PS1 Disc Loader 0.1";
static const char * subtitle_text = "based on tonyhax international v1.6.3";

/* --- low level helpers --------------------------------------------------- */

static void set_video_mode(void)
{
	uint32_t mode = GPU_DISPLAY_H320 | GPU_DISPLAY_V240 | GPU_DISPLAY_15BPP |
	                (is_pal ? GPU_DISPLAY_PAL : GPU_DISPLAY_NTSC);
	gpu_display_mode(mode);

	/*
	 * Centred 320x240 timings, in GPU clock units (matching ps1-bare-metal's
	 * setupGPU). The vertical Y2 must stay <= 0x106 (NTSC) / 0x139 (PAL), or
	 * the GPU stops generating the vblank interrupt and any wait-for-vblank
	 * hangs.
	 */
	gpu_set_hrange(0x260, 0xC60);
	if (is_pal) {
		gpu_set_vrange(0x2B, 0x11B);
	} else {
		gpu_set_vrange(0x10, 0x100);
	}

	/* Display reads from VRAM (0,0). */
	SendGP1Command(0x05000000);
}

static void set_draw_env(void)
{
	gpu_point_t p = { 0, 0 };
	gpu_size_t  s = { SCREEN_W, SCREEN_H };
	gpu_set_drawing_area(&p, &s);
	GPU_cw(0xE5000000); /* drawing offset (0,0) */
	GPU_cw(0xE6000000); /* mask settings: none  */
	/* Texture page: base (0,256), 16bpp colour mode. */
	GPU_cw(0xE1000000 | (2 << 7) | (1 << 4));
}

static void upload_fonts(void)
{
	static uint16_t small_tex[FONT_TEX_H][FONT_TEX_W];
	static uint16_t big_tex[FONT2_TEX_H][FONT2_TEX_W];

	unsigned glyphs = font_last - font_first + 1;

	for (unsigned g = 0; g < glyphs; g++) {
		unsigned col = g % 16, row = g / 16;
		for (int y = 0; y < 8; y++) {
			unsigned char bits = font8x8[g * 8 + y];
			for (int x = 0; x < 8; x++) {
				small_tex[row * 8 + y][col * 8 + x] =
					(bits & (0x80 >> x)) ? 0x7FFF : 0x0000;
			}
		}
	}

	for (unsigned g = 0; g < glyphs; g++) {
		unsigned col = g % 16, row = g / 16;
		for (int y = 0; y < 16; y++) {
			unsigned idx = (g * 16 + y) * 2;
			uint16_t bits = ((uint16_t) font16x16[idx] << 8) | font16x16[idx + 1];
			for (int x = 0; x < 16; x++) {
				big_tex[row * 16 + y][col * 16 + x] =
					(bits & (0x8000 >> x)) ? 0x7FFF : 0x0000;
			}
		}
	}

	gpu_flush_cache();
	GPU_dw(FONT_VRAM_X, FONT_VRAM_Y, FONT_TEX_W, FONT_TEX_H, &small_tex[0][0]);
	GPU_dw(FONT2_VRAM_X, FONT2_VRAM_Y, FONT2_TEX_W, FONT2_TEX_H, &big_tex[0][0]);
}

static void clear_rect(int x, int y, int w, int h)
{
	gpu_solid_rect_t rect;
	rect.pos.x = x;
	rect.pos.y = y;
	rect.size.width = w;
	rect.size.height = h;
	rect.color = 0x000000;
	rect.semi_transp = 0;
	gpu_draw_solid_rect(&rect);
}

static unsigned glyph_index(char ch)
{
	unsigned code = (unsigned char) ch;
	if (code < font_first || code > font_last) {
		code = ' ';
	}
	return code - font_first;
}

static void draw_glyph_small(int px, int py, char ch)
{
	unsigned g = glyph_index(ch);
	gpu_tex_rect_t r;
	r.pos.x = px;
	r.pos.y = py;
	r.size.width = GLYPH_W;
	r.size.height = GLYPH_H;
	r.texcoord.x = FONT_VRAM_X + (g % 16) * 8;
	r.texcoord.y = FONT_VRAM_Y + (g / 16) * 8;
	r.clut.x = 0;
	r.clut.y = 0;
	r.color = 0x808080;
	r.semi_transp = 0;
	r.raw_tex = 1;
	gpu_draw_tex_rect(&r);
}

static void draw_glyph_big(int px, int py, char ch)
{
	unsigned g = glyph_index(ch);
	gpu_tex_rect_t r;
	r.pos.x = px;
	r.pos.y = py;
	r.size.width = GLYPH_W_BIG;
	r.size.height = GLYPH_H_BIG;
	r.texcoord.x = FONT2_VRAM_X + (g % 16) * 16;
	r.texcoord.y = FONT2_VRAM_Y + (g / 16) * 16;
	r.clut.x = 0;
	r.clut.y = 0;
	r.color = 0x808080;
	r.semi_transp = 0;
	r.raw_tex = 1;
	gpu_draw_tex_rect(&r);
}

static int text_width_small(const char * s)
{
	int n = 0;
	while (s[n] != '\0') {
		n++;
	}
	return n * GLYPH_W;
}

static int text_width_big(const char * s)
{
	int n = 0;
	while (s[n] != '\0') {
		n++;
	}
	return n * GLYPH_W_BIG;
}

static void draw_header(void)
{
	int tx = (SCREEN_W - text_width_big(title_text)) / 2;
	if (tx < MARGIN_X) {
		tx = MARGIN_X;
	}
	for (int i = 0; title_text[i] != '\0'; i++) {
		draw_glyph_big(tx + i * GLYPH_W_BIG, TITLE_Y, title_text[i]);
	}

	int sx = (SCREEN_W - text_width_small(subtitle_text)) / 2;
	if (sx < MARGIN_X) {
		sx = MARGIN_X;
	}
	for (int i = 0; subtitle_text[i] != '\0'; i++) {
		draw_glyph_small(sx + i * GLYPH_W, SUBTITLE_Y, subtitle_text[i]);
	}

	clear_rect(MARGIN_X, DIVIDER_Y, SCREEN_W - 2 * MARGIN_X, 1);
	gpu_solid_rect_t line;
	line.pos.x = MARGIN_X;
	line.pos.y = DIVIDER_Y;
	line.size.width = SCREEN_W - 2 * MARGIN_X;
	line.size.height = 1;
	line.color = 0x808080;
	line.semi_transp = 0;
	gpu_draw_solid_rect(&line);
}

static void redraw(void)
{
	gpu_wait_vblank();
	clear_rect(0, 0, SCREEN_W, SCREEN_H);
	draw_header();
	for (int row = 0; row < line_count; row++) {
		const char * line = text_buffer[row];
		for (int col = 0; line[col] != '\0' && col < LOG_COLS; col++) {
			draw_glyph_small(MARGIN_X + col * GLYPH_W, LOG_TOP + row * GLYPH_H,
			                  line[col]);
		}
	}
	gpu_flush_cache();
}

static void push_line(const char * line)
{
	if (line_count >= LOG_ROWS) {
		for (int r = 1; r < LOG_ROWS; r++) {
			strcpy(text_buffer[r - 1], text_buffer[r]);
		}
		line_count = LOG_ROWS - 1;
	}

	int i = 0;
	while (line[i] != '\0' && i < LOG_COLS) {
		text_buffer[line_count][i] = line[i];
		i++;
	}
	text_buffer[line_count][i] = '\0';
	line_count++;
}

/* --- public API ---------------------------------------------------------- */

void debug_init(void)
{
	is_pal = gpu_is_pal();
	line_count = 0;

	gpu_reset();
	gpu_display_disable();
	set_video_mode();
	set_draw_env();
	upload_fonts();
	clear_rect(0, 0, SCREEN_W, SCREEN_H);
	draw_header();
	gpu_flush_cache();
	gpu_display_enable();
}

void debug_switch_standard(bool pal)
{
	is_pal = pal;
	gpu_display_disable();
	set_video_mode();
	set_draw_env();
	gpu_display_enable();
	redraw();
}

void debug_write(const char * format, ...)
{
	char buffer[128];
	va_list args;
	va_start(args, format);
	mini_vsprintf(buffer, format, args);
	va_end(args);

	char line[LOG_COLS + 1];
	int col = 0;
	for (int i = 0;; i++) {
		char c = buffer[i];
		if (c == '\0' || c == '\n') {
			line[col] = '\0';
			push_line(line);
			col = 0;
			if (c == '\0') {
				break;
			}
		} else if (col < LOG_COLS) {
			line[col++] = c;
		}
	}

	redraw();
}
