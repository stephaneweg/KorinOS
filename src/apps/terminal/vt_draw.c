/*
 * vt_draw.c -- le Terminal : dessin de la grille ; port de vt_draw.inc.
 *
 * L'ombre (vt_shadow) garde ce qui est a l'ecran, cellule par cellule. Un dessin compare chaque cellule
 * montree a son ombre et ne redessine que les lignes qui ont change, par plages de meme style : un fond
 * (ui_fill) et un ui_text par plage. Le curseur (barre) est redessine a part ; la cellule qu'il
 * couvrait est marquee changee.
 */
#include <string.h>
#include "term.h"

static uint32_t palette[256];
/* palette de base (comme le terminal de VS Code) et niveaux du cube */
static const uint32_t pal16[16] = {
	0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
	0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF,
};
static const unsigned char pal_lv[6] = { 0, 95, 135, 175, 215, 255 };

static long lc_valid, lc_row, lc_col;		/* curseur dessine : ligne, colonne */
static char textbuf[VT_MAXCOLS * 4 + 8];

/* palette de 256 couleurs : 16 de base, cube 6 x 6 x 6, 24 gris */
void pal_init (void)
{
	int i = 0;
	for (; i < 16; i++)
		palette[i] = pal16[i];
	for (int r = 0; r < 6; r++)
		for (int g = 0; g < 6; g++)
			for (int b = 0; b < 6; b++)
				palette[i++] = (uint32_t) pal_lv[r] << 16 | (uint32_t) pal_lv[g] << 8 | pal_lv[b];
	for (uint32_t v = 8; i < 256; v += 10)		/* gris : 8, 18 ... 238 */
		palette[i++] = v | v << 8 | v << 16;
}

/* couleur d'une cellule, couleur par defaut -> 0x00RRGGBB */
static uint32_t col_rgb (uint32_t c, uint32_t def)
{
	if (!c)
		return def;
	if (c >> 24 == 1)
		return c & 0xFFFFFF;
	return palette[c & 0xFF];
}

/* colonne -> x en pixels */
static long col_x (long c) { return (long) ((unsigned long) (c * adv100) / 100) + PAD; }

/* ligne de l'ecran -> cellules montrees (grille, ou historique si la vue est remontee) */
static struct cell *vt_line_disp (long r)
{
	if (vt.alt || !scroll)
		return vt.tbl[r];
	if (r - scroll >= 0)
		return vt.tbl[r - scroll];
	long i = vt.sbhead + (r - scroll);		/* r - scroll < 0 : tant de lignes avant la place libre */
	if (i < 0)
		i += VT_SBMAX;
	if (i >= VT_SBMAX)
		i -= VT_SBMAX;
	return vt_sb + (size_t) i * VT_MAXCOLS;
}

/* toute l'ombre marquee changee (lignes et colonnes en cours) */
static void shadow_inval (void)
{
	for (long r = 0; r < vt.rows; r++)
	{
		struct cell *c = vt_shadow + (size_t) r * VT_MAXCOLS;
		for (long i = 0; i < vt.cols; i++)
			c[i].cp = c[i].attr = 0xFFFFFFFF;
	}
}

/* ligne de l'ecran, premiere colonne, fin (exclue), cellules, attributs, couleurs */
static void draw_run (long r, long c0, long c1, const struct cell *cells, uint32_t attr, uint32_t fgc, uint32_t bgc)
{
	/* texte ; gras : 0-7 -> 8-15 (comme xterm) */
	if ((attr & VA_BOLD) && fgc >> 24 == 2 && (fgc & 0xFF) < 8)
		fgc += 8;
	uint32_t fg = col_rgb (fgc, COL_TXT), bg = col_rgb (bgc, COL_BG);
	if (attr & VA_REV)
	{						/* inverse */
		uint32_t t = fg;
		fg = bg;
		bg = t;
	}
	if (attr & VA_DIM)				/* attenue : moitie du chemin vers le fond */
		fg = ((fg & 0xFEFEFE) >> 1) + ((bg & 0xFEFEFE) >> 1);
	if (attr & VA_HIDDEN)				/* cache */
		fg = bg;
	/* texte de la plage en UTF-8 (0 : espace) */
	unsigned char *o = (unsigned char *) textbuf;
	int ink = 0;					/* 1 : autre chose que des espaces */
	for (long c = c0; c < c1; c++)
	{
		uint32_t cp = cells[c].cp;
		if (cp <= 0x20)
			*o++ = 0x20;
		else
		{
			ink = 1;
			if (cp < 0x80)
				*o++ = (unsigned char) cp;
			else if (cp < 0x800)
			{
				*o++ = (unsigned char) (0xC0 | cp >> 6);
				*o++ = (unsigned char) (0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				*o++ = (unsigned char) (0xE0 | cp >> 12);
				*o++ = (unsigned char) (0x80 | (cp >> 6 & 0x3F));
				*o++ = (unsigned char) (0x80 | (cp & 0x3F));
			}
			else
			{
				*o++ = (unsigned char) (0xF0 | cp >> 18);
				*o++ = (unsigned char) (0x80 | (cp >> 12 & 0x3F));
				*o++ = (unsigned char) (0x80 | (cp >> 6 & 0x3F));
				*o++ = (unsigned char) (0x80 | (cp & 0x3F));
			}
		}
	}
	*o = 0;
	long x0 = col_x (c0), x1 = col_x (c1), y = r * LH + PAD;	/* rectangle de la plage */
	ui_fill (x0, y, x1 - x0, LH, bg);
	if (ink)
	{	/* texte decoupe a la plage : un accent ou une descente ne deborde pas sur la ligne voisine (qui
		 * ne serait pas redessinee) */
		ui_clip (x0, y, x1 - x0, LH);
		ui_text ((long) textbuf, x0, y + LH - 4, fg, FONT_PX, (attr & VA_BOLD) ? font_bold : (long) ui.font_m, x1 - x0);
		ui_clip (0, 0, 0, 0);
	}
	if (attr & VA_UNDER)				/* souligne */
		ui_fill (x0, y + LH - 2, x1 - x0, 1, fg);
	if (attr & VA_STRIKE)				/* barre */
		ui_fill (x0, y + LH / 2, x1 - x0, 1, fg);
}

/* etend la zone a montrer (*y0 haut, *y1 bas) a la ligne r de l'ecran */
static void yrange (long r, long *y0, long *y1)
{
	long y = r * LH + PAD;
	if (y < *y0)
		*y0 = y;
	if (y + LH > *y1)
		*y1 = y + LH;
}

static int cell_eq (const struct cell *a, const struct cell *b) { return !memcmp (a, b, sizeof *a); }

/* dessine ce qui a change depuis le dernier dessin (tout si full_redraw) */
void term_draw (void)
{
	long y0, y1;
	if (full_redraw)
	{
		full_redraw = 0;
		ui_fill (0, 0, ui.w, ui.h, COL_BG);
		shadow_inval ();
		lc_valid = 0;
		y0 = 0;
		y1 = ui.h;
	}
	else
	{
		y0 = 0x7FFFFFFF;
		y1 = 0;
		if (lc_valid)
		{					/* l'ancien curseur s'efface */
			lc_valid = 0;
			if ((unsigned long) lc_row < (unsigned long) vt.rows)
			{
				yrange (lc_row, &y0, &y1);
				if ((unsigned long) lc_col < (unsigned long) vt.cols)
				{
					struct cell *s = vt_shadow + (size_t) lc_row * VT_MAXCOLS + lc_col;
					s->cp = s->attr = 0xFFFFFFFF;
				}
				else			/* (apres la derniere colonne) */
					ui_fill (col_x (lc_col), lc_row * LH + PAD, 2, LH, COL_BG);
			}
		}
	}
	for (long r = 0; r < vt.rows; r++)
	{
		struct cell *d = vt_line_disp (r);	/* cellules montrees */
		struct cell *s = vt_shadow + (size_t) r * VT_MAXCOLS;	/* ombre */
		long c = 0;
		while (c < vt.cols && cell_eq (&d[c], &s[c]))
			c++;
		if (c >= vt.cols)
			continue;
		/* une cellule changee : toute la ligne est redessinee (un glyphe deborde parfois d'un pixel sur
		 * la cellule voisine : une cellule seule laisserait des restes) */
		for (c = 0; c < vt.cols;)
		{
			long c0 = c;			/* debut de plage, son style */
			uint32_t attr = d[c].attr, fg = d[c].fg, bg = d[c].bg;
			for (c++; c < vt.cols && d[c].attr == attr && d[c].fg == fg && d[c].bg == bg; c++)
				;
			draw_run (r, c0, c, d, attr, fg, bg);
		}
		memmove (s, d, (size_t) vt.cols * sizeof *s);	/* l'ombre suit */
		yrange (r, &y0, &y1);
	}
	if (vt.modes & VM_CURSOR)
	{						/* curseur selon ?25 */
		long r = vt.cy + (vt.alt ? 0 : scroll);
		if ((unsigned long) r < (unsigned long) vt.rows)
		{
			long c = vt.cx + vt.wrap;
			ui_fill (col_x (c), r * LH + PAD + 1, 2, LH - 2, COL_CURSOR);
			lc_row = r;
			lc_col = c;
			lc_valid = 1;
			yrange (r, &y0, &y1);
		}
	}
	if (y1 > y0)
		ui_update (0, y0, ui.w, y1 - y0);
}
