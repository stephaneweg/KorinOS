/* users.c : les comptes de /etc/users.ini (docs/etude/MULTI-UTILISATEUR.md §2.1) pour init2 et Elegant,
 * qui lancent la session avec l'uid du compte. Le format :
 *
 *   [stephane]
 *   uid = 1000
 *   name = Stéphane
 *   home = /home/stephane
 *   groups = admin
 *
 * Le compte de la session, tant qu'il n'y a pas d'écran de connexion (étape 6) : « autologin = <nom> » de
 * /etc/system.ini, sinon le premier compte d'uid 1000 ou plus. Sans users.ini : -1 (rien ne change, tout
 * reste en uid 0 comme avant le multi-utilisateur). */
#include "korinrt.h"

#define USERS_MAX 4096

static char us_buf[USERS_MAX];

/* fichier -> son texte terminé par 0 dans us_buf, ou 0 */
static char *us_load(const char *path)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, us_buf, USERS_MAX - 1);
	sys_close(fd);
	if (n <= 0)
		return 0;
	us_buf[n] = 0;
	return us_buf;
}

static int us_space(char c)
{
	return c == ' ' || c == '\t';
}

/* ligne suivante de *p (coupée en place, sans les espaces des bouts) ; 0 à la fin */
static char *us_line(char **p)
{
	char *s = *p;
	if (!*s)
		return 0;
	char *e = s;
	while (*e && *e != '\n' && *e != '\r')
		e++;
	*p = *e ? e + 1 : e;
	*e = 0;
	while (us_space(*s))
		s++;
	while (e > s && us_space(e[-1]))
		*--e = 0;
	return s;
}

/* « clé = valeur » : coupe en place ; 0 si ce n'en est pas une */
static char *us_kv(char *line, char **val)
{
	char *eq = line;
	while (*eq && *eq != '=')
		eq++;
	if (!*eq)
		return 0;
	char *v = eq + 1, *k = eq;
	*eq = 0;
	while (k > line && us_space(k[-1]))
		*--k = 0;
	while (us_space(*v))
		v++;
	*val = v;
	return line;
}

/* uid du compte « name », ou d'abord le premier d'uid >= 1000 (name = 0) ; -1 : aucun */
long user_uid(const char *name)
{
	char *p = us_load("/etc/users.ini");
	if (!p)
		return -1;
	char sec[64] = "";
	char *l;
	while ((l = us_line(&p))) {
		if (*l == '[') {
			long n = 0;
			for (l++; *l && *l != ']' && n < 63; l++)
				sec[n++] = *l;
			sec[n] = 0;
			continue;
		}
		char *v, *k = us_kv(l, &v);
		if (!k || !str_eq(k, "uid") || !sec[0])
			continue;
		long u = (long)parse_dec(v);
		if (name ? str_eq(sec, name) : u >= 1000)
			return u;
	}
	return -1;
}

/* spawn d'un programme de la session : avec l'uid du compte (SYS_SPAWN_AS d'AppKit, réservé à l'uid 0) ;
 * sans compte, sans uid dans le noyau (le Pi) ou appelant d'un autre uid : spawn ordinaire, l'uid du père */
long spawn_session(const char *path, const char *args)
{
	long uid = korin_sys0(SYS_GETUID) == 0 ? user_session_uid() : -1;
	if (uid > 0) {
		long r = ksys(SYS_SPAWN_AS, (long)path, (long)args, uid, 0, 0, 0);
		if (r != E_NOSYS)
			return r;
	}
	return ksys(SYS_SPAWN, (long)path, (long)args, 0, 0, 0, 0);
}

/* le prochain lancement de l'appelant (f_spawn_stdio, f_spawn...) se fera avec l'uid de la session :
 * pour un service d'uid 0 qui ouvre un shell à quelqu'un (telnetd, la console texte) */
void spawn_session_next(void)
{
	long uid = korin_sys0(SYS_GETUID) == 0 ? user_session_uid() : -1;
	if (uid > 0)
		ksys(SYS_SPAWN_AS, 0, 0, uid, 0, 0, 0);
}

/* uid du compte de la session (autologin de /etc/system.ini, sinon le premier compte), -1 : aucun */
long user_session_uid(void)
{
	char name[64] = "";
	char *p = us_load("/etc/system.ini");
	char *l;
	while (p && (l = us_line(&p))) {
		char *v, *k = us_kv(l, &v);
		if (k && str_eq(k, "autologin")) {
			long n = 0;
			while (v[n] && n < 63) {
				name[n] = v[n];
				n++;
			}
			name[n] = 0;
		}
	}
	if (name[0]) {
		long u = user_uid(name);
		if (u >= 0)
			return u;
	}
	return user_uid(0);
}
