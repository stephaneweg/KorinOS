/* usermod - changer un compte de /etc/users.ini (docs/etude/MULTI-UTILISATEUR.md, étape 6 ; sudo usermod ...)
 *
 *   usermod [-n "Nom affiché"] [-a | -A] [-h dossier] [-p image] nom
 *      -a : administrateur (groupe admin) ; -A : plus administrateur
 *      -h : son dossier personnel (HOME:/), -p : son image (picture, PNG) */
#include "../useradd/usersfile.h"

/* « admin » ajouté à groups (on = 1) ou retiré (on = 0) */
static void admin_set(const char *name, int on)
{
	char g[160] = "", out[160] = "";
	uf_get(name, "groups", g, sizeof g);
	char *o = out, *p = g;
	int had = 0;
	while (*p) {
		while (*p == ' ' || *p == ',')
			p++;
		char *s = p;
		while (*p && *p != ' ' && *p != ',')
			p++;
		u64 n = (u64)(p - s);
		if (!n)
			continue;
		if (n == 5 && !memcmp(s, "admin", 5)) {
			had = 1;
			if (!on)
				continue;
		}
		if (o != out)
			o = str_cat(o, ", ");
		memcpy(o, s, n);
		o += n;
		*o = 0;
	}
	if (on && !had) {
		if (o != out)
			o = str_cat(o, ", ");
		str_cat(o, "admin");
	}
	uf_set(name, "groups", out);
}

int main(int argc, char **argv)
{
	const char *name = 0, *full = 0, *home = 0, *pic = 0;
	int admin = -1;
	for (int i = 1; i < argc; i++) {
		if (str_eq(argv[i], "-a"))
			admin = 1;
		else if (str_eq(argv[i], "-A"))
			admin = 0;
		else if (str_eq(argv[i], "-n") && i + 1 < argc)
			full = argv[++i];
		else if (str_eq(argv[i], "-h") && i + 1 < argc)
			home = argv[++i];
		else if (str_eq(argv[i], "-p") && i + 1 < argc)
			pic = argv[++i];
		else
			name = argv[i];
	}
	if (!name || (admin < 0 && !full && !home && !pic)) {
		uf_out("usage : usermod [-n \"Nom affiche\"] [-a | -A] [-h dossier] [-p image] nom\n");
		return 1;
	}
	if (uf_need_root("usermod"))
		return 1;
	uf_load();
	u64 b, e;
	if (!uf_section(name, &b, &e)) {
		uf_out("usermod : pas de compte ");
		uf_out(name);
		uf_out("\n");
		return 1;
	}
	if (full)
		uf_set(name, "name", full);
	if (home) {
		if (home[0] != '/') {
			uf_out("usermod : le dossier doit etre un chemin absolu (/home/nom)\n");
			return 1;
		}
		uf_set(name, "home", home);
		sys_mkdir(home);
	}
	if (pic)
		uf_set(name, "picture", pic);
	if (admin >= 0)
		admin_set(name, admin);
	if (uf_save() < 0) {
		uf_out("usermod : /etc/users.ini ne peut pas etre ecrit\n");
		return 1;
	}
	uf_out("Compte ");
	uf_out(name);
	uf_out(" change.\n");
	return 0;
}
