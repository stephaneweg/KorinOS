/* blkserv.c : la couche commune des périphériques de blocs (protocole
 * src/inc/blk.inc, inchangé), seule copie en C, traduction de
 * src/svc/inc/blkserv.inc (le Pi garde l'asm pour la carte SD). Interface :
 * blkserv.h (struct blk_driver, future table de module). Pilotes : imgblk,
 * usbstorage, nvme, sdhci, rtsx.
 *
 * Comme l'asm : le disque « blk.<nom> » et chaque partition « blk.<nom>p1 »…,
 * une partition n'étant qu'une fenêtre sur le disque ; MBR (p1-p4, logiques
 * p5-p12 par les EBR), un type MBR servi en lecture seule (type_ro) ; un thread
 * par périphérique, un appel au pilote à la fois ; les données passent par un
 * tampon partagé du client, attaché d'abord (BLK_ATTACH) ; la place d'un
 * client terminé est rendue au BLK_ATTACH suivant.
 *
 * En plus de l'asm (réuni des deux anciennes copies, 12/11) :
 *  * GPT derrière un MBR protecteur 0xEE : partitions numérotées comme leurs
 *    entrées (la cinquième entrée est p5 même si une entrée avant elle est vide
 *    ou si elle est placée avant la quatrième), CRC de l'en-tête et des
 *    entrées vérifiés et dits à l'opération table, 16 partitions servies,
 *    1024 entrées lues au plus ; noms p10… en décimal ;
 *  * un disque sans table (secteur de démarrage FAT, exFAT d'emblée) n'est
 *    servi qu'entier ; une partition hors du disque est ignorée, une qui en
 *    dépasse la fin est coupée ;
 *  * BLK_READ_D par les pages de l'appelant (SVC_MAP_CALLER, SVC_CALLER_PHYS) ;
 *  * opérations table (lecture seule et nom de plus « blk.<alias> »,
 *    partition par partition), denied (écriture refusée), signaux (stop,
 *    flush) ;
 *  * support amovible (present, capacity) : retiré, tout rend E_IO ; remis, la
 *    table est relue, les fenêtres mises à jour, les nouvelles partitions
 *    enregistrées, celles qui manquent rendent E_IO ;
 *  * le tampon d'un client terminé n'est démappé que si plus personne ne le
 *    tient (le même tampon peut être attaché au disque et à une partition) ;
 *  * un journal (opération log), et blk_free_name. */
#include "blkserv.h"

#define LOG_MAX 8 /* partitions logiques (p5 à p12) */
#define PART_MAX (BLK_GPT_MAX > 4 + LOG_MAX ? BLK_GPT_MAX : 4 + LOG_MAX)
#define DEV_MAX (1 + PART_MAX + BLK_ALIAS_MAX) /* le disque, les partitions, les noms de plus */
#define CL_MAX 8
#define STACK_THREAD 16384
#define BLK_SG_PAGES (BLK_D_MAX / 8 + 2) /* (une page entamée de chaque côté) */
#define GPT_ENTRIES_MAX 1024
#define POLL_MS 500

struct client {
	u32 pid; /* 0 : libre */
	long buf; /* numéro du tampon (BUF_FREE) */
	u64 start, end, phys;
};

struct dev {
	int used;
	long num;       /* partition (0 : le disque, ou un nom de plus) */
	char alias[16]; /* nom de plus ("" : non) */
	u64 base, size; /* size 0 : support retiré, ou partition disparue -> E_IO */
	long ro;
	u8 type;
	long chan;
	char name[32];
	struct client cl[CL_MAX];
};

static struct blk_driver *drv;
static char disk_name[16];
static u32 blk_lock;
static struct dev devs[DEV_MAX];
static u8 sector0[512] __attribute__((aligned(64)));
static u8 gsec[512] __attribute__((aligned(64)));
static struct blk_part table[PART_MAX];
static int table_n;
static struct blk_gpt gpt;
static u64 disk_size; /* (pendant la lecture de la table) */
static long disk_ro;

/* ---- verrou : un appel au pilote à la fois */

static void lock(void)
{
	while (__atomic_exchange_n(&blk_lock, 1, __ATOMIC_ACQUIRE))
		korin_sys0(SYS_YIELD);
}

static int trylock(void)
{
	return !__atomic_exchange_n(&blk_lock, 1, __ATOMIC_ACQUIRE);
}

static void unlock(void)
{
	__atomic_store_n(&blk_lock, 0, __ATOMIC_RELEASE);
}

static void blog(const char *fmt, ...)
{
	if (!drv->log)
		return;
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	drv->log(drv->ctx, buf);
}

static long call_rw(int write, u64 lba, u64 n, void *buf, u64 phys)
{
	lock();
	long r = write ? drv->write(drv->ctx, lba, n, buf, phys) : drv->read(drv->ctx, lba, n, buf, phys);
	unlock();
	return r;
}

static long call0(long (*fn)(void *))
{
	lock();
	long r = fn(drv->ctx);
	unlock();
	return r;
}

void blk_free_name(const char *prefix, char *buf, int size)
{
	for (int i = 0; i < 10; i++) {
		char n[32];
		snprintf(buf, size, "%s%d", prefix, i);
		snprintf(n, sizeof n, "blk.%s", buf);
		if (korin_sys2(SYS_SVC_OPEN, n, 0) < 0)
			return; /* (un canal de service n'est pas une poignée : rien à fermer) */
	}
}

/* ---- la table de partitions */

static u32 le32(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static u64 le64(const u8 *p)
{
	return le32(p) | (u64)le32(p + 4) << 32;
}

/* CRC-32 de la GPT (IEEE 802.3, polynôme réfléchi edb88320) */
static u32 crc32_add(u32 crc, const u8 *p, u64 n)
{
	crc = ~crc;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
	}
	return ~crc;
}

/* une partition dans la table (pas encore servie) ; hors du disque : ignorée,
 * au-delà de sa fin : coupée */
static struct blk_part *table_add(u64 first, u64 count, u8 type, long num)
{
	if (table_n >= PART_MAX)
		return 0;
	if (!count || first >= disk_size) {
		blog("p%ld (%lu, %lu secteurs) hors du disque : ignorée\n", num, (unsigned long)first,
		     (unsigned long)count);
		return 0;
	}
	if (first + count > disk_size || first + count < first) {
		blog("p%ld dépasse la fin du disque : coupée\n", num);
		count = disk_size - first;
	}
	struct blk_part *p = &table[table_n++];
	memset(p, 0, sizeof *p);
	p->num = num;
	p->first = first;
	p->count = count;
	p->type = type;
	p->ro = disk_ro;
	if (drv->type_ro && type == drv->type_ro) /* un type protégé (exFAT : la partition personnelle) */
		p->ro = 1;
	return p;
}

/* la GPT (MBR protecteur vu) : en-tête au secteur 1, entrées ; numéros = rangs des entrées + 1 */
static void read_gpt(void)
{
	if (call_rw(0, 1, 1, gsec, 0) || memcmp(gsec, "EFI PART", 8)) {
		blog("MBR protecteur sans en-tête GPT lisible\n");
		return;
	}
	u32 hsize = le32(gsec + 12);
	if (hsize < 92 || hsize > 512) {
		blog("GPT : en-tête de %u octets : refusé\n", hsize);
		return;
	}
	u32 hcrc = le32(gsec + 16);
	gsec[16] = gsec[17] = gsec[18] = gsec[19] = 0;
	int crc_ok = crc32_add(0, gsec, hsize) == hcrc;
	u64 lba = le64(gsec + 72);
	u32 count = le32(gsec + 80), esz = le32(gsec + 84), ecrc = le32(gsec + 88);
	if (esz < 128 || esz > 512 || (512 % esz) || count > GPT_ENTRIES_MAX || lba < 2 || lba >= disk_size) {
		blog("GPT : %u entrées de %u octets au secteur %lu : refusé\n", count, esz, (unsigned long)lba);
		return;
	}
	blog("GPT : %u entrées de %u octets au secteur %lu\n", count, esz, (unsigned long)lba);
	gpt.gpt = 1;
	gpt.first_usable = le64(gsec + 40);
	gpt.last_usable = le64(gsec + 48);
	gpt.alt_lba = le64(gsec + 32);
	memcpy(gpt.disk_guid, gsec + 56, 16);
	gpt.entries = count;
	gpt.entry_size = esz;
	gpt.entries_lba = lba;
	unsigned per = 512 / esz;
	u32 crc = 0;
	for (u32 i = 0; i < count; i++) {
		if (i % per == 0) {
			if (call_rw(0, lba + i / per, 1, gsec, 0)) {
				crc_ok = 0;
				break;
			}
			crc = crc32_add(crc, gsec, 512 < (count - i) * esz ? 512 : (count - i) * esz);
		}
		const u8 *e = gsec + (i % per) * esz;
		int empty = 1;
		for (int k = 0; k < 16; k++)
			empty &= e[k] == 0;
		if (empty)
			continue;
		u64 first = le64(e + 32), last = le64(e + 40);
		if (last < first)
			continue;
		struct blk_part *p = table_add(first, last - first + 1, 0xEE, (long)i + 1);
		if (!p)
			continue; /* (plus de place : la suite des entrées compte encore pour le CRC) */
		p->gpt = 1;
		memcpy(p->type_guid, e, 16);
		memcpy(p->uniq_guid, e + 16, 16);
		p->attrs = le64(e + 48);
		for (int k = 0; k < 36; k++)
			p->name[k] = (u16)(e[56 + 2 * k] | e[57 + 2 * k] << 8);
	}
	gpt.crc_ok = crc_ok && crc == ecrc;
	if (!gpt.crc_ok)
		blog("GPT : CRC faux\n");
}

/* la table du disque (disk secteurs, 0 : absent) -> table[], table_n, gpt */
static void read_table(u64 disk)
{
	table_n = 0;
	memset(&gpt, 0, sizeof gpt);
	disk_size = disk;
	if (!disk)
		return;
	if (call_rw(0, 0, 1, sector0, 0)) {
		blog("secteur 0 illisible\n");
		return;
	}
	/* MBR valide ? (0x55AA, et pas un secteur de démarrage d'emblée : « FAT32 » à
	   82 comme l'asm, exFAT, FAT12/16) */
	if ((sector0[510] | sector0[511] << 8) != 0xAA55) {
		blog("pas de table de partitions (pas de 55 aa) : disque entier seulement\n");
		return;
	}
	if (sector0[82] == 'F' || !memcmp(sector0 + 3, "EXFAT   ", 8) || !memcmp(sector0 + 54, "FAT1", 4)) {
		blog("secteur de démarrage d'emblée (sans table) : disque entier seulement\n");
		return;
	}
	int prot = 0;
	for (int p = 0; p < 4; p++)
		prot |= sector0[446 + 16 * p + 4] == 0xEE;
	if (prot) {
		read_gpt();
		return;
	}
	u64 ext = 0; /* la partition étendue (0 : aucune) */
	for (int p = 0; p < 4; p++) {
		const u8 *e = sector0 + 446 + 16 * p;
		u8 type = e[4];
		if (!type)
			continue;
		if (type == 0x05 || type == 0x0F) /* étendue (CHS ou LBA) */
			ext = le32(e + 8);
		else
			table_add(le32(e + 8), le32(e + 12), type, p + 1);
	}
	/* les logiques : p5, p6... une table (EBR) avant chacune ; la suivante
	   relative au début de l'étendue */
	u64 ebr = ext;
	for (long n = 5; ext && n < 5 + LOG_MAX; n++) {
		if (call_rw(0, ebr, 1, sector0, 0))
			break;
		if ((sector0[510] | sector0[511] << 8) != 0xAA55)
			break;
		const u8 *e = sector0 + 446;
		if (!e[4])
			break;
		table_add(le32(e + 8) + ebr, le32(e + 12), e[4], n);
		e = sector0 + 462;
		if (!e[4])
			break;
		u64 next = le32(e + 8) + ext;
		if (next <= ext) /* (une table qui boucle : fin) */
			break;
		ebr = next;
	}
}

/* ---- les périphériques */

static THREAD_ENTRY void dev_thread(struct dev *d);

static void describe(const struct dev *d)
{
	const char *ro = d->ro ? ", lecture seule" : "";
	if (d == &devs[0]) {
		if (d->size == (u64)-1)
			blog("%s : taille inconnue%s\n", d->name, ro);
		else
			blog("%s : %lu secteurs (%lu Mo)%s\n", d->name, (unsigned long)d->size,
			     (unsigned long)(d->size >> 11), ro);
	} else
		blog("%s : type %02x, secteurs %lu à %lu (%lu Mo)%s\n", d->name, d->type, (unsigned long)d->base,
		     (unsigned long)(d->base + d->size - 1), (unsigned long)(d->size >> 11), ro);
}

/* enregistré (« blk.<nom>[p<n>] » ou « blk.<alias> »), avec son thread si
 * thread -> 0, < 0 */
static long dev_start(struct dev *d, int thread)
{
	if (d->alias[0])
		snprintf(d->name, sizeof d->name, "blk.%s", d->alias);
	else if (d->num)
		snprintf(d->name, sizeof d->name, "blk.%sp%ld", disk_name, d->num);
	else
		snprintf(d->name, sizeof d->name, "blk.%s", disk_name);
	d->chan = korin_sys2(SYS_SVC_REGISTER, d->name, 0);
	if (d->chan < 0) {
		blog("%s refusé (%ld)\n", d->name, d->chan);
		return d->chan;
	}
	d->used = 1;
	if (!thread)
		return 0;
	u8 *stack = ak_malloc(STACK_THREAD);
	long t = stack ? korin_sys3(SYS_THREAD_CREATE, dev_thread, stack + STACK_THREAD, d) : E_NOMEM;
	if (t < 0)
		blog("%s : pas de thread (%ld)\n", d->name, t);
	return t < 0 ? t : 0;
}

static void window_set(struct dev *d, const struct blk_part *p)
{
	d->base = p->first;
	d->size = p->count;
	d->type = p->type;
	d->ro = p->ro;
}

/* une partition, ou un nom de plus, de plus */
static void dev_new(const struct blk_part *p, int alias)
{
	int i;
	for (i = 1; i < DEV_MAX && devs[i].used; i++)
		;
	if (i == DEV_MAX) {
		blog("plus de place pour p%ld\n", p->num);
		return;
	}
	struct dev *d = &devs[i];
	memset(d, 0, sizeof *d);
	if (alias)
		memcpy(d->alias, p->alias, sizeof d->alias - 1);
	else
		d->num = p->num;
	window_set(d, p);
	if (dev_start(d, 1) == 0)
		describe(d);
}

/* (re)lit la table et met les périphériques à jour (le premier passage les
 * enregistre) */
static void apply_table(void)
{
	u64 disk;
	if (drv->present) {
		lock();
		disk = drv->capacity(drv->ctx);
		unlock();
	} else
		disk = drv->sectors ? drv->sectors : (u64)-1;
	disk_ro = drv->read_only || !drv->write;
	read_table(disk);
	if (drv->table)
		drv->table(drv->ctx, table, table_n, &gpt);
	struct dev *dk = &devs[0];
	dk->base = 0;
	dk->size = disk;
	dk->ro = disk_ro;
	if (!dk->used)
		dev_start(dk, drv->present != 0); /* (support fixe : le thread principal le sert) */
	describe(dk);
	/* déjà servis : fenêtre mise à jour, ou fermée (taille 0 : E_IO) */
	u8 taken_p[PART_MAX], taken_a[PART_MAX];
	memset(taken_p, 0, sizeof taken_p);
	memset(taken_a, 0, sizeof taken_a);
	int aliases = 0;
	for (int i = 1; i < DEV_MAX; i++) {
		struct dev *d = &devs[i];
		if (!d->used)
			continue;
		aliases += d->alias[0] != 0;
		int k;
		for (k = 0; k < table_n; k++) {
			if (d->alias[0] ? !taken_a[k] && !strcmp(table[k].alias, d->alias)
					: !taken_p[k] && table[k].num == d->num)
				break;
		}
		if (k == table_n) {
			blog("%s : absente de la nouvelle table\n", d->name);
			d->size = 0;
			continue;
		}
		(d->alias[0] ? taken_a : taken_p)[k] = 1;
		window_set(d, &table[k]);
		describe(d);
	}
	/* les nouvelles : partitions, puis noms de plus */
	for (int k = 0; k < table_n; k++)
		if (!taken_p[k])
			dev_new(&table[k], 0);
	for (int k = 0; k < table_n; k++)
		if (table[k].alias[0] && !taken_a[k] && aliases++ < BLK_ALIAS_MAX)
			dev_new(&table[k], 1);
}

/* ---- les requêtes */

static struct client *client_of(struct dev *d, u32 pid)
{
	for (int i = 0; i < CL_MAX; i++)
		if (d->cl[i].pid == pid)
			return &d->cl[i];
	return 0;
}

/* le tampon est-il encore tenu par un client (de n'importe quel périphérique) ? */
static int buf_in_use(long buf)
{
	for (int i = 0; i < DEV_MAX; i++)
		for (int k = 0; k < CL_MAX; k++)
			if (devs[i].cl[k].pid && devs[i].cl[k].buf == buf)
				return 1;
	return 0;
}

static long op_attach(struct dev *d, struct msg *m)
{
	long va = korin_sys1(SYS_BUF_MAP, m->a); /* son tampon : mappé chez nous */
	if (va < 0)
		return va;
	u64 info[BI_SIZE / 8];
	u64 phys = korin_sys2(SYS_BUF_INFO, m->a, info) == 0 ? info[0] : 0; /* (pour le DMA) */
	u32 pid = m->pid;
	/* les places des clients terminés (fatfs démonté, mkfs) sont rendues, leur
	   tampon démappé s'il n'est plus tenu ailleurs */
	for (int i = 0; i < CL_MAX; i++) {
		struct client *c = &d->cl[i];
		u8 pl[PL_SIZE];
		if (!c->pid || c->pid == pid || korin_sys2(SYS_PROC_INFO, c->pid, pl) >= 0)
			continue;
		long b = c->buf;
		c->pid = 0;
		if (!buf_in_use(b))
			korin_sys1(SYS_BUF_FREE, b);
	}
	struct client *c = client_of(d, pid); /* une place (la sienne, ou libre) */
	if (!c)
		c = client_of(d, 0);
	if (!c)
		return E_NOMEM;
	u64 size = (u64)m->b > BUF_SLOT ? BUF_SLOT : (u64)m->b; /* une fenêtre au plus */
	c->start = (u64)va;
	c->end = (u64)va + size;
	c->phys = phys;
	c->buf = m->a;
	c->pid = pid;
	return 0;
}

static long op_rw(struct dev *d, struct msg *m)
{
	if (!d->size)
		return E_IO; /* (support retiré, partition disparue) */
	struct client *c = client_of(d, m->pid); /* le tampon de ce client */
	if (!c || !m->pid)
		return E_PERM;
	u64 sec = m->a, n = m->b, addr = m->c;
	if (addr < c->start || n > (c->end - c->start) >> 9 || addr + (n << 9) > c->end || sec + n > d->size ||
	    sec + n < sec)
		return E_INVAL;
	u64 phys = c->phys ? addr - c->start + c->phys : 0;
	if (m->op == BLK_WRITE) {
		if (d->ro) {
			if (drv->denied)
				drv->denied(drv->ctx, d->name, sec, n, m->pid);
			return E_PERM;
		}
		if (!n)
			return 0;
		return call_rw(1, d->base + sec, n, (void *)addr, phys);
	}
	if (!n)
		return 0;
	return call_rw(0, d->base + sec, n, (void *)addr, phys);
}

/* BLK_READ_D : droit dans la mémoire de l'appelant */
static long op_read_direct(struct dev *d, long req, struct msg *m)
{
	if (!drv->read_sg)
		return E_AGAIN;
	if (!d->size)
		return E_IO;
	u64 sec = m->a, n = m->b;
	if (!n || n > BLK_D_MAX || sec + n > d->size || sec + n < sec)
		return E_INVAL;
	long va = korin_sys4(SYS_SVC_MAP_CALLER, req, m->c, n << 9, MC_WRITE);
	if (va < 0)
		return va;
	u64 phys[BLK_SG_PAGES];
	long r = korin_sys3(SYS_SVC_CALLER_PHYS, va, phys, BLK_SG_PAGES);
	if (r < 0)
		return r;
	lock();
	r = drv->read_sg(drv->ctx, d->base + sec, n, (void *)va, phys);
	unlock();
	return r; /* (SVC_REPLY rend la fenêtre) */
}

static void on_signal(u64 bits)
{
	if ((bits & BLK_SIG_STOP) && drv->stop) {
		lock(); /* (gardé jusqu'à la fin : plus rien ne va au matériel arrêté) */
		drv->stop(drv->ctx);
		exit(0);
	}
	if ((bits & BLK_SIG_FLUSH) && drv->flush)
		call0(drv->flush);
	if (drv->signal)
		drv->signal(drv->ctx, bits);
}

/* (thread) un périphérique : ses requêtes */
static THREAD_ENTRY void dev_thread(struct dev *d)
{
	struct msg m __attribute__((aligned(16)));
	for (;;) {
		if (d->chan < 0)
			break;
		long req = korin_sys3(SYS_SVC_NEXT, d->chan, &m, 60000);
		if (req < 0)
			break;
		if (!req)
			continue;
		if (m.op == SVC_OP_SIGNAL) { /* (SVC_SIGNAL : pas de réponse) */
			on_signal((u64)m.a);
			continue;
		}
		long r;
		switch (m.op) {
		case BLK_INFO:
			m.a = d->ro;
			m.b = (i64)d->size;
			m.c = 512;
			m.d = korin_sys0(SYS_GETPID);
			r = 0;
			break;
		case BLK_ATTACH:
			r = op_attach(d, &m);
			break;
		case BLK_READ:
		case BLK_WRITE:
			r = op_rw(d, &m);
			break;
		case BLK_DMA:
			r = drv->dma ? call0(drv->dma) : 0;
			break;
		case BLK_UHS:
			r = drv->uhs ? call0(drv->uhs) : 0;
			break;
		case BLK_READ_D:
			r = op_read_direct(d, req, &m);
			break;
		case BLK_STATS: /* (hors verrou, comme l'asm : compteurs seulement) */
			r = drv->stats ? drv->stats(drv->ctx, (long)m.a, &m) : E_INVAL;
			break;
		default:
			r = E_INVAL;
		}
		m.op = r;
		korin_sys2(SYS_SVC_REPLY, req, &m);
	}
	if (d->chan >= 0)
		blog("%s : service fermé\n", d->name);
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

NORETURN void blk_serve(struct blk_driver *d)
{
	drv = d;
	if (d->version < 1 || d->version > BLK_DRIVER_V1 || (d->sector_size && d->sector_size != 512) ||
	    !d->read || !d->name || (d->present && !d->capacity)) {
		blog("pilote refusé (version %u, secteurs de %u octets)\n", d->version, d->sector_size);
		exit(1);
	}
	snprintf(disk_name, sizeof disk_name, "%s", d->name);
	apply_table();
	if (!drv->present)
		dev_thread(&devs[0]); /* support fixe : le thread principal sert le disque */
	/* support amovible : le thread principal le surveille */
	long ms = drv->poll_ms > 0 ? drv->poll_ms : POLL_MS;
	int present = devs[0].size != 0;
	for (;;) {
		korin_sys1(SYS_SLEEP, ms);
		if (!trylock())
			continue; /* (un transfert en cours : le support est là) */
		int changed = 0;
		long now = drv->present(drv->ctx, &changed);
		unlock();
		if (now && (changed || !present)) {
			blog("blk.%s : support remis, table relue\n", disk_name);
			apply_table();
		} else if (!now && present) {
			blog("blk.%s : support retiré, lectures et écritures en erreur\n", disk_name);
			for (int i = 0; i < DEV_MAX; i++)
				if (devs[i].used)
					devs[i].size = 0;
		}
		present = now != 0;
	}
}
