/* useradd - créer un compte (docs/etude/MULTI-UTILISATEUR.md §2.1, étape 6 ; dans /sbin : sudo useradd ...)
 *
 *   useradd [-n "Nom affiché"] [-a] nom
 *      -a : administrateur (groupe admin : peut s'élever, /etc/elevate.ini)
 *
 * Ajoute [nom] à /etc/users.ini (uid suivant, à partir de 1000 ; home = /home/nom ; shell /bin/bash) et crée
 * le dossier /home/nom avec ses sous-dossiers (Documents, Images, Musique, Vidéos, etc pour ses réglages). Le
 * compte n'a pas de mot de passe : sudo passwd nom en donne un. */
#include "usersfile.h"

int main(int argc, char **argv)
{
	const char *name = 0, *full = 0;
	int admin = 0;
	for (int i = 1; i < argc; i++) {
		if (str_eq(argv[i], "-a"))
			admin = 1;
		else if (str_eq(argv[i], "-n") && i + 1 < argc)
			full = argv[++i];
		else
			name = argv[i];
	}
	if (!name) {
		uf_out("usage : useradd [-n \"Nom affiche\"] [-a] nom\n");
		return 1;
	}
	if (uf_need_root("useradd"))
		return 1;
	if (!uf_name_ok(name)) {
		uf_out("useradd : nom de compte refuse (minuscules, chiffres, - et _, une lettre d'abord, 31 au plus)\n");
		return 1;
	}
	if (uf_load() < 0) {
		uf_out("useradd : /etc/users.ini illisible\n");
		return 1;
	}
	u64 b, e;
	if (uf_section(name, &b, &e)) {
		uf_out("useradd : ce compte existe deja\n");
		return 1;
	}
	long uid = uf_next_uid();
	char sec[512], n[24];
	u64 v = (u64)uid;
	int k = 0;
	char t[24];
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10));
	for (int i = 0; i < k; i++)
		n[i] = t[k - 1 - i];
	n[k] = 0;
	char home[64];
	str_cat(str_cat(home, "/home/"), name);
	char *o = sec;
	if (uf_len && uf_buf[uf_len - 1] != '\n')
		o = str_cat(o, "\n");
	o = str_cat(str_cat(str_cat(o, "\n["), name), "]\n");
	o = str_cat(str_cat(str_cat(o, "uid = "), n), "\n");
	if (full)
		o = str_cat(str_cat(str_cat(o, "name = "), full), "\n");
	o = str_cat(str_cat(str_cat(o, "home = "), home), "\n");
	o = str_cat(o, "shell = /bin/bash\n");
	o = str_cat(str_cat(o, "groups = "), admin ? "admin\n" : "\n");
	if (!uf_splice(uf_len, uf_len, sec) || uf_save() < 0) {
		uf_out("useradd : /etc/users.ini ne peut pas etre ecrit\n");
		return 1;
	}
	static const char *const subs[] = {"", "/Documents", "/Images", "/Musique", "/Videos", "/etc", 0};
	for (int i = 0; subs[i]; i++) {
		char d[96];
		str_cat(str_cat(d, home), subs[i]);
		sys_mkdir(d);
	}
	uf_out("Compte ");
	uf_out(name);
	uf_out(" cree (uid ");
	uf_out(n);
	uf_out(", ");
	uf_out(home);
	uf_out(admin ? ", administrateur" : "");
	uf_out("). Mot de passe : sudo passwd ");
	uf_out(name);
	uf_out("\n");
	return 0;
}
