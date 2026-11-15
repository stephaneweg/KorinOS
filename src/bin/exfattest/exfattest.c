/* exfattest - exerce le pilote exFAT (/media/sd1) depuis EL0, comme exfattest.asm :
 * listes, lecture de fichiers contigus (NoFatChain) et chaînés, recherche
 * insensible à la casse (accents compris), écriture, conversion contigu ->
 * chaîne, troncature, mkdir, unlink. Code de sortie = nombre d'échecs.
 * Écrit sur /media/sd1 : ne tourne que sur la partition de build/sd.img, reconnue à
 * son gros.bin de 300 000 octets (tools/exfatdata.py), jamais sur la partition
 * personnelle : sinon « rien n'est écrit », code 100. Laisse sur /media/sd1 le
 * fichier écrit, A.bin (100 octets) et « Créé par ARMPI/fichier.txt » (pour
 * fsck), comme l'asm. Pas d'argument. */
#include "korinrt.h"

#define PAT_MUL 0x9E3779B1u /* le motif : tools/exfatdata.py le calcule aussi */

static const char p_root[] = "/";
static const char p_sd2[] = "/media/sd1/";
static const char p_dossier[] = "/media/sd1/Dossier \xC3\xA9t\xC3\xA9";
static const char p_lisez[] = "/media/sd1/Lisez-moi exFAT.txt";
static const char p_lisez_up[] = "/media/sd1/LISEZ-MOI EXFAT.TXT";
static const char p_profond_up[] = "/media/sd1/DOSSIER \xC3\x89T\xC3\x89/SOUS/PROFOND.TXT";
static const char p_long[] = "/media/sd1/Dossier \xC3\xA9t\xC3\xA9/Un nom de fichier tr\xC3\xA8s tr\xC3\xA8s long pour "
			     "tester les entr\xC3\xA9" "es exFAT.txt";
static const char p_gros[] = "/media/sd1/gros.bin";
static const char p_chaine[] = "/media/sd1/chaine fragmentee.bin";
static const char p_new[] = "/media/sd1/\xC3\x89" "crit par ARMPI sur exFAT.txt";
static const char p_a[] = "/media/sd1/A.bin";
static const char p_b[] = "/media/sd1/B.bin";
static const char p_dir[] = "/media/sd1/nouveau dossier";
static const char p_dir2[] = "/media/sd1/Cr\xC3\xA9\xC3\xA9 par ARMPI";
static const char p_in2[] = "/media/sd1/Cr\xC3\xA9\xC3\xA9 par ARMPI/fichier.txt";
static const char p_rel[] = "dedans.txt";
static const char first[] = "PREMIERE LIGNE REECRITE PAR SEEK\n";
static const char line[] = "Ligne de test ecrite par exfattest depuis EL0.\n";
#define FIRST_LEN (sizeof first - 1)
#define LINE_LEN (sizeof line - 1)

static long errors;
static long statbuf[2];
static struct dirent dirent;
static char buf[1024];

static void fail(void)
{
	errors++;
	ak_puts("exfattest : *** ECHEC ***\n");
}

/* chemin : le répertoire affiché */
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

/* chemin : le fichier affiché (par 255 octets, arrêt au premier nul de chaque) */
static void cat_file(const char *path)
{
	ak_puts("--- cat ");
	ak_puts(path);
	ak_puts("\n");
	long fd = sys_open(path, O_READ);
	if (fd < 0) {
		fail();
		return;
	}
	long r;
	while ((r = sys_read(fd, buf, 255)) > 0) {
		buf[r] = 0;
		ak_puts(buf);
	}
	sys_close(fd);
}

/* octet[i] = les 8 bits hauts de (i*a + b) * PAT_MUL sur 32 bits */
static u8 pat(u64 i, u64 a, u64 b)
{
	return (u8)(((u32)(i * a + b) * PAT_MUL) >> 24);
}

/* chemin, a, b, taille attendue */
static void check_pattern(const char *path, u64 a, u64 b, u64 size)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0) {
		fail();
		return;
	}
	u64 pos = 0, bad = 0;
	long r;
	while ((r = sys_read(fd, buf, 1000)) > 0) {
		for (long i = 0; i < r; i++)
			bad += pat(pos + (u64)i, a, b) != (u8)buf[i];
		pos += (u64)r;
	}
	sys_close(fd);
	ak_puts("exfattest : ");
	ak_puts(path);
	ak_puts(" : ");
	print_dec((long)pos);
	ak_puts(" octets, erreurs ");
	print_dec((long)bad);
	ak_puts("\n");
	if (pos != size || bad)
		fail();
}

/* fichier à nom long accentué, 5000 lignes (plusieurs clusters), réécriture
 * du début par seek, vérification de taille et relecture */
static void write_test(void)
{
	long fd = sys_open(p_new, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0) {
		fail();
		return;
	}
	for (int i = 0; i < 5000; i++)
		if (sys_write(fd, line, LINE_LEN) != (long)LINE_LEN) {
			fail(); /* (fd laissé ouvert, comme l'asm) */
			return;
		}
	sys_seek(fd, 0, 0);
	sys_write(fd, first, FIRST_LEN);
	sys_close(fd);
	if (sys_stat(p_new, statbuf)) {
		fail();
		return;
	}
	ak_puts("exfattest : fichier \xC3\xA9" "crit, taille ");
	print_dec(statbuf[0]);
	ak_puts("\n");
	if ((u64)statbuf[0] != 5000 * LINE_LEN) {
		fail();
		return;
	}
	fd = sys_open(p_new, O_READ);
	if (fd < 0) {
		fail();
		return;
	}
	long r = sys_read(fd, buf, FIRST_LEN + 20);
	buf[r] = 0; /* (comme l'asm : r < 0 n'est pas vérifié) */
	ak_puts("exfattest : relu : ");
	ak_puts(buf);
	ak_puts("\n");
	sys_close(fd);
}

/* fd, premier octet (rang), nombre : le motif de check_pattern, a = 3, b = 0 */
static void write_pattern(long fd, u64 from, u64 n)
{
	while (n) {
		u64 k = n < 1000 ? n : 1000;
		for (u64 i = 0; i < k; i++)
			buf[i] = (char)pat(from + i, 3, 0);
		if (sys_write(fd, buf, k) != (long)k) {
			fail();
			return;
		}
		from += k;
		n -= k;
	}
}

/* A (1 cluster) puis B collé derrière, puis A grandit : plus de place
 * contiguë -> conversion en chaîne FAT. Vérification, troncature, nettoyage. */
static void convert_test(void)
{
	long fd = sys_open(p_a, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		goto bad;
	write_pattern(fd, 0, 4000);
	sys_close(fd);
	fd = sys_open(p_b, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		goto bad;
	write_pattern(fd, 0, 6000);
	sys_close(fd);
	fd = sys_open(p_a, O_WRITE | O_APPEND); /* A += 20000 octets */
	if (fd < 0)
		goto bad;
	write_pattern(fd, 4000, 20000);
	sys_close(fd);
	check_pattern(p_a, 3, 0, 24000);
	check_pattern(p_b, 3, 0, 6000);
	fd = sys_open(p_a, O_WRITE | O_TRUNC); /* troncature puis 100 octets */
	if (fd < 0)
		goto bad;
	write_pattern(fd, 0, 100);
	sys_close(fd);
	check_pattern(p_a, 3, 0, 100);
	check_pattern(p_b, 3, 0, 6000); /* la troncature ne lui a rien pris */
	if (sys_unlink(p_b))
		goto bad;
	return;
bad:
	fail();
}

/* mkdir, fichier dedans (chemin relatif), unlink refusé sur répertoire non vide */
static void dir_test(void)
{
	if (sys_mkdir(p_dir) || sys_chdir(p_dir))
		goto bad;
	long fd = sys_open(p_rel, O_WRITE | O_CREATE);
	if (fd < 0)
		goto bad;
	sys_write(fd, first, FIRST_LEN);
	sys_close(fd);
	list_dir(".");
	cat_file(p_rel);
	if (sys_unlink(p_dir) != -39) /* non vide */
		goto bad;
	if (sys_unlink(p_rel))
		goto bad;
	sys_chdir("..");
	if (sys_unlink(p_dir) || sys_mkdir(p_dir2)) /* un répertoire conservé, pour fsck */
		goto bad;
	fd = sys_open(p_in2, O_WRITE | O_CREATE);
	if (fd < 0)
		goto bad;
	sys_write(fd, line, LINE_LEN);
	sys_close(fd);
	return;
bad:
	fail();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (sys_stat(p_gros, statbuf) || statbuf[0] != 300000) { /* la partition d'essai ? */
		ak_puts("exfattest : /media/sd1 n'est pas la partition d'essai, rien n'est ecrit\n");
		_exit(100);
	}
	list_dir(p_root);
	list_dir(p_sd2);
	list_dir(p_dossier);
	cat_file(p_lisez); /* lecture simple */
	cat_file(p_lisez_up); /* même fichier, autre casse */
	cat_file(p_profond_up); /* « DOSSIER ÉTÉ/SOUS/PROFOND.TXT » */
	cat_file(p_long);
	check_pattern(p_gros, 7, 0, 300000); /* contigu */
	check_pattern(p_chaine, 13, 5, 50000); /* chaîne FAT fragmentée */
	write_test();
	convert_test();
	dir_test();
	list_dir(p_sd2);
	ak_puts("exfattest : termin\xC3\xA9, \xC3\xA9" "checs = ");
	print_dec(errors);
	ak_puts("\n");
	_exit((int)errors);
}
