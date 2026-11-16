/*
 * armpi/userconf.h -- les réglages personnels de l'utilisateur en cours (docs/etude/MULTI-UTILISATEUR.md,
 * §2.5, docs/DESIGN-CONNEXION.md §2.6) : un fichier de réglages (« theme.txt », « places.ini ») se lit d'abord
 * dans HOME:/etc/, puis dans /etc/ (la valeur de la machine) ; il s'écrit toujours dans HOME:/etc/ (créé au
 * besoin). HOME:/.config/korin/ (la place du 15/11) est encore lue entre les deux.
 *
 *   char p[128];
 *   FILE *f = fopen (korin_conf_path ("places.ini", p, sizeof p, 0), "r");	// lecture
 *   int fd = open (korin_conf_path ("places.ini", p, sizeof p, 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
 *
 * HOME:/ est l'alias de vfs du dossier personnel (/home tant qu'il n'y a pas de comptes, /home/<nom>
 * ensuite) : un programme n'a jamais besoin de connaître le nom de l'utilisateur.
 */
#ifndef _ARMPI_USERCONF_H
#define _ARMPI_USERCONF_H

#include <stdio.h>
#include <sys/stat.h>

#define KORIN_CONF_DIR "HOME:/etc"
#define KORIN_CONF_OLD "HOME:/.config/korin"	/* (15/11 : lue en repli, plus écrite) */

/* name -> le chemin à lire (write = 0 : le personnel s'il existe, sinon celui de /etc) ou à écrire
 * (write = 1 : le personnel, ses dossiers créés) dans buf ; rend buf */
static inline char *korin_conf_path (const char *name, char *buf, size_t cap, int write)
{
	struct stat st;
	snprintf (buf, cap, "%s/%s", KORIN_CONF_DIR, name);
	if (write)
	{
		mkdir (KORIN_CONF_DIR, 0755);			/* (EEXIST : déjà là) */
		if (stat (buf, &st) != 0)			/* la première fois : la copie de l'ancienne place */
		{						/* ou de /etc (les clés qu'on ne change pas restent */
			char e[160];				/* celles de la machine) */
			snprintf (e, sizeof e, "%s/%s", KORIN_CONF_OLD, name);
			if (stat (e, &st) != 0)
				snprintf (e, sizeof e, "/etc/%s", name);
			FILE *in = fopen (e, "rb"), *out = in ? fopen (buf, "wb") : 0;
			char t[1024];
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
	if (stat (buf, &st) != 0)
	{
		snprintf (buf, cap, "%s/%s", KORIN_CONF_OLD, name);
		if (stat (buf, &st) != 0)
			snprintf (buf, cap, "/etc/%s", name);
	}
	return buf;
}

#endif
