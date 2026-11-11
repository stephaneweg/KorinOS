/* Console texte du noyau : une grille de caractères en mémoire, dessinée sur
 * le framebuffer. Le défilement déplace la grille puis redessine : on ne relit
 * jamais le framebuffer (lent en écriture combinée ou non cachée).
 * Portable : ne dépend que de struct fb_info. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/lib.h>
#include <korin/spinlock.h>

static spinlock_t con_lock = SPINLOCK_INIT;

extern const uint8_t font_glyphs[256 * 16]; /* font8x16.inc */

#define FONT_W 8
#define FONT_H 16
#define MAX_COLS 480
#define MAX_ROWS 270

static struct fb_info fb;
static int ready;
static volatile int silent;
static uint32_t scale = 1;
static uint32_t cols, rows;      /* zone de texte, barre du haut exclue */
static uint32_t cx, cy;
static uint8_t cur_color = CON_NORMAL;
static uint16_t grid[MAX_ROWS][MAX_COLS]; /* octet bas : caractère, haut : couleur */
static uint32_t utf8_cp, utf8_left;

/* couleurs du moniteur, dans l'esprit de Givre (fond nuit, texte clair) */
static const uint8_t palette_rgb[CON_COLORS][3] = {
	[CON_NORMAL] = {0xE6, 0xEA, 0xF0},
	[CON_DIM] = {0x8A, 0x94, 0xA6},
	[CON_ACCENT] = {0x7C, 0xC4, 0xF2},
	[CON_OK] = {0x8F, 0xD6, 0x9C},
	[CON_WARN] = {0xF2, 0xC6, 0x6D},
	[CON_ERROR] = {0xF2, 0x7A, 0x7A},
};
static const uint8_t bg_rgb[3] = {0x14, 0x18, 0x22};
static const uint8_t bar_rgb[3] = {0x22, 0x2A, 0x3A};
static uint32_t palette[CON_COLORS], bg_px, bar_px;

static uint32_t pixel(const uint8_t rgb[3])
{
	if (fb.bgr)
		return ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2];
	return ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8) | rgb[0];
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t px)
{
	for (uint32_t j = 0; j < h; j++) {
		volatile uint32_t *p = fb.base + (uint64_t)(y + j) * fb.pitch + x;
		for (uint32_t i = 0; i < w; i++)
			p[i] = px;
	}
}

static void glyph(uint32_t px_x, uint32_t px_y, unsigned char c, uint32_t fg, uint32_t bg)
{
	const uint8_t *g = &font_glyphs[(uint32_t)c * FONT_H];
	for (uint32_t row = 0; row < FONT_H; row++) {
		uint8_t bits = g[row];
		for (uint32_t s = 0; s < scale; s++) {
			volatile uint32_t *p = fb.base + (uint64_t)(px_y + row * scale + s) * fb.pitch + px_x;
			for (uint32_t col = 0; col < FONT_W; col++) {
				uint32_t v = (bits & (0x80 >> col)) ? fg : bg;
				for (uint32_t t = 0; t < scale; t++)
					*p++ = v;
			}
		}
	}
}

static void draw_cell(uint32_t x, uint32_t y)
{
	uint16_t v = grid[y][x];
	uint8_t col = v >> 8;
	if (col >= CON_COLORS)
		col = CON_NORMAL;
	glyph(x * FONT_W * scale, (y + 1) * FONT_H * scale, v & 0xFF, palette[col], bg_px);
}

static void redraw(void)
{
	for (uint32_t y = 0; y < rows; y++)
		for (uint32_t x = 0; x < cols; x++)
			draw_cell(x, y);
}

static void bar_text(uint32_t x, const char *s)
{
	for (; *s && x < cols; s++, x++)
		glyph(x * FONT_W * scale, 0, (unsigned char)*s, palette[CON_ACCENT], bar_px);
}

void console_init(const struct fb_info *f)
{
	fb = *f;
	scale = fb.width >= 2560 ? 2 : 1;
	cols = fb.width / (FONT_W * scale);
	rows = fb.height / (FONT_H * scale);
	if (rows > 1)
		rows--; /* barre du haut */
	if (cols > MAX_COLS)
		cols = MAX_COLS;
	if (rows > MAX_ROWS)
		rows = MAX_ROWS;
	for (int i = 0; i < CON_COLORS; i++)
		palette[i] = pixel(palette_rgb[i]);
	bg_px = pixel(bg_rgb);
	bar_px = pixel(bar_rgb);
	ready = 1;
	console_clear();
}

int console_ready(void)
{
	return ready;
}

void console_clear(void)
{
	if (!ready)
		return;
	uint64_t f = spin_lock_irqsave(&con_lock);
	for (uint32_t y = 0; y < rows; y++)
		for (uint32_t x = 0; x < cols; x++)
			grid[y][x] = ' ' | (CON_NORMAL << 8);
	fill(0, 0, fb.width, FONT_H * scale, bar_px);
	fill(0, FONT_H * scale, fb.width, fb.height - FONT_H * scale, bg_px);
	bar_text(1, "Korin OS  x86-64");
	cx = cy = 0;
	spin_unlock_irqrestore(&con_lock, f);
}

void console_color(enum con_color c)
{
	cur_color = (uint8_t)c;
}

void console_size(uint32_t *c, uint32_t *r)
{
	*c = cols;
	*r = rows;
}

static void scroll(void)
{
	memmove(&grid[0][0], &grid[1][0], sizeof(grid[0]) * (rows - 1));
	for (uint32_t x = 0; x < cols; x++)
		grid[rows - 1][x] = ' ' | (CON_NORMAL << 8);
	redraw();
}

static void newline(void)
{
	cx = 0;
	if (++cy >= rows) {
		cy = rows - 1;
		scroll();
	}
}

static void putc_raw(unsigned char c)
{
	if (!ready || silent)
		return;
	switch (c) {
	case '\n':
		newline();
		return;
	case '\r':
		cx = 0;
		return;
	case '\b':
		if (cx > 0) {
			cx--;
			grid[cy][cx] = ' ' | (cur_color << 8);
			draw_cell(cx, cy);
		}
		return;
	case '\t':
		do
			putc_raw(' ');
		while (cx % 8);
		return;
	}
	if (c < 0x20)
		return;
	if (cx >= cols)
		newline();
	grid[cy][cx] = c | (cur_color << 8);
	draw_cell(cx, cy);
	cx++;
}

void console_putc(unsigned char c)
{
	uint64_t f = spin_lock_irqsave(&con_lock);
	putc_raw(c);
	spin_unlock_irqrestore(&con_lock, f);
}

void console_write(const char *s)
{
	uint64_t f = spin_lock_irqsave(&con_lock);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (utf8_left) {
			if ((c & 0xC0) == 0x80) {
				utf8_cp = (utf8_cp << 6) | (c & 0x3F);
				if (--utf8_left == 0)
					putc_raw(utf8_cp < 0x100 ? (unsigned char)utf8_cp : '?');
				continue;
			}
			utf8_left = 0;
			putc_raw('?');
		}
		if (c < 0x80)
			putc_raw(c);
		else if ((c & 0xE0) == 0xC0) {
			utf8_cp = c & 0x1F;
			utf8_left = 1;
		} else if ((c & 0xF0) == 0xE0) {
			utf8_cp = c & 0x0F;
			utf8_left = 2;
		} else if ((c & 0xF8) == 0xF0) {
			utf8_cp = c & 0x07;
			utf8_left = 3;
		} else
			putc_raw('?');
	}
	spin_unlock_irqrestore(&con_lock, f);
}

void console_silence(int on)
{
	silent = on;
	if (!on && ready)
		console_clear(); /* l'écran revient au noyau */
}

void console_status(const char *s)
{
	if (!ready || silent)
		return;
	uint32_t n = (uint32_t)strlen(s);
	uint32_t total = fb.width / (FONT_W * scale);
	if (n + 1 > total)
		return;
	uint32_t x = total - n - 1;
	uint64_t f = spin_lock_irqsave(&con_lock);
	for (uint32_t i = 0; i < n; i++)
		glyph((x + i) * FONT_W * scale, 0, (unsigned char)s[i], palette[CON_DIM], bar_px);
	spin_unlock_irqrestore(&con_lock, f);
}
