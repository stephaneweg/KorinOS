/* uname - quel système c'est : uname [-a] [-s] [-n] [-r] [-v] [-m] [-p]
 * (docs/OUTILS.md, d'après uname.c d'Onyx), comme uname.asm :
 *   -s  le nom du système (Korin) : par défaut
 *   -n  le nom de la machine (hostname = de /etc/system.ini ; korin sinon)
 *   -r  la version du système installé (le paquet armpi, qui porte le noyau : /var/pkg/db/armpi.ini)
 *   -v  le niveau d'ABI des kits (/etc/pkg/abi) et la date du noyau (/kernel8.img)
 *   -m  la machine (aarch64 ; x86_64 sur PC)
 *   -p  le paquet du système et sa version (armpi 2026.11.19)
 *   -a  tout, puis les cœurs et la mémoire
 * Les options se groupent (-sn) ; tout argument qui ne commence pas par « - », ou une lettre
 * inconnue : usage et code 1. « ? » pour une valeur absente. */
#include "korinrt.h"

#define FILE_CAP 4096
#define VAL_CAP 128

#if defined(__x86_64__)
#define MACHINE "x86_64"
#else
#define MACHINE "aarch64"
#endif

static int first = 1;
static char version[VAL_CAP], val[VAL_CAP], fbuf[FILE_CAP];
static long msg[MSG_SIZE / 8];
static u64 info[10];

/* un blanc avant chaque champ sauf le premier */
static void sep(void)
{
	if (!first)
		out_putc(' ');
	first = 0;
}

static void put_version(void)
{
	out_puts(version[0] ? version : "?");
}

/* 0..99 : deux chiffres */
static void two(u64 v)
{
	out_putc((int)('0' + v / 10));
	out_putc((int)('0' + v % 10));
}

/* secondes (heure locale) : AAAA-MM-JJ HH:MM */
static void put_date(u64 secs)
{
	struct civil c;
	civil_from_secs(secs, &c);
	out_putn(c.year);
	out_putc('-');
	two(c.month);
	out_putc('-');
	two(c.day);
	out_putc(' ');
	two(c.hour);
	out_putc(':');
	two(c.min);
}

/* chemin, tampon, capacité -> octets lus (0 : rien), tampon terminé par 0 */
static u64 read_file(const char *path, char *buf, u64 cap)
{
	u64 n = 0;
	buf[0] = 0;
	long fd = sys_open(path, O_READ);
	if (fd >= 0) {
		while (cap - 1 - n) {
			long r = sys_read(fd, buf + n, cap - 1 - n);
			if (r <= 0)
				break;
			n += (u64)r;
		}
		sys_close(fd);
	}
	buf[n] = 0;
	return n;
}

/* texte : coupé à la première fin de ligne, blancs de la fin retirés */
static void trim(char *s)
{
	char *p = s;
	while (*p && *p != 10 && *p != 13)
		p++;
	*p = 0;
	while (p > s && (p[-1] == ' ' || p[-1] == 9))
		*--p = 0;
}

/* lignes, clé, séparateur, sortie (VAL_CAP) : la valeur de « clé = valeur » (blancs autour), ""
 * si absente */
static void value_of(const char *t, const char *key, char sepc, char *out)
{
	out[0] = 0;
	while (*t) {
		const char *p = t;
		while (*p == ' ' || *p == 9)
			p++;
		size_t k = 0;
		while (key[k] && p[k] == key[k])
			k++;
		if (!key[k]) {
			p += k;
			while (*p == ' ' || *p == 9)
				p++;
			if (*p == sepc) {
				p++;
				while (*p == ' ' || *p == 9)
					p++;
				size_t n = 0;
				while (p[n] && p[n] != 10 && p[n] != 13 && n < VAL_CAP - 1) {
					out[n] = p[n];
					n++;
				}
				out[n] = 0;
				trim(out);
				return;
			}
		}
		for (;;) { /* ligne suivante */
			char c = *t;
			if (!c)
				return;
			t++;
			if (c == 10)
				break;
		}
	}
}

static void file_value(const char *path, const char *key, char sepc, char *out)
{
	out[0] = 0;
	read_file(path, fbuf, FILE_CAP);
	value_of(fbuf, key, sepc, out);
}

int main(int argc, char **argv)
{
	static const char letters[] = "snrvmpa";
	int bits = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] != '-')
			goto usage;
		for (const char *o = a + 1; *o; o++) {
			int k = 0;
			while (letters[k] && letters[k] != *o)
				k++;
			if (!letters[k])
				goto usage;
			bits |= 1 << k;
		}
	}
	if (bits & 64)
		bits |= 63;
	if (!(bits & 63))
		bits = 1;
	file_value("/var/pkg/db/armpi.ini", "version", '=', version);
	if (bits & 1) {
		sep();
		out_puts("Korin");
	}
	if (bits & 2) {
		sep();
		file_value("/etc/system.ini", "hostname", '=', val);
		out_puts(val[0] ? val : "korin");
	}
	if (bits & 4) {
		sep();
		put_version();
	}
	if (bits & 8) {
		sep();
		out_puts("abi ");
		read_file("/etc/pkg/abi", val, VAL_CAP);
		trim(val);
		out_puts(val[0] ? val : "?");
		vfs_msg(msg, V_STAT, 0, 0, "/kernel8.img", 0); /* date du noyau (V_STAT : d) */
		if (vfs_call(msg) >= 0 && msg[MSG_D / 8]) {
			u64 d = (u64)msg[MSG_D / 8]; /* secondes UTC */
			out_say(", noyau du ");
			put_date(d + (u64)sys_clock(1));
		}
	}
	if (bits & 16) {
		sep();
		out_puts(MACHINE);
	}
	if (bits & 32) {
		sep();
		out_puts("armpi");
		out_putc(' ');
		put_version();
	}
	if ((bits & 64) && korin_sys1(SYS_SYSINFO, info) >= 0) {
		out_puts(" (");
		out_putn(info[0]);
		out_say(" coeurs, ");
		out_putn(info[2] >> 10);
		out_say(" Mo de m\xC3\xA9moire)");
	}
	out_nl();
	return 0;
usage:
	err_msg("usage : uname [-a] [-s] [-n] [-r] [-v] [-m] [-p]");
	return 1;
}
