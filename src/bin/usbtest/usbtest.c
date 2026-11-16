/* usbtest - comme fstest, sur la clé USB (/media/usb0) : listes, lecture, création
 * (nom long accentué), écriture, relecture, seek, mkdir, unlink ; comme
 * usbtest.asm. Attend la clé 10 s (100 x 100 ms : « usbtest : pas de clé USB
 * (/usb) », code 1), puis liste /media/usb1/, « / », /media/usb0/ (deux fois), lit
 * README.TXT, vérifie gros.bin (300 000 octets, octet[i] = i*7), écrit un
 * fichier de 5 000 lignes, 1 Mo par blocs de 64 Ko, un dossier. Chaque échec
 * écrit « usbtest : *** ECHEC *** » et la suite continue ; code = nombre
 * d'échecs. Comme l'asm : arguments ignorés, sortie sans tampon (puts,
 * print_dec), le dossier courant reste /media/usb0/ après l'essai des dossiers. */
#include "korinrt.h"

static const char p_sd[] = "/media/usb0/";
static const char p_docs[] = "/media/usb0/";
static const char p_new[] = "/media/usb0/Fichier créé par ARMPI.txt";
static const char p_dir[] = "/media/usb0/essai";
static const char p_bulk[] = "/media/usb0/gros2.bin";
static const char p_rel[] = "dedans.txt";
static const char first[] = "PREMIERE LIGNE REECRITE PAR SEEK\n";
#define FIRST_LEN (sizeof first - 1)
static const char line[] = "Ligne de test ecrite par usbtest depuis EL0.\n";
#define LINE_LEN (sizeof line - 1)

static long errors;
static long statbuf[2];
static struct dirent dirent;
static char buf[1024];
static u8 big[65536] __attribute__((aligned(16)));

static void fail(void)
{
	errors++;
	ak_puts("usbtest : *** ECHEC ***\n");
}

/* le contenu du répertoire */
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

/* le fichier, par morceaux de 255 octets (chacun jusqu'à son premier NUL) */
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

/* lit gros.bin par blocs de 1000 o et vérifie octet[i] = (i*7) & 255 */
static void big_check(void)
{
	long fd = sys_open("/media/usb0/gros.bin", O_READ);
	if (fd < 0) {
		fail();
		return;
	}
	u64 pos = 0, bad = 0;
	for (;;) {
		long n = sys_read(fd, buf, 1000);
		if (n <= 0)
			break;
		for (u64 i = 0; i < (u64)n; i++)
			if ((u8)((pos + i) * 7) != (u8)buf[i])
				bad++;
		pos += (u64)n;
	}
	sys_close(fd);
	ak_puts("usbtest : gros.bin lu ");
	print_dec((long)pos);
	ak_puts(" octets, erreurs ");
	print_dec((long)bad);
	ak_puts("\n");
	if (pos != 300000 || bad)
		fail();
}

/* crée un fichier à nom long, y écrit 5000 lignes, réécrit le début (seek),
 * le relit */
static void write_test(void)
{
	long fd = sys_open(p_new, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		goto fail;
	for (long i = 0; i < 5000; i++) /* 5000 lignes : plusieurs clusters */
		if (sys_write(fd, line, LINE_LEN) != (long)LINE_LEN)
			goto fail;
	sys_seek(fd, 0, 0); /* réécriture du début */
	sys_write(fd, first, FIRST_LEN);
	sys_close(fd);
	if (sys_stat(p_new, statbuf)) /* taille attendue */
		goto fail;
	ak_puts("usbtest : fichier écrit, taille ");
	print_dec(statbuf[0]);
	ak_puts("\n");
	if (statbuf[0] != 5000 * (long)LINE_LEN)
		goto fail;
	fd = sys_open(p_new, O_READ); /* relecture du début */
	if (fd < 0)
		goto fail;
	long n = sys_read(fd, buf, LINE_LEN * 2);
	buf[n] = 0; /* (comme l'asm : sans vérifier n) */
	ak_puts("usbtest : relu : ");
	ak_puts(buf);
	sys_close(fd);
	list_dir(p_docs);
	return;
fail:
	fail();
}

/* mkdir, fichier dedans, unlink refusé sur un répertoire non vide, puis
 * nettoyage */
static void dir_test(void)
{
	if (sys_mkdir(p_dir))
		goto fail;
	if (sys_chdir(p_dir)) /* chemins relatifs */
		goto fail;
	long fd = sys_open(p_rel, O_WRITE | O_CREATE);
	if (fd < 0)
		goto fail;
	sys_write(fd, first, FIRST_LEN);
	sys_close(fd);
	list_dir(".");
	if (sys_unlink(p_dir) != -39) /* non vide : doit échouer (-39) */
		goto fail;
	if (sys_unlink(p_rel))
		goto fail;
	sys_chdir("..");
	if (sys_unlink(p_dir))
		goto fail;
	list_dir(p_sd);
	return;
fail:
	fail();
}

/* écrit 1 Mo (16 x 64 Ko, octet[i] = (i*13) & 255), chronométré, relit et
 * vérifie */
static void bulk_test(void)
{
	for (u64 i = 0; i < 65536; i++)
		big[i] = (u8)(i * 13);
	long t0 = sys_time_ms();
	long fd = sys_open(p_bulk, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		goto fail;
	for (int k = 0; k < 16; k++)
		if (sys_write(fd, big, 65536) != 65536)
			goto fail;
	sys_close(fd);
	long dt = sys_time_ms() - t0;
	ak_puts("usbtest : 1 Mo écrit en ");
	print_dec(dt);
	ak_puts(" ms\n");
	fd = sys_open(p_bulk, O_READ); /* relecture : 1 Mo exact ? */
	if (fd < 0)
		goto fail;
	u64 pos = 0, bad = 0;
	for (;;) {
		long n = sys_read(fd, big, 65536);
		if (n <= 0)
			break;
		for (u64 i = 0; i < (u64)n; i++)
			if ((u8)(((pos + i) & 0xFFFF) * 13) != big[i])
				bad++;
		pos += (u64)n;
	}
	sys_close(fd);
	ak_puts("usbtest : gros2.bin relu ");
	print_dec((long)pos);
	ak_puts(" octets, erreurs ");
	print_dec((long)bad);
	ak_puts("\n");
	if (pos != 1048576 || bad)
		goto fail;
	return;
fail:
	fail();
}

/* « usbtest : durée n ms » */
static void print_ms(long ms)
{
	ak_puts("usbtest : durée ");
	print_dec(ms);
	ak_puts(" ms\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long fd;
	for (int i = 100;; ) { /* la clé arrive après l'énumération USB */
		fd = sys_open(p_sd, O_READ);
		if (fd >= 0)
			break;
		sys_sleep(100);
		if (!--i) {
			ak_puts("usbtest : pas de clé USB (/usb)\n");
			_exit(1);
		}
	}
	sys_close(fd);
	list_dir("/media/usb1/"); /* seconde partition (exFAT) */
	list_dir("/");      /* listes */
	list_dir(p_sd);
	list_dir(p_docs);
	ak_puts("--- /media/usb0/README.TXT :\n"); /* lecture d'un petit fichier */
	cat_file("/media/usb0/README.TXT");
	long t0 = sys_time_ms();
	big_check(); /* 300 000 octets vérifiés */
	print_ms(sys_time_ms() - t0);
	t0 = sys_time_ms();
	write_test();
	print_ms(sys_time_ms() - t0);
	bulk_test(); /* 1 Mo par blocs de 64 Ko */
	dir_test();
	ak_puts("usbtest : terminé\n");
	_exit((int)errors);
}
