/*
 * calculator -- calculatrice 4 operations en double precision.
 * Souris (boutons) et clavier (chiffres, + - * /, Entree, Retour arriere, Echap, pave numerique). Fenetre
 * de taille fixe ; agrandie (mode poche d'Elegant, WEV_RESIZE : docs/MODE-POCHE.md), l'affichage et les
 * touches suivent la taille (layout), le texte grandit avec eux.
 * Port en C de calculator.asm (memes touches, memes calculs et memes arrondis, memes textes), un seul source
 * pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <math.h>
#include <string.h>
#include <uikit.h>

#define WIN_W		300			/* taille de conception */
#define WIN_H		424
#define DISP_H		96			/* affichage (a cette taille) */
#define BTN_H		56			/* touche (a cette taille) */
#define GAP		8
#define MARGIN		10
#define ENTRY_MAX	15
#define UK_BTN_PX	14			/* (texte de ui_button) */

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }

/* boutons : colonne, ligne, largeur, accent, code, libelle */
struct button { unsigned char col, row, span, acc; int code; const char *label; };
static const struct button buttons[] = {
	{ 0, 0, 1, 0, 'C', "C" }, { 1, 0, 1, 0, '<', "\xE2\x86\x90" }, { 2, 0, 1, 0, 'n', "\xC2\xB1" },
	{ 3, 0, 1, 1, '/', "\xC3\xB7" },
	{ 0, 1, 1, 0, '7', "7" }, { 1, 1, 1, 0, '8', "8" }, { 2, 1, 1, 0, '9', "9" }, { 3, 1, 1, 1, '*', "\xC3\x97" },
	{ 0, 2, 1, 0, '4', "4" }, { 1, 2, 1, 0, '5', "5" }, { 2, 2, 1, 0, '6', "6" }, { 3, 2, 1, 1, '-', "\xE2\x88\x92" },
	{ 0, 3, 1, 0, '1', "1" }, { 1, 3, 1, 0, '2', "2" }, { 2, 3, 1, 0, '3', "3" }, { 3, 3, 1, 1, '+', "+" },
	{ 0, 4, 2, 0, '0', "0" }, { 2, 4, 1, 0, '.', "," }, { 3, 4, 1, 1, '=', "=" },
};
#define NBUTTONS	((int) (sizeof buttons / sizeof buttons[0]))

/* la disposition : touche (largeur), ecart, hauteur, haut de la grille, affichage, texte */
static struct { unsigned long bw, gap, bh, y0, dh, px; } lay;
/* l'etat : acc, op, apres « = », saisie fraiche (0 affiche) */
static double acc;
static int op, after_eq, fresh;
static char entry[32], history[96];

/* ---------------------------------------------------------------------------
 *  Disposition
 * ------------------------------------------------------------------------- */

/* ui.w, ui.h -> lay : affichage et touches. A la taille de conception : les mesures d'origine ; plus
 * grand : tout suit, texte compris. (Divisions non signees, comme l'asm.) */
static void layout (void)
{
	unsigned long w = (unsigned long) ui.w, h = (unsigned long) ui.h;
	lay.gap = w >= 500 ? 12 : GAP;			/* ecart : 8, 12 en grand */
	unsigned long dh = DISP_H * h / WIN_H;		/* affichage : proportionnel a la hauteur */
	lay.dh = dh < DISP_H ? DISP_H : dh;
	lay.y0 = lay.dh + lay.gap + 4;			/* haut de la grille */
	lay.bh = (h - lay.y0 - 4 - (lay.gap << 2)) / 5;	/* touche : 5 rangees, 4 colonnes */
	lay.bw = (w - 2 * MARGIN - 3 * lay.gap) >> 2;
	long px = UK_BTN_PX;				/* texte des touches : 14, puis la moitie */
	long more = (long) lay.bh - BTN_H;		/* de ce que la touche gagne (32 au plus) */
	if (more > 0)
	{
		px += (unsigned long) more >> 1;
		if (px > 32)
			px = 32;
	}
	lay.px = (unsigned long) px;
}

static void btn_rect (const struct button *b, long *x, long *y, long *w, long *h)
{
	unsigned long step = lay.bw + lay.gap;
	*x = (long) (MARGIN + b->col * step);
	*w = (long) (b->span * step - lay.gap);
	*h = (long) lay.bh;
	*y = (long) (b->row * (lay.bh + lay.gap) + lay.y0);
}

/* -> code du bouton sous la souris, 0 sinon */
static int button_at (void)
{
	for (int i = 0; i < NBUTTONS; i++)
	{
		long x, y, w, h;
		btn_rect (&buttons[i], &x, &y, &w, &h);
		if (ui_hit (x, y, w, h))
			return buttons[i].code;
	}
	return 0;
}

/* code HID, caractere -> code d'action (0 : aucun) */
static int key_to_code (int k, int c)
{
	if (k == KEY_ENTER || k == 0x58)		/* (Entree du pave) */
		return '=';
	if (k == KEY_BACKSPACE)
		return '<';
	if (k == KEY_ESC || k == KEY_DELETE)
		return 'C';
	if ((unsigned) (k - 0x59) <= 8)			/* pave : 1..9 */
		return k - 0x59 + '1';
	switch (k)
	{
	case 0x62: return '0';
	case 0x63: return '.';
	case 0x54: return '/';
	case 0x55: return '*';
	case 0x56: return '-';
	case 0x57: return '+';
	}
	if ((unsigned) (c - '0') <= 9)			/* caracteres */
		return c;
	switch (c)
	{
	case '.': case '+': case '-': case '*': case '/': return c;
	case ',': return '.';
	case '=': return '=';
	case 'c': case 'C': return 'C';
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Logique
 * ------------------------------------------------------------------------- */

static void calc_reset (void)
{
	strcpy (entry, "0");
	acc = 0;
	op = 0;
	after_eq = 0;
	fresh = 1;
	history[0] = 0;
}

/* la saisie -> sa valeur */
static double parse_entry (void)
{
	const char *s = entry;
	double v = 0;
	int neg = 0;
	if (*s == '-')
	{
		neg = 1;
		s++;
	}
	for (;;)
	{
		unsigned c = (unsigned char) *s++;
		if (c == '.')
		{
			double f = 1;
			for (;;)
			{
				unsigned d = (unsigned char) *s++ - '0';
				if (d > 9)
					break;
				f = f / 10.0;
				v = v + (double) d * f;
			}
			break;
		}
		if (c - '0' > 9)
			break;
		v = v * 10.0 + (double) (c - '0');
	}
	return neg ? -v : v;
}

/* valeur, tampon (>= 32 o) -> 1 si ok, 0 si non representable. 12 chiffres significatifs, zeros inutiles
 * retires. */
static int fmt_double (double v, char *o)
{
	char *p = o;
	if (v != v)
		return 0;				/* NaN */
	if (!(v >= 0))
	{
		*p++ = '-';
		v = -v;
	}
	if (v >= 1e15)
		return 0;
	unsigned long n = (unsigned long) v;		/* chiffres de la partie entiere */
	int digits = 1;
	while (n >= 10)
	{
		n /= 10;
		digits++;
	}
	int dec = 12 - digits;				/* decimales = 12 - chiffres (0..11) */
	if (dec < 0)
		dec = 0;
	if (dec > 11)
		dec = 11;
	double m = 1;
	for (int i = 0; i < dec; i++)
		m *= 10.0;
	n = (unsigned long) round (v * m);		/* (frinta : au plus proche, moitie loin de zero) */
	char d[32];
	int k = 0;
	do
	{					/* chiffres a l'envers */
		d[k++] = (char) ('0' + n % 10);
		n /= 10;
	}
	while (n);
	while (k <= dec)
		d[k++] = '0';				/* au moins decimales+1 chiffres */
	while (k > 0)
	{
		k--;
		*p++ = d[k];
		if (k && k == dec)
			*p++ = '.';
	}
	if (dec)
	{					/* retirer les zeros et le point inutiles */
		while (p[-1] == '0')
			p--;
		if (p[-1] == '.')
			p--;
	}
	*p = 0;
	if (o[0] == '-' && o[1] == '0' && !o[2])
		strcpy (o, "0");			/* « -0 » -> « 0 » */
	return 1;
}

static const char *op_symbol (int c)
{
	return c == '+' ? "+" : c == '-' ? "\xE2\x88\x92" : c == '*' ? "\xC3\x97" : "\xC3\xB7";
}

/* morceau de texte traduit : les espaces autour restent (un dictionnaire ne garde pas ceux d'une cle) */
static void tr_copy (char *d, const char *s)
{
	char k[240];
	while (*s == ' ')
		*d++ = *s++;
	int n = 0, i = 0;
	for (; s[i] && i < 239; i++)
	{
		k[i] = s[i];
		if (s[i] != ' ')
			n = i + 1;
	}
	k[n] = 0;
	strcpy (d, TR (k));
	strcat (d, s + n);
}

/* acc = acc op saisie ; resultat formate dans la saisie -> 0 si erreur */
static int apply (void)
{
	double b = parse_entry ();
	double r = acc;
	if (op == '+')
		r = acc + b;
	else if (op == '-')
		r = acc - b;
	else if (op == '*')
		r = acc * b;
	else if (b == 0)
		goto err;
	else
		r = acc / b;
	acc = r;
	if (fmt_double (acc, entry))
		return 1;
err:
	calc_reset ();
	tr_copy (entry, "Erreur");
	history[0] = 0;
	fresh = 1;
	return 0;
}

/* historique : « acc op » */
static void make_history (void)
{
	strcpy (history, entry);
	strcat (history, " ");
	strcat (history, op_symbol (op));
}

/* historique : « acc op saisie = » */
static void make_history_eq (void)
{
	char t[32];
	fmt_double (acc, t);
	strcpy (history, t);
	strcat (history, " ");
	strcat (history, op_symbol (op));
	strcat (history, " ");
	strcat (history, entry);
	strcat (history, " =");
}

/* code d'action */
static void calc_input (int c)
{
	if ((unsigned) (c - '0') <= 9 || c == '.')
	{
		if (fresh)
		{				/* nouvelle saisie */
			entry[0] = 0;
			fresh = 0;
			if (after_eq)
			{			/* apres « = » : on repart de zero */
				after_eq = 0;
				op = 0;
				history[0] = 0;
			}
		}
		size_t n = strlen (entry);
		if (n >= ENTRY_MAX)
			return;
		if (c == '.')
		{
			if (strchr (entry, '.'))
				return;			/* un seul point */
			if (n == 0)
			{
				strcpy (entry, "0.");	/* « .5 » -> « 0.5 » */
				return;
			}
		}
		else if (n == 1 && entry[0] == '0')
			n = 0;				/* remplacer un « 0 » seul */
		entry[n] = (char) c;
		entry[n + 1] = 0;
		return;
	}
	switch (c)
	{
	case 'C':
		calc_reset ();
		return;
	case '<':
	{
		if (fresh)
			return;
		size_t n = strlen (entry);
		if (n > 1)
		{
			entry[--n] = 0;
			if (!(n == 1 && entry[0] == '-'))	/* « - » seul -> 0 */
				return;
		}
		strcpy (entry, "0");
		return;
	}
	case 'n':				/* changer le signe de la saisie */
		if (entry[0] == '-')
			memmove (entry, entry + 1, strlen (entry));
		else
		{
			size_t n = strlen (entry);
			if (n == 1 && entry[0] == '0')
				return;
			memmove (entry + 1, entry, n + 1);
			entry[0] = '-';
			fresh = 0;
		}
		return;
	case '=':
		if (!op)
			return;
		make_history_eq ();
		if (!apply ())
			return;
		op = 0;
		after_eq = 1;
		fresh = 1;
		return;
	}
	/* operateur */
	if (!op)
		acc = parse_entry ();		/* acc = saisie */
	else if (!fresh && !apply ())		/* (saisie fraiche : changement d'operateur seul) */
		return;
	op = c;
	after_eq = 0;				/* plus « apres = » */
	fresh = 1;
	fmt_double (acc, entry);
	make_history ();
}

/* ---------------------------------------------------------------------------
 *  Dessin
 * ------------------------------------------------------------------------- */

/* taille de texte a la taille de conception -> taille pour lay.dh */
static long disp_px (long px)
{
	return (long) ((unsigned long) px * lay.dh / DISP_H);
}

/* l'affichage (hauteur lay.dh) : historique en haut, saisie en bas, alignes a droite ; textes de 13 et 34
 * (24 si long) a la taille de conception, proportionnels a lay.dh au-dela */
static void draw_display (void)
{
	long dh = (long) lay.dh;
	long right = ui.w - (MARGIN + 12);		/* bord droit du texte */
	ui_fill (MARGIN, MARGIN, right + 12 - MARGIN, dh - GAP, tok (TJ_FACE));
	long px = disp_px (13);
	long w = ui_text_width ((long) history, px, (long) ui.font);
	ui_text ((long) history, right - w, 3 * dh >> 3, tok (TJ_TEXT2), px, (long) ui.font, 0);
	px = disp_px (strlen (entry) >= 12 ? 24 : 34);	/* taille du resultat : reduite si long */
	w = ui_text_width ((long) entry, px, (long) ui.font);
	ui_text ((long) entry, right - w, 7 * dh >> 3, tok (TJ_TEXT1), px, (long) ui.font, 0);
	ui_update (0, 0, ui.w, dh + GAP);
}

static void draw_all (void)
{
	draw_display ();
	for (int i = 0; i < NBUTTONS; i++)
	{
		const struct button *b = &buttons[i];
		long x, y, w, h;
		btn_rect (b, &x, &y, &w, &h);		/* la touche (ui_button, sans texte), */
		ui_button (x, y, w, h, (long) "", b->acc);	/* puis son libelle a la taille de lay */
		long px = (long) lay.px;
		unsigned long tw = (unsigned long) ui_text_width ((long) b->label, px, (long) ui.font);
		long tx = x + (long) (((unsigned long) w - tw) >> 1);	/* centre */
		long base = 5 * px / 14 + (long) ((unsigned long) h >> 1) + y;	/* milieu + 5/14 du texte */
		ui_text ((long) b->label, tx, base, b->acc ? tok (TJ_ACCENT_INK) : tok (TJ_TEXT1), px, (long) ui.font, 0);
	}
	ui_update_all ();
}

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	if (ui_window (WIN_W, WIN_H, (long) TR ("Calculatrice")) < 0)
		return 0;
	layout ();
	calc_reset ();
	draw_all ();
	for (;;)
	{
		long t = ui_event (1000);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MOUSE)
		{
			if (ui_ev_field (&ui, EV_KIND) != 1)
				continue;
			int c = button_at ();
			if (c)
			{
				calc_input (c);
				draw_display ();
			}
		}
		else if (t == WEV_KEY)
		{
			if (!ui_ev_field (&ui, EV_KIND))
				continue;
			int c = key_to_code (ui_ev_field (&ui, EV_CODE), ui_ev_field (&ui, EV_CHAR));
			if (c)
			{
				calc_input (c);
				draw_display ();
			}
		}
		else if (t == WEV_RESIZE || t == WEV_THEME)
		{
			/* autre taille (mode poche) : nouveau tampon, puis tout a cette taille ; autre theme : le fond
			 * (peint par UIKit a l'ouverture), puis le reste */
			if (t == WEV_RESIZE)
			{
				if (ui_resize (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y)) < 0)
					continue;
				layout ();
			}
			ui_fill (0, 0, ui.w, ui.h, tok (TJ_FACE));
			draw_all ();
		}
	}
	return 0;
}
