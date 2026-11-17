/*
 * home.c -- le dossier de l'agent pour le compte en cours (acp.h, acp_home).
 *
 * Vibe garde ses comptes, ses séances et ses journaux dans $VIBE_HOME (défaut ~/.vibe), claude-acp dans
 * $CLAUDE_ACP_HOME (défaut ~/.claude-acp) : un dossier par compte, /home/<nom>/.vibe (docs/etude/MULTI-UTILISATEUR.md).
 * Les paquets vibe, claude et forge posent dans /.vibe et /.claude-acp ce que l'agent sait de Korin : AGENTS.md et
 * skills/. Quand le dossier du compte n'est pas celui-là :
 *   - chaque compétence de skills/ y est recopiée quand elle manque ou que celle du paquet est plus récente
 *     (SKILL.md fait foi) ; AGENTS.md seulement s'il manque (il est à l'utilisateur une fois posé) ;
 *   - ce qu'un compte avait laissé dans /.vibe avant les comptes (.env et la clé, config.toml, séances, journaux,
 *     korin-app.txt...) y est déplacé au premier lancement, comme Jet le fait pour /home/jet.
 */
#include "acp.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef ACP_SYS_ROOT
#define ACP_SYS_ROOT ""		/* (un essai hors de Korin : -DACP_SYS_ROOT=\"/tmp/racine\") */
#endif

static int is_dir (const char *p)
{
	struct stat st;
	return stat (p, &st) == 0 && S_ISDIR (st.st_mode);
}

static long long mtime_of (const char *p)
{
	struct stat st;
	return stat (p, &st) == 0 ? (long long) st.st_mtime : -1;
}

static int copy_file (const char *from, const char *to)
{
	FILE *i = fopen (from, "rb");
	if (!i)
		return -1;
	FILE *o = fopen (to, "wb");
	if (!o)
	{
		fclose (i);
		return -1;
	}
	char buf[8192];
	size_t n;
	int r = 0;
	while ((n = fread (buf, 1, sizeof buf, i)) > 0)
		if (fwrite (buf, 1, n, o) != n)
		{
			r = -1;
			break;
		}
	fclose (i);
	if (fclose (o) != 0)
		r = -1;
	return r;
}

/* from -> to, les sous-dossiers compris ; les fichiers de to sont remplacés, ceux qu'il a de plus restent */
static void copy_tree (const char *from, const char *to)
{
	mkdir (to, 0755);
	DIR *d = opendir (from);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, ".."))
			continue;
		char a[1024], b[1024];
		snprintf (a, sizeof a, "%s/%s", from, e->d_name);
		snprintf (b, sizeof b, "%s/%s", to, e->d_name);
		if (is_dir (a))
			copy_tree (a, b);
		else
			copy_file (a, b);
	}
	closedir (d);
}

/* les compétences du paquet (sys/skills/<nom>) dans le dossier du compte, quand elles manquent ou ont changé */
static void sync_skills (const char *sys, const char *home)
{
	char from[600], to[600];
	snprintf (from, sizeof from, "%s/skills", sys);
	snprintf (to, sizeof to, "%s/skills", home);
	DIR *d = opendir (from);
	if (!d)
		return;
	mkdir (to, 0755);
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (e->d_name[0] == '.')
			continue;
		char a[900], b[900], sa[1000], sb[1000];
		snprintf (a, sizeof a, "%s/%s", from, e->d_name);
		snprintf (b, sizeof b, "%s/%s", to, e->d_name);
		if (!is_dir (a))
			continue;
		snprintf (sa, sizeof sa, "%s/SKILL.md", a);
		snprintf (sb, sizeof sb, "%s/SKILL.md", b);
		long long ma = mtime_of (sa), mb = mtime_of (sb);
		if (ma >= 0 && (mb < 0 || ma > mb))
			copy_tree (a, b);
	}
	closedir (d);
}

/* ce que l'utilisateur avait laissé dans sys avant les comptes (tout sauf AGENTS.md et skills/) -> home, sans rien
   écraser */
static void move_user_data (const char *sys, const char *home)
{
	DIR *d = opendir (sys);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		const char *n = e->d_name;
		if (!strcmp (n, ".") || !strcmp (n, "..") || !strcmp (n, "AGENTS.md") || !strcmp (n, "skills"))
			continue;
		char a[900], b[900];
		snprintf (a, sizeof a, "%s/%s", sys, n);
		snprintf (b, sizeof b, "%s/%s", home, n);
		struct stat st;
		if (stat (b, &st) != 0)
			rename (a, b);
	}
	closedir (d);
}

int acp_home (int claude, char *out, unsigned long cap)
{
	const char *name = claude ? "/.claude-acp" : "/.vibe";
	char sys[300];
	snprintf (sys, sizeof sys, "%s%s", ACP_SYS_ROOT, name);
	const char *v = getenv (claude ? "CLAUDE_ACP_HOME" : "VIBE_HOME"), *h = getenv ("HOME");
	if (v && *v)
		snprintf (out, cap, "%s", v);
	else
		snprintf (out, cap, "%s%s", h && h[0] == '/' && h[1] ? h : "", name);
	size_t l = strlen (out);
	while (l > 1 && out[l - 1] == '/')
		out[--l] = 0;
	mkdir (out, 0700);
	if (strcmp (out, sys) != 0 && is_dir (sys))
	{
		char mark[700];
		snprintf (mark, sizeof mark, "%s/.from-system", out);
		if (access (mark, F_OK) != 0)		/* (une seule fois par compte) */
		{
			move_user_data (sys, out);
			FILE *f = fopen (mark, "w");
			if (f)
				fclose (f);
		}
		char a[700], b[700];
		snprintf (a, sizeof a, "%s/AGENTS.md", sys);
		snprintf (b, sizeof b, "%s/AGENTS.md", out);
		if (access (b, F_OK) != 0)
			copy_file (a, b);
		sync_skills (sys, out);
	}
	return 0;
}
