/* blkserv.c : le service des blocs (protocole src/inc/blk.inc, inchangé), en
 * C, repris de src/svc/inc/blkserv.inc : un pilote fournit lire et écrire des
 * secteurs du disque entier ; blk_serve lit sa table de partitions et sert le
 * disque (« blk.<nom> ») et chaque partition (« blk.<nom>p1 »…), une
 * partition n'étant qu'une fenêtre sur le disque. Un thread par
 * périphérique ; les appels au pilote sont sérialisés (sd_lock).
 *
 * Comme l'asm : MBR (4 primaires p1-p4, logiques p5-p12 dans l'étendue), une
 * carte sans table (secteur de démarrage FAT d'emblée) n'est servie qu'en
 * disque entier, un type MBR peut être servi en lecture seule (type_ro).
 * En plus : GPT (MBR protecteur 0xEE : p1… dans l'ordre des entrées, 16 au
 * plus), BLK_LIRE_D par la fenêtre de l'appelant (SVC_MAP_CALLER, sans les
 * adresses physiques), support retiré (E_IO) puis remis (table relue,
 * fenêtres mises à jour, nouvelles partitions enregistrées). */
#include "sdmmc.h"

#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_WRITE 4
#define BLK_DMA 5
#define BLK_READ_D 6
#define BLK_STATS 7
#define BLK_UHS 8
#define BLK_D_MAX 2048

#define BUF_SLOT (64ull << 20)
#define MC_WRITE 1
#define PART_MAX 20
#define DEV_MAX (1 + PART_MAX)
#define CL_MAX 8
#define STACK 16384

#if defined(__x86_64__)
#define THREAD_ATTR __attribute__((force_align_arg_pointer, noreturn))
#else
#define THREAD_ATTR __attribute__((noreturn))
#endif

struct client {
	int pid; /* 0 : libre */
	long buf;
	uint8_t *va;
	uint64_t size, phys;
};

struct dev {
	int used, num; /* num : partition (0 : le disque) */
	uint64_t base, size;
	int ro;
	uint8_t type;
	long chan;
	char name[32];
	struct client cl[CL_MAX];
};

struct part {
	uint64_t base, size;
	uint8_t type;
	int num;
};

static struct dev devs[DEV_MAX];
static struct blk_driver *drv;
static char disk_name[16];
static uint8_t sec[512] __attribute__((aligned(64)));
static uint8_t gpt_sec[512] __attribute__((aligned(64)));
static uint8_t stacks[DEV_MAX][STACK] __attribute__((aligned(16)));

void blk_free_name(const char *prefix, char *buf)
{
	for (int i = 0; i < 10; i++) {
		char n[32];
		snprintf(buf, 8, "%s%d", prefix, i);
		snprintf(n, sizeof n, "blk.%s", buf);
		long c = korin_sys2(SYS_SVC_OPEN, n, 0);
		if (c < 0)
			return; /* (un canal de service n'est pas une poignée : rien à fermer) */
	}
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
	return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static int dread(uint64_t lba, unsigned n, void *buf)
{
	sd_lock();
	int r = drv->read(drv->ctx, lba, n, buf, 0);
	sd_unlock();
	return r;
}

static int add_part(struct part *t, int *np, uint64_t base, uint64_t size, uint8_t type, int num, uint64_t disk)
{
	if (*np >= PART_MAX)
		return 0;
	if (!size || base >= disk) {
		sd_log("p%d (%lu, %lu secteurs) hors du disque : ignorée\n", num, (unsigned long)base,
		       (unsigned long)size);
		return 0;
	}
	if (base + size > disk) {
		sd_log("p%d dépasse la fin du disque : coupée\n", num);
		size = disk - base;
	}
	t[*np].base = base;
	t[*np].size = size;
	t[*np].type = type;
	t[*np].num = num;
	(*np)++;
	return 1;
}

static void read_gpt(struct part *t, int *np, uint64_t disk)
{
	if (dread(1, 1, gpt_sec) < 0 || memcmp(gpt_sec, "EFI PART", 8)) {
		sd_log("MBR protecteur sans en-tête GPT lisible\n");
		return;
	}
	uint64_t lba = le64(gpt_sec + 72);
	uint32_t count = le32(gpt_sec + 80), esz = le32(gpt_sec + 84);
	if (esz < 128 || esz > 512 || (512 % esz) || count > 1024) {
		sd_log("GPT : entrées de %u octets, %u entrées : refusé\n", esz, count);
		return;
	}
	sd_log("GPT : %u entrées de %u octets au secteur %lu\n", count, esz, (unsigned long)lba);
	unsigned per = 512 / esz;
	for (uint32_t i = 0; i < count && *np < PART_MAX; i++) {
		if (i % per == 0 && dread(lba + i / per, 1, gpt_sec) < 0)
			return;
		const uint8_t *e = gpt_sec + (i % per) * esz;
		int empty = 1;
		for (int k = 0; k < 16; k++)
			empty &= e[k] == 0;
		if (empty)
			continue;
		uint64_t first = le64(e + 32), last = le64(e + 40);
		if (last < first)
			continue;
		/* type : 0x0C pour une partition de données Microsoft (FAT, exFAT) ou
		 * EFI, 0xEF pour l'ESP, comme un type MBR (fatfs regarde le système) */
		uint8_t type = e[0] == 0x28 && e[1] == 0x73 ? 0xEF : 0x0C;
		add_part(t, np, first, last - first + 1, type, (int)i + 1, disk);
	}
}

/* la table du disque -> nombre de partitions dans t */
static int read_table(struct part *t, uint64_t disk)
{
	int np = 0;
	if (dread(0, 1, sec) < 0) {
		sd_log("secteur 0 illisible\n");
		return 0;
	}
	if (sec[510] != 0x55 || sec[511] != 0xAA) {
		sd_log("pas de table de partitions (pas de 55 aa) : disque entier seulement\n");
		return 0;
	}
	if (sec[82] == 'F' || !memcmp(sec + 3, "EXFAT   ", 8) || !memcmp(sec + 54, "FAT1", 4)) {
		sd_log("secteur de démarrage d'emblée (sans table) : disque entier seulement\n");
		return 0;
	}
	uint8_t mbr[64];
	memcpy(mbr, sec + 446, 64);
	uint64_t ext = 0;
	for (int i = 0; i < 4; i++) {
		const uint8_t *e = mbr + 16 * i;
		uint8_t type = e[4];
		if (!type)
			continue;
		uint64_t base = le32(e + 8), size = le32(e + 12);
		if (type == 0xEE) {
			read_gpt(t, &np, disk);
			return np;
		}
		if (type == 0x05 || type == 0x0F) {
			ext = base;
			continue;
		}
		add_part(t, &np, base, size, type, i + 1, disk);
	}
	/* logiques : p5, p6… une table (EBR) avant chacune, la suivante relative
	 * au début de l'étendue */
	uint64_t ebr = ext;
	for (int num = 5; ext && num < 5 + 8; num++) {
		if (dread(ebr, 1, sec) < 0 || sec[510] != 0x55 || sec[511] != 0xAA)
			break;
		const uint8_t *e = sec + 446;
		if (!e[4])
			break;
		add_part(t, &np, ebr + le32(e + 8), le32(e + 12), e[4], num, disk);
		e = sec + 462;
		if (!e[4])
			break;
		uint64_t next = ext + le32(e + 8);
		if (next <= ebr) /* (une table qui boucle : fin) */
			break;
		ebr = next;
	}
	return np;
}

/* ---- un périphérique */

static struct client *client_of(struct dev *d, int pid)
{
	for (int i = 0; i < CL_MAX; i++)
		if (d->cl[i].pid == pid)
			return &d->cl[i];
	return 0;
}

/* le tampon n est-il encore tenu par un client vivant ailleurs ? */
static int buf_in_use(long buf, const struct client *except)
{
	for (int i = 0; i < DEV_MAX; i++)
		for (int k = 0; k < CL_MAX; k++) {
			const struct client *c = &devs[i].cl[k];
			if (c != except && c->pid && c->buf == buf)
				return 1;
		}
	return 0;
}

static long op_attach(struct dev *d, int pid, long num, uint64_t size)
{
	long va = korin_sys1(SYS_BUF_MAP, num);
	if (va < 0)
		return va;
	uint64_t bi[4];
	uint64_t phys = korin_sys2(SYS_BUF_INFO, num, bi) == 0 ? bi[0] : 0;
	/* les places des clients terminés (fatfs démonté, mkfs) sont rendues */
	for (int i = 0; i < CL_MAX; i++) {
		struct client *c = &d->cl[i];
		uint8_t info[64];
		if (!c->pid || c->pid == pid || korin_sys2(SYS_PROC_INFO, c->pid, info) >= 0)
			continue;
		long b = c->buf;
		c->pid = 0;
		if (!buf_in_use(b, 0))
			korin_sys1(SYS_BUF_FREE, b);
	}
	struct client *c = client_of(d, pid);
	if (!c)
		c = client_of(d, 0);
	if (!c)
		return E_NOMEM;
	if (size > BUF_SLOT || !size)
		size = BUF_SLOT;
	c->buf = num;
	c->va = (uint8_t *)va;
	c->size = size;
	c->phys = phys;
	c->pid = pid;
	return 0;
}

static long op_rw(struct dev *d, int pid, int op, uint64_t lba, uint64_t n, uint64_t addr)
{
	struct client *c = client_of(d, pid);
	if (!c)
		return E_PERM;
	uint64_t start = (uint64_t)c->va;
	if (addr < start || n > c->size / 512 || addr + n * 512 > start + c->size)
		return E_INVAL;
	if (lba + n > d->size || lba + n < lba)
		return E_INVAL;
	if (!n)
		return 0;
	uint64_t phys = c->phys ? c->phys + (addr - start) : 0;
	if (op == BLK_WRITE && d->ro)
		return E_PERM;
	sd_lock();
	int r = op == BLK_WRITE ? drv->write(drv->ctx, d->base + lba, (unsigned)n, (void *)addr, phys)
				: drv->read(drv->ctx, d->base + lba, (unsigned)n, (void *)addr, phys);
	sd_unlock();
	return r < 0 ? (r == E_PERM || r == E_INVAL ? r : E_IO) : 0;
}

static long op_read_direct(struct dev *d, long req, uint64_t lba, uint64_t n, uint64_t addr)
{
	if (!n || n > BLK_D_MAX || lba + n > d->size)
		return E_INVAL;
	long va = korin_sys4(SYS_SVC_MAP_CALLER, req, addr, n * 512, MC_WRITE);
	if (va < 0)
		return va;
	sd_lock();
	int r = drv->read(drv->ctx, d->base + lba, (unsigned)n, (void *)va, 0);
	sd_unlock();
	return r < 0 ? E_IO : 0; /* (SVC_REPLY rend la fenêtre) */
}

THREAD_ATTR static void dev_thread(long idx)
{
	struct dev *d = &devs[idx];
	uint8_t msg[512] __attribute__((aligned(16)));
	uint64_t *m = (uint64_t *)msg;
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, d->chan, msg, 60000);
		if (req < 0)
			break;
		if (req == 0)
			continue;
		int pid = (int)*(uint32_t *)(msg + MSG_PID);
		long r = E_INVAL;
		switch (m[0]) {
		case BLK_INFO:
			m[1] = (uint64_t)d->ro;
			m[2] = d->size;
			m[3] = 512;
			m[4] = (uint64_t)korin_sys0(SYS_GETPID);
			r = 0;
			break;
		case BLK_ATTACH:
			r = op_attach(d, pid, (long)m[1], m[2]);
			break;
		case BLK_READ:
		case BLK_WRITE:
			r = d->size ? op_rw(d, pid, (int)m[0], m[1], m[2], m[3]) : E_IO;
			break;
		case BLK_DMA:
		case BLK_UHS:
			r = 0; /* (le DMA est pris au démarrage s'il passe l'essai ; pas d'UHS) */
			break;
		case BLK_READ_D:
			r = d->size ? op_read_direct(d, req, m[1], m[2], m[3]) : E_IO;
			break;
		case BLK_STATS: {
			uint64_t st[4] = {0, 0, 0, 0};
			if (drv->stats)
				drv->stats(drv->ctx, st);
			m[1] = st[0]; /* commandes ; temps : non mesurés */
			m[2] = m[3] = m[4] = 0;
			r = 0;
			break;
		}
		}
		m[0] = (uint64_t)r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
	sd_log("%s : service fermé\n", d->name);
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

static int dev_start(int i)
{
	struct dev *d = &devs[i];
	if (d->num)
		snprintf(d->name, sizeof d->name, "blk.%sp%d", disk_name, d->num);
	else
		snprintf(d->name, sizeof d->name, "blk.%s", disk_name);
	d->chan = korin_sys2(SYS_SVC_REGISTER, d->name, 0);
	if (d->chan < 0) {
		sd_log("%s refusé (%ld)\n", d->name, d->chan);
		d->used = 0;
		return (int)d->chan;
	}
	d->used = 1;
	long t = korin_sys3(SYS_THREAD_CREATE, dev_thread, stacks[i] + STACK, i);
	if (t < 0) {
		sd_log("%s : pas de thread (%ld)\n", d->name, t);
		return (int)t;
	}
	return 0;
}

static void describe(const struct dev *d)
{
	if (d->num)
		sd_log("%s : type %02x, secteurs %lu à %lu (%lu Mo)%s\n", d->name, d->type, (unsigned long)d->base,
		       (unsigned long)(d->base + d->size - 1), (unsigned long)(d->size >> 11), d->ro ? ", lecture seule" : "");
	else
		sd_log("%s : %lu secteurs (%lu Mo)%s\n", d->name, (unsigned long)d->size,
		       (unsigned long)(d->size >> 11), d->ro ? ", lecture seule" : "");
}

/* (re)lit la table et met les périphériques à jour ; first : premier passage */
static void apply_table(int first)
{
	static struct part t[PART_MAX];
	uint64_t disk = drv->sectors(drv->ctx);
	int np = disk ? read_table(t, disk) : 0;
	struct dev *dk = &devs[0];
	dk->size = disk;
	dk->ro = drv->read_only;
	if (first)
		dev_start(0);
	describe(dk);
	/* partitions déjà connues : fenêtre mise à jour, ou fermée (taille 0) */
	for (int i = 1; i < DEV_MAX; i++) {
		struct dev *d = &devs[i];
		if (!d->used)
			continue;
		int found = 0;
		for (int k = 0; k < np; k++)
			if (t[k].num == d->num) {
				found = 1;
				d->base = t[k].base;
				d->size = t[k].size;
				d->type = t[k].type;
				t[k].num = -t[k].num; /* (prise) */
				break;
			}
		if (!found) {
			d->size = 0;
			sd_log("%s : absente de la nouvelle table\n", d->name);
		} else
			describe(d);
	}
	for (int k = 0; k < np; k++) {
		if (t[k].num < 0) {
			t[k].num = -t[k].num;
			continue;
		}
		int i;
		for (i = 1; i < DEV_MAX && devs[i].used; i++)
			;
		if (i == DEV_MAX)
			break;
		struct dev *d = &devs[i];
		memset(d, 0, sizeof *d);
		d->num = t[k].num;
		d->base = t[k].base;
		d->size = t[k].size;
		d->type = t[k].type;
		d->ro = drv->read_only || (drv->type_ro && d->type == drv->type_ro);
		if (dev_start(i) == 0)
			describe(d);
	}
	for (int i = 1; i < DEV_MAX; i++)
		if (devs[i].used)
			devs[i].ro = drv->read_only || (drv->type_ro && devs[i].type == drv->type_ro);
}

NORETURN void blk_serve(const char *name, struct blk_driver *d, long poll_ms)
{
	drv = d;
	snprintf(disk_name, sizeof disk_name, "%s", name);
	apply_table(1);
	int present = drv->sectors(drv->ctx) != 0;
	for (;;) {
		sd_sleep_ms(poll_ms);
		if (!sd_trylock())
			continue; /* (un transfert en cours : la carte est là) */
		int changed = 0;
		int now = drv->check(drv->ctx, &changed);
		sd_unlock();
		if (now && (changed || !present)) {
			sd_log("blk.%s : support remis, table relue\n", disk_name);
			apply_table(0);
		} else if (!now && present) {
			sd_log("blk.%s : support retiré, lectures et écritures en erreur\n", disk_name);
			for (int i = 0; i < DEV_MAX; i++)
				if (devs[i].used && i)
					devs[i].size = 0;
			devs[0].size = 0;
		}
		present = now;
	}
}
