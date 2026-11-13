/*
 * applist.cpp -- le catalogue des applications (src/inc/applist.inc de Korin) : les paquets SD0:/apps/<nom>.app
 * (main : l'exécutable ; app.txt : le manifeste, clés d'Onyx « name » et « category »). Pour les volets Panel,
 * Dock et Préchargement. (src/sys/common/applist.c est la même chose pour korinrt, pas pour libposix.)
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include "control.h"

long apps_n, cats_n;
AppInfo apps[APP_MAX];
char cats[CAT_MAX][CAT_NAME];

static void copy_n (char *d, const char *s, size_t n)
{
	strncpy (d, s, n - 1);
	d[n - 1] = 0;
}

long app_find (const char *name)
{
	for (long i = 0; i < apps_n; i++)
		if (strcmp (name, apps[i].name) == 0)
			return i;
	return -1;
}

/* -> catégorie (ajoutée si nouvelle ; -1 : plus de place) */
long cat_find (const char *name)
{
	long i;
	for (i = 0; i < cats_n; i++)
		if (strcmp (name, cats[i]) == 0)
			return i;
	if (i >= CAT_MAX)
		return -1;
	copy_n (cats[i], name, CAT_NAME);
	cats_n = i + 1;
	return i;
}

/* (ini_read d'un manifeste) data = l'application */
static void manifest_line (char *key, char *val, void *data)
{
	AppInfo &a = *(AppInfo *) data;
	if (strcmp (key, "name") == 0)		/* le libellé traduit (lang_tr) ; la catégorie reste la clé */
		copy_n (a.label, TR (val), APP_LABEL);
	else if (strcmp (key, "category") == 0)
	{
		long c = cat_find (val);
		if (c >= 0)
			a.cat = (unsigned char) c;
	}
}

/* le catalogue ; sans manifeste (ou sans catégorie) : « Autres », libellé = nom */
void applis_load (void)
{
	apps_n = 0;
	cats_n = 0;
	DIR *d = opendir ("SD0:/apps");
	if (!d)
		return;
	struct dirent *e;
	while (apps_n < APP_MAX && (e = readdir (d)) != 0)
	{
		size_t l = strlen (e->d_name);
		if (l < 4 || strcmp (e->d_name + l - 4, ".app") != 0)
			continue;
		char name[APP_NAME];
		l -= 4;
		if (l > APP_NAME - 1)
			l = APP_NAME - 1;
		memcpy (name, e->d_name, l);	/* nom sans .app */
		name[l] = 0;
		AppInfo &a = apps[apps_n];
		copy_n (a.name, name, APP_NAME);
		a.cat = 0xFF;			/* (pas encore de catégorie) */
		copy_n (a.label, name, APP_LABEL);	/* libellé : le nom, sauf manifeste */
		char path[96];
		snprintf (path, sizeof path, "SD0:/apps/%s.app/app.txt", name);
		ini_read (path, manifest_line, &a);
		apps_n++;
	}
	closedir (d);
	for (long i = 0; i < apps_n; i++)	/* sans catégorie : « Autres » */
		if (apps[i].cat == 0xFF)
			apps[i].cat = (unsigned char) cat_find ("Autres");	/* (-1 : 0xFF, comme l'asm) */
}

/* app : la lance (SD0:/apps/<nom>.app/main) -> pid ou erreur */
long app_launch (long app)
{
	char path[64];
	snprintf (path, sizeof path, "SD0:/apps/%s.app/main", apps[app].name);
	return spawn (path, 0);
}
