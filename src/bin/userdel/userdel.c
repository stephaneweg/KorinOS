/* userdel - retirer un compte de /etc/users.ini (docs/etude/MULTI-UTILISATEUR.md, étape 6 ; sudo userdel nom).
 * Son dossier personnel n'est pas touché (ses fichiers restent, à effacer à la main si on le veut) ; son mot de
 * passe est retiré de /etc/shadow.ini. system ne se retire pas. */
#include "../useradd/usersfile.h"

/* la ligne « nom = ... » de /etc/shadow.ini retirée */
static void shadow_drop(const char *name)
{
	static char sb[8192], nb[8192];
	long fd = sys_open("/etc/shadow.ini", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, sb, sizeof sb - 1);
	sys_close(fd);
	if (n <= 0)
		return;
	sb[n] = 0;
	u64 k = (u64)str_len(name);
	char *o = nb, *p = sb;
	while (*p) {
		char *l = p;
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
		const char *q = l;
		while (*q == ' ' || *q == '\t')
			q++;
		if (!memcmp(q, name, k) && (q[k] == ' ' || q[k] == '\t' || q[k] == '='))
			continue;
		memcpy(o, l, (u64)(p - l));
		o += p - l;
	}
	fd = sys_open("/etc/shadow.ini", O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0) {
		sys_write(fd, nb, (u64)(o - nb));
		sys_close(fd);
	}
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		uf_out("usage : userdel nom\n");
		return 1;
	}
	const char *name = argv[1];
	if (uf_need_root("userdel"))
		return 1;
	if (str_eq(name, "system")) {
		uf_out("userdel : system ne se retire pas\n");
		return 1;
	}
	uf_load();
	u64 b, e;
	if (!uf_section(name, &b, &e)) {
		uf_out("userdel : pas de compte ");
		uf_out(name);
		uf_out("\n");
		return 1;
	}
	char home[128] = "";
	uf_get(name, "home", home, sizeof home);
	if (!uf_splice(b, e, "") || uf_save() < 0) {
		uf_out("userdel : /etc/users.ini ne peut pas etre ecrit\n");
		return 1;
	}
	shadow_drop(name);
	uf_out("Compte ");
	uf_out(name);
	uf_out(" retire. Son dossier ");
	uf_out(home[0] ? home : "personnel");
	uf_out(" n'est pas touche.\n");
	return 0;
}
