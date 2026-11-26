/*
 * home.c -- le dossier de l'agent pour le compte en cours (acp.h, acp_home).
 *
 * Vibe garde ses comptes, ses séances et ses journaux dans $VIBE_HOME (défaut ~/.local/vibe), claude-acp dans
 * $CLAUDE_ACP_HOME (défaut ~/.local/claude-acp), mai-acp dans $MAI_ACP_HOME (défaut ~/.local/mai-acp) : un dossier par
 * compte, sous ~/.local/<app> comme les données des autres apps (docs/etude/MULTI-UTILISATEUR.md, question 4, 18/11 ;
 * ~/.vibe, ~/.claude-acp et ~/.mai-acp, les places d'avant, sont ignorées : rien n'y est repris).
 * Ce que l'agent sait de Korin est au système (décision du 26/11/2026, paquet agent-skills, tools/agents/home.py) :
 * /res/agents/skills, les compétences communes, lues là par l'agent (VIBE_SKILL_PATHS plus bas pour Vibe ;
 * claude_acp/agent.py pour claude-acp et mai-acp) et plus recopiées dans le compte -- seul l'uid 0 écrit dans /res,
 * un programme du compte ne peut plus y glisser une consigne ; /res/agents/<agent>/AGENTS.md, copié dans le dossier
 * du compte s'il y manque (il est à l'utilisateur une fois posé). La compétence d'une app vient avec elle
 * (/apps/<app>.app/SKILL.md). Une fois par compte :
 *   - les copies des compétences du système que libacp posait avant dans le compte sont retirées (sinon une copie
 *     ancienne, ou changée par un programme, passerait avant celle du système) ; les compétences de l'utilisateur,
 *     d'autres noms, restent ;
 *   - ce qu'un compte avait laissé dans /.vibe (/.claude-acp, /.mai-acp) avant les comptes (.env et la clé,
 *     config.toml, séances, journaux, korin-app.txt...) y est déplacé, comme Jet le fait pour /home/jet.
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

/* (le dossier d'un compte, une fois) les copies des compétences du système (res/<nom>) retirées de home/skills */
static void rm_tree (const char *p)
{
	DIR *d = opendir (p);
	if (!d)
	{
		unlink (p);
		return;
	}
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, ".."))
			continue;
		char a[1024];
		snprintf (a, sizeof a, "%s/%s", p, e->d_name);
		rm_tree (a);
	}
	closedir (d);
	rmdir (p);
}

static void drop_system_copies (const char *res, const char *home)
{
	DIR *d = opendir (res);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (e->d_name[0] == '.')
			continue;
		char b[900];
		snprintf (b, sizeof b, "%s/skills/%s", home, e->d_name);
		if (is_dir (b))
			rm_tree (b);
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

int acp_home (int profile, char *out, unsigned long cap)
{
	static const char *const NAME[] = { "/.vibe", "/.claude-acp", "/.mai-acp" };		/* (ceux d'avant /res/agents) */
	static const char *const RES[] = { "/res/agents/vibe", "/res/agents/claude-acp", "/res/agents/mai-acp" };
	static const char *const LOCAL[] = { "vibe", "claude-acp", "mai-acp" };		/* ~/.local/<nom> */
	static const char *const VAR[] = { "VIBE_HOME", "CLAUDE_ACP_HOME", "MAI_ACP_HOME" };
	if (profile < 0 || profile > ACP_PROFILE_MAI)
		profile = ACP_PROFILE_VIBE;
	const char *name = NAME[profile];
	char sys[300];
	snprintf (sys, sizeof sys, "%s%s", ACP_SYS_ROOT, name);
	const char *v = getenv (VAR[profile]), *h = getenv ("HOME");
	if (v && *v)
		snprintf (out, cap, "%s", v);
	else
	{
		const char *base = h && h[0] == '/' && h[1] ? h : "";
		char local[520];
		snprintf (local, sizeof local, "%s/.local", base);
		mkdir (local, 0700);
		snprintf (out, cap, "%s/%s", local, LOCAL[profile]);
	}
	size_t l = strlen (out);
	while (l > 1 && out[l - 1] == '/')
		out[--l] = 0;
	mkdir (out, 0700);
	/* les compétences des apps (/apps/<app>.app/SKILL.md, clé skill de app.txt : docs/ABI.md) : Vibe les cherche dans
	   les dossiers de skill_paths, que cette variable lui donne en plus de son config.toml ; claude-acp et mai-acp
	   lisent /apps eux-mêmes (claude_acp/agent.py, app_skills) */
	if (profile == ACP_PROFILE_VIBE)
		setenv ("VIBE_SKILL_PATHS", "[\"" ACP_SYS_ROOT "/res/agents/skills\", \"" ACP_SYS_ROOT "/apps\"]", 0);
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
	}
	char res[300], a[700], b[700];
	snprintf (res, sizeof res, "%s%s", ACP_SYS_ROOT, RES[profile]);
	snprintf (a, sizeof a, "%s/AGENTS.md", res);
	if (access (a, F_OK) != 0)	/* (un paquet d'avant agent-skills : AGENTS.md dans /.vibe...) */
		snprintf (a, sizeof a, "%s/AGENTS.md", sys);
	snprintf (b, sizeof b, "%s/AGENTS.md", out);
	if (strcmp (a, b) != 0 && access (b, F_OK) != 0)
		copy_file (a, b);
	char mark2[700], skills[400];
	snprintf (mark2, sizeof mark2, "%s/.skills-in-res", out);
	snprintf (skills, sizeof skills, "%s/res/agents/skills", ACP_SYS_ROOT);
	if (access (mark2, F_OK) != 0 && is_dir (skills))	/* (une fois : les copies d'avant /res/agents retirées) */
	{
		drop_system_copies (skills, out);
		FILE *f = fopen (mark2, "w");
		if (f)
			fclose (f);
	}
	return 0;
}
