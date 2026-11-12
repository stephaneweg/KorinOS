/* ramtest - essai des volumes en espace utilisateur (docs/MICRO-NOYAU.md,
 * section 4), comme ramtest.asm : « ramtest [VOLUME] » (RAM0 par défaut ;
 * IMG0 : fat32 sur une image). Répertoire, écriture et relecture par
 * morceaux, seek, readdir, stat, chdir / getcwd, unlink ; puis la vitesse
 * d'écriture et de lecture de 256 Ko, sur ce volume et sur SD0: (SD0:/gros.bin,
 * effacé ensuite).
 * « ramtest SD9 » (fat32 sur la carte, en lecture seule) : compare un fichier
 * lu par les services et par le noyau (SD0:), et vérifie le refus d'écrire.
 * Le volume est la ligne brute du noyau (krt_line, x0 dans l'asm), coupée au
 * premier « : » ou caractère <= espace ; une ligne vide donne « :/docs »
 * (comme l'asm). Avant les essais (sauf SD9), 2 s d'attente (lancé au
 * démarrage : le temps que vfs monte RAM0:). */
#include "korinrt.h"

#define N 3000          /* octets du fichier d'essai */
#define CMP_MAX 65536   /* comparaison (SD9) */
#define BIG 262144      /* fichier de vitesse */

static long info[2];
static struct dirent dent;
static char p_dir[64], p_file[64], p_big[64];
static u8 buf[CMP_MAX] __attribute__((aligned(16)));
static u8 back[CMP_MAX] __attribute__((aligned(16)));
static u8 big[4096] __attribute__((aligned(16)));

static long sys(long nr, long a, long b, long c)
{
	return ksys(nr, a, b, c, 0, 0, 0);
}

/* destination, volume, suite (« :/docs ») -> chemin complet */
static void make_path(char *d, const char *vol, const char *rest)
{
	while ((u8)*vol > ' ' && *vol != ':')
		*d++ = *vol++;
	while ((*d++ = *rest++))
		;
}

/* valeur, libellé : « ramtest : libellé valeur » */
static void res(long v, const char *label)
{
	ak_puts("ramtest : ");
	ak_puts(label);
	print_dec(v);
	ak_puts("\n");
}

/* chemin, destination (CMP_MAX au plus) -> octets lus */
static long read_all(const char *path, u8 *dst)
{
	long fd = sys(SYS_OPEN, (long)path, O_READ, 0);
	long n = 0;
	if (fd < 0)
		return 0;
	for (;;) {
		long r = sys(SYS_READ, fd, (long)(dst + n), 4096);
		if (r <= 0)
			break;
		n += r;
		if ((u64)n >= CMP_MAX - 4096)
			break;
	}
	sys(SYS_CLOSE, fd, 0, 0);
	return n;
}

static void list_dir(long fd)
{
	while (sys(SYS_READDIR, fd, (long)&dent, 0) == 1) {
		ak_puts("ramtest : entree ");
		ak_puts(dent.name);
		ak_puts("\n");
	}
	sys(SYS_CLOSE, fd, 0, 0);
}

/* SD9 : fat32 (services) contre le noyau, sur le même fichier ; écriture refusée */
NORETURN static void compare(void)
{
	long a = read_all("SD9:/lib/uikit.so", buf);
	long b = read_all("SD0:/lib/uikit.so", back);
	res(a, "SD9:/lib/uikit.so lu par fat32 -> ");
	res(b, "SD0:/lib/uikit.so lu par le noyau -> ");
	long d = 0;
	for (long i = 0; i < a; i++)
		if (buf[i] != back[i])
			d++;
	res(d, "octets differents -> ");
	res(sys(SYS_MKDIR, (long)p_dir, 0, 0), "mkdir sur SD9: (lecture seule) -> ");
	p_dir[4] = 0; /* lister la racine de SD9: */
	long fd = sys(SYS_OPEN, (long)p_dir, O_READ, 0);
	if (fd >= 0)
		list_dir(fd);
	for (;;)
		korin_sys1(SYS_EXIT, 0);
}

/* chemin : écrit puis relit BIG octets (morceaux de 4 Ko), affiche les
 * durées, efface le fichier */
static void speed(const char *path)
{
	long fd = sys(SYS_OPEN, (long)path, O_WRITE | O_CREATE | O_TRUNC, 0);
	if (fd < 0)
		return;
	long t = korin_sys0(SYS_TIME_MS);
	for (long n = 0; n < BIG; n += 4096)
		sys(SYS_WRITE, fd, (long)big, 4096);
	sys(SYS_CLOSE, fd, 0, 0);
	t = korin_sys0(SYS_TIME_MS) - t;
	ak_puts("ramtest : ");
	ak_puts(path);
	ak_puts(" : 256 Ko ecrits en ");
	print_dec(t);
	ak_puts(" ms");
	fd = sys(SYS_OPEN, (long)path, O_READ, 0);
	if (fd < 0)
		return;
	t = korin_sys0(SYS_TIME_MS);
	while (sys(SYS_READ, fd, (long)big, 4096) > 0)
		;
	sys(SYS_CLOSE, fd, 0, 0);
	t = korin_sys0(SYS_TIME_MS) - t;
	ak_puts(", relus en ");
	print_dec(t);
	ak_puts(" ms\n");
	sys(SYS_UNLINK, (long)path, 0, 0);
}

NORETURN static void fail_open(long e)
{
	res(e, "open -> ");
	for (;;)
		korin_sys1(SYS_EXIT, 1);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *vol = krt_line ? krt_line : "RAM0"; /* l'argument, sinon RAM0 */
	make_path(p_dir, vol, ":/docs");
	make_path(p_file, vol, ":/docs/essai.txt");
	make_path(p_big, vol, ":/gros.bin");
	ak_puts("ramtest : dossier d'essai ");
	ak_puts(p_dir);
	ak_puts("\n");
	if (p_dir[0] == 'S' && p_dir[1] == 'D' && p_dir[2] == '9' && p_dir[3] == ':')
		compare();
	korin_sys1(SYS_SLEEP, 2000);
	res(sys(SYS_MKDIR, (long)p_dir, 0, 0), "mkdir /docs -> "); /* 1. mkdir */
	res(sys(SYS_STAT, (long)p_dir, (long)info, 0), "stat /docs -> ");
	for (u32 i = 0; i < N; i++) /* 2. écrire N octets (motif : i x 7 + 3) */
		buf[i] = (u8)((i & 0xFF) * 7 + 3);
	long fd = sys(SYS_OPEN, (long)p_file, O_WRITE | O_CREATE | O_TRUNC, 0);
	if (fd < 0)
		fail_open(fd);
	res(sys(SYS_WRITE, fd, (long)buf, N), "ecrit -> ");
	sys(SYS_CLOSE, fd, 0, 0);
	fd = sys(SYS_OPEN, (long)p_file, O_READ, 0); /* 3. relire par morceaux de 700 */
	if (fd < 0)
		fail_open(fd);
	long got = 0;
	for (;;) {
		long r = sys(SYS_READ, fd, (long)(back + got), 700);
		if (r <= 0)
			break;
		got += r;
	}
	res(got, "relu (morceaux de 700) -> ");
	long d = 0;
	for (long i = 0; i < N; i++)
		if (buf[i] != back[i])
			d++;
	res(d, "octets differents -> ");
	sys(SYS_SEEK, fd, 2990, 0); /* 4. seek à 2990 : 10 octets restent */
	res(sys(SYS_READ, fd, (long)back, 100), "apres seek 2990, lu -> ");
	sys(SYS_CLOSE, fd, 0, 0);
	sys(SYS_STAT, (long)p_file, (long)info, 0); /* 5. stat */
	res(info[0], "stat : taille -> ");
	fd = sys(SYS_OPEN, (long)p_dir, O_READ, 0); /* 6. readdir */
	if (fd < 0)
		fail_open(fd);
	list_dir(fd);
	res(sys(SYS_CHDIR, (long)p_dir, 0, 0), "chdir /docs -> "); /* 7. chdir, getcwd */
	sys(SYS_GETCWD, (long)dent.name, 256, 0);
	ak_puts("ramtest : getcwd -> ");
	ak_puts(dent.name);
	ak_puts("\n");
	res(sys(SYS_UNLINK, (long)p_file, 0, 0), "unlink du fichier -> "); /* 8. unlink */
	res(sys(SYS_UNLINK, (long)p_dir, 0, 0), "unlink du repertoire -> ");
	res(sys(SYS_STAT, (long)p_file, (long)info, 0), "stat apres unlink -> ");
	speed(p_big); /* 9. vitesse */
	speed("SD0:/gros.bin");
	return 0;
}
