/*
 * vt.c -- le Terminal : emulateur VT/xterm (grille de cellules, analyseur) ; port de vt.inc.
 *
 * Grille lignes x colonnes de cellules (struct cell, 16 octets). Chaque ligne fait VT_MAXCOLS cellules ;
 * une table de pointeurs par ecran (rowp_main, rowp_alt) donne ses lignes : defiler fait tourner des
 * pointeurs. Une seule allocation a zero (comme SYS_VM_MAP dans l'asm) : principal, alternatif,
 * ombre, historique.
 *
 * Analyseur : l'automate de Paul Williams (VT500), reduit au sous-ensemble xterm de
 * docs/TERMINAL-ET-FORK.md 1.4. UTF-8 decode en points de code (une cellule par point de code, sans
 * table de largeurs). Les sequences inconnues sont avalees, jamais affichees.
 *
 * Le programme parle a travers un pseudo-terminal (service vterm) : ONLCR y fait CR LF, LF est un
 * simple saut de ligne ; les reponses (DSR CSI 5n / 6n, DA CSI c et CSI > c) partent sur le bout
 * maitre (term_reply_s de main.c).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "term.h"

/* etats de l'analyseur */
enum { ST_GROUND, ST_ESC, ST_ESCI, ST_CSI, ST_CSIP, ST_CSII, ST_CSIX, ST_STR };

struct vt vt;
struct cell *rowp_main[VT_MAXROWS], *rowp_alt[VT_MAXROWS];
struct cell *vt_shadow, *vt_sb;

/* -> 0 (< 0 : pas de memoire) */
int vt_init (void)
{
	size_t grid = (size_t) VT_MAXROWS * VT_MAXCOLS;
	struct cell *m = calloc (3 * grid + (size_t) VT_SBMAX * VT_MAXCOLS, sizeof (struct cell));
	if (!m)
		return -1;
	for (int r = 0; r < VT_MAXROWS; r++)
	{
		rowp_main[r] = m + (size_t) r * VT_MAXCOLS;
		rowp_alt[r] = m + grid + (size_t) r * VT_MAXCOLS;
	}
	vt_shadow = m + 2 * grid;			/* ombre (ce qui est a l'ecran) */
	vt_sb = m + 3 * grid;				/* historique */
	vt.tbl = rowp_main;
	vt.modes = VM_DEFAULT;
	pal_init ();
	return 0;
}

/* ---- cellules et lignes ---- */

static struct cell *vt_row (long r) { return vt.tbl[r]; }

/* n cellules a 0, sauf le fond (bg) */
static void cells_fill (struct cell *c, long n, uint32_t bg)
{
	for (; n > 0; n--, c++)
	{
		c->cp = c->attr = c->fg = 0;
		c->bg = bg;
	}
}

static void cells_move (struct cell *d, const struct cell *s, long n)
{
	if (n > 0 && d != s)
		memmove (d, s, (size_t) n * sizeof (struct cell));
}

/* ligne, premiere colonne, fin (exclue, bornee aux colonnes) : cellules effacees avec le fond du stylo */
static void vt_clear_span (long r, long c0, long c1)
{
	if ((unsigned long) c1 > (unsigned long) vt.cols)
		c1 = vt.cols;
	if ((unsigned long) c0 >= (unsigned long) c1)
		return;
	cells_fill (vt.tbl[r] + c0, c1 - c0, vt.pen.bg);
}

/* premiere ligne, fin (exclue) : lignes entieres effacees */
static void vt_clear_rows (long r0, long r1)
{
	for (; r0 < r1; r0++)
		vt_clear_span (r0, 0, VT_MAXCOLS);
}

/* les cellules d'une ligne copiees au bout de l'historique (colonnes en trop a 0) */
static void sb_push (const struct cell *line)
{
	struct cell *d = vt_sb + (size_t) vt.sbhead * VT_MAXCOLS;
	vt.sbhead = vt.sbhead + 1 >= VT_SBMAX ? 0 : vt.sbhead + 1;
	vt.sbn = vt.sbn + 1 > VT_SBMAX ? VT_SBMAX : vt.sbn + 1;
	if (scroll)					/* vue remontee : elle garde ses lignes */
		scroll = scroll + 1 > vt.sbn ? vt.sbn : scroll + 1;
	cells_move (d, line, vt.cols);
	cells_fill (d + vt.cols, VT_MAXCOLS - vt.cols, 0);
}

/* haut, bas (inclus), n, hist = 1 : historique permis. La zone monte de n lignes, des lignes vides
 * entrent en bas. Les lignes qui sortent du haut de l'ecran principal vont dans l'historique. */
static void vt_up (long top, long bot, long n, int hist)
{
	if (bot < top)
		return;
	if (n > bot - top + 1)
		n = bot - top + 1;
	if (n <= 0)
		return;
	struct cell **t = vt.tbl;
	int h = hist && !vt.alt && top == 0;
	for (; n; n--)
	{
		if (h)
		{
			sb_push (t[top]);
			vt.scrolls++;
		}
		struct cell *first = t[top];		/* la ligne du haut passe en bas */
		for (long r = top; r < bot; r++)
			t[r] = t[r + 1];
		t[bot] = first;
		vt_clear_span (bot, 0, VT_MAXCOLS);
	}
}

/* haut, bas (inclus), n : la zone descend de n lignes, des lignes vides entrent en haut */
static void vt_down (long top, long bot, long n)
{
	if (bot < top)
		return;
	if (n > bot - top + 1)
		n = bot - top + 1;
	if (n <= 0)
		return;
	struct cell **t = vt.tbl;
	for (; n; n--)
	{
		struct cell *last = t[bot];		/* la ligne du bas passe en haut */
		for (long r = bot; (unsigned long) r > (unsigned long) top; r--)
			t[r] = t[r - 1];
		t[top] = last;
		vt_clear_span (top, 0, VT_MAXCOLS);
	}
}

/* ---- curseur ---- */

/* colonne, ligne (signees) : bornees a l'ecran ; retour en attente annule */
static void vt_goto (long x, long y)
{
	if (x < 0)
		x = 0;
	if (x > vt.cols - 1)
		x = vt.cols - 1;
	if (y < 0)
		y = 0;
	if (y > vt.rows - 1)
		y = vt.rows - 1;
	vt.cx = x;
	vt.cy = y;
	vt.wrap = 0;
}

/* une ligne plus bas ; en bas de la zone de defilement, elle monte */
static void vt_lf (void)
{
	if (vt.cy == vt.bot)
		vt_up (vt.top, vt.bot, 1, 1);
	else if ((unsigned long) vt.cy < (unsigned long) (vt.rows - 1))
		vt.cy++;
}

/* une ligne plus haut ; en haut de la zone, elle descend (ESC M) */
static void vt_ri (void)
{
	if (vt.cy == vt.top)
		vt_down (vt.top, vt.bot, 1);
	else if (vt.cy)
		vt.cy--;
}

static void vt_save (struct saved *s)
{
	s->x = vt.cx;
	s->y = vt.cy;
	s->pen = vt.pen;
	s->wrap = vt.wrap;
	s->ok = 1;
}

/* curseur et stylo rendus (rien de sauve : coin, stylo par defaut) */
static void vt_restore (struct saved *s)
{
	if (!s->ok)
	{
		memset (&vt.pen, 0, sizeof vt.pen);
		vt_goto (0, 0);
		return;
	}
	vt.pen = s->pen;
	vt_goto (s->x, s->y);
}

/* clear = 1 : ecran alternatif efface en entrant */
static void vt_alt_enter (int clear)
{
	if (!vt.alt)
	{
		vt.alt = 1;
		vt.tbl = rowp_alt;
		scroll = 0;				/* pas d'historique sur cet ecran */
	}
	if (clear)
		vt_clear_rows (0, vt.rows);
}

/* retour a l'ecran principal */
static void vt_alt_leave (void)
{
	if (!vt.alt)
		return;
	vt.alt = 0;
	vt.tbl = rowp_main;
}

/* remise a zero douce (DECSTR, et fin d'un programme) : stylo, zone, modes, curseurs sauves,
 * analyseur ; l'ecran reste */
static void vt_soft_reset (void)
{
	memset (&vt.pen, 0, sizeof vt.pen);
	vt.top = 0;
	vt.bot = vt.rows - 1;
	vt.modes = (vt.modes | VM_CURSOR | VM_WRAP) & ~(long) (VM_APPCK | VM_PASTE);
	vt.sc.ok = 0;
	vt.sc2.ok = 0;
	vt.state = ST_GROUND;
	vt.uneed = 0;
	vt.wrap = 0;
}

/* remise a zero complete (ESC c) ; l'historique reste */
static void vt_ris (void)
{
	vt_alt_leave ();
	vt_soft_reset ();
	vt.modes = VM_DEFAULT;
	vt_clear_rows (0, vt.rows);
	vt_goto (0, 0);
}

/* 0 : du curseur a la fin, 1 : du debut au curseur, 2 : tout l'ecran, 3 : l'historique (ED) ; une
 * autre valeur fait comme 0 (comme l'asm) */
static void vt_erase (long how)
{
	vt.wrap = 0;
	if (how == 1)
	{
		vt_clear_rows (0, vt.cy);
		vt_clear_span (vt.cy, 0, vt.cx + 1);
	}
	else if (how == 2)
		vt_clear_rows (0, vt.rows);
	else if (how == 3)
	{
		vt.sbn = 0;
		vt.sbhead = 0;
		scroll = 0;
	}
	else
	{
		vt_clear_span (vt.cy, vt.cx, VT_MAXCOLS);
		vt_clear_rows (vt.cy + 1, vt.rows);
	}
}

/* colonnes, lignes : nouvelle taille. Pas de reformatage : les lignes sont coupees ou allongees
 * (cellules vides) ; si la ligne du curseur sort par le bas, l'ecran monte (vers l'historique). */
void vt_resize (long cols, long rows)
{
	if (cols < 2)
		cols = 2;
	if (cols > VT_MAXCOLS)
		cols = VT_MAXCOLS;
	if (rows < 1)
		rows = 1;
	if (rows > VT_MAXROWS)
		rows = VT_MAXROWS;
	long ocols = vt.cols, orows = vt.rows;
	if ((unsigned long) vt.cy >= (unsigned long) rows)
	{						/* la ligne du curseur reste la derniere */
		vt_up (0, orows - 1, vt.cy - rows + 1, 1);
		vt.cy = rows - 1;
	}
	for (long r = 0; r < rows; r++)			/* places neuves vides, dans les deux ecrans */
	{
		long c0 = r < orows ? ocols : 0;	/* premiere cellule neuve de la ligne */
		if ((unsigned long) c0 >= (unsigned long) cols)
			continue;
		cells_fill (rowp_main[r] + c0, cols - c0, 0);
		cells_fill (rowp_alt[r] + c0, cols - c0, 0);
	}
	vt.cols = cols;
	vt.rows = rows;
	vt.top = 0;
	vt.bot = rows - 1;
	vt_goto (vt.cx, vt.cy);
}

/* ---- ecriture ---- */

/* un point de code ecrit au curseur avec le stylo */
static void vt_print (uint32_t cp)
{
	vt.lastcp = cp;
	if (vt.wrap)
	{						/* retour a la ligne en attente */
		vt.wrap = 0;
		vt.cx = 0;
		vt_lf ();
	}
	struct cell *c = vt_row (vt.cy) + vt.cx;
	*c = vt.pen;
	c->cp |= cp;
	if ((unsigned long) (vt.cx + 1) < (unsigned long) vt.cols)
		vt.cx++;
	else if (vt.modes & VM_WRAP)			/* derniere colonne : le curseur y reste */
		vt.wrap = 1;
}

/* une commande C0 (< 0x20) */
static void vt_c0 (unsigned b)
{
	if (b == 8)
	{
		if (vt.cx)
			vt.cx--;
		vt.wrap = 0;
	}
	else if (b == 9)
	{						/* taquets toutes les 8 colonnes */
		long x = (vt.cx | 7) + 1;
		if ((unsigned long) x > (unsigned long) (vt.cols - 1))
			x = vt.cols - 1;
		vt.cx = x;
		vt.wrap = 0;
	}
	else if (b == 13)
	{
		vt.cx = 0;
		vt.wrap = 0;
	}
	else if (b >= 10 && b <= 12)
	{						/* LF, VT, FF */
		vt_lf ();
		if (vt.modes & VM_LFCR)
			vt.cx = 0;
		vt.wrap = 0;
	}
	/* BEL, SO, SI... : rien */
}

/* un octet (>= 0x20, hors sequence) : UTF-8 -> point de code -> vt_print */
static void vt_utf8 (unsigned b)
{
	if (b < 0x80)
	{
		if (vt.uneed)
		{					/* sequence coupee */
			vt.uneed = 0;
			vt_print (0xFFFD);
		}
		vt_print (b);
		return;
	}
	if (b < 0xC0)
	{
		if (!vt.uneed)
			goto bad;			/* suite sans debut */
		vt.ucp = (vt.ucp << 6) | (b & 0x3F);
		if (--vt.uneed)
			return;
		if (vt.ucp > 0x10FFFF || (vt.ucp & 0xFFFFF800) == 0xD800)	/* (demi-codets D800-DFFF) */
			goto bad;
		vt_print (vt.ucp);
		return;
	}
	if (vt.uneed)
	{
		vt.uneed = 0;
		vt_print (0xFFFD);
	}
	if (b < 0xC2)
		goto bad;
	if (b < 0xE0)
	{
		vt.uneed = 1;
		vt.ucp = b & 0x1F;
		return;
	}
	if (b < 0xF0)
	{
		vt.uneed = 2;
		vt.ucp = b & 0x0F;
		return;
	}
	if (b < 0xF5)
	{
		vt.uneed = 3;
		vt.ucp = b & 0x07;
		return;
	}
bad:
	vt.uneed = 0;
	vt_print (0xFFFD);
}

/* un octet apres ESC (0x20..0xFF) */
static void vt_esc (unsigned b)
{
	if (b < 0x30)
	{
		vt.state = ST_ESCI;
		vt.inter = b;
		return;
	}
	vt.state = ST_GROUND;				/* (le plus souvent : fini) */
	if (b >= 0x7F)
		return;
	switch (b)
	{
	case '[':
		vt.state = ST_CSI;
		vt.np = 0;
		vt.par[0] = 0;
		vt.prefix = 0;
		vt.inter = 0;
		break;
	case ']': case 'P': case 'X': case '^': case '_':	/* OSC, DCS, SOS, PM, APC */
		vt.state = ST_STR;
		break;
	case '7':
		vt_save (&vt.sc);
		break;
	case '8':
		vt_restore (&vt.sc);
		break;
	case 'E':
		vt.cx = 0;
		/* fall through */
	case 'D':
		vt.wrap = 0;
		vt_lf ();
		break;
	case 'M':
		vt.wrap = 0;
		vt_ri ();
		break;
	case 'c':
		vt_ris ();
		break;
	}						/* (ESC = ESC > ESC \ ... : rien) */
}

/* numero, valeur par defaut -> parametre CSI (0 ou absent : la valeur par defaut) */
static long vt_par (long i, long def)
{
	if ((unsigned long) i >= (unsigned long) vt.np || vt.par[i] == 0)
		return def;
	return vt.par[i];
}

/* set = 1 : CSI ? n h, 0 : CSI ? n l, pour chaque parametre */
static void vt_decset (int set)
{
	for (long i = 0; i < vt.np; i++)
	{
		uint32_t p = vt.par[i];
		long m = 0;
		switch (p)
		{
		case 1: m = VM_APPCK; break;
		case 7: m = VM_WRAP; break;
		case 25: m = VM_CURSOR; break;
		case 2004: m = VM_PASTE; break;
		case 47:
		case 1047:
			if (set)
				vt_alt_enter (0);
			else
			{
				if (p == 1047 && vt.alt)	/* ?1047l : l'alternatif est efface */
					vt_clear_rows (0, vt.rows);
				vt_alt_leave ();
			}
			continue;
		case 1048:
			if (set)
				vt_save (&vt.sc);
			else
				vt_restore (&vt.sc);
			continue;
		case 1049:
			if (set)
			{				/* ?1049h : sauve, ecran alternatif vide */
				vt_save (&vt.sc2);
				vt_alt_enter (1);
			}
			else
			{				/* ?1049l : ecran principal, curseur rendu */
				vt_alt_leave ();
				vt_restore (&vt.sc2);
			}
			continue;
		default:
			continue;
		}
		if (set)
			vt.modes |= m;
		else
			vt.modes &= ~m;
	}
}

/* ---- sequences CSI ---- */

static void csi_cuu (void)
{
	long n = vt_par (0, 1);
	long lim = vt.cy < vt.top ? 0 : vt.top;		/* borne : haut de la zone si dedans */
	long y = vt.cy - n;
	if (y < lim)
		y = lim;
	vt_goto (vt.cx, y);
}

static void csi_cud (void)
{
	long n = vt_par (0, 1);
	long lim = vt.cy > vt.bot ? vt.rows - 1 : vt.bot;	/* borne : bas de la zone si dedans */
	long y = vt.cy + n;
	if (y > lim)
		y = lim;
	vt_goto (vt.cx, y);
}

static void csi_cuf (void) { vt_goto (vt.cx + vt_par (0, 1), vt.cy); }
static void csi_cub (void) { vt_goto (vt.cx - vt_par (0, 1), vt.cy); }
static void csi_cnl (void) { csi_cud (); vt.cx = 0; }
static void csi_cpl (void) { csi_cuu (); vt.cx = 0; }
static void csi_cha (void) { vt_goto (vt_par (0, 1) - 1, vt.cy); }
static void csi_vpa (void) { vt_goto (vt.cx, vt_par (0, 1) - 1); }

static void csi_cup (void)
{
	long y = vt_par (0, 1) - 1;
	vt_goto (vt_par (1, 1) - 1, y);
}

static void csi_ed (void) { vt_erase (vt.np ? (long) vt.par[0] : 0); }

static void csi_el (void)
{
	vt.wrap = 0;
	long m = vt.np ? (long) vt.par[0] : 0;
	if (m == 0)					/* du curseur a la fin */
		vt_clear_span (vt.cy, vt.cx, VT_MAXCOLS);
	else if (m == 1)				/* du debut au curseur */
		vt_clear_span (vt.cy, 0, vt.cx + 1);
	else if (m == 2)				/* toute la ligne */
		vt_clear_span (vt.cy, 0, VT_MAXCOLS);
}

/* IL / DL : ins = 1 insere, 0 supprime (curseur dans la zone) */
static void vt_ildl (int ins)
{
	long n = vt_par (0, 1);
	if ((unsigned long) vt.cy < (unsigned long) vt.top || (unsigned long) vt.cy > (unsigned long) vt.bot)
		return;
	if (ins)
		vt_down (vt.cy, vt.bot, n);
	else
		vt_up (vt.cy, vt.bot, n, 0);
	vt.cx = 0;
	vt.wrap = 0;
}

static void csi_il (void) { vt_ildl (1); }
static void csi_dl (void) { vt_ildl (0); }

/* ICH / DCH / ECH : how = 0 insere, 1 supprime, 2 efface n cellules au curseur */
static void vt_chars (int how)
{
	unsigned long n = (unsigned long) vt_par (0, 1);
	vt.wrap = 0;
	long rest = vt.cols - vt.cx;			/* cellules du curseur a la fin */
	long k = n > (unsigned long) rest ? rest : (long) n;	/* n borne */
	if (how == 2)
	{
		vt_clear_span (vt.cy, vt.cx, vt.cx + k);
		return;
	}
	struct cell *p = vt_row (vt.cy) + vt.cx;	/* cellule du curseur */
	if (how == 0)
	{						/* ICH : decale a droite */
		cells_move (p + k, p, rest - k);
		cells_fill (p, k, vt.pen.bg);
	}
	else
	{						/* DCH : decale a gauche */
		cells_move (p, p + k, rest - k);
		cells_fill (p + (rest - k), k, vt.pen.bg);
	}
}

static void csi_ich (void) { vt_chars (0); }
static void csi_dch (void) { vt_chars (1); }
static void csi_ech (void) { vt_chars (2); }
static void csi_su (void) { vt_up (vt.top, vt.bot, vt_par (0, 1), 1); }
static void csi_sd (void) { vt_down (vt.top, vt.bot, vt_par (0, 1)); }

/* CSI haut ; bas r : zone de defilement (haut < bas), curseur au coin */
static void csi_stbm (void)
{
	unsigned long t = (unsigned long) vt_par (0, 1);
	unsigned long b = (unsigned long) vt_par (1, vt.rows);
	if (b > (unsigned long) vt.rows)
		b = (unsigned long) vt.rows;
	if (t >= b)
		return;
	vt.top = (long) t - 1;
	vt.bot = (long) b - 1;
	vt_goto (0, 0);
}

static void csi_scp (void) { vt_save (&vt.sc); }
static void csi_rcp (void) { vt_restore (&vt.sc); }

/* CSI n b : le dernier caractere n fois */
static void csi_rep (void)
{
	unsigned long n = (unsigned long) vt_par (0, 1);
	unsigned long max = (unsigned long) (vt.rows * vt.cols);
	if (n > max)
		n = max;
	if (!vt.lastcp)
		return;
	for (; n; n--)
		vt_print (vt.lastcp);
}

/* CSI n I : n taquets en avant */
static void csi_cht (void)
{
	unsigned long n = (unsigned long) vt_par (0, 1);
	if (n > VT_MAXCOLS)
		n = VT_MAXCOLS;
	for (; n; n--)
		vt_c0 (9);
}

/* CSI n Z : n taquets en arriere */
static void csi_cbt (void)
{
	long n = vt_par (0, 1);
	long x = vt.cx;
	for (; n && x; n--)
		x = (x - 1) & ~7L;
	vt.cx = x;
	vt.wrap = 0;
}

/* (couleur etendue de CSI 38 / 48, a partir du parametre *i) -> couleur (0 : forme inconnue) ; *i avance.
 * 5 ; n : palette ; 2 ; r ; g ; b : 24 bits. */
static uint32_t sgr_ext (long *i)
{
	long np = vt.np;
	if (*i >= np)
		return 0;
	uint32_t k = vt.par[(*i)++];
	if (k == 5)
	{
		uint32_t n = *i < np ? vt.par[*i] & 0xFF : 0;
		(*i)++;
		return VC_IDX | n;
	}
	if (k != 2)
		return 0;
	uint32_t c = 0;
	for (int j = 0; j < 3; j++)
	{
		uint32_t v = *i < np ? vt.par[*i] & 0xFF : 0;
		(*i)++;
		c = (c << 8) | v;
	}
	return VC_RGB | c;
}

/* CSI ... m : attributs et couleurs */
static void csi_sgr (void)
{
	uint32_t attr = 0, fg = 0, bg = 0;
	if (vt.np)					/* (CSI m : tout par defaut) */
	{
		attr = vt.pen.attr;
		fg = vt.pen.fg;
		bg = vt.pen.bg;
	}
	for (long i = 0; i < vt.np;)
	{
		uint32_t p = vt.par[i++], c;
		switch (p)
		{
		case 0: attr = fg = bg = 0; break;
		case 1: attr |= VA_BOLD; break;
		case 2: attr |= VA_DIM; break;
		case 3: attr |= VA_ITALIC; break;
		case 4: case 21: attr |= VA_UNDER; break;	/* (double souligne : souligne) */
		case 5: case 6: attr |= VA_BLINK; break;
		case 7: attr |= VA_REV; break;
		case 8: attr |= VA_HIDDEN; break;
		case 9: attr |= VA_STRIKE; break;
		case 22: attr &= ~(uint32_t) (VA_BOLD | VA_DIM); break;
		case 23: attr &= ~(uint32_t) VA_ITALIC; break;
		case 24: attr &= ~(uint32_t) VA_UNDER; break;
		case 25: attr &= ~(uint32_t) VA_BLINK; break;
		case 27: attr &= ~(uint32_t) VA_REV; break;
		case 28: attr &= ~(uint32_t) VA_HIDDEN; break;
		case 29: attr &= ~(uint32_t) VA_STRIKE; break;
		case 38:
			if ((c = sgr_ext (&i)))
				fg = c;
			break;
		case 39: fg = 0; break;
		case 48:
			if ((c = sgr_ext (&i)))
				bg = c;
			break;
		case 49: bg = 0; break;
		default:
			if (p >= 30 && p < 38)
				fg = VC_IDX | (p - 30);
			else if (p >= 90 && p < 98)
				fg = VC_IDX | (p - 90 + 8);
			else if (p >= 40 && p < 48)
				bg = VC_IDX | (p - 40);
			else if (p >= 100 && p < 108)
				bg = VC_IDX | (p - 100 + 8);
			break;				/* (inconnu : ignore) */
		}
	}
	vt.pen.attr = attr;
	vt.pen.fg = fg;
	vt.pen.bg = bg;
}

/* CSI c : DA (un VT220 avec couleurs, comme xterm le dit) */
static void csi_da (void)
{
	if (vt_par (0, 0) == 0)
		term_reply_s ("\033[?62;22c");
}

/* CSI n : DSR ; 5 : etat (0n), 6 : position du curseur (CSI ligne ; colonne R) */
static void csi_dsr (void)
{
	long p = vt_par (0, 0);
	if (p == 5)
		term_reply_s ("\033[0n");
	else if (p == 6)
	{
		char s[48];
		snprintf (s, sizeof s, "\033[%lu;%luR", (unsigned long) vt.cy + 1, (unsigned long) vt.cx + 1);
		term_reply_s (s);
	}
}

/* table des finaux CSI sans prefixe ni intermediaire */
static const struct { unsigned char final; void (*fn) (void); } csi_tab[] = {
	{ 'A', csi_cuu }, { 'B', csi_cud }, { 'C', csi_cuf }, { 'D', csi_cub },
	{ 'E', csi_cnl }, { 'F', csi_cpl }, { 'G', csi_cha }, { '`', csi_cha },
	{ 'H', csi_cup }, { 'f', csi_cup }, { 'd', csi_vpa }, { 'e', csi_cud },
	{ 'a', csi_cuf }, { 'J', csi_ed }, { 'K', csi_el }, { 'L', csi_il },
	{ 'M', csi_dl }, { '@', csi_ich }, { 'P', csi_dch }, { 'X', csi_ech },
	{ 'S', csi_su }, { 'T', csi_sd }, { 'r', csi_stbm }, { 'm', csi_sgr },
	{ 's', csi_scp }, { 'u', csi_rcp }, { 'b', csi_rep }, { 'I', csi_cht },
	{ 'Z', csi_cbt }, { 'n', csi_dsr }, { 'c', csi_da },
};

/* le final : execute la sequence CSI lue */
static void vt_csi (unsigned final)
{
	if (vt.prefix == '?')
	{
		if (final == 'h')
			vt_decset (1);
		else if (final == 'l')
			vt_decset (0);
		return;
	}
	if (vt.prefix == '>')
	{						/* CSI > c : DA secondaire */
		if (final == 'c')
			term_reply_s ("\033[>0;10;1c");
		return;
	}
	if (vt.prefix)					/* CSI > 4 ; 1 m, CSI = c... : avales */
		return;
	if (vt.inter)
	{						/* CSI ! p : DECSTR ; autres (CSI SP q...) : avales */
		if (vt.inter == '!' && final == 'p')
			vt_soft_reset ();
		return;
	}
	for (unsigned i = 0; i < sizeof csi_tab / sizeof csi_tab[0]; i++)
		if (csi_tab[i].final == final)
		{
			csi_tab[i].fn ();
			return;
		}
}

/* un octet d'une sequence CSI (>= 0x20) */
static void vt_csi_byte (unsigned b)
{
	long s = vt.state;
	if (b >= 0x80)
	{
		vt.state = ST_CSIX;
		return;
	}
	if (b >= 0x40)
	{						/* final */
		vt.state = ST_GROUND;
		if (s != ST_CSIX)
			vt_csi (b);
		return;
	}
	if (s == ST_CSIX)
		return;
	if (b < 0x30)
	{						/* intermediaire */
		vt.inter = b;
		vt.state = ST_CSII;
		return;
	}
	if (b >= 0x3C)
	{						/* prefixe ? > < = */
		if (s != ST_CSI)
		{
			vt.state = ST_CSIX;
			return;
		}
		vt.prefix = b;
		vt.state = ST_CSIP;
		return;
	}
	if (s == ST_CSII)
	{						/* chiffre apres un intermediaire */
		vt.state = ST_CSIX;
		return;
	}
	vt.state = ST_CSIP;
	if (!vt.np)
	{
		vt.np = 1;
		vt.par[0] = 0;
	}
	if (b == ';' || b == ':')
	{						/* (sous-parametres 38:2:r:g:b : comme ;) */
		if (vt.np < VT_NPAR)
			vt.par[vt.np++] = 0;
		return;
	}
	uint32_t *p = &vt.par[vt.np - 1];		/* chiffre */
	uint32_t v = *p * 10 + (b - '0');
	*p = v > 0xFFFF ? 0xFFFF : v;
}

/* un octet : l'entree de l'analyseur (sortie d'un programme, messages du Terminal) */
void term_putb (unsigned b)
{
	b &= 0xFF;
	long s = vt.state;
	if (b == 0x1B)
	{
		vt.state = ST_ESC;
		vt.uneed = 0;
		vt.inter = 0;
		return;
	}
	if (b == 0x18 || b == 0x1A || (s == ST_STR && b == 7))
	{						/* CAN, SUB : abandon ; BEL termine un OSC */
		vt.state = ST_GROUND;
		vt.uneed = 0;
		return;
	}
	if (s == ST_STR)				/* (le reste d'un OSC est avale) */
		return;
	if (b < 0x20)
	{
		vt.uneed = 0;				/* (UTF-8 coupe : abandonne) */
		vt_c0 (b);				/* execute aussi au milieu d'une sequence */
		return;
	}
	if (b == 0x7F)
		return;
	if (s == ST_GROUND)
		vt_utf8 (b);
	else if (s == ST_ESC)
		vt_esc (b);
	else if (s == ST_ESCI)
	{						/* ESC ( B, ESC # 8... : avales */
		if (b >= 0x30)
			vt.state = ST_GROUND;
	}
	else
		vt_csi_byte (b);
}
