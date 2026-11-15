/* acpidump - les tables ACPI du PC dans des fichiers, pour iasl -d (docs/etude/TOUCHPAD-PC.md) :
 *
 *	acpidump [DOSSIER]     (/acpi par défaut)
 *
 * Écrit, comme acpixtract d'ACPICA : rsdp.dat, xsdt.dat (ou rsdt.dat), facp.dat, dsdt.dat, facs.dat,
 * puis chaque table de la XSDT, signature en minuscules ; plusieurs tables d'une même signature
 * (SSDT) : ssdt1.dat, ssdt2.dat… dans l'ordre de la XSDT ; et tables.txt : une ligne par table
 * (fichier, signature, longueur, révision, OEM, table de l'OEM, somme de contrôle juste ou fausse).
 * Rien n'est écrit ailleurs que dans DOSSIER ; les tables sont copiées par le noyau (ACPI_TABLE,
 * lecture seule de la mémoire du micrologiciel), aucune n'est modifiée.
 *
 * Sous Windows ou Linux ensuite : iasl -e ssdt*.dat -d dsdt.dat (le DSDT, avec les noms des SSDT), ou
 * iasl -d sur chaque fichier.
 *
 * Même binaire dans /bin (sans droits : relance /sys/svc/acpidump et recopie ses lignes,
 * src/svc/common/sysfront.c) et dans /sys/svc (ACPI_TABLE demande les droits de /sys).
 * Pour le PC seulement (svc.conf : arch = x86_64). */
#include "sysfront.h"

#define MAXTAB (4u << 20) /* une table (un DSDT de PC fait 100 à 600 Ko) */

static u8 *buf;
static char dir[160];
static long tlog = -1; /* tables.txt */
static int written, bad;

static int sum_ok(const u8 *p, u32 n)
{
	u8 s = 0;
	for (u32 i = 0; i < n; i++)
		s += p[i];
	return s == 0;
}

static void lower4(const char *sig, char *out)
{
	for (int i = 0; i < 4; i++) {
		char c = sig[i];
		out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
		if (!((out[i] >= 'a' && out[i] <= 'z') || (out[i] >= '0' && out[i] <= '9')))
			out[i] = '_';
	}
	out[4] = 0;
}

static void tlog_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void tlog_line(const char *fmt, ...)
{
	char b[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	if (tlog >= 0)
		sys_write(tlog, b, n);
}

/* la table (sig, rang) dans name.dat -> 0, 1 : absente, < 0 erreur */
static int dump(const char *sig, unsigned idx, const char *name)
{
	long n = korin_sys4(SYS_ACPI_TABLE, sig, idx, buf, MAXTAB);
	if (n == E_NOENT)
		return 1;
	if (n < 0) {
		slog("%.4s : refusée (%ld)\n", sig, n);
		return (int)n;
	}
	if (n > (long)MAXTAB) {
		slog("%.4s : %ld octets, trop grande (%u au plus)\n", sig, n, MAXTAB);
		return -1;
	}
	char path[200];
	snprintf(path, sizeof path, "%s/%s.dat", dir, name);
	long fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0) {
		slog("%s : écriture impossible (%ld)\n", path, fd);
		return (int)fd;
	}
	long w = sys_write(fd, buf, (u64)n);
	sys_close(fd);
	if (w != n) {
		slog("%s : écrit %ld sur %ld octets\n", path, w, n);
		return -1;
	}
	written++;
	/* la somme : sur la longueur entière (RSDP : les 20 premiers octets, puis la longueur étendue ; FACS :
	   pas de somme) */
	const char *ck = "-";
	if (!memcmp(sig, "RSDP", 4)) {
		int ok = sum_ok(buf, 20) && (n <= 20 || sum_ok(buf, (u32)n));
		ck = ok ? "juste" : "FAUSSE";
		bad += !ok;
		tlog_line("%-10s RSDP %6ld  rév. %u  OEM %.6s\n", name, n, buf[15], (const char *)buf + 9);
	} else if (!memcmp(sig, "FACS", 4)) {
		tlog_line("%-10s FACS %6ld\n", name, n);
	} else {
		int ok = sum_ok(buf, (u32)n);
		ck = ok ? "juste" : "FAUSSE";
		bad += !ok;
		u32 orev = (u32)(buf[24] | buf[25] << 8 | buf[26] << 16 | (u32)buf[27] << 24);
		tlog_line("%-10s %.4s %6ld  rév. %u  OEM %.6s  %.8s  %08x  somme %s\n", name, (const char *)buf, n,
			  buf[8], (const char *)buf + 10, (const char *)buf + 16, orev, ck);
	}
	slog("%-10s %.4s %7ld octets, somme %s\n", name, !memcmp(sig, "****", 4) ? (const char *)buf : sig, n, ck);
	return 0;
}

int main(int argc, char **argv)
{
	slog_name("acpidump");
	if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
		slog("acpidump [DOSSIER]   les tables ACPI dans DOSSIER/<signature>.dat (/acpi par défaut)\n");
		return 1;
	}
	if (!sysfront_rights())
		return sysfront_run("/sys/svc/acpidump", argc, argv, 0);

	snprintf(dir, sizeof dir, "%s", argc == 2 ? argv[1] : "/acpi");
	long l = (long)strlen(dir);
	while (l > 1 && dir[l - 1] == '/' && dir[l - 2] != ':')
		dir[--l] = 0;
	buf = sys_vm_map(MAXTAB);
	if (!buf) {
		slog("mémoire refusée\n");
		return 1;
	}
	long r = korin_sys4(SYS_ACPI_TABLE, "RSDP", 0, buf, 64);
	if (r == E_NOSYS || r == E_NOENT) {
		slog("pas de tables ACPI ici (%ld) : PC seulement, noyau du 13/11 ou plus récent\n", r);
		return 1;
	}
	long st[2];
	if (sys_stat(dir, st) < 0) {
		long m = sys_mkdir(dir);
		if (m < 0) {
			slog("%s : dossier impossible (%ld)\n", dir, m);
			return 1;
		}
	}
	char path[200];
	snprintf(path, sizeof path, "%s/tables.txt", dir);
	tlog = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);

	dump("RSDP", 0, "rsdp");
	int has_x = dump("XSDT", 0, "xsdt") == 0;
	if (!has_x)
		dump("RSDT", 0, "rsdt");
	dump("DSDT", 0, "dsdt");
	dump("FACS", 0, "facs");

	/* les entrées de la XSDT, dans l'ordre : le nom de chacune d'après le nombre de sa signature */
	static char sigs[256][4];
	unsigned n = 0;
	for (; n < 256; n++) {
		long t = korin_sys4(SYS_ACPI_TABLE, "****", n, buf, 36);
		if (t < 0)
			break;
		memcpy(sigs[n], buf, 4);
	}
	for (unsigned i = 0; i < n; i++) {
		unsigned same = 0, rank = 0;
		for (unsigned j = 0; j < n; j++)
			if (!memcmp(sigs[j], sigs[i], 4)) {
				same++;
				if (j < i)
					rank++;
			}
		char name[16], lo[5];
		lower4(sigs[i], lo);
		if (same > 1)
			snprintf(name, sizeof name, "%s%u", lo, rank + 1);
		else
			snprintf(name, sizeof name, "%s", lo);
		dump("****", i, name);
	}
	if (tlog >= 0)
		sys_close(tlog);
	slog("%d table(s) dans %s (%u dans la XSDT), %d somme(s) fausse(s) ; liste : %s/tables.txt\n", written, dir, n,
	     bad, dir);
	return bad ? 2 : 0;
}
