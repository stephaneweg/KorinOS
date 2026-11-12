/* fstest - exerce le système de fichiers depuis EL0, comme fstest.asm :
 *   listes de « / », « SD0:/ », « SD0:/docs » ; lecture de SD0:/README.TXT ;
 *   SD0:/docs/notes/gros.bin (300 000 octets, octet[i] = (i*7) & 255) vérifié ;
 *   création de « SD0:/docs/Fichier créé par ARMPI.txt » (5 000 lignes, puis le
 *   début réécrit après un seek), taille et relecture ; SD0:/essai : mkdir, chdir,
 *   fichier relatif, unlink refusé sur le dossier non vide (-39), nettoyage.
 * Chaque échec : « fstest : *** ECHEC *** » ; code de sortie : le nombre d'échecs.
 * Comme l'asm : le fichier de SD0:/docs reste après l'essai ; sortie par puts et
 * print_dec d'AppKit (sans tampon). */
#include "korinrt.h"

static const char p_sd[] = "SD0:/";
static const char p_docs[] = "SD0:/docs";
static const char p_new[] = "SD0:/docs/Fichier cr\xC3\xA9\xC3\xA9 par ARMPI.txt";
static const char p_dir[] = "SD0:/essai";
static const char p_rel[] = "dedans.txt";
static const char first[] = "PREMIERE LIGNE REECRITE PAR SEEK\n";
static const char line[] = "Ligne de test ecrite par fstest depuis EL0.\n";
#define FIRST_LEN (sizeof first - 1)
#define LINE_LEN (sizeof line - 1)

static long errors;
static struct dirent dirent;
static char buf[1024];

static void fail(void)
{
	errors++;
	ak_puts("fstest : *** ECHEC ***\n");
}

/* le contenu d'un répertoire */
static void list_dir(const char *path)
{
	ak_puts("--- ");
	ak_puts(path);
	ak_puts("\n");
	long fd = sys_open(path, O_READ);
	if (fd < 0) {
		fail();
		return;
	}
	while (sys_readdir(fd, &dirent) == 1) {
		ak_puts("    ");
		ak_puts(dirent.name);
		if (dirent.attr & ATTR_DIR) {
			ak_puts("/\n");
			continue;
		}
		ak_puts("  ");
		print_dec((long)dirent.size);
		ak_puts(" o\n");
	}
	sys_close(fd);
}

/* un fichier, par morceaux de 255 octets */
static void cat_file(const char *path)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0) {
		fail();
		return;
	}
	for (;;) {
		long n = sys_read(fd, buf, 255);
		if (n <= 0)
			break;
		buf[n] = 0;
		ak_puts(buf);
	}
	sys_close(fd);
}

/* gros.bin lu par blocs de 1000 o : octet[i] = (i*7) & 255 */
static void big_check(void)
{
	long fd = sys_open("SD0:/docs/notes/gros.bin", O_READ);
	if (fd < 0) {
		fail();
		return;
	}
	u64 pos = 0, bad = 0;
	for (;;) {
		long n = sys_read(fd, buf, 1000);
		if (n <= 0)
			break;
		for (long i = 0; i < n; i++)
			if ((u8)((pos + (u64)i) * 7) != (u8)buf[i])
				bad++;
		pos += (u64)n;
	}
	sys_close(fd);
	ak_puts("fstest : gros.bin lu ");
	print_dec((long)pos);
	ak_puts(" octets, erreurs ");
	print_dec((long)bad);
	ak_puts("\n");
	if (pos != 300000 || bad)
		fail();
}

/* fichier à nom long : 5 000 lignes, début réécrit après un seek, relu */
static void write_test(void)
{
	long st[2] = {0, 0};
	long fd = sys_open(p_new, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		goto ko;
	for (int i = 0; i < 5000; i++)
		if (sys_write(fd, line, LINE_LEN) != (long)LINE_LEN)
			goto ko;
	sys_seek(fd, 0, 0);
	sys_write(fd, first, FIRST_LEN);
	sys_close(fd);
	if (sys_stat(p_new, st) != 0)
		goto ko;
	ak_puts("fstest : fichier \xC3\xA9" "crit, taille ");
	print_dec(st[0]);
	ak_puts("\n");
	if ((u64)st[0] != 5000 * LINE_LEN)
		goto ko;
	fd = sys_open(p_new, O_READ);
	if (fd < 0)
		goto ko;
	long n = sys_read(fd, buf, LINE_LEN * 2);
	buf[n] = 0; /* (comme l'asm : sans contrôle) */
	ak_puts("fstest : relu : ");
	ak_puts(buf);
	sys_close(fd);
	list_dir(p_docs);
	return;
ko:
	fail();
}

/* mkdir, fichier dedans, unlink refusé (non vide), nettoyage */
static void dir_test(void)
{
	if (sys_mkdir(p_dir) != 0 || sys_chdir(p_dir) != 0)
		goto ko;
	long fd = sys_open(p_rel, O_WRITE | O_CREATE);
	if (fd < 0)
		goto ko;
	sys_write(fd, first, FIRST_LEN);
	sys_close(fd);
	list_dir(".");
	if (sys_unlink(p_dir) != E_NOTEMPTY)
		goto ko;
	if (sys_unlink(p_rel) != 0)
		goto ko;
	sys_chdir("..");
	if (sys_unlink(p_dir) != 0)
		goto ko;
	list_dir(p_sd);
	return;
ko:
	fail();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	list_dir("/");
	list_dir(p_sd);
	list_dir(p_docs);
	ak_puts("--- SD0:/README.TXT :\n");
	cat_file("SD0:/README.TXT");
	big_check();
	write_test();
	dir_test();
	ak_puts("fstest : termin\xC3\xA9\n");
	_exit((int)errors);
}
