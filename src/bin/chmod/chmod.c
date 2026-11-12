/* chmod - le bit d'exécution : chmod mode fichiers (docs/OUTILS.md, docs/FICHIERS.md), comme
 * chmod.asm. mode : octal (755 : exécutable si un bit x, 0111) ou symbolique, des clauses séparées
 * par « , » : [ugoa]* puis + - ou = puis [rwxXst]* (+x, a-x, u=rwx,go=r ; « u+x-w » : plusieurs
 * opérations). Seul x compte : vfs tient l'index des exécutables (V_STATX pour l'état présent,
 * V_CHMOD pour le nouveau). Usage : code 2 ; un fichier inaccessible : message, code 1, on
 * continue ; un mode illisible : message, code 2, on s'arrête (comme l'asm, l'état du fichier
 * est demandé avant de lire le mode : un fichier absent dit « inaccessible » même si le mode est
 * illisible). */
#include "korinrt.h"

/* chemin -> 1 s'il est exécutable, 0 sinon, ou erreur (V_STATX) */
static long exec_state(const char *path)
{
	long m[MSG_SIZE / 8];
	vfs_msg(m, V_STATX, 0, 0, path, 0);
	long r = vfs_call(m);
	if (r < 0)
		return r;
	return (m[MSG_C / 8] & ATTR_EXEC) != 0;
}

/* mode (texte), exécutable maintenant -> exécutable ensuite (0/1), E_INVAL si illisible */
static long apply(const char *s, long cur)
{
	if ((u8)(s[0] - '0') <= 7) { /* octal */
		u64 v = 0;
		for (; *s; s++) {
			u8 d = (u8)(*s - '0');
			if (d > 7)
				return E_INVAL;
			v = (v << 3) + d;
		}
		return (v & 0111) != 0;
	}
	for (;;) {
		while (*s == 'u' || *s == 'g' || *s == 'o' || *s == 'a')
			s++;
		char op;
	next_op:
		op = *s++; /* + - = */
		if (op != '+' && op != '-' && op != '=')
			return E_INVAL;
		long x = 0;
		for (;; s++) {
			char c = *s;
			if (!c || c == ',' || c == '+' || c == '-' || c == '=')
				break;
			if (c == 'x' || c == 'X')
				x = 1;
			else if (c != 'r' && c != 'w' && c != 's' && c != 't')
				return E_INVAL;
		}
		if (op == '=')
			cur = x;
		else if (x)
			cur = op == '+';
		if (!*s)
			return cur;
		if (*s != ',')
			goto next_op; /* (« +x-w » : l'opération suivante) */
		s++;
	}
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		err_msg("usage : chmod mode fichier...  (mode : 755, +x, a-x, u=rwx,go=r)");
		return 2;
	}
	int code = 0;
	for (int i = 2; i < argc; i++) {
		const char *f = argv[i];
		long x = exec_state(f);
		if (x < 0)
			goto err;
		x = apply(argv[1], x);
		if (x < 0) {
			err_msg("mode illisible");
			return 2;
		}
		long m[MSG_SIZE / 8];
		vfs_msg(m, V_CHMOD, x ? 0755 : 0644, 0, f, 0);
		if (vfs_call(m) >= 0)
			continue;
	err:
		err_file(f);
		code = 1;
	}
	return code;
}
