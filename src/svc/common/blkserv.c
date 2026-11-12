/* blkserv.c : serveur commun des périphériques blocs (src/inc/blk.inc),
 * traduction de src/svc/inc/blkserv.inc (le pilote de la carte SD du Pi garde
 * l'asm ; imgblk, usbstorage et nvme s'en servent en C).
 *
 * blk_start lit la table de partitions (MBR, logiques par les EBR ; GPT derrière
 * un MBR protecteur 0xEE) et sert le disque (« blk.<nom> ») et chaque partition
 * (« blk.<nom>p1 »...), une partition n'étant qu'une fenêtre sur le disque. Un
 * thread par périphérique (le thread principal sert le disque) ; les appels au
 * pilote passent un à un (blk_lock). Les données passent par un tampon partagé
 * du client, attaché d'abord (BLK_ATTACH) ; lire / écrire désignent une adresse
 * dans ce tampon.
 *
 * En plus de l'asm (12/11, pour nvme) : la GPT (partitions numérotées comme
 * leurs entrées : la cinquième entrée est p5 même si une entrée avant elle est
 * vide ; CRC de l'en-tête et des entrées vérifiés et dits au crochet), les
 * crochets blk_table_fn (lecture seule et nom de plus, partition par partition),
 * blk_deny_fn (écriture refusée), blk_signal_fn (SVC_SIGNAL du disque). Sans
 * crochet et sur un disque MBR, rien ne change. */
#include "blkserv.h"

#define LOG_MAX 8              /* partitions logiques (p5 à p12) */
#define PART_MAX (BLK_GPT_MAX > 4 + LOG_MAX ? BLK_GPT_MAX : 4 + LOG_MAX)
#define DEV_MAX (1 + PART_MAX + BLK_ALIAS_MAX) /* le disque, les partitions, les noms de plus */
#define CL_MAX 8
#define STACK_THREAD 16384
#define BLK_SG_PAGES (BLK_D_MAX / 8 + 2) /* (une page entamée de chaque côté) */
#define GPT_ENTRIES_MAX 1024

struct client {
	u64 pid, start, end, phys;
	long num; /* (le tampon, pour BUF_FREE : l'asm le recalcule depuis l'adresse) */
};
struct dev {
	u64 base, size, ro;
	long chan;
	char name[32];
	struct client cl[CL_MAX];
};

blk_fn blk_read_fn, blk_write_fn, blk_dma_fn, blk_uhs_fn;
long (*blk_sg_fn)(u64 sector, u64 count, void *addr, const u64 *phys);
long (*blk_stat_fn)(long busy_us, struct msg *m);
u8 blk_type_ro;
void (*blk_table_fn)(struct blk_part *t, int n, const struct blk_gpt *g);
void (*blk_deny_fn)(const char *dev, u64 sector, u64 count, u32 pid);
void (*blk_signal_fn)(u64 bits);

static u32 blk_lock;
static struct dev devs[DEV_MAX];
static u8 sector0[512] __attribute__((aligned(16)));
static u8 gsec[512] __attribute__((aligned(16)));
static const char *disk_name;
static long disk_ro;
static struct dev *last_dev;
static struct blk_part table[PART_MAX];
static int table_n;
static struct blk_gpt gpt;

/* appel du pilote, sous verrou (un appel à la fois) */
static long blk_call(blk_fn fn, u64 sector, u64 count, void *addr, u64 phys)
{
	while (__atomic_exchange_n(&blk_lock, 1, __ATOMIC_ACQUIRE))
		korin_sys0(SYS_YIELD);
	long r = fn(sector, count, addr, phys);
	__atomic_store_n(&blk_lock, 0, __ATOMIC_RELEASE);
	return r;
}

static long blk_call_sg(u64 sector, u64 count, void *addr, const u64 *phys)
{
	while (__atomic_exchange_n(&blk_lock, 1, __ATOMIC_ACQUIRE))
		korin_sys0(SYS_YIELD);
	long r = blk_sg_fn(sector, count, addr, phys);
	__atomic_store_n(&blk_lock, 0, __ATOMIC_RELEASE);
	return r;
}

/* « blk.<nom>[p<n>] », ou « blk.<alias> » -> canal */
static long blk_register(struct dev *d, long part, const char *alias)
{
	if (alias)
		snprintf(d->name, sizeof d->name, "blk.%s", alias);
	else if (part)
		snprintf(d->name, sizeof d->name, "blk.%sp%ld", disk_name, part);
	else
		snprintf(d->name, sizeof d->name, "blk.%s", disk_name);
	d->chan = korin_sys2(SYS_SVC_REGISTER, d->name, 0);
	return d->chan;
}

static THREAD_ENTRY void blk_thread(struct dev *d);

/* un périphérique de plus (partition ou nom de plus) : enregistré, avec son thread */
static void blk_serve_window(u64 first, u64 count, long ro, long num, const char *alias)
{
	if (last_dev >= &devs[DEV_MAX - 1])
		return;
	struct dev *d = ++last_dev;
	d->base = first;
	d->size = count;
	d->ro = ro;
	if (blk_register(d, num, alias) < 0)
		return;
	u8 *stack = ak_malloc(STACK_THREAD);
	if (!stack)
		return;
	korin_sys3(SYS_THREAD_CREATE, blk_thread, stack + STACK_THREAD, d);
}

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

/* une partition dans la table (pas encore servie) */
static struct blk_part *table_add(u64 first, u64 count, u8 type, long num)
{
	if (table_n >= PART_MAX)
		return 0;
	struct blk_part *p = &table[table_n++];
	memset(p, 0, sizeof *p);
	p->num = num;
	p->first = first;
	p->count = count;
	p->type = type;
	p->ro = disk_ro;
	if (blk_type_ro && type == blk_type_ro) /* un type protégé (exFAT : la partition personnelle) */
		p->ro = 1;
	return p;
}

/* la GPT (MBR protecteur vu) : en-tête au secteur 1, entrées ; numéros = rangs des entrées + 1 */
static void read_gpt(u64 disk)
{
	if (blk_call(blk_read_fn, 1, 1, gsec, 0) || memcmp(gsec, "EFI PART", 8))
		return;
	u32 hsize = le32(gsec + 12);
	if (hsize < 92 || hsize > 512)
		return;
	u32 hcrc = le32(gsec + 16);
	gsec[16] = gsec[17] = gsec[18] = gsec[19] = 0;
	int crc_ok = crc32_add(0, gsec, hsize) == hcrc;
	u64 lba = le64(gsec + 72);
	u32 count = le32(gsec + 80), esz = le32(gsec + 84), ecrc = le32(gsec + 88);
	if (esz < 128 || esz > 512 || (512 % esz) || count > GPT_ENTRIES_MAX || lba < 2 || lba >= disk)
		return;
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
			if (blk_call(blk_read_fn, lba + i / per, 1, gsec, 0)) {
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
		if (last < first || first >= disk)
			continue;
		if (last >= disk)
			last = disk - 1; /* (dépasse la fin : coupée) */
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
}

void blk_start(const char *name, long ro, u64 sectors)
{
	blk_call(blk_read_fn, 0, 1, sector0, 0); /* table de partitions (résultat ignoré, comme l'asm) */
	disk_name = name;
	disk_ro = ro;
	last_dev = &devs[0]; /* le disque entier */
	devs[0].base = 0;
	devs[0].size = sectors ? sectors : (u64)-1;
	devs[0].ro = ro;
	blk_register(&devs[0], 0, 0);
	/* MBR valide ? (0x55AA, et pas un secteur de démarrage FAT d'emblée : « FAT32 » à 82) */
	if ((sector0[510] | sector0[511] << 8) == 0xAA55 && sector0[82] != 'F') {
		int prot = 0;
		for (int p = 0; p < 4; p++)
			prot |= sector0[446 + 16 * p + 4] == 0xEE;
		if (prot) {
			read_gpt(devs[0].size);
		} else {
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
				if (blk_call(blk_read_fn, ebr, 1, sector0, 0))
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
	}
	if (blk_table_fn)
		blk_table_fn(table, table_n, &gpt);
	for (int i = 0; i < table_n; i++)
		blk_serve_window(table[i].first, table[i].count, table[i].ro, table[i].num, 0);
	int aliases = 0;
	for (int i = 0; i < table_n; i++)
		if (table[i].alias[0] && aliases++ < BLK_ALIAS_MAX)
			blk_serve_window(table[i].first, table[i].count, table[i].ro, 0, table[i].alias);
	blk_thread(&devs[0]);
}

/* (thread) un périphérique : ses requêtes */
static THREAD_ENTRY void blk_thread(struct dev *d)
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
			if (blk_signal_fn)
				blk_signal_fn((u64)m.a);
			continue;
		}
		long r;
		switch (m.op) {
		case BLK_INFO:
			m.a = d->ro;
			m.b = d->size;
			m.c = 512;
			m.d = korin_sys0(SYS_GETPID);
			r = 0;
			break;
		case BLK_STATS:
			r = blk_stat_fn ? blk_stat_fn(m.a, &m) : E_INVAL;
			break;
		case BLK_DMA:
		case BLK_UHS: {
			blk_fn f = m.op == BLK_DMA ? blk_dma_fn : blk_uhs_fn;
			r = f ? blk_call(f, 0, 0, 0, 0) : 0;
			break;
		}
		case BLK_READ_D: {
			if (!blk_sg_fn) {
				r = E_AGAIN;
				break;
			}
			u64 sec = m.a, n = m.b;
			if (!n || n > BLK_D_MAX || sec + n > d->size || sec + n < sec) {
				r = E_INVAL;
				break;
			}
			long va = korin_sys4(SYS_SVC_MAP_CALLER, req, m.c, n << 9, MC_WRITE);
			if (va < 0) {
				r = va;
				break;
			}
			u64 phys[BLK_SG_PAGES];
			r = korin_sys3(SYS_SVC_CALLER_PHYS, va, phys, BLK_SG_PAGES);
			if (r >= 0)
				r = blk_call_sg(d->base + sec, n, (void *)va, phys);
			break;
		}
		case BLK_ATTACH: {
			long va = korin_sys1(SYS_BUF_MAP, m.a); /* son tampon : mappé chez nous */
			if (va < 0) {
				r = va;
				break;
			}
			u64 info[BI_SIZE / 8];
			u64 phys = korin_sys2(SYS_BUF_INFO, m.a, info) == 0 ? info[0] : 0; /* (pour le DMA) */
			u32 pid = m.pid;
			/* les places des clients terminés (fatfs démonté, mkfs) sont rendues, leur
			   tampon démappé */
			for (int i = 0; i < CL_MAX; i++) {
				struct client *c = &d->cl[i];
				u8 pl[PL_SIZE];
				if (!c->pid || c->pid == pid)
					continue;
				if (korin_sys2(SYS_PROC_INFO, c->pid, pl) >= 0)
					continue;
				c->pid = 0;
				korin_sys1(SYS_BUF_FREE, (c->start - BUF_VA_BASE) >> 26); /* (BUF_SLOT = 64 Mo) */
			}
			struct client *c = 0, *libre = 0; /* une place (la sienne, ou libre) */
			for (int i = 0; i < CL_MAX && !c; i++) {
				if (d->cl[i].pid == pid)
					c = &d->cl[i];
				else if (!d->cl[i].pid && !libre)
					libre = &d->cl[i];
			}
			if (!c)
				c = libre;
			if (!c) {
				r = E_NOMEM;
				break;
			}
			u64 size = (u64)m.b > BUF_SLOT ? BUF_SLOT : (u64)m.b; /* une fenêtre au plus */
			c->start = va;
			c->end = va + size;
			c->phys = phys;
			c->num = m.a;
			c->pid = pid;
			r = 0;
			break;
		}
		case BLK_READ:
		case BLK_WRITE: {
			struct client *c = 0; /* le tampon de ce client */
			for (int i = 0; i < CL_MAX; i++)
				if (d->cl[i].pid == m.pid) {
					c = &d->cl[i];
					break;
				}
			if (!c) {
				r = E_PERM;
				break;
			}
			u64 sec = m.a, n = m.b, addr = m.c;
			if (addr < c->start || addr + (n << 9) > c->end || sec + n > d->size || sec + n < sec) {
				r = E_INVAL;
				break;
			}
			u64 phys = c->phys ? addr - c->start + c->phys : 0;
			if (m.op == BLK_WRITE) {
				if (d->ro) {
					if (blk_deny_fn)
						blk_deny_fn(d->name, sec, n, m.pid);
					r = E_PERM;
					break;
				}
				r = blk_call(blk_write_fn, d->base + sec, n, (void *)addr, phys);
			} else {
				r = blk_call(blk_read_fn, d->base + sec, n, (void *)addr, phys);
			}
			break;
		}
		default:
			r = E_INVAL;
		}
		m.op = r;
		korin_sys2(SYS_SVC_REPLY, req, &m);
	}
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}
