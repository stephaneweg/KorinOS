/*
 * armpi/appdata.h -- les données d'une app pour l'utilisateur en cours (décision du 25/11/2026) : une app n'écrit
 * plus dans son dossier /apps/<app>.app (écrit par le système seulement), mais dans HOME:/.settings/<app>/.
 *
 *   char p[200];
 *   FILE *f = fopen (korin_app_data ("notes", "config.ini", p, sizeof p, 0), "r");	// lecture
 *   FILE *g = fopen (korin_app_data ("notes", "config.ini", p, sizeof p, 1), "w");	// écriture
 *
 * Lecture : le fichier personnel s'il existe, sinon l'ancienne place (/apps/<app>.app/<fichier>, d'avant le
 * 25/11 : les données déjà là restent lues). Écriture : le personnel, ses dossiers créés ; la première fois, le
 * fichier de l'ancienne place y est d'abord recopié (une écriture en ajout ne perd rien). <fichier> peut contenir
 * des sous-dossiers (« ins/piano.fmi ») ; korin_app_dir donne le dossier lui-même, créé.
 */
#ifndef _ARMPI_APPDATA_H
#define _ARMPI_APPDATA_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef KORIN_APPDATA_ROOT
#define KORIN_APPDATA_ROOT "HOME:/.settings"
#endif
#ifndef KORIN_APPDATA_OLD
#define KORIN_APPDATA_OLD "/apps"	/* (l'ancienne place : /apps/<app>.app/<fichier>) */
#endif

/* les dossiers de path jusqu'au dernier « / » (exclu), créés */
static inline void korin_appdata_mkdirs (const char *path)
{
	char d[256];
	size_t n = strlen (path);
	if (n >= sizeof d)
		return;
	memcpy (d, path, n + 1);
	for (size_t i = 7; i < n; i++)		/* (après « HOME:/ ») */
		if (d[i] == '/')
		{
			d[i] = 0;
			mkdir (d, 0755);		/* (EEXIST : déjà là) */
			d[i] = '/';
		}
}

/* app, sous-dossier (« » : le dossier de l'app) -> HOME:/.settings/<app>/<sous> dans buf, créé ; rend buf */
static inline char *korin_app_dir (const char *app, const char *sub, char *buf, size_t cap)
{
	snprintf (buf, cap, "%s/%s/%s%s", KORIN_APPDATA_ROOT, app, sub, *sub ? "/" : "");
	korin_appdata_mkdirs (buf);
	size_t n = strlen (buf);
	if (n && buf[n - 1] == '/')
		buf[n - 1] = 0;
	return buf;
}

static inline char *korin_app_data (const char *app, const char *file, char *buf, size_t cap, int write)
{
	struct stat st;
	char old[256];
	snprintf (buf, cap, "%s/%s/%s", KORIN_APPDATA_ROOT, app, file);
	snprintf (old, sizeof old, "%s/%s.app/%s", KORIN_APPDATA_OLD, app, file);
	if (!write)
	{
		if (stat (buf, &st) != 0 && stat (old, &st) == 0)
			snprintf (buf, cap, "%s", old);
		return buf;
	}
	korin_appdata_mkdirs (buf);
	if (stat (buf, &st) != 0 && stat (old, &st) == 0 && S_ISREG (st.st_mode))
	{
		FILE *in = fopen (old, "rb"), *out = in ? fopen (buf, "wb") : 0;
		char t[4096];
		size_t n;
		while (out && (n = fread (t, 1, sizeof t, in)) > 0)
			fwrite (t, 1, n, out);
		if (out)
			fclose (out);
		if (in)
			fclose (in);
	}
	return buf;
}

#endif
