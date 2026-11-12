/* test - évalue une condition : test expression (docs/OUTILS.md), comme test.asm et cmd_test de
 * src/inc/testcmd.inc (la commande interne de sh) : « ! » (autant qu'on veut, en tête), un mot seul
 * (vrai s'il n'est pas vide), deux mots (-z -n, sinon -d -f -s et toute autre lettre : le chemin
 * existe), trois mots (= != == et toute chaîne qui commence par = ou ! ; sinon des nombres : la
 * deuxième lettre e -eq, n -ne, l -lt ou -le, toute autre -gt ou -ge, selon la troisième « t »).
 * Plus de trois mots : faux (même avec « ! »). Sans mot : faux ; « ! » seul : vrai. Si argv[0]
 * commence par « [ », le dernier mot doit être « ] ». Code 0 vrai, 1 faux.
 *
 * Comme l'asm : avec deux mots, la lettre est le deuxième octet du premier (« -dx » vaut -d, un
 * premier mot vide fait lire l'octet qui suit son 0) ; un nombre s'arrête au premier caractère qui
 * n'est pas un chiffre (« 12abc » vaut 12, « abc » vaut 0, un seul « - » en tête). */
#include "korinrt.h"

/* « -12x » -> -12 (dec_signe) */
static long signed_dec(const char *s)
{
	long v = 0;
	int neg = 0;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	for (;;) {
		unsigned d = (u8)*s++ - '0';
		if (d > 9)
			break;
		v = v * 10 + d;
	}
	return neg ? -v : v;
}

/* lettre, texte -> 0 si vrai (test_unaire) */
static int unary(int op, const char *s)
{
	long st[2];
	if (op == 'z')
		return *s != 0;
	if (op == 'n')
		return *s == 0;
	if (sys_stat(s, st) < 0)
		return 1;
	if (op == 'd')
		return !(st[1] & ATTR_DIR);
	if (op == 'f')
		return (st[1] & ATTR_DIR) != 0;
	if (op == 's')
		return st[0] == 0;
	return 0; /* -e, -r, -w, -x... */
}

/* gauche, opérateur, droite -> 0 si vrai (test_binaire) */
static int binary(const char *a, const char *op, const char *b)
{
	if (op[0] == '=')
		return !str_eq(a, b);
	if (op[0] == '!')
		return str_eq(a, b) != 0;
	long x = signed_dec(a), y = signed_dec(b);
	if (op[1] == 'e')
		return x != y;
	if (op[1] == 'n')
		return x == y;
	if (op[1] == 'l')
		return op[2] == 't' ? x >= y : x > y;
	return op[2] == 't' ? x <= y : x < y;
}

/* argv -> 0 si vrai (cmd_test) */
static int eval(char **argv)
{
	long n = 0;
	while (argv[n + 1])
		n++;
	if (argv[0][0] == '[') {
		if (!n || !str_eq(argv[n], "]"))
			return 1;
		n--;
	}
	char **a = argv + 1;
	int neg = 0;
	for (;;) {
		if (!n)
			return 1 ^ neg;
		if (!str_eq(a[0], "!"))
			break;
		neg ^= 1;
		a++;
		n--;
	}
	int r;
	if (n == 1)
		r = a[0][0] == 0;
	else if (n == 2) {
		const volatile char *p = a[0]; /* (comme l'asm : l'octet après le premier) */
		r = unary(p[1], a[1]);
	} else if (n == 3)
		r = binary(a[0], a[1], a[2]);
	else
		return 1;
	return r ^ neg;
}

int main(int argc, char **argv)
{
	(void)argc;
	return eval(argv);
}
