/*
 * applets/dockconf.cpp -- les réglages du dock (SD0:/etc/dock.ini), port de src/inc/dockconf.inc (Korin), la
 * DockLayout du dockconf.h d'Onyx. Une ligne par réglage :
 *
 *	drawer    = Accessoires, editor	un tiroir : sa catégorie, son application principale (vide : la
 *					première par nom), dans l'ordre du dock
 *	hidden    = Jeux		une catégorie sans tiroir
 *	launcher  = terminal		un lanceur après les tiroirs (6 au plus)
 *	desk      = Principal		un espace de travail, son nom (1 à 6)
 *	corbeille = oui | non		(Korin)
 *	taille    = auto | grande | moyenne | petite (Korin)
 *
 * Les clés tiroir et lanceur sont lues comme drawer et launcher. Le dock a un tiroir pour CHAQUE catégorie des
 * applications de la carte, sauf les cachées et Shell, Settings, Emulators (et leurs noms français) : celles que le
 * fichier ne nomme pas viennent à la fin, par nom. Repris de src/sys/common/dockconf.c (la même chose pour
 * korinrt), sur le catalogue d'applist.cpp (apps[], cats[]).
 */
#include <string.h>
#include "dock.h"

long dl_ncats, dl_nlaunchers, dl_ndesks;
long dl_trash = 1;
long dl_size = -1;
unsigned char dl_cat[CAT_MAX];
unsigned char dl_hidden[CAT_MAX];
unsigned char dl_napps[CAT_MAX];
unsigned char ap_order[APP_MAX];
char dl_app[CAT_MAX][APP_NAME];
char dl_launcher[DOCK_MAXLAUNCHERS][APP_NAME];
char dl_desk[DOCK_MAXDESKS][DL_DESK];

/* catégories sans tiroir (suite de chaînes, une vide à la fin) */
static const char no_docked[] = "Shell\0Settings\0Emulators\0R\xC3\xA9glages\0Param\xC3\xA8tres\0\xC3\x89mulateurs\0";
static const char *const sizes[3] = { "grande", "moyenne", "petite" };

long dk_ci_cmp (const char *a, const char *b)
{
	for (;;)
	{
		unsigned long c = (unsigned char) *a++, d = (unsigned char) *b++;
		if (c - 'A' <= 25)
			c += 32;
		if (d - 'A' <= 25)
			d += 32;
		if (c != d || !c)
			return (long) (c - d);
	}
}

void dk_copy_n (char *d, const char *s, long n)
{
	while (--n > 0 && *s)
		*d++ = *s++;
	*d = 0;
}

/* -> 1 si la catégorie peut avoir un tiroir */
long dock_category_docked (const char *cat)
{
	if (!*cat)
		return 0;
	for (const char *n = no_docked; *n; n += strlen (n) + 1)
		if (!dk_ci_cmp (cat, n))
			return 0;
	return 1;
}

/* catégorie (catalogue) -> sa ligne (-1 : aucune) */
static long dl_find (long cat)
{
	for (long i = 0; i < dl_ncats; i++)
		if (dl_cat[i] == cat)
			return i;
	return -1;
}

/* catégorie -> sa ligne (nouvelle si elle n'en a pas ; -1 : plus de place) */
static long dl_add (long cat)
{
	long r = dl_find (cat);
	if (r >= 0)
		return r;
	r = dl_ncats;
	if (r >= CAT_MAX)
		return -1;
	dl_ncats = r + 1;
	dl_cat[r] = (unsigned char) cat;
	dl_hidden[r] = 0;
	dl_napps[r] = 0;
	dl_app[r][0] = 0;
	return r;
}

/* un lanceur de plus (6 au plus) */
void dl_launcher_add (const char *name)
{
	if (!*name || dl_nlaunchers >= DOCK_MAXLAUNCHERS)
		return;
	dk_copy_n (dl_launcher[dl_nlaunchers++], name, APP_NAME);
}

void dl_line (char *key, char *val, void *)
{
	if (!strcmp (key, "drawer") || !strcmp (key, "tiroir"))
	{
		char *rest;
		char *cat = ini_field (val, &rest);	/* « catégorie, application » */
		if (!dock_category_docked (cat))
			return;
		long c = cat_find (cat);
		if (c < 0)
			return;
		long r = dl_add (c);
		if (r < 0 || !rest)
			return;
		char *app = ini_field (rest, &rest);
		if (!dl_app[r][0])		/* (la première ligne garde sa place) */
			dk_copy_n (dl_app[r], app, APP_NAME);
		return;
	}
	if (!strcmp (key, "launcher") || !strcmp (key, "lanceur"))
	{
		dl_launcher_add (val);
		return;
	}
	if (!strcmp (key, "hidden"))
	{
		if (!*val)
			return;
		long c = cat_find (val);
		if (c < 0)
			return;
		long r = dl_add (c);
		if (r >= 0)
			dl_hidden[r] = 1;
		return;
	}
	if (!strcmp (key, "desk"))
	{
		if (dl_ndesks >= DOCK_MAXDESKS)
			return;
		dk_copy_n (dl_desk[dl_ndesks++], val, DL_DESK);
		return;
	}
	if (!strcmp (key, "corbeille"))
	{
		dl_trash = strcmp (val, "non") ? 1 : 0;
		return;
	}
	if (!strcmp (key, "taille"))
	{
		/* grande, moyenne, petite ; autre chose (« auto ») : rien ne change, comme l'asm */
		for (long i = 0; i < 3; i++)
			if (!strcmp (val, sizes[i]))
			{
				dl_size = i;
				return;
			}
	}
}

void dl_desks_default (void)
{
	if (dl_ndesks)
		return;
	dl_ndesks = 4;
	for (long i = 0; i < 4; i++)
	{
		dl_desk[i][0] = (char) ('1' + i);
		dl_desk[i][1] = 0;
	}
}

long dock_layout_load (void)
{
	dl_ncats = 0;
	dl_nlaunchers = 0;
	dl_ndesks = 0;
	dl_trash = 1;
	dl_size = -1;
	long found = ini_read (DL_PATH, dl_line);
	if (!found)				/* sans fichier : terminal, fichiers */
	{
		dl_launcher_add (DL_DEFAULT_TERMINAL);
		dl_launcher_add (DL_DEFAULT_FILES);
	}
	dl_desks_default ();
	long first = dl_ncats;			/* les catégories des applications : comptées, les nouvelles ajoutées */
	for (long i = 0; i < apps_n; i++)
	{
		long c = apps[i].cat;
		/* (0xFF, sans catégorie : l'asm lit ca_nom hors du tableau ; ici, pas de tiroir) */
		if (c >= CAT_MAX || !dock_category_docked (cats[c]))
			continue;
		long r = dl_add (c);
		if (r >= 0)
			dl_napps[r]++;
	}
	for (long i = first + 1; i < dl_ncats; i++)	/* (les nouvelles par nom) */
		for (long j = i; j > first; j--)
		{
			if (dk_ci_cmp (dl_cat_name (j - 1), dl_cat_name (j)) <= 0)
				break;
			dl_swap (j - 1, j);
		}
	for (long r = 0; r < dl_ncats; r++)	/* une application principale absente : aucune */
		if (dl_app[r][0] && app_find (dl_app[r]) < 0)
			dl_app[r][0] = 0;
	return found;
}

/* ligne -> le nom de sa catégorie */
const char *dl_cat_name (long row) { return cats[dl_cat[row]]; }

/* deux lignes : les échange */
void dl_swap (long a, long b)
{
	unsigned char t = dl_cat[a];
	dl_cat[a] = dl_cat[b];
	dl_cat[b] = t;
	t = dl_hidden[a];
	dl_hidden[a] = dl_hidden[b];
	dl_hidden[b] = t;
	t = dl_napps[a];
	dl_napps[a] = dl_napps[b];
	dl_napps[b] = t;
	char tmp[APP_NAME];
	memcpy (tmp, dl_app[a], APP_NAME);
	memcpy (dl_app[a], dl_app[b], APP_NAME);
	memcpy (dl_app[b], tmp, APP_NAME);
}

const char *app_label (long app) { return apps[app].label; }

/* ap_order <- les applications par libellé (tri par insertion, comme l'asm) */
void apps_sort (void)
{
	for (long i = 0; i < apps_n; i++)
	{
		ap_order[i] = (unsigned char) i;
		for (long j = i; j > 0; j--)
		{
			if (dk_ci_cmp (app_label (ap_order[j - 1]), app_label (ap_order[j])) <= 0)
				break;
			unsigned char t = ap_order[j - 1];
			ap_order[j - 1] = ap_order[j];
			ap_order[j] = t;
		}
	}
}

/* ligne -> l'application principale de sa catégorie : celle choisie, sinon la première par nom (-1 : aucune) ;
 * apps_sort fait */
long dl_main (long row)
{
	if (dl_app[row][0])
	{
		long a = app_find (dl_app[row]);
		if (a >= 0)
			return a;
	}
	for (long i = 0; i < apps_n; i++)
	{
		long a = ap_order[i];
		if (apps[a].cat == dl_cat[row])
			return a;
	}
	return -1;
}
