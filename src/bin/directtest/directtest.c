/* directtest - la lecture directe (FS_READ_DIRECT, docs/ABI.md) rend-elle les
 * bons octets ? Comme directtest.asm : lit un fichier par petits morceaux (la
 * référence, 2 Mo au plus), puis le relit par gros morceaux, à diverses
 * positions, vers une région jamais touchée : chaque octet doit être le même,
 * et rien ne doit déborder après la fin.
 *	directtest [chemin]	(SD0:/bin/fasmarm par défaut)
 * Sortie : une ligne par échec, puis « directtest : PASS / FAIL (n) » ; codes
 * 0, 1 (échecs), 2 (erreur d'ouverture, de lecture de la référence, de
 * région). Le chemin est la ligne brute du noyau (krt_line), entière, comme
 * l'asm (x0 : pas coupée aux espaces). */
#include "korinrt.h"

#define REF_MAX 0x200000 /* on regarde les 2 premiers Mo */
#define REGION 0x300000
#define SMALL 4096
#define GUARD 64 /* octets relus après la fin */

/* position, octets, décalage du tampon */
static const u64 tests[][3] = {
	{0, 65536, 0},		/* */
	{0, 1048576, 0},	/* une requête pleine */
	{512, 200000, 0},	/* bouts de secteur à la fin */
	{1000, 300000, 0},	/* position hors secteur */
	{4096, 131072, 8},	/* tampon à cheval, aligné sur 8 */
	{65536, 1500000, 2048}, /* plusieurs requêtes */
	{1, 70000, 4},		/* */
	{0, 70000, 1},		/* tampon non aligné : autre chemin */
	{511, 524288, 512},	/* */
	{100, 3000000, 0},	/* plus que le fichier */
	{(u64)-1, 0, 0},
};

static NORETURN void fail_err(long e)
{
	ak_puts("directtest : erreur ");
	print_dec(e);
	ak_puts("\n");
	_exit(2);
}

static long map_region(void)
{
	return korin_sys4(SYS_VM_MAP, 0, REGION, PROT_READ | PROT_WRITE, 0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *path = krt_line && *krt_line ? krt_line : "SD0:/bin/fasmarm";
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		fail_err(fd);
	long r = map_region();
	if (r < 0)
		fail_err(r);
	u8 *ref = (u8 *)r;
	u64 size = 0; /* la référence */
	for (;;) {
		r = sys_read(fd, ref + size, SMALL);
		if (r < 0)
			fail_err(r);
		if (r == 0)
			break;
		size += (u64)r;
		if (size >= REF_MAX)
			break;
	}
	ak_puts("directtest : reference ");
	print_dec((long)size);
	ak_puts("\n");
	long fails = 0;
	for (int t = 0; tests[t][0] != (u64)-1; t++) {
		u64 pos = tests[t][0], n = tests[t][1], off = tests[t][2];
		if (pos >= size) /* le fichier est trop court */
			continue;
		sys_seek(fd, (long)pos, 0);
		r = map_region();
		if (r < 0)
			fail_err(r);
		u8 *reg = (u8 *)r;
		u8 *b = reg + off;
		u64 got = (u64)sys_read(fd, b, n);
		u64 want = size - pos; /* (la référence peut s'arrêter avant le fichier) */
		if (n < want)
			want = n;
		const char *bad = 0;
		if (got < want || (size < REF_MAX && got != want)) {
			bad = "directtest : ECHEC taille, position ";
		} else {
			u64 i = 0;
			for (; i < want; i++)
				if (b[i] != ref[pos + i])
					break;
			if (i < want)
				bad = "directtest : ECHEC octets, position ";
			else if (want == got) /* (seulement si on a tout comparé) : rien après la fin */
				for (u64 e = i + GUARD; i < e; i++)
					if (b[i]) {
						bad = "directtest : ECHEC octets, position ";
						break;
					}
		}
		if (bad) {
			ak_puts(bad);
			print_dec((long)pos);
			ak_puts(" / ");
			print_dec((long)n);
			ak_puts(" / ");
			print_dec((long)got);
			ak_puts("\n");
			fails++;
		}
		korin_sys2(SYS_VM_UNMAP, reg, REGION);
	}
	if (fails) {
		ak_puts("directtest : FAIL (");
		print_dec(fails);
		ak_puts(")\n");
		_exit(1);
	}
	ak_puts("directtest : PASS\n");
	_exit(0);
}
