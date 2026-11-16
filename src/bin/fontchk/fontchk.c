/* fontchk - lit deux fois /fonts/DejaVuSans.ttf dans deux tampons et les
 * compare (comme fontchk.asm) : « fontchk : premier ecart a N (adr1 A, adr2 B) »
 * au premier octet différent (adresses des octets dans les tampons), puis
 * « fontchk : octets differents N » ; code 0. Comme l'asm : arguments ignorés,
 * aucune erreur vérifiée (fichier absent : taille 0, 0 octet différent ; une
 * seule lecture de la taille entière), tampons du tas d'AppKit, sortie sans
 * tampon (puts, print_dec, lang_puts). */
#include "korinrt.h"

static const char path[] = "/fonts/DejaVuSans.ttf";
static long stbuf[2];

/* le fichier entier (une lecture) dans buf */
static void readall(u8 *buf, u64 size)
{
	long fd = sys_open(path, O_READ);
	sys_read(fd, buf, size);
	sys_close(fd);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	sys_stat(path, stbuf);
	u64 size = (u64)stbuf[0];
	u8 *a = ak_malloc(size);
	u8 *b = ak_malloc(size);
	readall(a, size);
	readall(b, size);
	u64 diffs = 0;
	for (u64 i = 0; i < size; i++) {
		if (a[i] == b[i])
			continue;
		if (!diffs) {
			lang_puts("fontchk : premier ecart a ");
			print_dec((long)i);
			ak_puts(" (adr1 ");
			print_dec((long)(a + i));
			ak_puts(", adr2 ");
			print_dec((long)(b + i));
			ak_puts("\n");
		}
		diffs++;
	}
	lang_puts("fontchk : octets differents ");
	print_dec((long)diffs);
	ak_puts("\n");
	_exit(0);
}
