/*
 * applet.cpp -- le cadre des volets du panneau de configuration (applet.inc de Korin) : des lignes (titres, choix
 * uniques par groupe, cases à cocher, notes) en deux colonnes, un message, le bouton « Appliquer » ; ap.extra peut
 * dessiner dans la 2e colonne (l'aperçu du thème) ; ap.draw et ap.click remplacent les lignes par le dessin du volet.
 * Trop de lignes pour le volet : elles défilent (molette, barre à droite).
 *
 * Les lignes de réglages (fm_*) : une carte de listes déroulantes (Région, Écran), le bas reste au cadre.
 * Les aides : ini_set (une clé d'un fichier « clé = valeur »), file_write, relaunch (relancer un programme de la
 * session), tr_cat (un morceau traduit), ini_read (src/inc/applist.inc).
 */
#include <fcntl.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include "control.h"

extern char **environ;

/* src/inc/syscalls.inc */
enum { SYS_KILL = 0x14, SYS_SLEEP = 0x17, SYS_TIME_MS = 0x19, SYS_PROCLIST_FROM = 0x1F, SYS_SVC_OPEN = 0x31,
       SYS_SVC_CALL = 0x32 };

Pane ap;
char ap_file[AP_FILE_MAX];
char ap_out[AP_FILE_MAX + 128];
long fm_n;
long fm_w[FM_N];
static const char *fm_note;
static const char *fm_lib[FM_N];

/* ---- les lignes ---- */

/* une ligne de plus -> son numéro (-1 : plus de place) */
long applet_row (const char *text, int kind, int group, int checked, long value)
{
	if (ap.nrows >= ROWS_MAX)
		return -1;
	Row &r = ap.rows[ap.nrows];
	r.text = text;
	r.kind = (unsigned char) kind;
	r.group = (unsigned char) group;
	r.checked = (unsigned char) checked;
	r.value = value;
	return ap.nrows++;
}

Row *row_ptr (long i) { return &ap.rows[i]; }

/* -> valeur de la ligne cochée du groupe (-1 : aucune) */
long applet_choice (int group)
{
	for (long i = 0; i < ap.nrows; i++)
	{
		const Row &r = ap.rows[i];
		if (r.kind == R_CHOICE && r.group == group && r.checked)
			return r.value;
	}
	return -1;
}

/* -> colonnes de lignes (1 si le volet dessine dans la 2e : aperçu, ou prend les deux pour de longs libellés :
 * ap.colw) ; *shown = lignes que le volet montre */
static long ap_place (long *shown)
{
	long cols = (ap.extra || ap.colw != COL_W) ? 1 : AP_COLS;
	*shown = ap.percol * cols;
	return cols;
}

/* ap.scroll ramené entre 0 et (lignes - montrées) */
void ap_border (void)
{
	long shown;
	ap_place (&shown);
	long m = ap.nrows - shown;
	if (m < 0)
		m = 0;
	if (ap.scroll > m)
		ap.scroll = m;
	if (ap.scroll < 0)
		ap.scroll = 0;
}

/* n = crans de molette : les lignes défilent -> 1 si elles ont bougé (le volet peut le remplacer : ap.wheel) */
int pane_wheel (long n)
{
	if (ap.wheel)
		return ap.wheel (n);
	long old = ap.scroll;
	ap.scroll = old - n;
	ap_border ();
	return ap.scroll != old;
}

/* clic dans la fenêtre -> 0 rien, 1 Appliquer, 2 ligne changée */
int pane_click (long x, long y)
{
	if (ap.xclick && ap.xclick (x, y))	/* (un clic sur le dessin du volet) */
		return 2;
	if (ap.widgets == 1)			/* (widgets : ils ont tout vu, sauf le bas du cadre pour les lignes de
						 * réglages) */
		return 0;
	long by = ui.h - (BTN_H + 12);		/* Appliquer, en bas à droite */
	long bx = ui.w - (AP_MARGIN + BTN_W);
	if (y >= by && y < by + BTN_H && x >= bx && x < bx + BTN_W)
		return 1;
	if (!ap.click)
	{
		long ry = y - (AP_HEAD + 4);	/* la barre de défilement (tactile : pas de molette) : 3 lignes plus */
		unsigned long rail = (unsigned long) (ap.percol * ROW_H);	/* haut ou plus bas */
		if (x >= ui.w - 14 && ry >= 0 && (unsigned long) ry < rail)
			return pane_wheel ((unsigned long) ry < (rail >> 1) ? 3 : -3) ? 2 : 0;
	}
	if (ap.click)				/* volet dessiné par lui-même */
	{
		if (!ap.click (x, y))
			return 0;
		ap.msg = 0;
		return 2;
	}
	long cx = x - (SIDE_W + AP_MARGIN);	/* colonne, ligne */
	if (cx < 0)
		return 0;
	long col = (long) ((unsigned long) cx / (unsigned long) ap.colw), shown;
	if (col >= ap_place (&shown))
		return 0;
	long ry = y - (AP_HEAD + 4);
	if (ry < 0)
		return 0;
	long row = ry / ROW_H;
	if (row >= ap.percol)
		return 0;
	long i = col * ap.percol + row + ap.scroll;
	if (i >= ap.nrows)
		return 0;
	Row &r = ap.rows[i];
	if (r.kind == R_CHECK)
		r.checked ^= 1;
	else if (r.kind == R_CHOICE)		/* choix : décocher le groupe, cocher */
	{
		for (long j = 0; j < ap.nrows; j++)
			if (ap.rows[j].kind == R_CHOICE && ap.rows[j].group == r.group)
				ap.rows[j].checked = 0;
		r.checked = 1;
	}
	else
		return 0;
	ap.msg = 0;				/* (un changement efface le message) */
	return 2;
}

/* le volet, à droite de la liste : son titre, ses lignes (deux colonnes), son dessin à lui (ap.extra : aperçu),
 * le message, Appliquer */
void pane_draw (void)
{
	ui_fill (SIDE_W, 0, ui.w - SIDE_W, ui.h, tok (ap.surface ? TJ_SURFACE : TJ_FACE));
	if (ap.title)
		ui_text ((long) TR (ap.title), SIDE_W + AP_MARGIN, AP_TITLE_Y, tok (TJ_TEXT1), 20, FB (), 0);
	if (ap.sub)
		ui_text ((long) TR (ap.sub), SIDE_W + AP_MARGIN, AP_SUB_Y, tok (TJ_TEXT2), 13, F (), 0);
	if (ap.draw)				/* volet dessiné par lui-même */
		ap.draw ();
	else
	{
		ap_border ();
		long shown, cols = ap_place (&shown);
		int sliders = 0;
		for (long i = 0; i < ap.nrows; i++)	/* les curseurs des lignes cachées : cachés */
			if (ap.rows[i].kind == R_SLIDER)
			{
				long k = i - ap.scroll;
				if (k < 0 || k >= shown)
					ui_w_show (ap.rows[i].value, 0);
				sliders = 1;
			}
		for (long i = ap.scroll; i < ap.nrows; i++)
		{
			long k = i - ap.scroll, col = k / ap.percol;
			if (col >= cols)
				break;
			long x = col * COL_W + SIDE_W + AP_MARGIN;
			long y = (k - col * ap.percol) * ROW_H + AP_HEAD + 4;
			const Row &r = ap.rows[i];
			if (r.kind == R_SLIDER)		/* curseur, puis sa valeur et l'unité */
			{
				long sw = ap.colw - 8 - 56;
				ui_w_move (r.value, x, y + 1, sw, ROW_H - 2);	/* (à sa place, puis montré : il s'y dessine) */
				ui_w_show (r.value, 1);
				char t[32];
				long v = ui_w_get_value (r.value), n = 0;
				char d[24];
				unsigned long u = (unsigned long) (v < 0 ? -v : v);
				do
					d[n++] = (char) ('0' + u % 10);
				while (u /= 10);
				char *o = t;
				if (v < 0)
					*o++ = '-';
				while (n)
					*o++ = d[--n];
				strcpy (o, r.text ? r.text : "");
				ui_text ((long) t, x + sw + 10, y + 18, tok (TJ_TEXT2), 13, F (), 0);
				continue;
			}
			if (r.kind == R_TITLE)
			{
				ui_text ((long) TR (r.text), x, y + 18, tok (TJ_TEXT2), 14, FB (), COL_W - 8);
				continue;
			}
			if (r.kind == R_NOTE)
			{
				ui_text ((long) TR (r.text), x, y + 18, tok (TJ_TEXT3), 13, F (), COL_W - 8);
				continue;
			}
			if (r.kind == R_CHECK)	/* case : carré arrondi, « ✓ » */
			{
				uint32_t c = tok (r.checked ? TJ_ACCENT : TJ_LINE_STRONG);
				ui_rbox (x, y + 5, 16, 16, 4, c, c);
				if (r.checked)
					ui_text ((long) "\xE2\x9C\x93", x + 3, y + 18, 0xFFFFFF, 13, FB (), 0);	/* (en dur, comme l'asm) */
				else
					ui_rbox (x + 1, y + 6, 14, 14, 3, tok (TJ_SURFACE), tok (TJ_SURFACE));
			}
			else if (r.checked)	/* choix : rond */
			{
				ui_rbox (x, y + 5, 16, 16, 8, tok (TJ_ACCENT), tok (TJ_ACCENT));
				ui_rbox (x + 5, y + 10, 6, 6, 3, 0xFFFFFF, 0xFFFFFF);
			}
			else
			{
				ui_rbox (x, y + 5, 16, 16, 8, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
				ui_rbox (x + 1, y + 6, 14, 14, 7, tok (TJ_SURFACE), tok (TJ_SURFACE));
			}
			ui_text ((long) TR (r.text), x + 26, y + 18, tok (TJ_TEXT1), 14, F (), ap.colw - 34);	/* libellé */
		}
		if (ap.nrows > shown)		/* barre de défilement : s'il en faut */
		{
			unsigned long rail = (unsigned long) (ap.percol * ROW_H);
			ui_rbox (ui.w - 10, AP_HEAD + 4, 6, (long) rail, 3, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
			long h = (long) (rail * (unsigned long) shown / (unsigned long) ap.nrows);	/* montrées / toutes */
			long y = (long) (rail * (unsigned long) ap.scroll / (unsigned long) ap.nrows) + AP_HEAD + 4;
			ui_rbox (ui.w - 10, y, 6, h, 3, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
		}
		if (ap.extra)			/* dessin du volet (2e colonne) */
			ap.extra (SIDE_W + AP_MARGIN + COL_W, AP_HEAD + 8, COL_W - 8);
		if (sliders)
			ui_w_draw (0);
	}
	if (ap.widgets == 1)			/* (widgets : leur propre bas) */
		return;
	ui_fill (SIDE_W, ui.h - AP_BOTTOM, ui.w - SIDE_W, 1, tok (TJ_LINE));	/* trait au-dessus du bouton */
	if (ap.msg)
		ui_text ((long) TR (ap.msg), SIDE_W + AP_MARGIN, ui.h - 22, tok (TJ_TEXT2), 13, F (),
			 ui.w - (SIDE_W + BTN_W + 3 * AP_MARGIN));
	ap_button (ui.w - (AP_MARGIN + BTN_W), ui.h - (BTN_H + 12), ap.button ? ap.button : "Appliquer", 1);
}

/* x, y = coin, primary = 1 principal : bouton plat */
void ap_button (long x, long y, const char *label, int primary)
{
	const char *t = TR (label);
	uint32_t c = tok (primary ? TJ_ACCENT_DARK : TJ_LINE_STRONG);	/* contour, puis face */
	ui_rbox (x, y, BTN_W, BTN_H, 6, c, c);
	if (primary)
		ui_rbox (x + 1, y + 1, BTN_W - 2, BTN_H - 2, 5, tok (TJ_ACCENT), tok (TJ_ACCENT));
	else
		ui_rbox (x + 1, y + 1, BTN_W - 2, BTN_H - 2, 5, tok (TJ_RAISED), tok (TJ_RAISED_LOW));
	long tw = ui_text_width ((long) t, 14, F ());	/* texte centré */
	long tx = x + (long) ((unsigned long) (BTN_W - tw) >> 1);
	ui_text ((long) t, tx, y + 19, primary ? 0xFFFFFF : tok (TJ_TEXT1), 14, F (), 0);
}

/* ---- lignes de réglages ---- */

/* sub = sous-titre (0 : aucun), note = note sous la carte (0 : aucune) */
void fm_begin (const char *sub, const char *note)
{
	ap.sub = sub;
	fm_note = note;
	fm_n = 0;
	ap.surface = 1;
	ap.widgets = 2;
	ap.draw = fm_draw;
	ap.close = fm_close;
}

long fm_top (void) { return ap.sub ? FM_Y_SUB : AP_HEAD + 4; }

static void fm_change_cb (long w, long code) { fm_change (w, code); }

/* une ligne de plus, sa liste vide */
void fm_line (const char *label)
{
	long w = ui_dropdown (SIDE_W, 0, FM_LW, FM_LH, 0);
	fm_lib[fm_n] = label;
	fm_w[fm_n] = w;
	fm_n++;
	ui_w_on (w, (long) fm_change_cb, 0);
	fm_place ();
}

/* un élément de plus ; current = 1 si c'est le choix actuel */
void fm_item (long line, const char *text, int current)
{
	long i = ui_list_add (fm_w[line], (long) TR (text), 0);
	if (current)
		fm_choose (line, i);
}

void fm_choose (long line, long item) { ui_w_set_value (fm_w[line], item); }
long fm_value (long line) { return ui_w_get_value (fm_w[line]); }

/* (ui_w_on) un changement efface le message */
void fm_change (long, long code)
{
	if (code != UN_CHANGE)
		return;
	ap.msg = 0;
	ap.redraw = 1;
}

/* les listes à leur place, d'après la largeur de la fenêtre */
void fm_place (void)
{
	long x = ui.w - (AP_MARGIN + FM_PAD + FM_LW), y = fm_top () + (FM_H - FM_LH) / 2;
	for (long i = 0; i < fm_n; i++, y += FM_H)
		ui_w_move (fm_w[i], x, y, FM_LW, FM_LH);
}

/* (ap.draw) la carte, ses filets et libellés, la note, les listes */
void fm_draw (void)
{
	fm_place ();
	long y = fm_top (), cw = ui.w - (SIDE_W + 2 * AP_MARGIN), ch = fm_n * FM_H;
	ui_rbox (SIDE_W + AP_MARGIN, y, cw, ch, FM_RADIUS, tok (TJ_LINE), tok (TJ_LINE));
	ui_rbox (SIDE_W + AP_MARGIN + 1, y + 1, cw - 2, ch - 2, FM_RADIUS - 1, tok (TJ_FACE), tok (TJ_FACE));
	for (long i = 0; i < fm_n; i++, y += FM_H)
	{
		if (i)				/* filet */
			ui_fill (SIDE_W + AP_MARGIN + 1, y, cw - 2, 1, tok (TJ_LINE));
		ui_text ((long) TR (fm_lib[i]), SIDE_W + AP_MARGIN + FM_PAD + 4, y + FM_H / 2 + 5, tok (TJ_TEXT1), 14, F (),
			 cw - (FM_LW + 3 * FM_PAD));
	}
	if (fm_note)
		ui_text ((long) TR (fm_note), SIDE_W + AP_MARGIN + 2, y + 22, tok (TJ_TEXT2), 13, F (), cw);
	ui_w_draw (0);
}

/* (ap.close) les listes s'en vont */
void fm_close (void)
{
	for (long i = 0; i < fm_n; i++)
		ui_w_destroy (fm_w[i]);
	fm_n = 0;
}

/* ---- textes ---- */

/* dst = destination, piece = morceau de texte -> fin : ajoute la traduction. Les espaces autour du morceau restent
 * ici (un dictionnaire ne garde pas ceux d'une clé). */
char *tr_cat (char *dst, const char *piece)
{
	dst += strlen (dst);
	while (*piece == ' ')			/* espaces de tête */
		*dst++ = *piece++;
	char key[240];
	long n = 0, keep = 0;			/* longueur sans ceux de queue */
	while (piece[n] && n < 239)
	{
		key[n] = piece[n];
		n++;
		if (piece[n - 1] != ' ')
			keep = n;
	}
	key[keep] = 0;
	strcpy (dst, TR (key));
	dst += strlen (dst);
	strcpy (dst, piece + keep);
	return dst + strlen (dst);
}

/* ---- fichiers « clé = valeur » (src/inc/applist.inc) ---- */

/* s -> s sans espaces (ni tabulations, ni CR) devant ni derrière */
char *ini_trim (char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen (s);
	while (e > s && (unsigned char) e[-1] <= ' ')
		*--e = 0;
	return s;
}

/* « a, b, c » -> premier champ (sans espaces) ; *rest = la suite (0 : aucune) */
char *ini_field (char *s, char **rest)
{
	char *c = strchr (s, ',');
	if (c)
	{
		*c = 0;
		*rest = c + 1;
	}
	else
		*rest = 0;
	return ini_trim (s);
}

int ini_read (const char *path, void (*fn) (char *key, char *val, void *data), void *data)
{
	enum { INI_MAX = 4096 };
	int fd = open (path, O_RDONLY);
	if (fd < 0)
		return 0;
	char buf[INI_MAX];
	long n = read (fd, buf, INI_MAX - 1);	/* (un seul read, comme l'asm : 4 Ko au plus) */
	close (fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	char *p = buf;
	while (*p)
	{
		char *e = p;			/* fin de ligne -> 0 ; « # » : commentaire */
		while (*e && *e != '\n')
		{
			if (*e == '#')
				*e = 0;
			e++;
		}
		char last = *e;
		*e = 0;
		char *eq = p;			/* clé = valeur ? */
		while (*eq && *eq != '=')
			eq++;
		if (*eq == '=')
		{
			*eq = 0;
			char *k = ini_trim (p);
			for (char *c = k; *c; c++)	/* clé en minuscules (ASCII) */
				if (*c >= 'A' && *c <= 'Z')
					*c += 32;
			fn (k, ini_trim (eq + 1), data);
		}
		if (!last)
			break;
		p = e + 1;
	}
	return 1;
}

/* path, key, val (0 : retirer la clé) : les lignes de cette clé sont remplacées par « clé=valeur » (à la fin)
 * -> 0 ou erreur */
long ini_set (const char *path, const char *key, const char *val)
{
	static char in[AP_FILE_MAX], out[AP_FILE_MAX + 128];
	long kl = (long) strlen (key), n = 0;
	int fd = open (path, O_RDONLY);		/* contenu actuel */
	if (fd >= 0)
	{
		n = read (fd, in, AP_FILE_MAX - 1);
		if (n < 0)
			n = 0;
		close (fd);
	}
	in[n] = 0;
	char *o = out, *p = in;			/* sortie : les autres lignes */
	while (*p)
	{
		char *e = p;			/* fin de ligne */
		while (*e && *e != '\n')
			e++;
		if (*e)
			e++;
		char *k = p;			/* la clé ? (espaces, puis clé, puis espace ou « = ») */
		while (*k == ' ')
			k++;
		if (strncmp (k, key, (size_t) kl) == 0 && (k[kl] == '=' || k[kl] == ' '))
		{
			p = e;			/* ligne de la clé : retirée */
			continue;
		}
		while (p < e)			/* ligne gardée */
			*o++ = *p++;
	}
	if (val)				/* « clé=valeur » */
	{
		if (o != out && o[-1] != '\n')	/* (ligne précédente terminée) */
		{
			*o++ = '\r';
			*o++ = '\n';
		}
		o = stpcpy (o, key);
		*o++ = '=';
		o = stpcpy (o, val);
		*o++ = '\r';
		*o++ = '\n';
	}
	fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return fd;
	write (fd, out, (size_t) (o - out));
	close (fd);
	return 0;
}

/* écrit le fichier entier -> 0 ou erreur */
long file_write (const char *path, const char *text)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return fd;
	write (fd, text, strlen (text));
	close (fd);
	return 0;
}

/* ---- process ---- */

long sys_ms (void) { return armpi_syscall (SYS_TIME_MS, 0, 0, 0, 0, 0, 0); }
void sys_sleep (long ms) { armpi_syscall (SYS_SLEEP, ms, 0, 0, 0, 0, 0); }
long svc_open (const char *name, long wait_ms) { return armpi_syscall (SYS_SVC_OPEN, (long) name, wait_ms, 0, 0, 0, 0); }
long svc_call (long ch, void *msg) { return armpi_syscall (SYS_SVC_CALL, ch, (long) msg, 0, 0, 0, 0); }

/* name, after = pid après lequel chercher (0 : depuis le début ; pour le suivant de même nom, le pid rendu)
 * -> pid (0 : aucun) ; par paquets de PF_N entrées (src/inc/procfind.inc) */
long proc_find (const char *name, long after)
{
	enum { PF_N = 16 };
	long list[PF_N * 8];			/* 64 octets par process : pid, …, nom (+24, 32 octets) */
	long from = after & 0xFFFF;		/* emplacement de départ */
	for (;;)
	{
		long n = armpi_syscall (SYS_PROCLIST_FROM, (long) list, PF_N, from, 0, 0, 0);
		if (n <= 0)
			return 0;
		for (long i = 0; i < n; i++)
		{
			const long *e = list + i * 8;
			from = e[0] & 0xFFFF;	/* (la suite : après cet emplacement) */
			if (strncmp (name, (const char *) (e + 3), 32) == 0)
				return e[0];
		}
		if (n < PF_N)			/* paquet plein : il y a peut-être une suite */
			return 0;
	}
}

/* SYS_SPAWN d'AppKit (chemin, argument) -> pid ou erreur */
long spawn (const char *path, const char *arg)
{
	/* (un argument vide n'est pas passé : libposix le garderait en « "" », et webd ou smbd, lancés par
	   « spawn (WEBD, "") » depuis leur volet, le prenaient pour une commande inconnue et s'arrêtaient) */
	char *argv[3] = { (char *) path, arg && *arg ? (char *) arg : 0, 0 };
	pid_t pid;
	int e = posix_spawn (&pid, path, 0, 0, argv, environ);
	return e ? -e : pid;
}

/* name = nom du process (« panel »), path = chemin : arrête le programme de la session (s'il tourne) et le
 * relance */
void relaunch (const char *name, const char *path)
{
	long pid = 0;
	while ((pid = proc_find (name, pid)) != 0)	/* les process de ce nom */
		armpi_syscall (SYS_KILL, pid, 0, 0, 0, 0, 0);
	sys_sleep (200);			/* (le temps que le serveur le voie partir) */
	spawn (path, 0);
}
