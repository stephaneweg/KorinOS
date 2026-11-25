/* conf.c : les réglages personnels de l'utilisateur en cours (docs/etude/MULTI-UTILISATEUR.md, §2.5),
 * comme armpi/userconf.h de libposix : un fichier de réglages se lit d'abord dans HOME:/etc/, puis dans
 * /etc/ (la valeur de la machine) ; il s'écrit toujours dans HOME:/etc/ (docs/DESIGN-CONNEXION.md §2.6).
 * HOME:/.config/korin/ (la place du 15/11) est encore lue entre les deux. */
#include "korinrt.h"

static char conf_buf[160];

/* nom (« panel.ini »), write -> le chemin à lire (le personnel s'il existe, sinon /etc/<nom>) ou à
 * écrire (le personnel, ses dossiers créés) ; tampon commun, valable jusqu'à l'appel suivant */
const char *conf_path(const char *name, int write)
{
	long st[2];
	str_cat(str_cat(conf_buf, "HOME:/etc/"), name);
	if (write) {
		sys_mkdir("HOME:/etc"); /* (E_EXIST : déjà là) */
		if (sys_stat(conf_buf, st) < 0) { /* la première fois : la copie de l'ancienne place ou de /etc */
			char e[160];
			str_cat(str_cat(e, "HOME:/.config/korin/"), name);
			if (sys_stat(e, st) < 0)
				str_cat(str_cat(e, "/etc/"), name);
			long in = sys_open(e, O_READ);
			if (in >= 0) {
				long out = sys_open(conf_buf, O_WRITE | O_CREATE | O_TRUNC);
				static char t[1024];
				long n;
				while (out >= 0 && (n = sys_read(in, t, sizeof t)) > 0)
					sys_write(out, t, (u64)n);
				if (out >= 0)
					sys_close(out);
				sys_close(in);
			}
		}
		return conf_buf;
	}
	if (sys_stat(conf_buf, st) < 0) {
		str_cat(str_cat(conf_buf, "HOME:/.config/korin/"), name);
		if (sys_stat(conf_buf, st) < 0)
			str_cat(str_cat(conf_buf, "/etc/"), name);
	}
	return conf_buf;
}

/* comme conf_path (lecture), pour le compte d'uid uid et non pour HOME: de l'appelant : init2 et Elegant
 * (uid 0, dont le HOME: est celui du compte par défaut) lisent startup.txt, session.off, theme.txt du compte
 * réellement ouvert (A4, E30) ; uid <= 0 ou sans dossier : conf_path */
static char conf_buf_uid[200];
const char *conf_path_uid(long uid, const char *name)
{
	char home[160];
	if (uid <= 0 || !user_home(uid, home, sizeof home))
		return conf_path(name, 0);
	long st[2];
	str_cat(str_cat(str_cat(conf_buf_uid, home), "/etc/"), name);
	if (sys_stat(conf_buf_uid, st) >= 0)
		return conf_buf_uid;
	str_cat(str_cat(str_cat(conf_buf_uid, home), "/.config/korin/"), name);
	if (sys_stat(conf_buf_uid, st) >= 0)
		return conf_buf_uid;
	str_cat(str_cat(conf_buf_uid, "/etc/"), name);
	return conf_buf_uid;
}

/* ---- les listes de ce qu'on ne lance pas au démarrage (volet Démarrage de Réglages, docs/DEMARRAGE.md) :
 * /etc/services.off (la machine : lignes de /etc/services.txt qu'init2 saute) et session.off (personnel,
 * conf_path : lignes de /etc/session, session.poche ou startup.txt qu'Elegant et init2 sautent). Une ligne par
 * entrée, telle qu'elle est écrite dans le fichier de démarrage ; « # » : commentaire. */

/* path, buf de size octets -> la liste dans buf (terminée par 0), 0 si le fichier manque (buf vide) */
long list_load(const char *path, char *buf, long size)
{
	buf[0] = 0;
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, buf, (u64)(size - 1));
	sys_close(fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	return n;
}

static int list_blank(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

/* list (list_load), line (« chemin [arguments] ») -> 1 si la ligne, sans les blancs autour, y est */
int list_has(const char *list, const char *line)
{
	if (!list || !line)
		return 0;
	while (list_blank(*line))
		line++;
	u64 n = 0;
	while (line[n] && line[n] != '\n')
		n++;
	while (n && list_blank(line[n - 1]))
		n--;
	if (!n)
		return 0;
	const char *p = list;
	while (*p) {
		while (list_blank(*p))
			p++;
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *f = e;
		while (f > p && list_blank(f[-1]))
			f--;
		if (*p != '#' && (u64)(f - p) == n && !memcmp(p, line, n))
			return 1;
		p = *e ? e + 1 : e;
	}
	return 0;
}
