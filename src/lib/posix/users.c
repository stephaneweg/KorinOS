/* users.c -- les comptes (docs/etude/MULTI-UTILISATEUR.md §2.1, étape 2) : getuid demande l'uid au noyau
 * (SYS_GETUID) ; getpwuid, getpwnam, getpwent et getlogin lisent /etc/users.ini :
 *
 *   [stephane]
 *   uid = 1000
 *   name = Stéphane          (pw_gecos)
 *   home = /home/stephane    (pw_dir ; défaut : $HOME, puis /home)
 *   shell = /bin/bash        (pw_shell ; défaut : /bin/bash s'il est là, sinon /bin/sh)
 *
 * « onyx » reste le nom du compte de l'appelant et « root » celui de l'uid 0, pour les ports. Le noyau du Pi
 * n'a pas d'uid (E_NOSYS), et sans users.ini rien ne change : un seul compte, « onyx », d'uid 0 (sur le Pi)
 * ou de celui du noyau, dont le dossier est $HOME. Les groupes ne donnent aucun droit : gid 0 pour tous. */
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "posix_internal.h"

/* uid du noyau, ou -1 (le Pi : pas d'uid, tout est en uid 0 comme avant) */
static long kernel_uid (void)
{
	long u = __armpi_sys0 (SYS_GETUID);
	return u < 0 ? -1 : u;
}

uid_t getuid (void) { long u = kernel_uid (); return u < 0 ? 0 : (uid_t) u; }
uid_t geteuid (void) { return getuid (); }
gid_t getgid (void) { return 0; }
gid_t getegid (void) { return 0; }
int setuid (uid_t u)
{
	if (kernel_uid () < 0)
		return u == 0 ? 0 : POSIX_ERR (EPERM);
	long r = __armpi_sys1 (SYS_SETUID, (long) u);
	return r < 0 ? POSIX_ERR (EPERM) : 0;
}
int setgid (gid_t g) { return g == 0 ? 0 : POSIX_ERR (EPERM); }
int seteuid (uid_t u) { return setuid (u); }
int setegid (gid_t g) { return setgid (g); }
int getgroups (int n, gid_t *list) { (void) n; (void) list; return 0; }
int issetugid (void) { return 0; }

/* le shell de l'utilisateur : /bin/bash s'il est sur la carte, sinon le sh natif (docs/BASH.md) */
static const char *login_shell (void)
{
	static int s_bash = -1;
	if (s_bash < 0)
	{
		struct stat st;
		s_bash = stat ("/bin/bash", &st) == 0 && S_ISREG (st.st_mode);
	}
	return s_bash ? "/bin/bash" : "/bin/sh";
}

/* un compte lu dans users.ini */
struct account
{
	char name[64], gecos[96], home[160], shell[96];
	long uid;
};

static void trim (char *s)
{
	char *b = s, *e;
	while (*b == ' ' || *b == '\t')
		b++;
	if (b != s)
		memmove (s, b, strlen (b) + 1);
	e = s + strlen (s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
}

static void copy (char *d, size_t cap, const char *s)
{
	size_t n = strlen (s);
	if (n >= cap)
		n = cap - 1;
	memcpy (d, s, n);
	d[n] = 0;
}

/* le n-ième compte de users.ini (dans l'ordre du fichier) -> 1, 0 : plus de compte (ou pas de fichier) */
static int account_at (int n, struct account *a)
{
	FILE *f = fopen ("/etc/users.ini", "r");
	if (!f)
		return 0;
	char line[256];
	int idx = -1, found = 0;
	while (fgets (line, sizeof line, f))
	{
		trim (line);
		if (line[0] == '[')
		{
			if (idx == n)
				break;			/* (le compte voulu est complet) */
			idx++;
			if (idx == n)
			{
				char *e = strchr (line, ']');
				if (e)
					*e = 0;
				memset (a, 0, sizeof *a);
				a->uid = -1;
				copy (a->name, sizeof a->name, line + 1);
				found = 1;
			}
			continue;
		}
		if (idx != n || line[0] == '#' || line[0] == ';')
			continue;
		char *eq = strchr (line, '=');
		if (!eq)
			continue;
		*eq = 0;
		char *k = line, *v = eq + 1;
		trim (k);
		trim (v);
		if (!strcmp (k, "uid"))
			a->uid = strtol (v, 0, 10);
		else if (!strcmp (k, "name"))
			copy (a->gecos, sizeof a->gecos, v);
		else if (!strcmp (k, "home"))
			copy (a->home, sizeof a->home, v);
		else if (!strcmp (k, "shell"))
			copy (a->shell, sizeof a->shell, v);
	}
	fclose (f);
	return found && a->uid >= 0;
}

static int account_by_uid (long uid, struct account *a)
{
	for (int i = 0; account_at (i, a); i++)
		if (a->uid == uid)
			return 1;
	return 0;
}

static int account_by_name (const char *name, struct account *a)
{
	for (int i = 0; account_at (i, a); i++)
		if (!strcmp (a->name, name))
			return 1;
	return 0;
}

/* sans users.ini (ou sans ce compte) : le compte de l'appelant s'appelle « onyx », son dossier est $HOME */
static void account_default (long uid, struct account *a)
{
	const char *home = getenv ("HOME");
	memset (a, 0, sizeof *a);
	a->uid = uid;
	copy (a->name, sizeof a->name, uid == 0 && kernel_uid () > 0 ? "system" : "onyx");
	copy (a->home, sizeof a->home, uid == 0 && kernel_uid () > 0 ? "/" : home ? home : "/home");
}

static int fill_pw (const struct account *a, struct passwd *pw, char *buf, size_t len)
{
	const char *home = a->home[0] ? a->home : getenv ("HOME") ? getenv ("HOME") : "/home";
	const char *shell = a->shell[0] ? a->shell : login_shell ();
	size_t need = strlen (a->name) + strlen (a->gecos) + strlen (home) + strlen (shell) + 8;
	if (len < need)
		return ERANGE;
	char *p = buf;
	pw->pw_name = strcpy (p, a->name); p += strlen (a->name) + 1;
	pw->pw_passwd = strcpy (p, "x"); p += 2;
	pw->pw_uid = (uid_t) a->uid;
	pw->pw_gid = 0;
	pw->pw_comment = strcpy (p, a->gecos); p += strlen (a->gecos) + 1;
	pw->pw_gecos = pw->pw_comment;
	pw->pw_dir = strcpy (p, home); p += strlen (home) + 1;
	pw->pw_shell = strcpy (p, shell);
	return 0;
}

int getpwuid_r (uid_t uid, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
	struct account a;
	*res = 0;
	if (!account_by_uid ((long) uid, &a))
	{
		if (uid != getuid () && uid != 0)
			return 0;
		account_default ((long) uid, &a);
	}
	int r = fill_pw (&a, pw, buf, len);
	if (r == 0)
		*res = pw;
	return r;
}

int getpwnam_r (const char *name, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
	struct account a;
	*res = 0;
	if (!name)
		return 0;
	if (account_by_name (name, &a))
	{
		int r = fill_pw (&a, pw, buf, len);
		if (r == 0)
			*res = pw;
		return r;
	}
	if (!strcmp (name, "onyx"))			/* (alias : le compte de l'appelant) */
		return getpwuid_r (getuid (), pw, buf, len, res);
	if (!strcmp (name, "root"))			/* (alias : l'uid 0) */
		return getpwuid_r (0, pw, buf, len, res);
	return 0;
}

struct passwd *getpwuid (uid_t uid)
{
	static struct passwd pw;
	static char buf[512];
	struct passwd *r;
	getpwuid_r (uid, &pw, buf, sizeof buf, &r);
	return r;
}

struct passwd *getpwnam (const char *name)
{
	static struct passwd pw;
	static char buf[512];
	struct passwd *r;
	getpwnam_r (name, &pw, buf, sizeof buf, &r);
	return r;
}

/* les comptes de users.ini, dans l'ordre ; sans le fichier : le seul compte de l'appelant */
static int s_pwent;
struct passwd *getpwent (void)
{
	static struct passwd pw;
	static char buf[512];
	struct account a;
	int i = s_pwent++;
	if (account_at (i, &a))
		return fill_pw (&a, &pw, buf, sizeof buf) == 0 ? &pw : 0;
	if (i == 0 && !account_at (0, &a))
		return getpwuid (getuid ());
	return 0;
}
void setpwent (void) { s_pwent = 0; }
void endpwent (void) { s_pwent = 0; }

char *getlogin (void)
{
	static char s_name[64];
	struct passwd *p = getpwuid (getuid ());
	copy (s_name, sizeof s_name, p ? p->pw_name : "onyx");
	return s_name;
}

int getlogin_r (char *b, size_t n)
{
	const char *s = getlogin ();
	if (n <= strlen (s))
		return ERANGE;
	strcpy (b, s);
	return 0;
}
