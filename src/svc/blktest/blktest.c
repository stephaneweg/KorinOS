/* blktest - essai d'un service de blocs (protocole src/inc/blk.inc) sans vfs
 * ni fatfs : sdhci, rtsx, sd.asm, imgblk… (docs/etude/PC-PILOTES.md,
 * « Carte SD »). Il joue le client comme fatfs : tampon partagé (BUF_CREATE,
 * BUF_GRANT au service, BLK_ATTACH), puis BLK_INFO, BLK_READ, BLK_WRITE,
 * BLK_READ_D, BLK_STATS. Tout va au journal du noyau (écran).
 *
 *	blktest [DISQUE] info          le disque et ses partitions (blk.sd0, blk.sd0p1…)
 *	blktest [DISQUE] read LBA [N]  N secteurs (défaut 1) : somme, début en hexa
 *	blktest [DISQUE] all           lectures de toutes tailles, comparées entre elles,
 *	                               partitions, lecture directe, débit (sans écrire)
 *	blktest [DISQUE] all -w        de plus : écritures (1 à 2048 secteurs) relues, à la
 *	                               fin du disque, contenu d'origine remis
 *	                               IMAGE D'ESSAI SEULEMENT (écrit sur la carte)
 *	blktest [DISQUE] safe -w       écriture non destructive dans l'espace libre avant la
 *	                               première partition (sauvé, écrit, relu, remis, relu)
 *	blktest [DISQUE] deny -w       une écriture doit être refusée : début, milieu et fin du
 *	                               périphérique (motif écrit si elle passe : IMAGE D'ESSAI
 *	                               SEULEMENT), contenu relu identique
 *	blktest [DISQUE] log           le journal du noyau dans KORINLOG.TXT (racine de la
 *	                               partition 1, FAT32, fichier créé d'avance : sa taille
 *	                               ne change pas ; seuls ses clusters sont écrits)
 *	DISQUE : sd0 par défaut (le service blk.sd0) */
#include "korinrt.h"

#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_WRITE 4
#define BLK_READ_D 6
#define BLK_STATS 7
#define BUF_BYTES (2u << 20)
#define MAXS 4096 /* secteurs du tampon */

static uint8_t msg[512] __attribute__((aligned(16)));
static uint8_t *buf;     /* le tampon partagé (2 Mo) */
static long buf_num = -1;
static uint8_t keep[MAXS * 512] __attribute__((aligned(64))); /* copies (2 Mo) */
static uint8_t keep2[MAXS * 512] __attribute__((aligned(64)));
static int failures;

static void say(const char *fmt, ...)
{
	char b[300];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	korin_sys2(SYS_PRINT, b, n);
}

static void fail(const char *fmt, ...)
{
	char b[300];
	va_list ap;
	va_start(ap, fmt);
	int n = snprintf(b, sizeof b, "blktest : ÉCHEC : ");
	n += vsnprintf(b + n, sizeof b - n, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	korin_sys2(SYS_PRINT, b, n);
	failures++;
}

static uint32_t fnv(const uint8_t *p, uint64_t n)
{
	uint32_t h = 2166136261u;
	while (n--)
		h = (h ^ *p++) * 16777619u;
	return h;
}

struct dev {
	long chan;
	uint64_t sectors;
	int ro;
	long pid;
	char name[32];
};

static long call(long chan, uint64_t op, uint64_t a, uint64_t b, uint64_t c)
{
	uint64_t *m = (uint64_t *)msg;
	memset(msg, 0, 48);
	m[0] = op;
	m[1] = a;
	m[2] = b;
	m[3] = c;
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* ouvre blk.<name>, l'attache au tampon -> 0, < 0 (wait : attente en ms) */
static long dev_open(struct dev *d, const char *name, long wait)
{
	snprintf(d->name, sizeof d->name, "blk.%s", name);
	d->chan = korin_sys2(SYS_SVC_OPEN, d->name, wait);
	if (d->chan < 0)
		return d->chan;
	long r = call(d->chan, BLK_INFO, 0, 0, 0);
	if (r < 0)
		return r;
	uint64_t *m = (uint64_t *)msg;
	d->ro = (int)m[1];
	d->sectors = m[2];
	d->pid = (long)m[4];
	if (m[3] != 512)
		say("blktest : %s : secteurs de %lu octets ?\n", d->name, (unsigned long)m[3]);
	if (buf_num < 0) {
		buf_num = korin_sys1(SYS_BUF_CREATE, BUF_BYTES);
		if (buf_num < 0)
			return buf_num;
		long va = korin_sys1(SYS_BUF_MAP, buf_num);
		if (va < 0)
			return va;
		buf = (uint8_t *)va;
	}
	korin_sys2(SYS_BUF_GRANT, buf_num, d->pid);
	return call(d->chan, BLK_ATTACH, (uint64_t)buf_num, BUF_BYTES, 0);
}

static long dread(struct dev *d, uint64_t lba, uint64_t n, uint8_t *at)
{
	return call(d->chan, BLK_READ, lba, n, (uint64_t)at);
}

static long dwrite(struct dev *d, uint64_t lba, uint64_t n, const uint8_t *at)
{
	return call(d->chan, BLK_WRITE, lba, n, (uint64_t)at);
}

/* lecture vers keep2 par morceaux de « step » secteurs */
static long read_pieces(struct dev *d, uint64_t lba, uint64_t n, uint64_t step, uint8_t *out)
{
	for (uint64_t i = 0; i < n; i += step) {
		uint64_t k = n - i < step ? n - i : step;
		long r = dread(d, lba + i, k, buf);
		if (r < 0)
			return r;
		memcpy(out + i * 512, buf, k * 512);
	}
	return 0;
}

static void pattern(uint8_t *p, uint64_t lba, uint64_t n, uint32_t seed)
{
	for (uint64_t s = 0; s < n; s++) {
		uint8_t *x = p + s * 512;
		for (int i = 0; i < 512; i++)
			x[i] = (uint8_t)((lba + s) * 31 + i * 7 + seed);
		memcpy(x, &(uint64_t){lba + s}, 8);
		memcpy(x + 8, &seed, 4);
	}
}

static void hexline(const uint8_t *p, int n)
{
	char b[3 * 32 + 2];
	int o = 0;
	for (int i = 0; i < n && i < 32; i++)
		o += snprintf(b + o, sizeof b - o, "%02x ", p[i]);
	say("  %s\n", b);
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void info(const char *disk)
{
	struct dev d, p;
	long r = dev_open(&d, disk, 2000);
	if (r < 0) {
		fail("%s : %ld\n", d.name, r);
		return;
	}
	say("blktest : %s : %lu secteurs (%lu Mo)%s, service pid %ld\n", d.name, (unsigned long)d.sectors,
	    (unsigned long)(d.sectors >> 11), d.ro ? ", lecture seule" : "", d.pid);
	for (int i = 1; i <= 20; i++) {
		char n[24];
		snprintf(n, sizeof n, "%sp%d", disk, i);
		if (dev_open(&p, n, 0) < 0)
			continue;
		say("blktest : %s : %lu secteurs (%lu Mo)%s\n", p.name, (unsigned long)p.sectors,
		    (unsigned long)(p.sectors >> 11), p.ro ? ", lecture seule" : "");
	}
	if (dread(&d, 0, 1, buf) < 0) {
		fail("secteur 0 illisible\n");
		return;
	}
	say("blktest : secteur 0 : somme %08x, fin %02x %02x\n", fnv(buf, 512), buf[510], buf[511]);
	hexline(buf, 32);
	for (int i = 0; i < 4; i++) {
		const uint8_t *e = buf + 446 + 16 * i;
		if (e[4])
			say("blktest : MBR %d : type %02x, début %u, %u secteurs\n", i + 1, e[4], le32(e + 8), le32(e + 12));
	}
}

static void cmd_read(const char *disk, uint64_t lba, uint64_t n)
{
	struct dev d;
	if (dev_open(&d, disk, 2000) < 0) {
		fail("%s absent\n", d.name);
		return;
	}
	if (n > MAXS)
		n = MAXS;
	long t = korin_sys0(SYS_TIME_MS);
	long r = dread(&d, lba, n, buf);
	t = korin_sys0(SYS_TIME_MS) - t;
	if (r < 0) {
		fail("lecture de %lu secteurs depuis %lu : %ld\n", (unsigned long)n, (unsigned long)lba, r);
		return;
	}
	say("blktest : %lu secteurs depuis %lu : somme %08x (%ld ms)\n", (unsigned long)n, (unsigned long)lba,
	    fnv(buf, n * 512), t);
	hexline(buf, 32);
}

static void rate(const char *what, uint64_t bytes, long ms)
{
	if (ms < 1)
		ms = 1;
	uint64_t kbs = bytes * 1000 / 1024 / (uint64_t)ms;
	say("blktest : %s : %lu Ko en %ld ms, %lu,%02lu Mo/s\n", what, (unsigned long)(bytes >> 10), ms,
	    (unsigned long)(kbs / 1024), (unsigned long)(kbs % 1024 * 100 / 1024));
}

/* lectures de toutes tailles comparées ; partition 1 ; lecture directe ; débit */
static void read_tests(struct dev *d, const char *disk)
{
	uint64_t n = d->sectors < 2048 ? d->sectors : 2048;
	long r = dread(d, 0, n, buf);
	if (r < 0) {
		fail("lecture de %lu secteurs d'un coup : %ld\n", (unsigned long)n, r);
		return;
	}
	memcpy(keep, buf, n * 512);
	say("blktest : %lu secteurs d'un coup : somme %08x\n", (unsigned long)n, fnv(keep, n * 512));
	static const uint64_t steps[] = {1, 2, 7, 8, 255, 256, 257, 1000};
	for (unsigned i = 0; i < sizeof steps / sizeof *steps; i++) {
		memset(keep2, 0x5A, n * 512);
		r = read_pieces(d, 0, n, steps[i], keep2);
		if (r < 0)
			fail("lecture par %lu : %ld\n", (unsigned long)steps[i], r);
		else if (memcmp(keep, keep2, n * 512))
			fail("lecture par %lu : contenu différent\n", (unsigned long)steps[i]);
		else
			say("blktest : par morceaux de %lu secteurs : identique\n", (unsigned long)steps[i]);
	}
	/* au-delà de la fin : refusé */
	r = dread(d, d->sectors - 1, 2, buf);
	if (r >= 0)
		fail("lecture au-delà de la fin acceptée\n");
	else
		say("blktest : lecture au-delà de la fin refusée (%ld)\n", r);
	r = dread(d, d->sectors - 1, 1, buf);
	if (r < 0)
		fail("dernier secteur illisible (%ld)\n", r);
	else
		say("blktest : dernier secteur (%lu) lu, somme %08x\n", (unsigned long)(d->sectors - 1), fnv(buf, 512));
	/* partition 1 : son secteur 0 est celui du disque à son début */
	struct dev p;
	char pn[24];
	snprintf(pn, sizeof pn, "%sp1", disk);
	if (dev_open(&p, pn, 0) == 0) {
		uint32_t base = le32(keep + 446 + 8);
		if (le32(keep + 446 + 8) && keep[446 + 4] != 0xEE) {
			if (dread(&p, 0, 8, buf) < 0 || dread(d, base, 8, buf + 8 * 512) < 0)
				fail("lecture par la partition 1\n");
			else if (memcmp(buf, buf + 8 * 512, 8 * 512))
				fail("partition 1 : son début n'est pas le secteur %u du disque\n", base);
			else
				say("blktest : %s : son début est le secteur %u du disque, signature %02x %02x\n", p.name, base,
				    buf[510], buf[511]);
		}
		r = dread(&p, p.sectors, 1, buf);
		if (r >= 0)
			fail("%s : lecture au-delà de la fin acceptée\n", p.name);
	}
	/* BLK_READ_D : dans notre mémoire, sans le tampon */
	uint64_t dn = n < 300 ? n : 300;
	memset(keep2, 0, dn * 512);
	r = call(d->chan, BLK_READ_D, 5, dn, (uint64_t)keep2);
	if (r == -11)
		say("blktest : lecture directe : pas permise (E_AGAIN)\n");
	else if (r < 0)
		fail("lecture directe : %ld\n", r);
	else if (memcmp(keep2, keep + 5 * 512, (dn < n - 5 ? dn : n - 5) * 512))
		fail("lecture directe : contenu différent\n");
	else
		say("blktest : lecture directe de %lu secteurs : identique\n", (unsigned long)dn);
	/* débit : 16 Mo (ou le disque) par 1 Mo */
	uint64_t total = d->sectors < 32768 ? d->sectors : 32768, done = 0;
	long t = korin_sys0(SYS_TIME_MS);
	for (; done + 2048 <= total; done += 2048)
		if ((r = dread(d, done, 2048, buf)) < 0) {
			fail("débit : %ld à %lu\n", r, (unsigned long)done);
			break;
		}
	rate("lecture par 1 Mo", done * 512, korin_sys0(SYS_TIME_MS) - t);
	t = korin_sys0(SYS_TIME_MS);
	for (done = 0; done < 1024; done++)
		if ((r = dread(d, done, 1, buf)) < 0)
			break;
	rate("lecture par secteur", done * 512, korin_sys0(SYS_TIME_MS) - t);
}

/* écrit, relit et compare n secteurs à lba ; l'original est remis */
static void write_one(struct dev *d, uint64_t lba, uint64_t n, uint32_t seed)
{
	long r = dread(d, lba, n, buf);
	if (r < 0) {
		fail("écriture : lecture d'origine (%lu, %lu) : %ld\n", (unsigned long)lba, (unsigned long)n, r);
		return;
	}
	memcpy(keep, buf, n * 512);
	pattern(buf, lba, n, seed);
	memcpy(keep2, buf, n * 512);
	long t = korin_sys0(SYS_TIME_MS);
	r = dwrite(d, lba, n, buf);
	t = korin_sys0(SYS_TIME_MS) - t;
	if (r < 0) {
		fail("écriture de %lu secteurs à %lu : %ld\n", (unsigned long)n, (unsigned long)lba, r);
		return;
	}
	memset(buf, 0, n * 512);
	r = dread(d, lba, n, buf);
	if (r < 0 || memcmp(buf, keep2, n * 512)) {
		uint64_t bad = 0;
		while (r >= 0 && bad < n && !memcmp(buf + bad * 512, keep2 + bad * 512, 512))
			bad++;
		fail("relecture de %lu secteurs à %lu : %ld, premier secteur différent %lu\n", (unsigned long)n,
		     (unsigned long)lba, r, (unsigned long)bad);
	} else
		say("blktest : %lu secteurs écrits à %lu (%ld ms), relus identiques, somme %08x\n", (unsigned long)n,
		    (unsigned long)lba, t, fnv(keep2, n * 512));
	memcpy(buf, keep, n * 512);
	r = dwrite(d, lba, n, buf);
	memset(buf, 0, n * 512);
	if (r < 0 || dread(d, lba, n, buf) < 0 || memcmp(buf, keep, n * 512))
		fail("contenu d'origine pas remis à %lu (%ld)\n", (unsigned long)lba, r);
}

static void write_tests(struct dev *d)
{
	if (d->sectors < 16384) {
		fail("disque trop petit pour l'essai d'écriture\n");
		return;
	}
	uint64_t z = d->sectors - 8192; /* les 4 derniers Mo */
	static const uint64_t sizes[] = {1, 2, 8, 255, 256, 257, 1024, 2048, 4096};
	uint64_t at = z;
	for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
		if (at + sizes[i] > d->sectors)
			at = z;
		write_one(d, at, sizes[i], 0x1234 + i);
		at += sizes[i] + 3;
	}
	/* écriture par la partition 1 : visible sur le disque */
	/* débit d'écriture : 1 Mo, 4 fois */
	long t = korin_sys0(SYS_TIME_MS), r = 0;
	uint64_t done = 0;
	if (read_pieces(d, z, 4096, 2048, keep) == 0) {
		for (int k = 0; k < 4 && r >= 0; k++, done += 2048) {
			pattern(buf, z + (k & 1) * 2048, 2048, 77 + k);
			r = dwrite(d, z + (k & 1) * 2048, 2048, buf);
		}
		rate("écriture par 1 Mo", done * 512, korin_sys0(SYS_TIME_MS) - t);
		for (int k = 0; k < 2; k++) {
			memcpy(buf, keep + k * 2048 * 512, 2048 * 512);
			dwrite(d, z + k * 2048, 2048, buf);
		}
		if (read_pieces(d, z, 4096, 2048, keep2) < 0 || memcmp(keep, keep2, 4096 * 512))
			fail("fin du disque pas remise après l'essai de débit\n");
	}
	if (d->ro)
		say("blktest : (disque en lecture seule : les écritures devaient être refusées)\n");
}

/* l'espace libre avant la première partition (au-delà du secteur 33 : MBR,
 * en-tête et entrées de la GPT) : sauvé, écrit, relu, remis */
static void safe_test(struct dev *d)
{
	if (dread(d, 0, 1, buf) < 0) {
		fail("secteur 0 illisible\n");
		return;
	}
	uint64_t first = ~0ull;
	for (int i = 0; i < 4; i++) {
		const uint8_t *e = buf + 446 + 16 * i;
		if (e[4] == 0xEE) {
			fail("GPT : l'espace libre n'est pas cherché (essai refusé)\n");
			return;
		}
		if (e[4] && le32(e + 8) < first)
			first = le32(e + 8);
	}
	if (first == ~0ull || first < 64 + 8) {
		fail("pas d'espace libre avant la première partition (%lu)\n", (unsigned long)first);
		return;
	}
	uint64_t n = first - 64 < 1024 ? first - 64 : 1024;
	say("blktest : espace libre : secteurs 64 à %lu ; essai sur %lu secteurs\n", (unsigned long)(first - 1),
	    (unsigned long)n);
	write_one(d, 64, 1, 0x51);
	if (n >= 8)
		write_one(d, 64, 8, 0x52);
	write_one(d, 64, n, 0x53);
}

/* écritures qui doivent être refusées (partition en lecture seule) : début,
 * milieu, fin ; le contenu relu doit être celui d'avant */
static void deny_test(struct dev *d)
{
	const uint64_t at[] = {0, d->sectors / 2, d->sectors - 8};
	for (unsigned i = 0; i < 3; i++) {
		uint64_t n = d->sectors - at[i] < 8 ? d->sectors - at[i] : 8;
		if (dread(d, at[i], n, buf) < 0) {
			fail("%s : lecture de %lu : refusée\n", d->name, (unsigned long)at[i]);
			continue;
		}
		memcpy(keep, buf, n * 512);
		pattern(buf, at[i], n, 0xDE17);
		long r = dwrite(d, at[i], n, buf);
		memset(buf, 0, n * 512);
		long r2 = dread(d, at[i], n, buf);
		if (r >= 0)
			fail("%s : écriture de %lu secteurs à %lu ACCEPTÉE\n", d->name, (unsigned long)n, (unsigned long)at[i]);
		else
			say("blktest : %s : écriture à %lu refusée (%ld)\n", d->name, (unsigned long)at[i], r);
		if (r2 < 0 || memcmp(buf, keep, n * 512))
			fail("%s : contenu changé à %lu\n", d->name, (unsigned long)at[i]);
	}
}

/* ---- le journal dans KORINLOG.TXT (FAT32, racine de la partition) */

static void log_to_file(const char *disk)
{
	struct dev p;
	char pn[24];
	snprintf(pn, sizeof pn, "%sp1", disk);
	if (dev_open(&p, pn, 1000) < 0 && dev_open(&p, disk, 1000) < 0) {
		fail("pas de blk.%s\n", pn);
		return;
	}
	if (dread(&p, 0, 1, buf) < 0 || memcmp(buf + 82, "FAT32", 5)) {
		fail("%s : pas un FAT32\n", p.name);
		return;
	}
	unsigned bps = buf[11] | buf[12] << 8, spc = buf[13], rsv = buf[14] | buf[15] << 8, nf = buf[16];
	uint32_t fatsz = le32(buf + 36), root = le32(buf + 44);
	if (bps != 512 || !spc) {
		fail("FAT32 : %u octets par secteur\n", bps);
		return;
	}
	uint64_t data = rsv + (uint64_t)nf * fatsz;
	uint32_t cl = root, file = 0, size = 0;
	for (int guard = 0; cl >= 2 && cl < 0x0FFFFFF8 && guard < 64 && !file; guard++) {
		uint64_t s0 = data + (uint64_t)(cl - 2) * spc;
		for (unsigned s = 0; s < spc && !file; s++) {
			if (dread(&p, s0 + s, 1, buf) < 0)
				return;
			for (int e = 0; e < 16; e++) {
				const uint8_t *x = buf + 32 * e;
				if (!memcmp(x, "KORINLOG", 8) && !memcmp(x + 8, "TXT", 3) && !(x[11] & 0x18)) {
					file = (uint32_t)(x[26] | x[27] << 8) | (uint32_t)(x[20] | x[21] << 8) << 16;
					size = le32(x + 28);
					break;
				}
			}
		}
		if (file)
			break;
		if (dread(&p, rsv + cl / 128, 1, buf) < 0)
			return;
		cl = le32(buf + (cl % 128) * 4) & 0x0FFFFFFF;
	}
	if (!file || !size) {
		fail("KORINLOG.TXT absent de la racine (ou vide) : le créer sous Windows d'abord\n");
		return;
	}
	static char logb[1 << 20];
	long n = korin_sys2(SYS_KLOG, logb, sizeof logb);
	if (n < 0)
		n = 0;
	say("blktest : journal de %ld octets vers KORINLOG.TXT (%u octets)\n", n, size);
	uint64_t off = 0;
	cl = file;
	for (int guard = 0; off < size && cl >= 2 && cl < 0x0FFFFFF8 && guard < 1 << 16; guard++) {
		uint64_t s0 = data + (uint64_t)(cl - 2) * spc;
		for (unsigned s = 0; s < spc && off < size; s++, off += 512) {
			for (int i = 0; i < 512; i++) {
				uint64_t k = off + i;
				buf[i] = k < (uint64_t)n ? (uint8_t)logb[k] : (k + 1) % 80 == 0 ? '\n' : ' ';
			}
			if (dwrite(&p, s0 + s, 1, buf) < 0) {
				fail("écriture du journal à %lu\n", (unsigned long)off);
				return;
			}
		}
		if (dread(&p, rsv + cl / 128, 1, buf) < 0)
			return;
		cl = le32(buf + (cl % 128) * 4) & 0x0FFFFFFF;
	}
	say("blktest : journal écrit (%lu octets)\n", (unsigned long)(off < size ? off : size));
}

int main(int argc, char **argv)
{
	const char *disk = "sd0";
	int a = 1, wr = 0;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-w"))
			wr = 1;
	if (a < argc && strcmp(argv[a], "info") && strcmp(argv[a], "read") && strcmp(argv[a], "all") &&
	    strcmp(argv[a], "safe") && strcmp(argv[a], "log") && strcmp(argv[a], "deny") && strcmp(argv[a], "-w"))
		disk = argv[a++];
	const char *what = a < argc ? argv[a] : "info";
	if (!strcmp(what, "info"))
		info(disk);
	else if (!strcmp(what, "read"))
		cmd_read(disk, a + 1 < argc ? parse_dec(argv[a + 1]) : 0, a + 2 < argc ? parse_dec(argv[a + 2]) : 1);
	else if (!strcmp(what, "log"))
		log_to_file(disk);
	else if (!strcmp(what, "deny")) {
		struct dev d;
		long r = dev_open(&d, disk, 3000);
		if (r < 0)
			fail("%s : %ld\n", d.name, r);
		else if (wr)
			deny_test(&d);
		else
			say("blktest : deny écrit si le refus manque : ajouter -w pour confirmer\n");
	} else if (!strcmp(what, "all") || !strcmp(what, "safe")) {
		struct dev d;
		long r = dev_open(&d, disk, 3000);
		if (r < 0) {
			fail("%s : %ld\n", d.name, r);
			return 1;
		}
		if (!strcmp(what, "all")) {
			info(disk);
			read_tests(&d, disk);
			if (wr)
				write_tests(&d);
		} else if (wr)
			safe_test(&d);
		else
			say("blktest : safe écrit sur la carte : ajouter -w pour confirmer\n");
		r = call(d.chan, BLK_STATS, (uint64_t)-1, 0, 0);
		if (r >= 0)
			say("blktest : statistiques : %lu commandes\n", (unsigned long)((uint64_t *)msg)[1]);
	} else
		say("blktest : info, read LBA N, all [-w], safe -w, log\n");
	say("blktest : fin, %d échec(s)\n", failures);
	return failures != 0;
}
