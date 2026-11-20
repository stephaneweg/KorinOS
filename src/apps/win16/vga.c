/* vga.c -- win16, la machine DOS : la carte VGA (docs/etude/WIN16.md §9.3) et son rendu dans la fenêtre de Korin.
 *
 * Modes texte (0-3, 7 ; 25, 28, 43 ou 50 lignes) : caractère et attribut en B8000 (B0000 en mode 7), comme sur une
 * vraie carte. Modes de la CGA (4, 5, 6) : la mémoire entrelacée de B8000. Modes de l'EGA et de la VGA en plans
 * (0Dh, 0Eh, 0Fh, 10h, 11h, 12h) et le mode 13h « déchaîné » (Mode X) : 4 plans de 64 Ko derrière A0000-AFFFF, lus
 * et écrits par les registres du séquenceur et du contrôleur graphique (modes d'écriture 0-3, verrous, set/reset,
 * masque de bits, rotation, lecture par plan et comparaison de couleurs) ; le cœur du processeur passe alors par
 * cpu_vga_rd et cpu_vga_wr. Mode 13h chaîné : la mémoire de A0000 telle quelle. Le rendu relit tout cela à chaque
 * image avec le DAC (palette de 6 bits) et l'agrandit à la fenêtre (320 × 200 -> 640 × 400).
 * Référence : vga.c de winevdm / Wine (LGPL-2.1) et la documentation des registres de la VGA. */
#include <stdio.h>
#include <string.h>
#include "dos.h"
#include "be.h"

#define BDA(o)	(0x400u + (o))

int vga_width = 640, vga_height = 400;
static int mode = 3, cols = 80, rows = 25, char_h = 16, graphics;
static uint8_t seq_index, gc_index, crtc_index;
static uint8_t crtc[0x19], seq[5], gc[9], attr[0x15], attr_index, attr_ff, misc = 0x67;
static uint8_t dac[256][3], dac_w, dac_r, dac_sub, dac_state, pel_mask = 0xff;
static uint8_t font[256 * 32];	/* la police du mode texte (le plan 2), 32 octets par caractère */
static uint8_t plane[4][65536];	/* la mémoire de la VGA en plans */
static uint8_t latch[4];
static uint64_t frames;

enum { K_TEXT, K_CGA4, K_CGA2, K_PLANAR, K_256 };
static int kind = K_TEXT;

/* ======================================================================== la palette */
static const uint8_t ega16[16] = { 0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };

static void dac_set (int i, uint8_t r, uint8_t g, uint8_t b)
{
	dac[i & 255][0] = r & 63;
	dac[i & 255][1] = g & 63;
	dac[i & 255][2] = b & 63;
}

/* les 64 couleurs de l'EGA (rgbRGB) */
static void dac_ega (void)
{
	for (int i = 0; i < 64; i++)
		dac_set (i, (uint8_t) (((i >> 2) & 1) * 42 + ((i >> 5) & 1) * 21), (uint8_t) (((i >> 1) & 1) * 42 + ((i >> 4) & 1) * 21),
			(uint8_t) ((i & 1) * 42 + ((i >> 3) & 1) * 21));
}

/* la palette de 256 couleurs du BIOS de la VGA : 16 de la CGA, 16 gris, 3 × 3 × 24 teintes */
static void dac_256 (void)
{
	for (int i = 0; i < 16; i++)
	{
		int e = ega16[i];
		dac_set (i, (uint8_t) (((e >> 2) & 1) * 42 + ((e >> 5) & 1) * 21), (uint8_t) (((e >> 1) & 1) * 42 + ((e >> 4) & 1) * 21),
			(uint8_t) ((e & 1) * 42 + ((e >> 3) & 1) * 21));
	}
	static const uint8_t grey[16] = { 0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63 };
	for (int i = 0; i < 16; i++)
		dac_set (16 + i, grey[i], grey[i], grey[i]);
	static const uint8_t lv[3][3][5] = {	/* intensité haute, moyenne, basse ; saturation forte, moyenne, faible */
		{ { 0, 16, 31, 47, 63 }, { 31, 39, 47, 55, 63 }, { 45, 49, 54, 58, 63 } },
		{ { 0, 7, 14, 21, 28 }, { 14, 17, 21, 24, 28 }, { 20, 22, 24, 26, 28 } },
		{ { 0, 4, 8, 12, 16 }, { 8, 10, 12, 14, 16 }, { 11, 12, 13, 15, 16 } } };
	int n = 32;
	for (int v = 0; v < 3; v++)
		for (int s = 0; s < 3; s++)
		{
			const uint8_t *l = lv[v][s];
			/* (24 teintes : bleu -> magenta -> rouge -> jaune -> vert -> cyan -> bleu, 4 pas par arête) */
			for (int h = 0; h < 24; h++)
			{
				int seg = h / 4, k = h % 4;
				int up = l[k], down = l[4 - k], lo = l[0], hi = l[4];
				int r = 0, g = 0, b = 0;
				switch (seg)
				{
				case 0: r = up; g = lo; b = hi; break;
				case 1: r = hi; g = lo; b = down; break;
				case 2: r = hi; g = up; b = lo; break;
				case 3: r = down; g = hi; b = lo; break;
				case 4: r = lo; g = hi; b = up; break;
				case 5: r = lo; g = down; b = hi; break;
				}
				dac_set (n++, (uint8_t) r, (uint8_t) g, (uint8_t) b);
			}
		}
	for (; n < 256; n++)
		dac_set (n, 0, 0, 0);
}

/* les modes de 200 lignes (4, 5, 6, 0Dh, 0Eh) : le BIOS de la VGA y imite la CGA (bit 4 = intensité) */
static void dac_cga (void)
{
	for (int i = 0; i < 64; i++)
	{
		int in = (i >> 4) & 1;
		int r = ((i >> 2) & 1) * 42 + in * 21, g = ((i >> 1) & 1) * 42 + in * 21, b = (i & 1) * 42 + in * 21;
		if ((i & 0x17) == 6)
			g = 21;	/* (le marron de la CGA) */
		dac_set (i, (uint8_t) r, (uint8_t) g, (uint8_t) b);
	}
}

/* une des 16 couleurs de la CGA -> le registre de palette des modes de 200 lignes */
static uint8_t cga16 (int c) { return (uint8_t) ((c & 7) | (c & 8 ? 0x10 : 0)); }

static int cga_bg, cga_bright = 1, cga_pal = 1;

static void cga_apply (void);

static uint32_t rgb_of (int idx)
{
	idx &= pel_mask;
	uint32_t r = dac[idx][0] & 63, g = dac[idx][1] & 63, b = dac[idx][2] & 63;
	return (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
}

/* une couleur de 4 bits, par les registres de palette des attributs -> pixel */
static uint32_t text_color (int c)
{
	int p = attr[c & 15] & 0x3f;
	if (attr[0x10] & 0x80)
		p = (p & 0x0f) | (attr[0x14] & 3) << 4;
	return rgb_of (p | (attr[0x14] & 0x0c) << 4);
}

/* ======================================================================== la mémoire en plans */
static uint8_t vga_rd (uint32_t a)
{
	uint32_t off = a - 0xa0000;
	for (int p = 0; p < 4; p++)
		latch[p] = plane[p][off];
	if (gc[5] & 8)
	{
		/* (mode de lecture 1 : comparaison des couleurs) */
		uint8_t r = 0xff;
		for (int p = 0; p < 4; p++)
			if (gc[7] & (1 << p))
				r &= (uint8_t) ~(latch[p] ^ ((gc[2] >> p) & 1 ? 0xff : 0));
		return r;
	}
	return latch[gc[4] & 3];
}

static uint8_t alu (uint8_t v, int p)
{
	switch ((gc[3] >> 3) & 3)
	{
	case 1: return v & latch[p];
	case 2: return v | latch[p];
	case 3: return v ^ latch[p];
	}
	return v;
}

static void vga_wr (uint32_t a, uint8_t v)
{
	uint32_t off = a - 0xa0000;
	int wm = gc[5] & 3, rot = gc[3] & 7;
	uint8_t rv = (uint8_t) ((v >> rot) | (v << (8 - rot)));
	for (int p = 0; p < 4; p++)
	{
		if (!(seq[2] & (1 << p)))
			continue;
		uint8_t d, mask = gc[8];
		switch (wm)
		{
		case 0:
			d = (gc[1] & (1 << p)) ? ((gc[0] >> p) & 1 ? 0xff : 0) : rv;
			d = alu (d, p);
			break;
		case 1:
			plane[p][off] = latch[p];
			continue;
		case 2:
			d = alu ((v >> p) & 1 ? 0xff : 0, p);
			break;
		default:
			mask &= rv;
			d = alu ((gc[0] >> p) & 1 ? 0xff : 0, p);
			break;
		}
		plane[p][off] = (uint8_t) ((d & mask) | (latch[p] & ~mask));
	}
}

/* le mode 13h déchaîné par le programme (Mode X) : la mémoire plate passe dans les plans, et inversement */
static void chain_changed (void)
{
	if (kind != K_256)
		return;
	int unchained = !(seq[4] & 8);
	if (unchained == cpu_vga_on)
		return;
	uint8_t *flat = lin_ptr (0xa0000);
	if (unchained)
		for (int i = 0; i < 65536; i++)
			plane[i & 3][i >> 2] = flat[i];
	else
		for (int i = 0; i < 65536; i++)
			flat[i] = plane[i & 3][i >> 2];
	cpu_vga_on = unchained;
}

/* ======================================================================== l'état du mode */
static uint32_t text_base (void) { return mode == 7 ? 0xb0000 : 0xb8000; }
static int page_size (void) { return (cols * rows * 2 + 0xff) & ~0xff; }
static int cur_page (void) { return lin_ptr (BDA (0x62))[0]; }

static void load_font (const uint8_t *src, int h, int first, int count)
{
	for (int c = first; c < first + count && c < 256; c++)
	{
		memset (font + c * 32, 0, 32);
		memcpy (font + c * 32, src + (c - first) * h, (size_t) h);
	}
}

static uint16_t font_off (int h) { return h == 16 ? FONT16_OFF : h == 14 ? FONT14_OFF : FONT8_OFF; }

static void font_rom (int h)
{
	load_font (lin_ptr (RM (VROM_SEG, font_off (h))), h, 0, 256);
	char_h = h;
}

static void resize (int w, int h)
{
	if (w != vga_width || h != vga_height)
	{
		vga_width = w;
		vga_height = h;
		if (bios_win)
			be_window_resize (bios_win, w, h);
	}
}

static void set_rows (int r)
{
	rows = r;
	lin_ptr (BDA (0x84))[0] = (uint8_t) (rows - 1);
	wr16 (BDA (0x85), (uint16_t) char_h);
	if (graphics)
		return;
	wr16 (BDA (0x4c), (uint16_t) page_size ());
	crtc[0x09] = (uint8_t) ((crtc[0x09] & 0xe0) | (char_h - 1));
	crtc[0x0a] = (uint8_t) (char_h - 3);
	crtc[0x0b] = (uint8_t) (char_h - 2);
	int h = rows * char_h;
	resize (640, h < 200 ? 200 : h);
}

static void cursor_crtc (void)
{
	int pg = cur_page ();
	int col = lin_ptr (BDA (0x50 + pg * 2))[0], row = lin_ptr (BDA (0x51 + pg * 2))[0];
	int pos = (int) rd16 (BDA (0x4e)) / 2 + row * cols + col;
	crtc[0x0e] = (uint8_t) (pos >> 8);
	crtc[0x0f] = (uint8_t) pos;
}

static void start_crtc (void)
{
	int s = (int) rd16 (BDA (0x4e)) / (graphics ? 1 : 2);
	crtc[0x0c] = (uint8_t) (s >> 8);
	crtc[0x0d] = (uint8_t) s;
}

static uint8_t *cell (int page, int row, int col)
{
	return lin_ptr (text_base () + (uint32_t) (page * page_size () + (row * cols + col) * 2));
}

/* les registres du CRTC qui décrivent l'image : largeur (caractères - 1), affichage vertical, doublage, pas */
static void crtc_mode (int hchars, int lines, int dbl, int max_scan, int offset, int byte_mode)
{
	crtc[0x01] = (uint8_t) (hchars - 1);
	int vde = lines - 1;
	crtc[0x12] = (uint8_t) vde;
	crtc[0x07] = (uint8_t) ((crtc[0x07] & ~0x42) | ((vde >> 8) & 1) << 1 | ((vde >> 9) & 1) << 6);
	crtc[0x09] = (uint8_t) ((dbl ? 0x80 : 0) | 0x40 | max_scan);
	crtc[0x13] = (uint8_t) offset;
	crtc[0x14] = byte_mode == 2 ? 0x40 : 0x00;
	crtc[0x17] = byte_mode == 1 ? 0xe3 : 0xa3;
}

/* la palette des modes 4 et 5 : fond, intensité, palette 0 (vert, rouge, marron) ou 1 (cyan, magenta, blanc) */
static void cga_apply (void)
{
	if (kind == K_CGA2)
	{
		attr[1] = cga16 (cga_bg);
		return;
	}
	attr[0] = cga16 (cga_bg);
	if (kind != K_CGA4)
		return;
	for (int i = 1; i < 4; i++)
		attr[i] = cga16 (i * 2 + cga_pal + (cga_bright ? 8 : 0));
}

void vga_set_mode (int m, int clear)
{
	svga_off ();
	mode = m;
	graphics = !(m <= 3 || m == 7);
	kind = !graphics ? K_TEXT : (m == 4 || m == 5) ? K_CGA4 : m == 6 ? K_CGA2 : m == 0x13 ? K_256 : K_PLANAR;
	cols = (m <= 1 || m == 4 || m == 5 || m == 0x0d || m == 0x13) ? 40 : 80;
	attr_ff = 0;
	dac_ega ();
	for (int i = 0; i < 16; i++)
		attr[i] = ega16[i];
	if (m >= 4 && m <= 6 || m == 0x0d || m == 0x0e)
	{
		dac_cga ();
		for (int i = 0; i < 16; i++)
			attr[i] = cga16 (i);
	}
	attr[0x10] = graphics ? 0x01 : 0x0c;
	attr[0x11] = 0;
	attr[0x12] = 0x0f;
	attr[0x13] = 0;
	attr[0x14] = 0;
	seq[2] = graphics ? 0x0f : 0x03;
	seq[4] = kind == K_256 ? 0x0e : graphics ? 0x06 : 0x02;
	memset (gc, 0, sizeof gc);
	gc[5] = kind == K_256 ? 0x40 : graphics ? 0x00 : 0x10;
	gc[6] = graphics ? 0x05 : 0x0e;
	gc[7] = 0x0f;
	gc[8] = 0xff;
	int ch = 16, nrows = 25;
	switch (m)
	{
	case 4: case 5:
		cga_bg = 0; cga_bright = 1; cga_pal = 1;	/* (cyan, magenta, blanc vifs) */
		cga_apply ();
		ch = 8; crtc_mode (40, 400, 1, 1, 40, 0);
		break;
	case 6:
		attr[1] = 0x17;
		ch = 8; crtc_mode (80, 400, 1, 1, 40, 0);
		break;
	case 0x0d: ch = 8; crtc_mode (40, 400, 1, 0, 20, 1); break;
	case 0x0e: ch = 8; crtc_mode (80, 400, 1, 0, 40, 1); break;
	case 0x0f: case 0x10: ch = 14; crtc_mode (80, 350, 0, 0, 40, 1); break;
	case 0x11: case 0x12: ch = 16; nrows = 30; crtc_mode (80, 480, 0, 0, 40, 1); break;
	case 0x13:
		for (int i = 0; i < 16; i++)
			attr[i] = (uint8_t) i;
		attr[0x10] = 0x41;
		dac_256 ();
		ch = 8; crtc_mode (80, 400, 0, 1, 40, 2);
		break;
	default:
		crtc_mode (cols, 400, 0, 15, cols / 2, 0);
		break;
	}
	if (m == 0x11)
		attr[1] = 0x3f;
	cpu_vga_on = kind == K_PLANAR;
	cpu_vga_rd = vga_rd;
	cpu_vga_wr = vga_wr;
	font_rom (graphics ? ch : 16);
	/* l'INT 43h : la police des modes graphiques */
	wr16 (0x43 * 4, font_off (ch));
	wr16 (0x43 * 4 + 2, VROM_SEG);
	lin_ptr (BDA (0x49))[0] = (uint8_t) m;
	wr16 (BDA (0x4a), (uint16_t) cols);
	wr16 (BDA (0x4e), 0);
	wr16 (BDA (0x63), m == 7 ? 0x3b4 : 0x3d4);
	lin_ptr (BDA (0x62))[0] = 0;
	memset (lin_ptr (BDA (0x50)), 0, 16);
	wr16 (BDA (0x60), 0x0e0d);
	lin_ptr (BDA (0x65))[0] = 0x29;
	lin_ptr (BDA (0x87))[0] = 0x60;
	lin_ptr (BDA (0x88))[0] = 0xf9;
	lin_ptr (BDA (0x89))[0] = 0x51;
	set_rows (nrows);
	if (graphics)
		wr16 (BDA (0x4c), (uint16_t) (kind == K_256 ? 0xfa00 : kind == K_CGA4 || kind == K_CGA2 ? 0x4000 :
			(m >= 0x11 ? 38400 : m >= 0x0f ? 28000 : m == 0x0e ? 16000 : 8000)));
	if (clear)
	{
		if (!graphics)
			for (uint32_t i = 0; i < 0x8000; i += 2)
				wr16 (text_base () + i, 0x0720);
		else if (kind == K_PLANAR)
			memset (plane, 0, sizeof plane);
		else if (kind == K_256)
			memset (lin_ptr (0xa0000), 0, 0x10000);
		else
			memset (lin_ptr (0xb8000), 0, 0x8000);
	}
	start_crtc ();
	cursor_crtc ();
	w16_log ("DOS : mode vidéo %02xh\n", m);
}

void vga_init (void)
{
	memset (crtc, 0, sizeof crtc);
	vga_set_mode (3, 1);
}

/* ======================================================================== les pixels du BIOS (INT 10h 0Ch, 0Dh, texte) */
static int gfx_w (void) { return mode == 0x0d || kind == K_CGA4 || kind == K_256 ? 320 : 640; }
static int gfx_h (void) { return mode >= 0x11 && mode <= 0x12 ? 480 : mode == 0x0f || mode == 0x10 ? 350 : 200; }

static void put_pixel (int x, int y, int c)
{
	if (x < 0 || y < 0 || x >= gfx_w () || y >= gfx_h ())
		return;
	int x_or = c & 0x80;
	if (kind == K_CGA4 || kind == K_CGA2)
	{
		int bpp = kind == K_CGA4 ? 2 : 1, ppb = 8 / bpp;
		uint8_t *b = lin_ptr (0xb8000 + (uint32_t) ((y & 1) * 0x2000 + (y >> 1) * 80 + x / ppb));
		int sh = (ppb - 1 - x % ppb) * bpp;
		uint8_t m = (uint8_t) (((1 << bpp) - 1) << sh), v = (uint8_t) ((c & ((1 << bpp) - 1)) << sh);
		*b = x_or ? (uint8_t) (*b ^ v) : (uint8_t) ((*b & ~m) | v);
	}
	else if (kind == K_256)
	{
		uint8_t *b = cpu_vga_on ? &plane[x & 3][(y * 320 + x) >> 2] : lin_ptr (0xa0000 + (uint32_t) (y * 320 + x));
		*b = x_or ? (uint8_t) (*b ^ c) : (uint8_t) c;
	}
	else if (kind == K_PLANAR)
	{
		uint32_t off = (uint32_t) (y * (crtc[0x13] * 2) + x / 8) + (uint32_t) rd16 (BDA (0x4e)) * 0;
		uint8_t bit = (uint8_t) (0x80 >> (x & 7));
		for (int p = 0; p < 4; p++)
		{
			int on = (c >> p) & 1;
			if (x_or)
			{
				if (on)
					plane[p][off] ^= bit;
			}
			else if (on)
				plane[p][off] |= bit;
			else
				plane[p][off] &= (uint8_t) ~bit;
		}
	}
}

static int get_pixel (int x, int y)
{
	if (x < 0 || y < 0 || x >= gfx_w () || y >= gfx_h ())
		return 0;
	if (kind == K_CGA4 || kind == K_CGA2)
	{
		int bpp = kind == K_CGA4 ? 2 : 1, ppb = 8 / bpp;
		uint8_t b = lin_ptr (0xb8000 + (uint32_t) ((y & 1) * 0x2000 + (y >> 1) * 80 + x / ppb))[0];
		return (b >> ((ppb - 1 - x % ppb) * bpp)) & ((1 << bpp) - 1);
	}
	if (kind == K_256)
		return cpu_vga_on ? plane[x & 3][(y * 320 + x) >> 2] : lin_ptr (0xa0000 + (uint32_t) (y * 320 + x))[0];
	uint32_t off = (uint32_t) (y * (crtc[0x13] * 2) + x / 8);
	int c = 0;
	for (int p = 0; p < 4; p++)
		c |= ((plane[p][off] >> (7 - (x & 7))) & 1) << p;
	return c;
}

/* un caractère dans un mode graphique : la police de l'INT 43h, fond à 0 (sauf en OU exclusif) */
static void gfx_char (int col, int row, uint8_t ch, int color)
{
	uint32_t f = far_lin (rd32 (0x43 * 4)) + (uint32_t) ch * (uint32_t) char_h;
	for (int y = 0; y < char_h; y++)
	{
		uint8_t bits = lin_ptr (f + (uint32_t) y)[0];
		for (int x = 0; x < 8; x++)
		{
			int on = bits & (0x80 >> x);
			if (color & 0x80)
			{
				if (on)
					put_pixel (col * 8 + x, row * char_h + y, color);
			}
			else
				put_pixel (col * 8 + x, row * char_h + y, on ? color : 0);
		}
	}
}

static void gfx_scroll (int up, int n, int fill, int r0, int c0, int r1, int c1)
{
	if (r1 >= rows)
		r1 = rows - 1;
	if (c1 >= cols)
		c1 = cols - 1;
	if (r0 > r1 || c0 > c1)
		return;
	int h = r1 - r0 + 1;
	if (n <= 0 || n > h)
		n = h;
	int y0 = r0 * char_h, y1 = (r1 + 1) * char_h - 1, x0 = c0 * 8, x1 = (c1 + 1) * 8 - 1, d = n * char_h;
	for (int i = 0; i <= y1 - y0; i++)
	{
		int y = up ? y0 + i : y1 - i, src = up ? y + d : y - d;
		for (int x = x0; x <= x1; x++)
			put_pixel (x, y, (src >= y0 && src <= y1) ? get_pixel (x, src) : fill);
	}
}

/* ======================================================================== l'écran texte du BIOS */
static void scroll (int up, int n, uint8_t at, int r0, int c0, int r1, int c1, int page)
{
	if (graphics)
	{
		gfx_scroll (up, n, kind == K_256 ? at : at & 15, r0, c0, r1, c1);
		return;
	}
	if (r1 >= rows)
		r1 = rows - 1;
	if (c1 >= cols)
		c1 = cols - 1;
	if (r0 > r1 || c0 > c1)
		return;
	int h = r1 - r0 + 1;
	if (n <= 0 || n > h)
		n = h;
	for (int i = 0; i < h; i++)
	{
		int dst = up ? r0 + i : r1 - i, src = up ? dst + n : dst - n;
		for (int c = c0; c <= c1; c++)
		{
			uint8_t *d = cell (page, dst, c);
			if (i < h - n)
				memcpy (d, cell (page, src, c), 2);
			else
			{
				d[0] = ' ';
				d[1] = at;
			}
		}
	}
}

static void tty_color (uint8_t ch, int color)
{
	int pg = cur_page ();
	uint8_t *cp = lin_ptr (BDA (0x50 + pg * 2));
	int col = cp[0], row = cp[1];
	switch (ch)
	{
	case 7:
		break;
	case 8:
		if (col)
			col--;
		break;
	case 10:
		row++;
		break;
	case 13:
		col = 0;
		break;
	default:
		if (graphics)
			gfx_char (col, row, ch, color);
		else
			cell (pg, row, col)[0] = ch;
		if (++col >= cols)
		{
			col = 0;
			row++;
		}
	}
	if (row >= rows)
	{
		uint8_t at = graphics ? 0 : cell (pg, rows - 1, 0)[1];
		scroll (1, 1, graphics ? 0 : (at ? at : 7), 0, 0, rows - 1, cols - 1, pg);
		row = rows - 1;
	}
	cp[0] = (uint8_t) col;
	cp[1] = (uint8_t) row;
	cursor_crtc ();
}

void vga_tty (uint8_t ch)
{
	tty_color (ch, kind == K_256 ? 15 : kind == K_CGA4 ? 3 : kind == K_CGA2 ? 1 : 15);
}

int vga_int10 (void)
{
	int pg;
	switch (rAH)
	{
	case 0x00:
		vga_set_mode (rAL & 0x7f, !(rAL & 0x80));
		set8l (R_EAX, rAL > 7 && rAL != 0x13 ? 0x20 : rAL == 6 ? 0x3f : 0x30);
		break;
	case 0x01:
		crtc[0x0a] = rCH;
		crtc[0x0b] = rCL;
		wr16 (BDA (0x60), rCX);
		break;
	case 0x02:
		pg = rBH & 7;
		lin_ptr (BDA (0x50 + pg * 2))[0] = rDL;
		lin_ptr (BDA (0x51 + pg * 2))[0] = rDH;
		if (pg == cur_page ())
			cursor_crtc ();
		break;
	case 0x03:
		pg = rBH & 7;
		set16 (R_EDX, rd16 (BDA (0x50 + pg * 2)));
		set16 (R_ECX, rd16 (BDA (0x60)));
		break;
	case 0x05:
		lin_ptr (BDA (0x62))[0] = rAL & 7;
		wr16 (BDA (0x4e), (uint16_t) ((rAL & 7) * (graphics ? rd16 (BDA (0x4c)) : page_size ())));
		start_crtc ();
		cursor_crtc ();
		break;
	case 0x06:
	case 0x07:
		scroll (rAH == 6, rAL, rBH, rCH, rCL, rDH, rDL, cur_page ());
		break;
	case 0x08:
	{
		pg = rBH & 7;
		const uint8_t *cp = lin_ptr (BDA (0x50 + pg * 2));
		if (graphics)
		{
			set16 (R_EAX, 0);
			break;
		}
		const uint8_t *c = cell (pg, cp[1], cp[0]);
		set16 (R_EAX, (uint16_t) (c[0] | c[1] << 8));
		break;
	}
	case 0x09:
	case 0x0a:
	{
		pg = rBH & 7;
		const uint8_t *cp = lin_ptr (BDA (0x50 + pg * 2));
		int pos = cp[1] * cols + cp[0];
		for (int i = 0; i < rCX && pos + i < cols * rows; i++)
		{
			if (graphics)
			{
				gfx_char ((pos + i) % cols, (pos + i) / cols, rAL, rBL);
				continue;
			}
			uint8_t *c = cell (pg, 0, pos + i);
			c[0] = rAL;
			if (rAH == 0x09)
				c[1] = rBL;
		}
		break;
	}
	case 0x0b:	/* la palette de la CGA */
		if (rBH == 0)
		{
			cga_bg = rBL & 15;
			cga_bright = (rBL & 0x10) != 0;
		}
		else
			cga_pal = rBL & 1;
		cga_apply ();
		break;
	case 0x0c:
		put_pixel (rCX, rDX, rAL);
		break;
	case 0x0d:
		set8l (R_EAX, (uint8_t) get_pixel (rCX, rDX));
		break;
	case 0x0e:
		tty_color (rAL, graphics ? rBL : 0);
		break;
	case 0x0f:
		set8l (R_EAX, (uint8_t) mode);
		set8h (R_EAX, (uint8_t) cols);
		set8h (R_EBX, (uint8_t) cur_page ());
		break;
	case 0x10:
		switch (rAL)
		{
		case 0x00: if (rBL < 0x15) attr[rBL] = rBH; break;
		case 0x01: attr[0x11] = rBH; break;
		case 0x02:
		{
			const uint8_t *p = lin_ptr (RM (cpu_sreg (S_ES), rDX));
			memcpy (attr, p, 16);
			attr[0x11] = p[16];
			break;
		}
		case 0x03: attr[0x10] = (uint8_t) ((attr[0x10] & ~8) | (rBL ? 8 : 0)); break;
		case 0x07: set8h (R_EBX, rBL < 0x15 ? attr[rBL] : 0); break;
		case 0x08: set8h (R_EBX, attr[0x11]); break;
		case 0x09: memcpy (lin_ptr (RM (cpu_sreg (S_ES), rDX)), attr, 16); lin_ptr (RM (cpu_sreg (S_ES), rDX))[16] = attr[0x11]; break;
		case 0x10: dac_set (rBX, rDH, rCH, rCL); break;
		case 0x12:
		{
			const uint8_t *p = lin_ptr (RM (cpu_sreg (S_ES), rDX));
			for (int i = 0; i < rCX; i++)
				dac_set (rBX + i, p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
			break;
		}
		case 0x13:
			if (rBL == 0)
				attr[0x10] = (uint8_t) ((attr[0x10] & ~0x80) | (rBH & 1) << 7);
			else
				attr[0x14] = rBH;
			break;
		case 0x15:
			set8h (R_EDX, dac[rBL][0]);
			set8h (R_ECX, dac[rBL][1]);
			set8l (R_ECX, dac[rBL][2]);
			break;
		case 0x17:
		{
			uint8_t *p = lin_ptr (RM (cpu_sreg (S_ES), rDX));
			for (int i = 0; i < rCX; i++)
				memcpy (p + i * 3, dac[(rBX + i) & 255], 3);
			break;
		}
		case 0x18: pel_mask = rBL; break;
		case 0x19: set8l (R_EBX, pel_mask); break;
		case 0x1a: set16 (R_EBX, (uint16_t) ((attr[0x14] & 0x0f) << 8 | (attr[0x10] >> 7))); break;
		case 0x1b:
			for (int i = 0; i < rCX; i++)
			{
				uint8_t *d = dac[(rBX + i) & 255];
				uint8_t g = (uint8_t) ((d[0] * 30 + d[1] * 59 + d[2] * 11 + 50) / 100);
				d[0] = d[1] = d[2] = g;
			}
			break;
		}
		break;
	case 0x11:
		switch (rAL)
		{
		case 0x00: case 0x10:
			load_font (lin_ptr (RM (cpu_sreg (S_ES), rBP)), rBH, rDX, rCX);
			if (rAL == 0x10)
			{
				char_h = rBH;
				set_rows (400 / char_h);
			}
			break;
		case 0x01: case 0x11: font_rom (14); set_rows (28); break;
		case 0x02: case 0x12: font_rom (8); set_rows (50); break;
		case 0x04: case 0x14: font_rom (16); set_rows (25); break;
		case 0x20:	/* (police des modes graphiques : INT 1Fh) */
			wr16 (0x1f * 4, rBP);
			wr16 (0x1f * 4 + 2, cpu_sreg (S_ES));
			break;
		case 0x21: case 0x22: case 0x23: case 0x24:
		{
			int h = rAL == 0x21 ? rCX : rAL == 0x22 ? 14 : rAL == 0x23 ? 8 : 16;
			if (rAL == 0x21)
			{
				wr16 (0x43 * 4, rBP);
				wr16 (0x43 * 4 + 2, cpu_sreg (S_ES));
			}
			else
			{
				wr16 (0x43 * 4, font_off (h));
				wr16 (0x43 * 4 + 2, VROM_SEG);
			}
			char_h = h;
			int n = rBL == 1 ? 14 : rBL == 2 ? 25 : rBL == 3 ? 43 : rDL;
			set_rows (n ? n : gfx_h () / h);
			break;
		}
		case 0x30:
		{
			static const uint16_t offs[8] = { 0, 0, FONT14_OFF, FONT8_OFF, FONT8_OFF + 1024, FONT14_OFF, FONT16_OFF, FONT16_OFF };
			int b = rBH & 7;
			uint16_t seg = VROM_SEG, off = offs[b];
			if (b < 2)
			{
				uint32_t v = rd32 (b == 0 ? 0x1f * 4 : 0x43 * 4);
				seg = (uint16_t) (v >> 16);
				off = (uint16_t) v;
			}
			cpu_set_sreg (S_ES, seg);
			set16 (R_EBP, off);
			set16 (R_ECX, (uint16_t) char_h);
			set8l (R_EDX, (uint8_t) (rows - 1));
			break;
		}
		}
		break;
	case 0x12:
		switch (rBL)
		{
		case 0x10: set16 (R_EBX, 0x0003); set16 (R_ECX, 0x0009); break;
		case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x36: set8l (R_EAX, 0x12); break;
		}
		break;
	case 0x13:
	{
		pg = rBH & 7;
		uint8_t *cp = lin_ptr (BDA (0x50 + pg * 2));
		uint16_t save = rd16 (BDA (0x50 + pg * 2));
		cp[0] = rDL;
		cp[1] = rDH;
		const uint8_t *s = lin_ptr (RM (cpu_sreg (S_ES), rBP));
		int with_attr = rAL & 2;
		for (int i = 0; i < rCX; i++)
		{
			uint8_t ch = s[with_attr ? i * 2 : i], at = with_attr ? s[i * 2 + 1] : rBL;
			if (!graphics && ch != 7 && ch != 8 && ch != 10 && ch != 13)
				cell (pg, cp[1], cp[0])[1] = at;
			tty_color (ch, at);
		}
		if (!(rAL & 1))
			wr16 (BDA (0x50 + pg * 2), save);
		cursor_crtc ();
		break;
	}
	case 0x1a:
		if (rAL == 0)
		{
			set8l (R_EAX, 0x1a);
			set16 (R_EBX, 0x0008);
		}
		break;
	case 0x4f:	/* le VESA BIOS Extension (svga.c) */
		return vesa_int10 ();
	case 0x1b:	/* l'état de la carte, et la table des modes qu'elle sait faire (QuickBASIC la lit pour SCREEN 12, 13) */
	{
		static const uint8_t func[16] = { 0xff, 0xe0, 0x0f, 0, 0, 0, 0, 0x07, 0x02, 0x08, 0xff, 0x0e, 0, 0, 0x3f, 0 };
		memcpy (lin_ptr (RM (ROM_SEG, 0xe000)), func, sizeof func);
		uint32_t b = RM (cpu_sreg (S_ES), rDI);
		memset (lin_ptr (b), 0, 64);
		wr32 (b, (uint32_t) ROM_SEG << 16 | 0xe000);
		lin_ptr (b)[4] = (uint8_t) mode;
		wr16 (b + 5, (uint16_t) cols);
		wr16 (b + 7, rd16 (BDA (0x4c)));
		wr16 (b + 9, rd16 (BDA (0x4e)));
		memcpy (lin_ptr (b + 0x0b), lin_ptr (BDA (0x50)), 16);
		wr16 (b + 0x1b, rd16 (BDA (0x60)));
		lin_ptr (b)[0x1d] = (uint8_t) cur_page ();
		wr16 (b + 0x1e, rd16 (BDA (0x63)));
		lin_ptr (b)[0x22] = (uint8_t) rows;
		wr16 (b + 0x23, (uint16_t) char_h);
		lin_ptr (b)[0x25] = 8;
		wr16 (b + 0x27, (uint16_t) (kind == K_256 ? 256 : kind == K_CGA4 ? 4 : kind == K_CGA2 ? 2 : 16));
		lin_ptr (b)[0x29] = (uint8_t) (graphics ? 1 : 8);
		lin_ptr (b)[0x2a] = (uint8_t) (mode >= 0x11 && mode <= 0x12 ? 3 : mode == 0x0f || mode == 0x10 ? 1 : 2);
		lin_ptr (b)[0x31] = 3;	/* (256 Ko) */
		set8l (R_EAX, 0x1b);
		break;
	}
	}
	return DONE;
}

/* ======================================================================== les ports */
static uint8_t retrace (void)
{
	/* (70 images par seconde ; le retour de trame occupe la fin de chacune) */
	static uint8_t toggle;
	uint64_t us = (uint64_t) be_ticks () * 1000;
	int vr = (us % 14286) > 13600;
	toggle ^= 1;
	return (uint8_t) ((vr ? 8 : 0) | (vr || toggle ? 1 : 0));
}

uint32_t vga_in (uint16_t port)
{
	switch (port)
	{
	case 0x3c0: return attr_index;
	case 0x3c1: return (attr_index & 0x1f) < 0x15 ? attr[attr_index & 0x1f] : 0;
	case 0x3c4: return seq_index;
	case 0x3c5: return seq_index < 5 ? seq[seq_index] : 0;
	case 0x3c6: return pel_mask;
	case 0x3c7: return dac_state;
	case 0x3c8: return dac_w;
	case 0x3c9:
	{
		uint8_t v = dac[dac_r][dac_sub];
		if (++dac_sub == 3)
		{
			dac_sub = 0;
			dac_r++;
		}
		return v;
	}
	case 0x3cc: return misc;
	case 0x3ce: return gc_index;
	case 0x3cf: return gc_index < 9 ? gc[gc_index] : 0;
	case 0x3b4: case 0x3d4: return crtc_index;
	case 0x3b5: case 0x3d5: return crtc_index < 0x19 ? crtc[crtc_index] : 0;
	case 0x3ba: case 0x3da:
		attr_ff = 0;
		return retrace ();
	}
	return 0xff;
}

static uint8_t cga_mode_reg, cga_color_reg;

void vga_out (uint16_t port, uint8_t v)
{
	switch (port)
	{
	case 0x3c0:
		if (!attr_ff)
			attr_index = v;
		else if ((attr_index & 0x1f) < 0x15)
			attr[attr_index & 0x1f] = v;
		attr_ff ^= 1;
		break;
	case 0x3c2: misc = v; break;
	case 0x3c4: seq_index = v; break;
	case 0x3c5:
		if (seq_index < 5)
		{
			seq[seq_index] = v;
			if (seq_index == 4)
				chain_changed ();
		}
		break;
	case 0x3c6: pel_mask = v; break;
	case 0x3c7: dac_r = v; dac_sub = 0; dac_state = 0; break;
	case 0x3c8: dac_w = v; dac_sub = 0; dac_state = 3; break;
	case 0x3c9:
		dac[dac_w][dac_sub] = v & 63;
		if (++dac_sub == 3)
		{
			dac_sub = 0;
			dac_w++;
		}
		break;
	case 0x3ce: gc_index = v; break;
	case 0x3cf: if (gc_index < 9) gc[gc_index] = v; break;
	case 0x3b4: case 0x3d4: crtc_index = v; break;
	case 0x3b5: case 0x3d5:
		if (crtc_index < 0x19)
			crtc[crtc_index] = v;
		if (svga_on && (crtc_index == 0x0c || crtc_index == 0x0d))	/* (FutureLib change de page par le CRTC) */
			svga_crtc_start ((uint32_t) (crtc[0x0c] << 8 | crtc[0x0d]) * 4);
		break;
	case 0x3d8: cga_mode_reg = v; break;
	case 0x3d9:	/* (la couleur de la CGA : fond et palette des modes 4 et 5) */
		cga_color_reg = v;
		{
			cga_bg = v & 15;
			cga_bright = (v & 0x10) != 0;
			cga_pal = (v & 0x20) != 0;
			cga_apply ();
		}
		break;
	}
	(void) cga_mode_reg;
	(void) cga_color_reg;
}

/* ======================================================================== le rendu */
static uint8_t shadow[0x8000 + 16];

/* la géométrie de l'image d'un mode graphique d'après le CRTC */
static void gfx_geometry (int *w, int *h, int *row_bytes, uint32_t *start)
{
	int vde = crtc[0x12] | (crtc[0x07] & 2) << 7 | (crtc[0x07] & 0x40) << 3;
	int lines = vde + 1;
	int rep = ((crtc[0x09] & 0x1f) + 1) * ((crtc[0x09] & 0x80) ? 2 : 1);
	*h = lines / rep;
	*start = (uint32_t) (crtc[0x0c] << 8 | crtc[0x0d]);
	*row_bytes = crtc[0x13] * 2;
	if (kind == K_256)
		*w = (crtc[0x01] + 1) * 4;
	else if (kind == K_PLANAR)
		*w = (crtc[0x01] + 1) * 8;
	else
	{
		*w = kind == K_CGA4 ? 320 : 640;
		*h = 200;
	}
	if (*w <= 0 || *w > 1024)
		*w = 320;
	if (*h <= 0 || *h > 768)
		*h = 200;
}

static void draw_mouse (uint32_t *px, int stride, int W, int H, int sx, int sy)
{
	int mx, my;
	if (!mouse_graphic_cursor (&mx, &my))
		return;
	int vw, vh;
	vga_mouse_space (&vw, &vh);
	int x0 = mx * W / vw, y0 = my * H / vh;
	static const uint16_t arrow[16] = { 0x8000, 0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00, 0xff80,
		0xfc00, 0xec00, 0xc600, 0x8600, 0x0300, 0x0300, 0x0000 };
	static const uint16_t inner[16] = { 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7c00, 0x7e00, 0x7f00, 0x7c00,
		0x6c00, 0x4600, 0x0600, 0x0300, 0x0000, 0x0000, 0x0000 };
	(void) sx;
	(void) sy;
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++)
		{
			int X = x0 + x, Y = y0 + y;
			if (X >= W || Y >= H || !(arrow[y] & (0x8000 >> x)))
				continue;
			px[Y * stride + X] = (inner[y] & (0x8000 >> x)) ? 0xffffff : 0x000000;
		}
}

static void render_svga (void);

static void render_gfx (void)
{
	int w, h, rb;
	uint32_t start;
	gfx_geometry (&w, &h, &rb, &start);
	int sx = w <= 360 ? 2 : 1, sy = h <= 300 ? 2 : 1;
	resize (w * sx, h * sy);
	int W, H, stride;
	uint32_t *px = be_pixels (bios_win, &W, &H, &stride);
	if (!px)
		return;
	uint32_t pal[256];
	if (kind == K_256)
		for (int i = 0; i < 256; i++)
			pal[i] = rgb_of (i);
	else
		for (int i = 0; i < 16; i++)
			pal[i] = text_color (i & attr[0x12]);
	const uint8_t *flat = lin_ptr (0xa0000), *cga = lin_ptr (0xb8000);
	uint32_t line[1024];
	for (int y = 0; y < h && y * sy < H; y++)
	{
		for (int x = 0; x < w; x++)
		{
			int c;
			switch (kind)
			{
			case K_256:
				if (cpu_vga_on)
					c = plane[x & 3][(start + (uint32_t) (y * rb) + (uint32_t) (x >> 2)) & 0xffff];
				else
					c = flat[(start * 4 + (uint32_t) (y * w + x)) & 0xffff];
				break;
			case K_PLANAR:
			{
				uint32_t off = (start + (uint32_t) (y * rb) + (uint32_t) (x >> 3)) & 0xffff;
				int b = 7 - (x & 7);
				c = ((plane[0][off] >> b) & 1) | ((plane[1][off] >> b) & 1) << 1 | ((plane[2][off] >> b) & 1) << 2
					| ((plane[3][off] >> b) & 1) << 3;
				break;
			}
			case K_CGA4:
				c = (cga[(y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;
				break;
			default:
				c = (cga[(y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 3)] >> (7 - (x & 7))) & 1;
				break;
			}
			line[x] = pal[c];
		}
		for (int ry = 0; ry < sy && y * sy + ry < H; ry++)
		{
			uint32_t *d = px + (y * sy + ry) * stride;
			for (int x = 0; x < w && x * sx < W; x++)
				for (int rx = 0; rx < sx && x * sx + rx < W; rx++)
					d[x * sx + rx] = line[x];
		}
	}
	draw_mouse (px, stride, W, H, sx, sy);
	be_present (bios_win, 0, 0, W, H);
}

void vga_render (void)
{
	if (!bios_win)
		return;
	frames++;
	if (svga_on)
	{
		render_svga ();
		return;
	}
	if (graphics)
	{
		render_gfx ();
		return;
	}
	int w, h, stride;
	uint32_t *px = be_pixels (bios_win, &w, &h, &stride);
	if (!px)
		return;
	uint32_t start = (uint32_t) (crtc[0x0c] << 8 | crtc[0x0d]) * 2;
	const uint8_t *mem = lin_ptr (text_base () + start);
	int n = cols * rows * 2;
	int blink_on = (be_ticks () / 267) & 1, cur_on = (be_ticks () / 133) & 1;
	int cpos = (crtc[0x0e] << 8 | crtc[0x0f]) - (int) (start / 2);
	int cs = crtc[0x0a] & 0x1f, ce = crtc[0x0b] & 0x1f, cvis = !(crtc[0x0a] & 0x20) && cs <= ce;
	/* rien de changé : pas de rendu */
	int mcol = -1, mrow = -1;
	uint16_t mand = 0xffff, mxor = 0;
	if (!mouse_text_cursor (&mcol, &mrow, &mand, &mxor))
		mcol = mrow = -1;
	uint8_t state[16] = { (uint8_t) blink_on, (uint8_t) cur_on, (uint8_t) cpos, (uint8_t) (cpos >> 8), (uint8_t) cs,
		(uint8_t) ce, (uint8_t) cvis, (uint8_t) cols, (uint8_t) rows, (uint8_t) char_h, (uint8_t) mcol, (uint8_t) mrow };
	static int dac_dirty_frames;
	if (!memcmp (shadow, mem, (size_t) n) && !memcmp (shadow + 0x8000, state, 16) && ++dac_dirty_frames < 30)
		return;
	dac_dirty_frames = 0;
	memcpy (shadow, mem, (size_t) n);
	memcpy (shadow + 0x8000, state, 16);
	int cw = w / cols;
	uint32_t pal[16];
	for (int i = 0; i < 16; i++)
		pal[i] = text_color (i);
	int blink = (attr[0x10] & 8) != 0;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++)
		{
			uint8_t ch = mem[(r * cols + c) * 2], at = mem[(r * cols + c) * 2 + 1];
			if (r == mrow && c == mcol)
			{
				/* (le curseur de la souris : les masques du pilote, sur le caractère et l'attribut) */
				uint16_t cl = (uint16_t) ((ch | at << 8) & mand) ^ mxor;
				ch = (uint8_t) cl;
				at = (uint8_t) (cl >> 8);
			}
			int fg = at & 15, bg = at >> 4;
			if (blink)
			{
				bg &= 7;
				if ((at & 0x80) && !blink_on)
					fg = bg;
			}
			const uint8_t *g = font + ch * 32;
			int is_cur = cvis && cur_on && r * cols + c == cpos;
			for (int y = 0; y < char_h && r * char_h + y < h; y++)
			{
				uint8_t bits = g[y];
				if (is_cur && y >= cs && y <= ce)
					bits = 0xff;
				uint32_t *row = px + (r * char_h + y) * stride + c * cw;
				for (int x = 0; x < cw; x++)
				{
					int bx = x * 8 / cw;
					row[x] = (bits & (0x80 >> bx)) ? pal[fg] : pal[bg];
				}
			}
		}
	be_present (bios_win, 0, 0, w, h);
}

/* les coordonnées virtuelles de la souris : 640 de large ; en hauteur, les lignes du mode (texte : 8 par ligne) */
void vga_mouse_space (int *w, int *h)
{
	if (svga_on)
	{
		svga_size (w, h);
		return;
	}
	*w = 640;
	if (!graphics)
	{
		*h = rows * 8;
		return;
	}
	int gw, gh, rb;
	uint32_t st;
	gfx_geometry (&gw, &gh, &rb, &st);
	*h = gh;
	if (gw > 640)
		*w = gw;
}

/* ======================================================================== pour svga.c */
uint32_t vga_rgb (int idx) { return rgb_of (idx); }
void vga_dac_put (int i, uint8_t r, uint8_t g, uint8_t b) { dac_set (i, r, g, b); }
void vga_dac_get (int i, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = dac[i & 255][0];
	*g = dac[i & 255][1];
	*b = dac[i & 255][2];
}

static void render_svga (void)
{
	int w, h;
	svga_size (&w, &h);
	int sx = w <= 400 ? 2 : 1, sy = h <= 300 ? 2 : 1;
	resize (w * sx, h * sy);
	int W, H, stride;
	uint32_t *px = be_pixels (bios_win, &W, &H, &stride);
	if (!px)
		return;
	svga_render (px, stride, W, H, sx, sy);
	draw_mouse (px, stride, W, H, sx, sy);
	be_present (bios_win, 0, 0, W, H);
}
