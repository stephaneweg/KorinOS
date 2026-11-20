/* gfxtest.c -- essai des modes graphiques de la machine DOS de win16 (étape D4).
 *
 *   GFXTEST <mode>     mode en hexadécimal : 4, 6, D, E, 10, 12, 13, ou X (Mode X 320 × 240)
 *
 * Dessine des barres de couleurs par la mémoire vidéo (CGA entrelacée, plans de l'EGA en mode d'écriture 2, 13h
 * linéaire, Mode X par le masque des plans), un cadre par l'INT 10h 0Ch, écrit le nom du mode par la console (texte
 * du BIOS en mode graphique), attend une touche et revient au mode texte. Compilé par Open Watcom. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>

static void set_mode (int m)
{
	union REGS r;
	r.h.ah = 0;
	r.h.al = (unsigned char) m;
	int86 (0x10, &r, &r);
}

static void pixel (int x, int y, int c)
{
	union REGS r;
	r.h.ah = 0x0c;
	r.h.al = (unsigned char) c;
	r.h.bh = 0;
	r.w.cx = (unsigned) x;
	r.w.dx = (unsigned) y;
	int86 (0x10, &r, &r);
}

static void frame (int w, int h, int c)
{
	int i;
	for (i = 0; i < w; i++)
	{
		pixel (i, 0, c);
		pixel (i, h - 1, c);
	}
	for (i = 0; i < h; i++)
	{
		pixel (0, i, c);
		pixel (w - 1, i, c);
	}
}

int main (int argc, char **argv)
{
	unsigned char far *v;
	int x, y, w = 320, h = 200, m;
	if (argc < 2)
	{
		printf ("GFXTEST mode (4, 6, D, E, 10, 12, 13, X)\n");
		return 1;
	}
	m = argv[1][0] == 'X' || argv[1][0] == 'x' ? 0x113 : (int) strtol (argv[1], 0, 16);
	set_mode (m & 0xff);
	switch (m)
	{
	case 4:
	case 6:
	{
		int bpp = m == 4 ? 2 : 1;
		w = m == 4 ? 320 : 640;
		v = (unsigned char far *) MK_FP (0xb800, 0);
		for (y = 0; y < 200; y++)
			for (x = 0; x < w; x++)
			{
				int c = bpp == 2 ? x * 4 / w : (x / 16) & 1;
				unsigned off = (unsigned) ((y & 1) * 0x2000 + (y >> 1) * 80 + x / (8 / bpp));
				int sh = (8 / bpp - 1 - x % (8 / bpp)) * bpp;
				v[off] = (unsigned char) ((v[off] & ~(((1 << bpp) - 1) << sh)) | (c << sh));
			}
		break;
	}
	case 0x0d: case 0x0e: case 0x10: case 0x12:
		w = m == 0x0d ? 320 : 640;
		h = m == 0x12 ? 480 : m == 0x10 ? 350 : 200;
		v = (unsigned char far *) MK_FP (0xa000, 0);
		outp (0x3ce, 5);
		outp (0x3cf, 2);	/* mode d'écriture 2 : la couleur de l'octet, sur les bits du masque */
		for (x = 0; x < w; x++)
		{
			outp (0x3ce, 8);
			outp (0x3cf, 0x80 >> (x & 7));
			for (y = 0; y < h; y++)
			{
				volatile unsigned char l = v[y * (w / 8) + x / 8];	/* (charge les verrous) */
				(void) l;
				v[y * (w / 8) + x / 8] = (unsigned char) (x * 16 / w);
			}
		}
		outp (0x3ce, 8);
		outp (0x3cf, 0xff);
		outp (0x3ce, 5);
		outp (0x3cf, 0);
		break;
	case 0x13:
		v = (unsigned char far *) MK_FP (0xa000, 0);
		for (y = 0; y < 200; y++)
			for (x = 0; x < 320; x++)
				v[y * 320 + x] = (unsigned char) (y < 100 ? x * 256 / 320 : (x / 20) + 16);
		break;
	case 0x113:
		/* Mode X : 13h déchaîné, 240 lignes (les registres du CRTC de Michael Abrash) */
		outp (0x3c4, 4);
		outp (0x3c5, 0x06);
		outp (0x3d4, 0x14);
		outp (0x3d5, 0x00);
		outp (0x3d4, 0x17);
		outp (0x3d5, 0xe3);
		outp (0x3c2, 0xe3);
		outp (0x3d4, 0x11);
		outp (0x3d5, inp (0x3d5) & 0x7f);
		{
			static const unsigned short crt[] = { 0x0d06, 0x3e07, 0x4109, 0xea10, 0xac11, 0xdf12, 0xe715, 0x0616 };
			int i;
			for (i = 0; i < 8; i++)
				outpw (0x3d4, crt[i]);
		}
		h = 240;
		v = (unsigned char far *) MK_FP (0xa000, 0);
		for (x = 0; x < 320; x++)
		{
			outp (0x3c4, 2);
			outp (0x3c5, 1 << (x & 3));
			for (y = 0; y < 240; y++)
				v[y * 80 + x / 4] = (unsigned char) (32 + (x / 10 + y / 30) % 72);
		}
		outp (0x3c4, 2);
		outp (0x3c5, 0x0f);
		break;
	default:
		set_mode (3);
		printf ("mode inconnu\n");
		return 1;
	}
	if (m != 0x113)
	{
		frame (w, h, m == 4 ? 3 : m == 6 ? 1 : 15);
		printf ("Mode %Xh", m);
	}
	getch ();
	set_mode (3);
	return 0;
}
