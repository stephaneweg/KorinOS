/* tr - remplace ou efface des caractères : tr [-d] ensemble1 [ensemble2] (docs/OUTILS.md), comme
 * tr.asm : octets ; « a-z » (intervalle), \n, \t, \x = x ; 256 octets au plus par ensemble ;
 * ensemble2 plus court : son dernier octet sert pour la suite ; lit l'entrée standard seulement ;
 * usage et code 1 s'il manque un ensemble. Comme l'asm, un ensemble2 vide prend pour « dernier
 * octet » celui qui précède ensemble2 en mémoire (le 256e d'ensemble1). */
#include "korinrt.h"

static struct {
	u8 set1[256];
	u8 set2[256]; /* (juste après set1, comme ens1 et ens2 de l'asm) */
} sets;
static u16 table[256]; /* 256 : effacé */

/* texte -> octets dans out (256 au plus) -> leur nombre */
static long expand(const char *t, u8 *out)
{
	long n = 0;
	for (;;) {
		u32 c = (u8)*t++;
		if (!c)
			break;
		if (c == '\\') {
			c = (u8)*t++;
			if (!c)
				break;
			if (c == 'n')
				c = 10;
			else if (c == 't')
				c = 9;
		} else if (t[0] == '-' && t[1]) {
			u32 d = (u8)t[1];
			t += 2;
			for (; c <= d; c++) {
				if (n >= 256)
					return n;
				out[n++] = (u8)c;
			}
			continue;
		}
		if (n >= 256)
			break;
		out[n++] = (u8)c;
	}
	return n;
}

int main(int argc, char **argv)
{
	(void)argc;
	int i = 1, del = 0;
	long n1, n2 = 0;
	if (!argv[1])
		goto usage;
	if (str_eq(argv[1], "-d")) {
		del = 1;
		i = 2;
	}
	if (!argv[i])
		goto usage;
	n1 = expand(argv[i++], sets.set1);
	if (!del) {
		if (!argv[i])
			goto usage;
		n2 = expand(argv[i], sets.set2);
	}
	for (int k = 0; k < 256; k++)
		table[k] = (u16)k;
	for (long k = 0; k < n1; k++) {
		u16 v = 256;
		if (!del) {
			long j = (u64)k < (u64)n2 ? k : n2 - 1;
			v = ((u8 *)&sets)[256 + j];
		}
		table[sets.set1[k]] = v;
	}
	rd_init(&krt_in, 0);
	for (;;) {
		int c = rd_getc(&krt_in);
		if (c < 0)
			break;
		if (table[c] != 256)
			out_putc(table[c]);
	}
	return 0;
usage:
	err_msg("usage : tr [-d] ensemble1 [ensemble2]");
	return 1;
}
