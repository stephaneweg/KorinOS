/* vga.c -- win16, la machine DOS : la carte VGA (docs/etude/WIN16.md §9.3) et son rendu dans la fenêtre de Korin.
 *
 * Étape D1 : les modes texte (0-3, 7 ; 25, 28, 43 ou 50 lignes ; polices de la ROM ou chargées), les registres
 * (CRTC, séquenceur, contrôleur graphique, attributs, DAC) et les services de l'INT 10h qui vont avec. Les modes
 * graphiques (CGA, EGA, VGA, VESA) viennent à l'étape D4. Le texte est dans la mémoire du PC à B8000 (B0000 en
 * mode 7), comme sur une vraie carte ; le rendu le relit à chaque image. */
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
static uint64_t frames;

/* ---- la palette ---- */
static void dac_default (void)
{
	/* les 64 couleurs de l'EGA (rgbRGB), puis les 16 de la CGA et un dégradé de gris (le reste à l'étape D4) */
	for (int i = 0; i < 64; i++)
	{
		dac[i][0] = (uint8_t) (((i >> 2) & 1) * 42 + ((i >> 5) & 1) * 21);
		dac[i][1] = (uint8_t) (((i >> 1) & 1) * 42 + ((i >> 4) & 1) * 21);
		dac[i][2] = (uint8_t) ((i & 1) * 42 + ((i >> 3) & 1) * 21);
	}
	static const uint8_t ega[16] = { 0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };
	for (int i = 0; i < 16; i++)
		attr[i] = ega[i];
	attr[0x10] = 0x0c;	/* texte, clignotement */
	attr[0x12] = 0x0f;
	attr[0x13] = 0x08;
	attr[0x14] = 0;
}

static uint32_t rgb_of (int idx)
{
	idx &= pel_mask;
	uint32_t r = dac[idx][0] & 63, g = dac[idx][1] & 63, b = dac[idx][2] & 63;
	return (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
}

/* une couleur de l'attribut (0..15) -> pixel */
static uint32_t text_color (int c)
{
	int p = attr[c & 15] & 0x3f;
	if (attr[0x10] & 0x80)
		p = (p & 0x0f) | (attr[0x14] & 3) << 4;
	return rgb_of (p | (attr[0x14] & 0x0c) << 4);
}

/* ---- l'état du mode texte ---- */
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

static void font_rom (int h)
{
	uint16_t off = h == 16 ? FONT16_OFF : h == 14 ? FONT14_OFF : FONT8_OFF;
	load_font (lin_ptr (RM (VROM_SEG, off)), h, 0, 256);
	char_h = h;
}

static void set_rows (int r)
{
	rows = r;
	lin_ptr (BDA (0x84))[0] = (uint8_t) (rows - 1);
	wr16 (BDA (0x85), (uint16_t) char_h);
	wr16 (BDA (0x4c), (uint16_t) page_size ());
	crtc[0x09] = (uint8_t) ((crtc[0x09] & 0xe0) | (char_h - 1));
	crtc[0x0a] = (uint8_t) (char_h - 3);
	crtc[0x0b] = (uint8_t) (char_h - 2);
	int w = 640, h = rows * char_h;
	if (h < 200)
		h = 200;
	if (w != vga_width || h != vga_height)
	{
		vga_width = w;
		vga_height = h;
		if (bios_win)
			be_window_resize (bios_win, w, h);
	}
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
	int s = (int) rd16 (BDA (0x4e)) / 2;
	crtc[0x0c] = (uint8_t) (s >> 8);
	crtc[0x0d] = (uint8_t) s;
}

static uint8_t *cell (int page, int row, int col)
{
	return lin_ptr (text_base () + (uint32_t) (page * page_size () + (row * cols + col) * 2));
}

void vga_set_mode (int m, int clear)
{
	mode = m;
	graphics = !(m <= 3 || m == 7);
	cols = m <= 1 ? 40 : 80;
	attr_ff = 0;
	dac_default ();
	font_rom (16);
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
	set_rows (25);
	if (graphics)
		w16_log ("DOS : mode vidéo %02xh (les modes graphiques viennent à l'étape D4)\n", m);
	if (clear)
	{
		if (graphics)
			memset (lin_ptr (0xa0000), 0, 0x20000);
		else
			for (uint32_t i = 0; i < 0x8000; i += 2)
				wr16 (text_base () + i, 0x0720);
	}
	start_crtc ();
	cursor_crtc ();
}

void vga_init (void)
{
	memset (crtc, 0, sizeof crtc);
	seq[2] = 0x03;
	seq[4] = 0x02;
	gc[5] = 0x10;
	gc[6] = 0x0e;
	vga_set_mode (3, 1);
}

/* ---- l'écran texte du BIOS ---- */
static void scroll (int up, int n, uint8_t at, int r0, int c0, int r1, int c1, int page)
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

void vga_tty (uint8_t ch)
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
		if (!graphics)
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
		scroll (1, 1, at ? at : 7, 0, 0, rows - 1, cols - 1, pg);
		row = rows - 1;
	}
	cp[0] = (uint8_t) col;
	cp[1] = (uint8_t) row;
	cursor_crtc ();
}

static void dac_set (int i, uint8_t r, uint8_t g, uint8_t b)
{
	dac[i & 255][0] = r & 63;
	dac[i & 255][1] = g & 63;
	dac[i & 255][2] = b & 63;
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
		wr16 (BDA (0x4e), (uint16_t) ((rAL & 7) * page_size ()));
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
			uint8_t *c = cell (pg, 0, pos + i);
			c[0] = rAL;
			if (rAH == 0x09)
				c[1] = rBL;
		}
		break;
	}
	case 0x0e:
		vga_tty (rAL);
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
		case 0x1a: set16 (R_EBX, (uint16_t) ((attr[0x14] & 0x0f) << 8 | (attr[0x10] >> 7))); break;
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
			if (ch == 7 || ch == 8 || ch == 10 || ch == 13 || graphics)
				vga_tty (ch);
			else
			{
				cell (pg, cp[1], cp[0])[0] = ch;
				cell (pg, cp[1], cp[0])[1] = at;
				vga_tty (ch);	/* (réécrit le même caractère et avance) */
			}
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
	}
	return DONE;
}

/* ---- les ports ---- */
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
	case 0x3c4: return seq[0] ;
	case 0x3c5: return 0;
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
	case 0x3ce: return 0;
	case 0x3b5: case 0x3d5:
	{
		return crtc_index < 0x19 ? crtc[crtc_index] : 0;
	}
	case 0x3ba: case 0x3da:
		attr_ff = 0;
		return retrace ();
	}
	return 0xff;
}

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
	case 0x3c5: if (seq_index < 5) seq[seq_index] = v; break;
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
		break;
	}
	(void) gc;
}

/* ---- le rendu ---- */
static uint8_t shadow[0x8000 + 16];

void vga_render (void)
{
	if (!bios_win)
		return;
	frames++;
	int w, h, stride;
	uint32_t *px = be_pixels (bios_win, &w, &h, &stride);
	if (!px)
		return;
	if (graphics)
	{
		static int done;
		if (!done)
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
					px[y * stride + x] = 0;
		done = 1;
		be_present (bios_win, 0, 0, w, h);
		return;
	}
	uint32_t start = (uint32_t) (crtc[0x0c] << 8 | crtc[0x0d]) * 2;
	const uint8_t *mem = lin_ptr (text_base () + start);
	int n = cols * rows * 2;
	int blink_on = (be_ticks () / 267) & 1, cur_on = (be_ticks () / 133) & 1;
	int cpos = (crtc[0x0e] << 8 | crtc[0x0f]) - (int) (start / 2);
	int cs = crtc[0x0a] & 0x1f, ce = crtc[0x0b] & 0x1f, cvis = !(crtc[0x0a] & 0x20) && cs <= ce;
	/* rien de changé : pas de rendu */
	uint8_t state[16] = { (uint8_t) blink_on, (uint8_t) cur_on, (uint8_t) cpos, (uint8_t) (cpos >> 8), (uint8_t) cs,
		(uint8_t) ce, (uint8_t) cvis, (uint8_t) cols, (uint8_t) rows, (uint8_t) char_h };
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
