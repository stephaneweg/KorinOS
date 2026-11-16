/* gpt - lire et écrire une table de partitions GPT (src/lib/gpt, docs/etude/INSTALLATEUR.md §3.4) sur une
 * image disque : les essais de l'installateur, avant qu'il n'écrive un vrai disque par le plan validé (nvme -I).
 *
 *   gpt show IMAGE                      la table (disque, zone utile, partitions, espace libre)
 *   gpt new IMAGE                       une table neuve et vide (efface les entrées)
 *   gpt add IMAGE TYPE TAILLE NOM       une partition dans le plus grand espace libre, aligné sur 1 Mio ;
 *                                       TYPE : efi, basic, linux, msr, recovery, ou un GUID ;
 *                                       TAILLE : en Mio, ou « rest » (tout l'espace libre)
 *   gpt del IMAGE N                     retire l'entrée N
 *
 * Écrire une image demande de pouvoir l'écrire (vfs) ; un disque se partitionne par l'installateur. */
#include "korinrt.h"
#include "../../lib/gpt/gpt.h"

static long fd = -1;
static struct gpt g;

static void out(const char *s)
{
	sys_write(1, s, (u64)str_len(s));
}

static void outn(u64 v)
{
	char t[24], o[24];
	int k = 0;
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10));
	int j = 0;
	while (k)
		o[j++] = t[--k];
	o[j] = 0;
	out(o);
}

static long img_rw(void *ctx, u64 sector, u64 count, void *buf, int write)
{
	(void)ctx;
	if (sys_seek(fd, (long)(sector * GPT_SECTOR), 0) < 0)
		return E_IO;
	long r = write ? sys_write(fd, buf, count * GPT_SECTOR) : sys_read(fd, buf, count * GPT_SECTOR);
	return r == (long)(count * GPT_SECTOR) ? 0 : E_IO;
}

static long img_rd(void *ctx, uint64_t s, uint64_t n, void *b) { return img_rw(ctx, s, n, b, 0); }
static long img_wr(void *ctx, uint64_t s, uint64_t n, void *b) { return img_rw(ctx, s, n, b, 1); }

static const struct { const char *name; const uint8_t *g; } types[] = {
	{"efi", GPT_T_EFI}, {"basic", GPT_T_BASIC}, {"linux", GPT_T_LINUX}, {"msr", GPT_T_MSR}, {"recovery", GPT_T_RECOVERY}};

static const char *type_name(const uint8_t *t)
{
	for (u64 i = 0; i < sizeof types / sizeof types[0]; i++)
		if (!memcmp(t, types[i].g, 16))
			return types[i].name;
	return 0;
}

static void show(void)
{
	char s[40];
	gpt_guid_text(g.disk_guid, s);
	out("disque ");
	out(s);
	out(" : ");
	outn(g.disk_sectors);
	out(" secteurs (");
	outn(g.disk_sectors / 2048);
	out(" Mio), zone utile ");
	outn(g.first_usable);
	out("..");
	outn(g.last_usable);
	out(g.from_backup ? " (copie de secours : la principale est abimee)\n" : "\n");
	for (int i = 0; i < GPT_ENTRIES; i++) {
		const struct gpt_entry *e = &g.e[i];
		if (!gpt_used(e))
			continue;
		char n[40];
		gpt_name_get(e, n, sizeof n);
		out("  p");
		outn((u64)i + 1);
		out(" ");
		outn(e->first);
		out("..");
		outn(e->last);
		out(" (");
		outn((e->last - e->first + 1) / 2048);
		out(" Mio) ");
		const char *t = type_name(e->type);
		if (t)
			out(t);
		else {
			gpt_guid_text(e->type, s);
			out(s);
		}
		out(" « ");
		out(n);
		out(" » ");
		gpt_guid_text(e->guid, s);
		out(s);
		out("\n");
	}
	u64 a, b;
	if (gpt_free_space(&g, &a, &b)) {
		out("  libre : ");
		outn(a);
		out("..");
		outn(b);
		out(" (");
		outn((b - a + 1) / 2048);
		out(" Mio)\n");
	}
}

static void rnd(uint8_t b[16])
{
	korin_sys2(SYS_GETRANDOM, b, 16);
	b[7] = (uint8_t)((b[7] & 0x0F) | 0x40); /* (version 4, petit-boutiste : l'octet 7) */
	b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		out("usage : gpt show|new|add|del IMAGE ...\n");
		return 1;
	}
	const char *cmd = argv[1];
	int write = !str_eq(cmd, "show");
	fd = sys_open(argv[2], write ? O_READ | O_WRITE : O_READ);
	if (fd < 0) {
		out("gpt : image illisible\n");
		return 1;
	}
	long size = sys_seek(fd, 0, 2);
	if (size < 1024 * 1024) {
		out("gpt : image trop petite\n");
		return 1;
	}
	u64 sectors = (u64)size / GPT_SECTOR;
	if (str_eq(cmd, "new")) {
		uint8_t guid[16];
		rnd(guid);
		gpt_init(&g, sectors, guid);
	} else {
		long r = gpt_read(&g, img_rd, 0, sectors);
		if (r) {
			out(r == -2 ? "gpt : pas de table GPT\n" : "gpt : table abimee (sommes fausses)\n");
			return 1;
		}
	}
	if (str_eq(cmd, "add")) {
		if (argc < 6) {
			out("usage : gpt add IMAGE TYPE TAILLE|rest NOM\n");
			return 1;
		}
		uint8_t t[16];
		const uint8_t *tp = 0;
		for (u64 i = 0; i < sizeof types / sizeof types[0]; i++)
			if (str_eq(argv[3], types[i].name))
				tp = types[i].g;
		if (!tp) {
			if (gpt_guid_parse(argv[3], t)) {
				out("gpt : type inconnu\n");
				return 1;
			}
			tp = t;
		}
		u64 a, b;
		if (!gpt_free_space(&g, &a, &b)) {
			out("gpt : plus d'espace libre\n");
			return 1;
		}
		if (!str_eq(argv[4], "rest")) {
			u64 want = parse_dec(argv[4]) * 2048;
			if (!want || want > b - a + 1) {
				out("gpt : taille impossible\n");
				return 1;
			}
			b = a + want - 1;
		}
		uint8_t guid[16];
		rnd(guid);
		int n = gpt_add(&g, tp, a, b, argv[5], guid);
		if (n < 0) {
			out("gpt : partition refusee\n");
			return 1;
		}
		out("p");
		outn((u64)n);
		out(" ajoutee\n");
	} else if (str_eq(cmd, "del")) {
		if (argc < 4)
			return 1;
		gpt_del(&g, (int)parse_dec(argv[3]));
	} else if (!str_eq(cmd, "show") && !str_eq(cmd, "new")) {
		out("gpt : commande inconnue\n");
		return 1;
	}
	if (write && gpt_write(&g, img_wr, 0)) {
		out("gpt : ecriture impossible\n");
		return 1;
	}
	show();
	sys_close(fd);
	return 0;
}
