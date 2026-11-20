/* svga.c -- win16, la machine DOS : la SVGA par le VESA BIOS Extension 2.0 (INT 10h AX=4Fxxh ; WIN16.md §9.3).
 *
 * 8 Mo de mémoire vidéo dans l'arène, à 16 Mo (VRAM_LIN) : c'est aussi l'adresse « physique » du tampon linéaire
 * (un programme en mode protégé ou en mode réel « déplié » y écrit tout droit). En mode réel, la fenêtre A de
 * 64 Ko en A0000 montre une banque de cette mémoire (4F05h, ou l'appel lointain WinFuncPtr). Modes de 320 × 200 à
 * 1280 × 1024 en 8, 15, 16, 24 et 32 bits (les numéros du VESA pour ceux qui en ont, 0x120 et plus sinon).
 * Le rendu convertit la mémoire en pixels de la fenêtre (palette du DAC en 8 bits). */
#include <stdio.h>
#include <string.h>
#include "dos.h"
#include "be.h"

#define VRAM_LIN	0x01000000u
#define VRAM_SIZE	(8u << 20)
#define INFO_OFF	0xe100		/* en F000 : les chaînes et la liste des modes */

struct vmode { uint16_t num, w, h; uint8_t bpp; };
static const struct vmode modes[] = {
	{ 0x100, 640, 400, 8 }, { 0x101, 640, 480, 8 }, { 0x103, 800, 600, 8 }, { 0x105, 1024, 768, 8 },
	{ 0x107, 1280, 1024, 8 }, { 0x10d, 320, 200, 15 }, { 0x10e, 320, 200, 16 }, { 0x10f, 320, 200, 24 },
	{ 0x110, 640, 480, 15 }, { 0x111, 640, 480, 16 }, { 0x112, 640, 480, 24 }, { 0x113, 800, 600, 15 },
	{ 0x114, 800, 600, 16 }, { 0x115, 800, 600, 24 }, { 0x116, 1024, 768, 15 }, { 0x117, 1024, 768, 16 },
	{ 0x118, 1024, 768, 24 }, { 0x119, 1280, 1024, 15 }, { 0x11a, 1280, 1024, 16 }, { 0x11b, 1280, 1024, 24 },
	{ 0x120, 320, 200, 8 }, { 0x121, 320, 240, 8 }, { 0x122, 400, 300, 8 }, { 0x123, 512, 384, 8 },
	{ 0x124, 320, 200, 32 }, { 0x125, 320, 240, 32 }, { 0x126, 640, 480, 32 }, { 0x127, 800, 600, 32 },
	{ 0x128, 1024, 768, 32 }, { 0x129, 1280, 1024, 32 }, { 0x12a, 320, 240, 15 }, { 0x12b, 320, 240, 16 },
	{ 0x12c, 400, 300, 15 }, { 0x12d, 400, 300, 16 }, { 0x12e, 400, 300, 32 }, { 0x12f, 512, 384, 15 },
	{ 0x130, 512, 384, 16 }, { 0x131, 512, 384, 32 }, { 0x132, 640, 400, 15 }, { 0x133, 640, 400, 16 },
	{ 0x134, 640, 400, 32 }, { 0x135, 320, 240, 24 }, { 0x136, 400, 300, 24 }, { 0x137, 512, 384, 24 },
	{ 0x138, 640, 400, 24 },
};
#define NMODES ((int) (sizeof modes / sizeof modes[0]))

int svga_on;
static const struct vmode *cur;
static uint32_t pitch, bank, start_x, start_y;

static const struct vmode *find (uint16_t num)
{
	for (int i = 0; i < NMODES; i++)
		if (modes[i].num == (num & 0x1ff))
			return &modes[i];
	return 0;
}

static int bytespp (const struct vmode *m) { return (m->bpp + 7) / 8; }

/* ---- la fenêtre A (A0000-AFFFF) sur la banque courante ---- */
static uint8_t svga_rd (uint32_t a) { return lin_ptr (VRAM_LIN + ((bank << 16) + (a - 0xa0000)) % VRAM_SIZE)[0]; }
static void svga_wr (uint32_t a, uint8_t v) { lin_ptr (VRAM_LIN + ((bank << 16) + (a - 0xa0000)) % VRAM_SIZE)[0] = v; }

void svga_off (void)
{
	svga_on = 0;
	cur = 0;
}

void svga_size (int *w, int *h)
{
	*w = cur ? cur->w : 640;
	*h = cur ? cur->h : 480;
}

static void mode_info (const struct vmode *m, uint32_t b)
{
	uint8_t *p = lin_ptr (b);
	memset (p, 0, 256);
	int bpp = bytespp (m);
	uint32_t pg = (uint32_t) m->w * (uint32_t) bpp;
	wr16 (b + 0x00, 0x009b | 0x0080);	/* présent, infos, couleur, graphique, fenêtres, tampon linéaire */
	p[0x02] = 0x07;			/* fenêtre A : existe, lisible, inscriptible */
	p[0x03] = 0x00;
	wr16 (b + 0x04, 64);		/* granularité et taille : 64 Ko */
	wr16 (b + 0x06, 64);
	wr16 (b + 0x08, 0xa000);
	wr16 (b + 0x0a, 0);
	wr32 (b + 0x0c, (uint32_t) ROM_SEG << 16 | (INFO_OFF + 0x40));
	wr16 (b + 0x10, (uint16_t) pg);
	wr16 (b + 0x12, m->w);
	wr16 (b + 0x14, m->h);
	p[0x16] = 8;
	p[0x17] = 16;
	p[0x18] = 1;
	p[0x19] = m->bpp == 15 ? 16 : m->bpp;
	p[0x1a] = (uint8_t) ((pg * m->h + 65535) / 65536);
	p[0x1b] = m->bpp == 8 ? 4 : 6;	/* pixels tassés, couleur directe */
	p[0x1c] = 0;
	uint32_t pages = VRAM_SIZE / (pg * m->h);
	p[0x1d] = (uint8_t) (pages > 1 ? (pages - 1 > 255 ? 255 : pages - 1) : 0);
	p[0x1e] = 1;
	if (m->bpp > 8)
	{
		static const uint8_t f15[8] = { 5, 10, 5, 5, 5, 0, 1, 15 }, f16[8] = { 5, 11, 6, 5, 5, 0, 0, 0 },
			f24[8] = { 8, 16, 8, 8, 8, 0, 0, 0 }, f32[8] = { 8, 16, 8, 8, 8, 0, 8, 24 };
		memcpy (p + 0x1f, m->bpp == 15 ? f15 : m->bpp == 16 ? f16 : m->bpp == 24 ? f24 : f32, 8);
		p[0x27] = 0x02;
	}
	wr32 (b + 0x28, VRAM_LIN);
	wr16 (b + 0x32, (uint16_t) pg);	/* (VBE 3 : pas en mode linéaire) */
}

static void set_mode (const struct vmode *m, int clear)
{
	vga_set_mode (0x13, clear);	/* (les registres et le DAC d'un mode de 256 couleurs) */
	cur = m;
	svga_on = 1;
	pitch = (uint32_t) m->w * (uint32_t) bytespp (m);
	bank = 0;
	start_x = start_y = 0;
	if (clear)
		memset (lin_ptr (VRAM_LIN), 0, VRAM_SIZE);
	cpu_vga_on = 1;
	cpu_vga_rd = svga_rd;
	cpu_vga_wr = svga_wr;
	w16_log ("DOS : mode VESA %03xh, %d×%d en %d bits\n", m->num, m->w, m->h, m->bpp);
}

static void ok (void) { set16 (R_EAX, 0x004f); }
static void fail (void) { set16 (R_EAX, 0x014f); }

int vesa_int10 (void)
{
	uint32_t b = RM (cpu_sreg (S_ES), rDI);
	switch (rAL)
	{
	case 0x00:
	{
		uint8_t *p = lin_ptr (b);
		int v2 = !memcmp (p, "VBE2", 4);
		memset (p, 0, v2 ? 512 : 256);
		memcpy (p, "VESA", 4);
		wr16 (b + 4, 0x0200);
		char *s = (char *) lin_ptr (RM (ROM_SEG, INFO_OFF));
		strcpy (s, "Korin OS");
		strcpy (s + 0x10, "Korin");
		strcpy (s + 0x20, "win16 SVGA");
		strcpy (s + 0x30, "1.0");
		wr32 (b + 6, (uint32_t) ROM_SEG << 16 | INFO_OFF);
		wr32 (b + 10, 0);
		uint16_t *list = (uint16_t *) lin_ptr (RM (ROM_SEG, INFO_OFF + 0x80));
		for (int i = 0; i < NMODES; i++)
			list[i] = modes[i].num;
		list[NMODES] = 0xffff;
		wr32 (b + 14, (uint32_t) ROM_SEG << 16 | (INFO_OFF + 0x80));
		wr16 (b + 18, (uint16_t) (VRAM_SIZE >> 16));
		if (v2)
		{
			wr16 (b + 20, 0x0100);
			wr32 (b + 22, (uint32_t) ROM_SEG << 16 | (INFO_OFF + 0x10));
			wr32 (b + 26, (uint32_t) ROM_SEG << 16 | (INFO_OFF + 0x20));
			wr32 (b + 30, (uint32_t) ROM_SEG << 16 | (INFO_OFF + 0x30));
		}
		ok ();
		break;
	}
	case 0x01:
	{
		const struct vmode *m = find (rCX);
		if (!m)
		{
			fail ();
			break;
		}
		mode_info (m, b);
		ok ();
		break;
	}
	case 0x02:
	{
		uint16_t n = rBX & 0x1ff;
		if (n < 0x100)
		{
			svga_off ();
			vga_set_mode (n, !(rBX & 0x8000));
			ok ();
			break;
		}
		const struct vmode *m = find (n);
		if (!m)
		{
			fail ();
			break;
		}
		set_mode (m, !(rBX & 0x8000));
		ok ();
		break;
	}
	case 0x03:
		set16 (R_EBX, cur ? cur->num : lin_ptr (0x449)[0]);
		ok ();
		break;
	case 0x04:
		if (rDL == 0)
			set16 (R_EBX, 1);
		ok ();
		break;
	case 0x05:
		if (rBH == 1)
			set16 (R_EDX, (uint16_t) bank);
		else if (rBL == 0)
			bank = rDX % (VRAM_SIZE >> 16);
		ok ();
		break;
	case 0x06:
		if (!cur)
		{
			fail ();
			break;
		}
		if (rBL == 0)
			pitch = (uint32_t) rCX * (uint32_t) bytespp (cur);
		else if (rBL == 2)
			pitch = rCX;
		set16 (R_EBX, (uint16_t) pitch);
		set16 (R_ECX, (uint16_t) (pitch / (uint32_t) bytespp (cur)));
		set16 (R_EDX, (uint16_t) (VRAM_SIZE / pitch > 0xffff ? 0xffff : VRAM_SIZE / pitch));
		if (rBL == 3)
			set16 (R_EBX, 0x7fff);
		ok ();
		break;
	case 0x07:
		if (rBL == 0 || rBL == 0x80)
		{
			start_x = rCX;
			start_y = rDX;
		}
		else if (rBL == 1)
		{
			set16 (R_EBX, 0);
			set16 (R_ECX, (uint16_t) start_x);
			set16 (R_EDX, (uint16_t) start_y);
		}
		ok ();
		break;
	case 0x08:
		set8h (R_EBX, 6);	/* (DAC de 6 bits seulement) */
		ok ();
		break;
	case 0x09:
	{
		uint8_t *p = lin_ptr (b);
		for (int i = 0; i < rCX; i++)
		{
			uint32_t idx = (uint32_t) (rDX + i) & 255;
			if (rBL == 1)
				vga_dac_get ((int) idx, p + i * 4 + 2, p + i * 4 + 1, p + i * 4);
			else
				vga_dac_put ((int) idx, p[i * 4 + 2], p[i * 4 + 1], p[i * 4]);
		}
		ok ();
		break;
	}
	default:
		fail ();
		break;
	}
	return DONE;
}

/* ---- le rendu ---- */
void svga_render (uint32_t *px, int stride, int W, int H, int sx, int sy)
{
	if (!cur)
		return;
	int bpp = bytespp (cur);
	uint32_t pal[256];
	if (bpp == 1)
		for (int i = 0; i < 256; i++)
			pal[i] = vga_rgb (i);
	const uint8_t *vram = lin_ptr (VRAM_LIN);
	uint32_t base = start_y * pitch + start_x * (uint32_t) bpp;
	for (int y = 0; y < cur->h && y * sy < H; y++)
	{
		const uint8_t *row = vram + (base + (uint32_t) y * pitch) % VRAM_SIZE;
		uint32_t *d = px + y * sy * stride;
		for (int x = 0; x < cur->w && x * sx < W; x++)
		{
			uint32_t c;
			const uint8_t *s = row + x * bpp;
			switch (cur->bpp)
			{
			case 8: c = pal[s[0]]; break;
			case 15:
			{
				uint32_t v = s[0] | s[1] << 8;
				c = ((v >> 10) & 31) * 255 / 31 << 16 | ((v >> 5) & 31) * 255 / 31 << 8 | (v & 31) * 255 / 31;
				break;
			}
			case 16:
			{
				uint32_t v = s[0] | s[1] << 8;
				c = ((v >> 11) & 31) * 255 / 31 << 16 | ((v >> 5) & 63) * 255 / 63 << 8 | (v & 31) * 255 / 31;
				break;
			}
			default: c = (uint32_t) s[2] << 16 | (uint32_t) s[1] << 8 | s[0]; break;
			}
			for (int rx = 0; rx < sx; rx++)
				d[x * sx + rx] = c;
		}
		for (int ry = 1; ry < sy && y * sy + ry < H; ry++)
			memcpy (px + (y * sy + ry) * stride, d, (size_t) (cur->w * sx < W ? cur->w * sx : W) * 4);
	}
}

void svga_init (void)
{
	/* WinFuncPtr : mov ax,4F05h ; int 10h ; retf */
	static const uint8_t winfunc[] = { 0xb8, 0x05, 0x4f, 0xcd, 0x10, 0xcb };
	memcpy (lin_ptr (RM (ROM_SEG, INFO_OFF + 0x40)), winfunc, sizeof winfunc);
}

/* l'adresse de début écrite dans le CRTC (registres 0Ch, 0Dh) : en octets, déjà multipliée par 4 */
void svga_crtc_start (uint32_t bytes)
{
	if (!cur)
		return;
	start_y = bytes / pitch;
	start_x = (bytes % pitch) / (uint32_t) bytespp (cur);
}
