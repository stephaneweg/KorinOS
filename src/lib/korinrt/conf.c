/* conf.c : les réglages personnels de l'utilisateur en cours (docs/etude/MULTI-UTILISATEUR.md, §2.5),
 * comme armpi/userconf.h de libposix : un fichier de réglages se lit d'abord dans HOME:/.config/korin/,
 * puis dans /etc/ (la valeur de la machine) ; il s'écrit toujours dans HOME:/.config/korin/. */
#include "korinrt.h"

static char conf_buf[160];

/* nom (« panel.ini »), write -> le chemin à lire (le personnel s'il existe, sinon /etc/<nom>) ou à
 * écrire (le personnel, ses dossiers créés) ; tampon commun, valable jusqu'à l'appel suivant */
const char *conf_path(const char *name, int write)
{
	long st[2];
	str_cat(str_cat(conf_buf, "HOME:/.config/korin/"), name);
	if (write) {
		sys_mkdir("HOME:/.config"); /* (E_EXIST : déjà là) */
		sys_mkdir("HOME:/.config/korin");
		if (sys_stat(conf_buf, st) < 0) { /* la première fois : la copie de /etc */
			char e[160];
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
	if (sys_stat(conf_buf, st) < 0)
		str_cat(str_cat(conf_buf, "/etc/"), name);
	return conf_buf;
}
