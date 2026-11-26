/*
 * applets/preload.cpp -- le volet Préchargement du panneau de configuration : à gauche les programmes de la carte
 * (applications, /bin), à droite ceux de /etc/preload.txt avec leur état (SYS_IMAGE_LIST, relu chaque
 * seconde : image épinglée ou pas encore). La flèche droite ajoute le programme choisi (à la liste et tout de suite :
 * commande preload, qui notifie à la fin), la flèche gauche le retire (commande unload). Les commentaires du fichier
 * sont gardés ; « Actualiser » relit tout.
 *
 * Port en C++ de applets/preload.inc et de preload_name (src/inc/preload_notify.inc), Korin. Le preloadconf d'Onyx
 * (user/Apps/preloadconf) avait la même idée (la liste, sa taille, en mémoire ou pas ; ce qu'on peut ajouter : les
 * apps, puis /bin ; effet immédiat par image_preload, image_unload) sur des widgets ; c'est l'asm de Korin, deux
 * listes dessinées et deux flèches, qui fait foi pour ce qui se voit, et les commandes preload et unload de Korin.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <armpi/syscall.h>
#include "../control.h"

enum
{
	SYS_IMAGE_LIST = 0x65,			/* (tampon, max) -> n : entrées IML_* de 128 octets */
	IML_FSIZE = 0,				/* taille du fichier */
	IML_KIND = 40,				/* bit 0 : IML_PIN (épinglée) */
	IML_PATH = 48,				/* chemin résolu (80 octets) */
	IML_SIZE = 128,
	IML_PIN = 1,
	PL_LMAX = 192,				/* programmes à gauche */
	PL_RMAX = 32,				/* préchargeables à droite */
	PL_PATH = 80,
	PL_W = 186,				/* une liste */
	PL_X1 = SIDE_W + AP_MARGIN,
	PL_BX = PL_X1 + PL_W + 8,		/* les flèches */
	PL_BW = 32,
	PL_BH = 30,
	PL_X2 = PL_BX + PL_BW + 8,
	PL_Y = AP_HEAD + 26,
	PL_RH = 22,
	PL_BAR = 12,				/* bande de défilement (tactile) */
	PL_IMG = 64,				/* images lues */
};

static const char pl_s_left[] = "Programmes";
static const char pl_s_right[] = "Pr\xC3\xA9" "charg\xC3\xA9s";
static const char pl_s_yes[] = "pr\xC3\xA9" "charg\xC3\xA9";
static const char pl_s_no[] = "pas encore";
static const char pl_s_add[] = "\xE2\x86\x92";		/* U+2192 */
static const char pl_s_remove[] = "\xE2\x86\x90";	/* U+2190 */
#define PL_FILE		"/etc/preload.txt"

static long pl_n[2];				/* lignes à gauche, à droite */
static long pl_sel[2];				/* choisie (-1 : aucune) */
static long pl_off[2];				/* première montrée */
static long pl_focus;				/* la liste que la molette fait défiler */
static unsigned long pl_stat[PL_RMAX];		/* 0 : pas encore, sinon la taille (| 1) */
static char pl_left[PL_LMAX][PL_PATH];
static char pl_right[PL_RMAX][PL_PATH];
static unsigned char pl_img[PL_IMG * IML_SIZE];

/* ---- src/inc/preload_notify.inc ---- */

/* path = chemin (« /apps/jet.app/main », « /bin/fasmarm »), out (64 octets) : le nom (« jet », « fasmarm ») */
static void preload_name (const char *path, char *out)
{
	const char *end = path + strlen (path), *b = end;	/* le dernier composant */
	while (b > path && b[-1] != '/')
		b--;
	if (end - b == 4 && !memcmp (b, "main", 4) && b > path)	/* « main » : le paquet */
	{
		end = b - 1;
		b = end;
		while (b > path && b[-1] != '/')
			b--;
		if (end - b > 4 && !memcmp (end - 4, ".app", 4))
			end -= 4;
	}
	long n = 0;
	while (b < end && n < 63)
		out[n++] = *b++;
	out[n] = 0;
}

/* ---- /etc/preload.txt ---- */

/* le fichier dans ap_file (terminé par 0) -> octets */
static long pl_file_read (void)
{
	long n = 0;
	int fd = open (PL_FILE, O_RDONLY);
	if (fd >= 0)
	{
		n = read (fd, ap_file, AP_FILE_MAX - 1);
		if (n < 0)
			n = 0;
		close (fd);
	}
	ap_file[n] = 0;
	return n;
}

/* ap_out jusqu'à end : le fichier réécrit */
static void pl_file_write (const char *end)
{
	unsigned me;
	if (ap_elevate (TR ("Réglages veut modifier le préchargement des programmes."), &me))	/* (/etc : uid 0) */
		return;
	int fd = open (PL_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		write (fd, ap_out, end - ap_out);
		close (fd);
	}
	ap_unelevate (me);
}

/* [*b, *e) sans espaces, tabulations ni CR autour (ni LF à la fin) */
static void pl_trim (const char **b, const char **e)
{
	while (*b < *e && (**b == ' ' || **b == '\t' || **b == '\r'))
		(*b)++;
	while (*e > *b && ((*e)[-1] == ' ' || (*e)[-1] == '\t' || (*e)[-1] == '\n' || (*e)[-1] == '\r'))
		(*e)--;
}

/* deux chemins -> 1 si égaux aux majuscules près (ASCII) */
static int pl_same (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = (unsigned char) *a, y = (unsigned char) *b;
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

/* path à gauche, s'il n'est pas déjà à droite */
static void pl_left_add (const char *path)
{
	for (long i = 0; i < pl_n[1]; i++)
		if (!strcmp (pl_right[i], path))
			return;
	if (pl_n[0] >= PL_LMAX)
		return;
	strncpy (pl_left[pl_n[0]], path, PL_PATH - 1);
	pl_left[pl_n[0]++][PL_PATH - 1] = 0;
}

/* (ap.tick) l'état de chaque programme de droite -> 1 s'il a changé */
static int pl_status (void)
{
	long n = armpi_syscall (SYS_IMAGE_LIST, (long) pl_img, PL_IMG, 0, 0, 0, 0);
	if (n < 0)
		n = 0;
	int changed = 0;
	for (long r = 0; r < pl_n[1]; r++)
	{
		unsigned long st = 0;
		for (long i = 0; i < n; i++)
		{
			const unsigned char *e = pl_img + i * IML_SIZE;
			if ((*(const unsigned long *) (e + IML_KIND) & IML_PIN)
			    && pl_same ((const char *) e + IML_PATH, pl_right[r]))
			{
				st = *(const unsigned long *) (e + IML_FSIZE) | 1;	/* (non nul : chargée) */
				break;
			}
		}
		if (pl_stat[r] != st)
			changed = 1;
		pl_stat[r] = st;
	}
	return changed;
}

/* les deux listes, relues ; rien de choisi */
static void pl_load (void)
{
	pl_sel[0] = pl_sel[1] = -1;
	pl_off[0] = pl_off[1] = 0;
	pl_n[0] = pl_n[1] = 0;
	/* à droite : les lignes du fichier (ni vides ni « # ») */
	pl_file_read ();
	const char *p = ap_file;
	while (*p)
	{
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *b = p, *f = e;
		pl_trim (&b, &f);
		p = *e ? e + 1 : e;
		if (b >= f || *b == '#' || pl_n[1] >= PL_RMAX)
			continue;
		long l = f - b;
		if (l > PL_PATH - 1)
			l = PL_PATH - 1;
		memcpy (pl_right[pl_n[1]], b, l);
		pl_right[pl_n[1]++][l] = 0;
	}
	/* à gauche : les applications, puis /bin */
	char path[PL_PATH + 208];			/* (/bin/ + un nom de 255 octets au plus) */
	applis_load ();
	for (long i = 0; i < apps_n; i++)
	{
		strcpy (path, "/apps/");
		strcat (path, apps[i].name);
		strcat (path, ".app/main");
		pl_left_add (path);
	}
#if defined (__x86_64__)	/* (le PC : seulement les apps de /apps, décision du 25/11/2026 ; le noyau refuse le reste) */
	DIR *d = 0;
#else
	DIR *d = opendir ("/bin");
#endif
	if (d)
	{
		struct dirent *de;
		while ((de = readdir (d)) != 0)
		{
			if (de->d_name[0] == '.')
				continue;
			snprintf (path, sizeof path, "/bin/%s", de->d_name);
			pl_left_add (path);
		}
		closedir (d);
	}
	pl_status ();
}

/* -> lignes que chaque liste montre */
static long pl_rows (void) { return (long) ((unsigned long) (ui.h - (long) PL_Y - AP_BOTTOM - 6) / PL_RH); }

/* i = 0 le bouton du haut, 1 celui du bas -> son y */
static long pl_btn_y (long i) { return i * (PL_BH + 8) + PL_Y + 70; }

/* i = rang : un bouton (accentué si un programme est choisi du bon côté) */
static void pl_btn (long i, const char *arrow)
{
	long y = pl_btn_y (i);
	uint32_t c = tok (pl_sel[i] < 0 ? TJ_LINE_STRONG : TJ_ACCENT);
	ui_rbox (PL_BX, y, PL_BW, PL_BH, 6, c, c);
	ui_text ((long) arrow, PL_BX + 8, y + 21, 0x00FFFFFF, 15, FB (), 0);	/* (blanc en dur, comme l'asm) */
}

static void pl_draw (void)
{
	ui_text ((long) TR (pl_s_left), PL_X1, AP_HEAD + 18, tok (TJ_TEXT2), 13, FB (), PL_W);
	ui_text ((long) TR (pl_s_right), PL_X2, AP_HEAD + 18, tok (TJ_TEXT2), 13, FB (), PL_W);
	long rows = pl_rows ();
	char name[64];
	for (long l = 0; l < 2; l++)
	{
		long x = l ? PL_X2 : PL_X1;
		ui_rbox (x, PL_Y, PL_W, PL_RH * rows + 2, 6, tok (TJ_LINE), tok (TJ_LINE));	/* cadre, fond */
		ui_rbox (x + 1, PL_Y + 1, PL_W - 2, PL_RH * rows, 5, tok (TJ_SURFACE), tok (TJ_SURFACE));
		for (long k = 0, i = pl_off[l]; k < rows && i < pl_n[l]; k++, i++)
		{
			long y = k * PL_RH + PL_Y + 1;
			if (pl_sel[l] == i)	/* choisie */
				ui_rbox (x + 3, y, PL_W - 6 - PL_BAR, PL_RH, 4, tok (TJ_ACCENT_LIGHT), tok (TJ_ACCENT_LIGHT));
			preload_name (l ? pl_right[i] : pl_left[i], name);
			long tx = 8;
			if (l)			/* à droite : le voyant d'abord (vert : préchargée, ambre : pas encore ;
						 * en dur, comme l'asm) */
			{
				tx = 20;
				uint32_t c = pl_stat[i] ? 0x002EA043 : 0x00C8A020;
				ui_rbox (x + 7, y + 7, 8, 8, 4, c, c);
			}
			ui_text ((long) name, x + tx, y + 16, tok (TJ_TEXT1), 13, F (), l ? 76 : PL_W - 34);
			if (l)			/* l'état en toutes lettres */
				ui_text ((long) TR (pl_stat[i] ? pl_s_yes : pl_s_no), x + 98, y + 16, tok (TJ_TEXT3), 11, F (),
					PL_W - 98 - PL_BAR);
		}
		long n = pl_n[l];		/* trop de lignes : la barre */
		if (n > rows)
		{
			long h = PL_RH * rows;
			ui_rbox (x + PL_W - 9, PL_Y + 3, 6, h - 4, 3, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
			long ch = (long) ((unsigned long) (h * rows) / (unsigned long) n) - 4;	/* curseur */
			long cy = (long) ((unsigned long) (h * pl_off[l]) / (unsigned long) n) + PL_Y + 3;
			ui_rbox (x + PL_W - 9, cy, 6, ch, 3, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
		}
	}
	pl_btn (0, pl_s_add);			/* la flèche vers la droite */
	pl_btn (1, pl_s_remove);		/* la flèche vers la gauche */
}

/* n = crans : la dernière liste touchée défile -> 1 */
static int pl_wheel (long n)
{
	long m = pl_n[pl_focus] - pl_rows ();	/* dernière première possible */
	if (m < 0)
		m = 0;
	long o = pl_off[pl_focus] - n;
	if (o > m)
		o = m;
	if (o < 0)
		o = 0;
	pl_off[pl_focus] = o;
	return 1;
}

/* flèche droite : le programme choisi à gauche ajouté au fichier et préchargé */
static void pl_add (void)
{
	if (pl_sel[0] < 0 || pl_n[1] >= PL_RMAX)
		return;
	char path[PL_PATH];
	strcpy (path, pl_left[pl_sel[0]]);
	long n = pl_file_read ();
	memcpy (ap_out, ap_file, n);
	char *o = ap_out + n;
	if (o != ap_out && o[-1] != '\n')	/* ligne précédente terminée */
	{
		*o++ = '\r';
		*o++ = '\n';
	}
	long l = strlen (path);
	memcpy (o, path, l);
	o += l;
	*o++ = '\r';
	*o++ = '\n';
	pl_file_write (o);
#if !defined (__x86_64__)	/* (le PC : au prochain démarrage, par init2 ; seul le démarrage précharge, 25/11/2026) */
	spawn ("/bin/preload", path);
#endif
	pl_load ();
}

/* flèche gauche : le programme choisi à droite retiré du fichier et de la mémoire */
static void pl_remove (void)
{
	if (pl_sel[1] < 0)
		return;
	char path[PL_PATH];
	strcpy (path, pl_right[pl_sel[1]]);
	pl_file_read ();
	const char *p = ap_file;
	char *o = ap_out;
	while (*p)
	{
		const char *e = p;		/* fin de ligne (LF compris) */
		while (*e && *e++ != '\n')
			;
		const char *b = p, *f = e;	/* cette ligne ? */
		pl_trim (&b, &f);
		long l = f - b;
		if (l && (long) strlen (path) == l && !memcmp (b, path, l))
		{
			p = e;			/* retirée */
			continue;
		}
		while (p < e)
			*o++ = *p++;
	}
	pl_file_write (o);
	spawn ("/bin/unload", path);
	pl_load ();
}

/* clic -> 1 : changé */
static int pl_click (long x, long y)
{
	if ((unsigned long) (x - PL_BX) < PL_BW)	/* les boutons */
	{
		for (long i = 0; i < 2; i++)
		{
			long by = pl_btn_y (i);
			if (y >= by && y < by + PL_BH)
			{
				if (i)
					pl_remove ();
				else
					pl_add ();
				return 1;
			}
		}
		return 0;
	}
	long l = 0;				/* quelle liste */
	unsigned long dx = (unsigned long) (x - PL_X1);
	if (dx >= PL_W)
	{
		l = 1;
		dx = (unsigned long) (x - PL_X2);
		if (dx >= PL_W)
			return 0;
	}
	pl_focus = l;
	long ry = y - (PL_Y + 1);
	if (ry < 0)
		return 0;
	unsigned long h = (unsigned long) (pl_rows () * PL_RH);
	if ((unsigned long) ry >= h)
		return 0;
	if (dx - (PL_W - PL_BAR) < PL_BAR)	/* la bande : 3 lignes */
		return pl_wheel ((unsigned long) ry >= (h >> 1) ? -3 : 3);
	long i = ry / PL_RH + pl_off[l];
	if (i >= pl_n[l])
		return 0;
	pl_sel[l] = i;
	pl_sel[l ^ 1] = -1;			/* l'autre liste : plus de choix */
	return 1;
}

static void pl_open (void)
{
	ap.draw = pl_draw;
	ap.click = pl_click;
	ap.wheel = pl_wheel;
	ap.tick = pl_status;
	ap.button = "Actualiser";
	pl_load ();
}

/* (le bouton du bas) Actualiser */
static void pl_apply (void) { pl_load (); }

const Applet preload_applet = { "preload", pl_s_right, "Pr\xC3\xA9" "chargement", "preload", pl_open, pl_apply };
