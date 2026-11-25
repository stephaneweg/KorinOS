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

static void us_copy(char *d, const char *s, long cap)
{
	long n = 0;
	while (s[n] && n < cap - 1) {
		d[n] = s[n];
		n++;
	}
	d[n] = 0;
}

/* le compte d'uid uid de users.ini : son nom (section) et son dossier (« home = ») -> 1, 0 : aucun */
static int user_info(long uid, char *name, long ncap, char *out, long cap)
{
	char *p = us_load("/etc/users.ini");
	if (out)
		out[0] = 0;
	if (name)
		name[0] = 0;
	if (!p || uid < 0)
		return 0;
	char home[160] = "", sec[64] = "";
	long cur = -2;
	char *l;
	for (;;) {
		l = us_line(&p);
		if (!l || *l == '[') { /* fin d'une section : la bonne ? */
			if (cur == uid && sec[0]) {
				if (out)
					us_copy(out, home, cap);
				if (name)
					us_copy(name, sec, ncap);
				return 1;
			}
			if (!l)
				return 0;
			cur = -2;
			home[0] = 0;
			long n = 0;
			for (l++; *l && *l != ']' && n < 63; l++)
				sec[n++] = *l;
			sec[n] = 0;
			continue;
		}
		char *v, *k = us_kv(l, &v);
		if (!k)
			continue;
		if (str_eq(k, "uid"))
			cur = (long)parse_dec(v);
		else if (str_eq(k, "home"))
			us_copy(home, v, sizeof home);
	}
}

/* le dossier personnel (« home = » de users.ini) du compte d'uid uid dans out (cap octets) -> 1, 0 : aucun */
int user_home(long uid, char *out, long cap)
{
	return user_info(uid, 0, 0, out, cap) && out[0] == '/';
}

/* le nom (la section de users.ini) du compte d'uid uid -> 1, 0 : aucun */
int user_name(long uid, char *out, long cap)
{
	return user_info(uid, out, cap, 0, 0);
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

/* uid du compte de la session : celui que l'écran de connexion a ouvert (/tmp/session.ini, « uid = N »),
 * sinon autologin de /etc/system.ini, sinon le premier compte ; -1 : aucun */
long user_session_uid(void)
{
	char name[64] = "";
	char *p = us_load("/tmp/session.ini");
	char *l;
	while (p && (l = us_line(&p))) {
		char *v, *k = us_kv(l, &v);
		if (k && str_eq(k, "uid")) {
			long u = (long)parse_dec(v);
			if (u > 0) /* (jamais l'uid 0 : la session est celle d'une personne) */
				return u;
			break;
		}
	}
	p = us_load("/etc/system.ini");
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

/* ---- le repère secret (A5, E39) : deux mots tirés une fois au hasard, que la carte d'élévation et l'écran de
 * connexion montrent ; une app ne peut ni lire le fichier (vfs : uid 0 seulement, dans tous les modes) ni capturer
 * l'écran (Elegant, G4) : une fausse carte ne le connaît pas */
static const char *const sm_animals[16] = {"renard", "hibou", "loutre", "cerf", "lynx", "héron", "blaireau", "phoque",
					   "colibri", "bison", "panda", "dauphin", "castor", "faucon", "tortue", "koala"};
static const char *const sm_colors[16] = {"bleu", "rouge", "vert", "doré", "violet", "orange", "argenté", "noir",
					  "blanc", "rose", "turquoise", "brun", "gris", "jaune", "pourpre", "corail"};

int secure_mark(const char **animal, const char **color)
{
	char b[16];
	long fd = sys_open("/etc/secure-mark", O_READ);
	long n = fd >= 0 ? sys_read(fd, b, sizeof b - 1) : -1;
	if (fd >= 0)
		sys_close(fd);
	if (n < 2) {
		if (korin_sys0(SYS_GETUID) != 0)
			return 0;
		u8 r[2] = {0, 0};
		if (korin_sys2(SYS_GETRANDOM, r, 2) != 2)
			return 0;
		b[0] = (char)('a' + (r[0] & 15));
		b[1] = (char)('a' + (r[1] & 15));
		b[2] = '\n';
		fd = sys_open("/etc/secure-mark", O_WRITE | O_CREATE | O_TRUNC);
		if (fd < 0)
			return 0;
		sys_write(fd, b, 3);
		sys_close(fd);
	}
	if (b[0] < 'a' || b[0] > 'p' || b[1] < 'a' || b[1] > 'p')
		return 0;
	*animal = sm_animals[b[0] - 'a'];
	*color = sm_colors[b[1] - 'a'];
	return 1;
}
