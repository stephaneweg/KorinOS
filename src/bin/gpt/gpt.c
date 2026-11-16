/* gpt - lire et écrire une table de partitions GPT (src/lib/gpt, docs/etude/INSTALLATEUR.md §3.4) sur une
 * image disque : les essais de l'installateur, avant qu'il n'écrive un vrai disque par le plan validé (nvme -I).
 *
 *   gpt show IMAGE                      la table (disque, zone utile, partitions, espace libre)
 *   gpt new IMAGE                       une table neuve et vide (efface les entrées)
 *   gpt add IMAGE TYPE TAILLE NOM       une partition dans le plus grand espace libre, aligné sur 1 Mio ;
 *                                       TYPE : efi, basic, linux, msr, recovery, ou un GUID ;
 *                                       TAILLE : en Mio, ou « rest » (tout l'espace libre)
 *   gpt del IMAGE N                     retire l'entrée N
 *   gpt disktest blk.DISQUE ext4read    (essai) la partition 3, déjà en ext4 (installkey.py --disktest ext4read) :
 *                                       montée par ext4fs, tout l'arbre relu (« E4 F chemin taille crc32 »)
 *   gpt disktest blk.DISQUE [FS]        (essai de la clé d'installation, uid 0) le mode installation du pilote :
 *                                       écriture refusée avant le plan, plan touchant l'EFI refusé, plan sur
 *                                       l'espace libre accepté, nouvelle partition écrite et relue ; au journal
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

static void rnd(uint8_t b[16]);

/* ---- un disque par le protocole blk (src/svc/common/blkserv.h) */

#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_WRITE 4
#define BLK_PLAN 9
#define BLK_RESCAN 10
#define PLAN_WIPE 1
#define BUF_VA_BASE 0x6800000000ull
#define BUF_SLOT 0x4000000ull
#define DBUF 65536

static long dchan = -1;
static u8 *dbuf;
static u64 dm[MSG_SIZE / 8];

static long blk(long ch, long op, long a, long b, long c)
{
	for (int i = 0; i < MSG_SIZE / 8; i++)
		dm[i] = 0;
	dm[0] = (u64)op;
	dm[1] = (u64)a;
	dm[2] = (u64)b;
	dm[3] = (u64)c;
	long r = korin_sys2(SYS_SVC_CALL, ch, dm);
	return r < 0 ? r : (long)dm[0];
}

/* le service blk ouvert, son tampon partagé attaché -> 0 ; *sectors : sa taille */
static long disk_open(const char *svc, long *ch, u64 *sectors)
{
	*ch = korin_sys2(SYS_SVC_OPEN, svc, 3000);
	if (*ch < 0)
		return *ch;
	long r = blk(*ch, BLK_INFO, 0, 0, 0);
	if (r < 0)
		return r;
	*sectors = dm[2];
	long pid = (long)dm[4];
	static long id = -1;
	if (id < 0) {
		id = korin_sys1(SYS_BUF_CREATE, DBUF);
		if (id < 0)
			return id;
		dbuf = (u8 *)(BUF_VA_BASE + (u64)id * BUF_SLOT);
	}
	korin_sys2(SYS_BUF_GRANT, id, pid);
	return blk(*ch, BLK_ATTACH, id, DBUF, 0);
}

static long disk_rw(void *ctx, u64 sector, u64 count, void *buf, int write)
{
	long ch = ctx ? *(long *)ctx : dchan;
	while (count) {
		u64 k = count > DBUF / GPT_SECTOR ? DBUF / GPT_SECTOR : count;
		if (write)
			memcpy(dbuf, buf, k * GPT_SECTOR);
		long r = blk(ch, write ? BLK_WRITE : BLK_READ, (long)sector, (long)k, (long)dbuf);
		if (r < 0)
			return r;
		if (!write)
			memcpy(buf, dbuf, k * GPT_SECTOR);
		buf = (u8 *)buf + k * GPT_SECTOR;
		sector += k;
		count -= k;
	}
	return 0;
}

static long disk_rd(void *ctx, uint64_t s, uint64_t n, void *b) { return disk_rw(ctx, s, n, b, 0); }
static long disk_wr(void *ctx, uint64_t s, uint64_t n, void *b) { return disk_rw(ctx, s, n, b, 1); }

static void jn(const char *what, long v)
{
	char t[24], o[32];
	int k = 0, j = 0;
	u64 x = (u64)(v < 0 ? -v : v);
	do
		t[k++] = (char)('0' + x % 10);
	while ((x /= 10));
	if (v < 0)
		o[j++] = '-';
	while (k)
		o[j++] = t[--k];
	o[j] = 0;
	ak_puts("gpt disktest : ");
	ak_puts(what);
	ak_puts(" -> ");
	ak_puts(o);
	ak_puts("\n");
}

static long plan_send(u64 flags, u32 crc, u64 a, u64 b)
{
	for (int i = 0; i < MSG_SIZE / 8; i++)
		dm[i] = 0;
	dm[0] = BLK_PLAN;
	dm[1] = flags;
	dm[2] = crc;
	dm[3] = 1;
	dm[6] = a; /* (données : MSG_DATA = 48) */
	dm[7] = b;
	((u32 *)dm)[10] = 16;
	long r = korin_sys2(SYS_SVC_CALL, dchan, dm);
	return r < 0 ? r : (long)dm[0];
}

/* le scénario du mode installation (docs/etude/INSTALLATEUR.md §8) ; chaque ligne au journal */
/* (disktest, exFAT ou FAT32) le volume monté par fatfs : un dossier, un petit fichier, une copie de /bin/bash ;
 * démonté (écrit), remonté, relu et comparé, démonté (jugé ensuite par fsck.exfat) */
static u8 cbuf[65536], cbuf2[65536];

static long copier(const char *from, const char *to)
{
	long a = sys_open(from, O_READ), b = sys_open(to, O_WRITE | O_CREATE | O_TRUNC);
	long total = 0, n;
	if (a < 0 || b < 0)
		return a < 0 ? a : b;
	while ((n = sys_read(a, cbuf, sizeof cbuf)) > 0) {
		if (sys_write(b, cbuf, n) != n)
			return -5;
		total += n;
	}
	sys_close(a);
	sys_close(b);
	return total;
}

static long comparer(const char *x, const char *y)
{
	long a = sys_open(x, O_READ), b = sys_open(y, O_READ);
	if (a < 0 || b < 0)
		return a < 0 ? a : b;
	long total = 0, n, m;
	for (;;) {
		n = sys_read(a, cbuf, sizeof cbuf);
		m = sys_read(b, cbuf2, sizeof cbuf2);
		if (n != m || (n > 0 && memcmp(cbuf, cbuf2, n)))
			return -1;
		if (n <= 0)
			break;
		total += n;
	}
	sys_close(a);
	sys_close(b);
	return total;
}

static void essai_fatfs(const char *part)
{
	jn("montage par fatfs (attendu : 0)", vu_mount("kt", part, "fatfs"));
	jn("dossier (attendu : 0)", sys_mkdir("/media/kt/Dossier"));
	long f = sys_open("/media/kt/petit.txt", O_WRITE | O_CREATE | O_TRUNC);
	jn("petit fichier (ouverture)", f);
	if (f >= 0) {
		jn("petit fichier (octets)", sys_write(f, "bonjour exFAT\n", 14));
		sys_close(f);
	}
	jn("copie de /bin/bash (octets)", copier("/bin/bash", "/media/kt/Dossier/bash"));
	jn("sync (attendu : 0)", vu_sync("kt"));
	jn("demontage (attendu : 0)", vu_unmount("kt"));
	jn("remontage (attendu : 0)", vu_mount("kt", part, "fatfs"));
	jn("relecture de la copie (octets, identique)", comparer("/bin/bash", "/media/kt/Dossier/bash"));
	jn("demontage (attendu : 0)", vu_unmount("kt"));
}

/* ---- (disktest ext4read) l'arbre d'un volume monté : une ligne par entrée, au journal */

static u32 crc_tab[256];

static u32 crc32(u32 c, const u8 *p, long n)
{
	if (!crc_tab[1])
		for (u32 i = 0; i < 256; i++) {
			u32 x = i;
			for (int k = 0; k < 8; k++)
				x = (x >> 1) ^ (0xEDB88320u & -(x & 1));
			crc_tab[i] = x;
		}
	c = ~c;
	while (n-- > 0)
		c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return ~c;
}

static char hexd(u32 v)
{
	return (char)(v < 10 ? '0' + v : 'a' + v - 10);
}

static long arbre_n, arbre_err;

static void arbre(const char *dir, const char *rel)
{
	long fd = sys_open(dir, O_READ);
	if (fd < 0) {
		arbre_err++;
		return;
	}
	struct dirent e;
	static char line[700];
	while (sys_readdir(fd, &e) > 0) {
		char p[600], r[600];
		p[0] = r[0] = 0;
		str_cat(str_cat(str_cat(p, dir), "/"), e.name);
		if (rel[0])
			str_cat(str_cat(str_cat(r, rel), "/"), e.name);
		else
			str_cat(r, e.name);
		line[0] = 0;
		if (e.attr & ATTR_DIR) {
			str_cat(str_cat(str_cat(line, "E4 D "), r), "\n");
			ak_puts(line);
			arbre(p, r);
		} else {
			long f = sys_open(p, O_READ), n, tot = 0;
			u32 c = 0;
			if (f < 0) {
				arbre_err++;
				continue;
			}
			while ((n = sys_read(f, cbuf, sizeof cbuf)) > 0) {
				c = crc32(c, cbuf, n);
				tot += n;
			}
			sys_close(f);
			char num[24], hx[9];
			int k = 0;
			long t = tot;
			char tmp[24];
			do
				tmp[k++] = (char)('0' + t % 10);
			while ((t /= 10));
			for (int j = 0; j < k; j++)
				num[j] = tmp[k - 1 - j];
			num[k] = 0;
			for (int j = 0; j < 8; j++)
				hx[j] = hexd((c >> (28 - 4 * j)) & 15);
			hx[8] = 0;
			str_cat(str_cat(str_cat(str_cat(str_cat(str_cat(str_cat(line, "E4 F "), r), " "), num), " "), hx), "\n");
			ak_puts(line);
		}
		arbre_n++;
	}
	sys_close(fd);
}

static int disktest_ext4read(const char *svc)
{
	char p[40];
	p[0] = 0;
	str_cat(str_cat(p, svc), "p3");
	jn("montage par ext4fs (attendu : 0)", vu_mount("kt", p, "ext4fs"));
	jn("info (attendu : 0)", vu_info("kt"));
	arbre("/media/kt", "");
	jn("entrees relues", arbre_n);
	jn("erreurs (attendu : 0)", arbre_err);
	long f = sys_open("/media/kt/nouveau.txt", O_WRITE | O_CREATE);
	jn("ecriture (attendu : refus, -30)", f);
	if (f >= 0)
		sys_close(f);
	jn("chemin sans la casse (attendu : 6)", sys_open("/media/kt/CASSE/fichier.txt", O_READ) >= 0 ? 6 : -1);
	jn("demontage (attendu : 0)", vu_unmount("kt"));
	ak_puts("gpt disktest : fin\n");
	return 0;
}

static int disktest(const char *svc, const char *fs)
{
	if (str_eq(fs, "ext4read"))
		return disktest_ext4read(svc);
	u64 sectors;
	long r = disk_open(svc, &dchan, &sectors);
	jn("ouverture du disque", r);
	if (r < 0)
		return 1;
	r = gpt_read(&g, disk_rd, 0, sectors);
	jn("lecture de la GPT", r);
	if (r)
		return 1;
	static u8 z[GPT_SECTOR];
	jn("ecriture du secteur 0 avant le plan (attendu : refus)", disk_rw(0, 0, 1, z, 1));
	int efi = -1;
	for (int i = 0; i < GPT_ENTRIES; i++)
		if (gpt_used(&g.e[i]) && !memcmp(g.e[i].type, GPT_T_EFI, 16))
			efi = i;
	if (efi >= 0)
		jn("plan sur la partition EFI (attendu : refus)", plan_send(0, g.header_crc, g.e[efi].first, g.e[efi].last));
	jn("plan sur une table changee (attendu : refus)", plan_send(0, g.header_crc ^ 1, 34, 40));
	u64 a, b;
	if (!gpt_free_space(&g, &a, &b)) {
		jn("pas d'espace libre", 0);
		return 1;
	}
	if ((b + 1) / 2048 * 2048 - 1 > a) /* la fin alignée sur 1 Mio, elle aussi */
		b = (b + 1) / 2048 * 2048 - 1;
	jn("plan sur l'espace libre (attendu : 0)", plan_send(0, g.header_crc, a, b));
	uint8_t guid[16];
	rnd(guid);
	int ext4 = str_eq(fs, "ext4"); /* ext4 : « Linux filesystem » (Windows ne propose pas de la formater) */
	int n = gpt_add(&g, ext4 ? GPT_T_LINUX : GPT_T_BASIC, a, b, "KORIN OS", guid);
	jn("partition ajoutee (numero)", n);
	jn("ecriture de la GPT", gpt_write(&g, disk_wr, 0));
	jn("relecture de la table (BLK_RESCAN)", blk(dchan, BLK_RESCAN, 0, 0, 0));
	char p[40], d[8];
	str_cat(p, svc);
	int k = 0, m = n;
	char t[4];
	do
		t[k++] = (char)('0' + m % 10);
	while ((m /= 10));
	d[0] = 'p';
	int j = 1;
	while (k)
		d[j++] = t[--k];
	d[j] = 0;
	str_cat(p + str_len(p), d);
	long pc;
	u64 ps;
	r = disk_open(p, &pc, &ps);
	jn("ouverture de la nouvelle partition", r);
	if (r >= 0)
		jn("ecriture dans la nouvelle partition (attendu : 0)", disk_rw(&pc, 0, 1, z, 1));
	{ /* puis mkfs -t FS dessus (jugé ensuite par e2fsck ou fsck.exfat : tools/pc/installkey.py run --write) */
		static char args[80] = "-t ";
		str_cat(args + str_len(args), fs);
		str_cat(args + str_len(args), " -y -L KORINTEST ");
		str_cat(args + str_len(args), p);
		long mp = ksys(SYS_SPAWN, (long)"/sbin/mkfs", (long)args, 0, 0, 0, 0);
		jn("mkfs (pid)", mp);
		long code = mp > 0 ? korin_sys1(SYS_WAIT, mp) : mp;
		jn("mkfs (code de sortie, attendu : 0)", code);
		if (!code && !ext4)
			essai_fatfs(p);
	}
	for (int i = 0; i < GPT_ENTRIES; i++) /* une partition de données d'avant (Windows) : toujours refusée */
		if (gpt_used(&g.e[i]) && !memcmp(g.e[i].type, GPT_T_BASIC, 16) && i + 1 != n) {
			str_cat(p, svc);
			char q[8] = "p";
			q[1] = (char)('0' + (i + 1));
			q[2] = 0;
			str_cat(p + str_len(p), q);
			long wc;
			u64 ws;
			if (disk_open(p, &wc, &ws) >= 0)
				jn("ecriture dans une partition d'avant (attendu : refus)", disk_rw(&wc, 0, 1, z, 1));
			break;
		}
	ak_puts("gpt disktest : fin\n");
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
		out("usage : gpt show|new|add|del IMAGE ... | gpt disktest blk.DISQUE [ext4|exfat|fat32]\n");
		return 1;
	}
	const char *cmd = argv[1];
	if (str_eq(cmd, "disktest"))
		return disktest(argv[2], argc > 3 ? argv[3] : "ext4");
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
