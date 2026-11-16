/* mkext4.c : formater en ext4 (voir mkext4.h). Les champs du superbloc, des descripteurs de groupe et des inodes
 * sont écrits octet par octet (petit-boutiste) : pas de structure empaquetée, le même code sur tous les compilateurs.
 * Références : Documentation/filesystems/ext4 du noyau Linux (disk layout), e2fsprogs (mke2fs). */
#include "mkext4.h"

void *memcpy(void *d, const void *s, __SIZE_TYPE__ n);
void *memset(void *d, int c, __SIZE_TYPE__ n);

#define B EXT4_BLOCK
#define BPG 32768u          /* blocs par groupe */
#define ISZ 256u            /* taille d'un inode */
#define INODE_RATIO 16384u
#define DESC 32u
#define FIRST_INO 11u
#define JOURNAL_INO 8u
#define ROOT_INO 2u
#define LF_INO 11u

/* superbloc : les champs utilisés */
#define S_INODES 0x00
#define S_BLOCKS 0x04
#define S_RBLOCKS 0x08
#define S_FREE_BLOCKS 0x0C
#define S_FREE_INODES 0x10
#define S_FIRST_DATA 0x14
#define S_LOG_BLOCK 0x18
#define S_LOG_CLUSTER 0x1C
#define S_BPG 0x20
#define S_CPG 0x24
#define S_IPG 0x28
#define S_WTIME 0x30
#define S_MAX_MNT 0x36
#define S_MAGIC 0x38
#define S_STATE 0x3A
#define S_ERRORS 0x3C
#define S_LASTCHECK 0x40
#define S_REV 0x4C
#define S_FIRST_INO 0x54
#define S_INODE_SIZE 0x58
#define S_GROUP_NR 0x5A
#define S_COMPAT 0x5C
#define S_INCOMPAT 0x60
#define S_RO_COMPAT 0x64
#define S_UUID 0x68
#define S_NAME 0x78
#define S_JOURNAL_INUM 0xE0
#define S_HASH_SEED 0xEC
#define S_DEF_HASH 0xFC
#define S_JNL_BACKUP 0xFD
#define S_DESC_SIZE 0xFE
#define S_MKFS_TIME 0x108
#define S_JNL_BLOCKS 0x10C
#define S_MIN_EXTRA 0x15C
#define S_WANT_EXTRA 0x15E
#define S_FLAGS 0x160
#define S_CSUM_TYPE 0x175
#define S_CSUM 0x3FC

#define COMPAT_HAS_JOURNAL 0x4u
#define COMPAT_EXT_ATTR 0x8u
#define INCOMPAT_FILETYPE 0x2u
#define INCOMPAT_EXTENTS 0x40u
#define RO_SPARSE_SUPER 0x1u
#define RO_LARGE_FILE 0x2u
#define RO_HUGE_FILE 0x8u
#define RO_DIR_NLINK 0x20u
#define RO_EXTRA_ISIZE 0x40u
#define RO_METADATA_CSUM 0x400u

#define BG_INODE_UNINIT 0x1u
#define BG_ITABLE_ZEROED 0x4u

#define EXTENTS_FL 0x80000u

static uint8_t blk[B];   /* un bloc de travail */
static uint8_t sb[1024]; /* le superbloc */
static uint8_t *gdt;     /* (dans gdt_buf) les descripteurs */
static uint8_t gdt_buf[64 * B];  /* 64 blocs : 8192 groupes, 1 Tio ; au-delà : refusé */

static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v); w16(p + 2, v >> 16); }

static uint32_t crc_table[256];

uint32_t ext4_crc32c(uint32_t crc, const void *p, uint64_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c >> 1) ^ (0x82F63B78u & (0u - (c & 1)));
			crc_table[i] = c;
		}
	const uint8_t *b = p;
	while (n--)
		crc = crc_table[(crc ^ *b++) & 0xFF] ^ (crc >> 8);
	return crc;
}

static uint32_t seed; /* crc32c(~0, uuid) */

static int has_backup(uint64_t g)
{
	if (g <= 1)
		return 1;
	for (uint64_t p = 3; p <= g; p *= 3)
		if (p == g)
			return 1;
	for (uint64_t p = 5; p <= g; p *= 5)
		if (p == g)
			return 1;
	for (uint64_t p = 7; p <= g; p *= 7)
		if (p == g)
			return 1;
	return 0;
}

struct layout {
	uint64_t blocks, groups, gdt_blocks, ipg, itable_blocks;
	uint64_t jgroup, jstart, jblocks;
};

static uint64_t meta_blocks(const struct layout *l, uint64_t g)
{
	return (has_backup(g) ? 1 + l->gdt_blocks : 0) + 2 + l->itable_blocks;
}

static uint64_t group_first(uint64_t g) { return g * BPG; }
static uint64_t group_size(const struct layout *l, uint64_t g)
{
	uint64_t n = l->blocks - group_first(g);
	return n > BPG ? BPG : n;
}
/* le premier bloc de la bitmap des blocs du groupe (après la copie du superbloc et des descripteurs) */
static uint64_t group_bitmap(const struct layout *l, uint64_t g)
{
	return group_first(g) + (has_backup(g) ? 1 + l->gdt_blocks : 0);
}

static void bset(uint8_t *bm, uint64_t i) { bm[i >> 3] |= (uint8_t)(1u << (i & 7)); }

static void inode_csum(uint8_t *ino, uint32_t num)
{
	uint32_t gen = (uint32_t)ino[0x64] | (uint32_t)ino[0x65] << 8 | (uint32_t)ino[0x66] << 16 | (uint32_t)ino[0x67] << 24;
	uint8_t le[4];
	w16(ino + 0x7C, 0);
	w16(ino + 0x82, 0);
	w32(le, num);
	uint32_t c = ext4_crc32c(seed, le, 4);
	w32(le, gen);
	c = ext4_crc32c(c, le, 4);
	c = ext4_crc32c(c, ino, ISZ);
	w16(ino + 0x7C, c & 0xFFFF);
	w16(ino + 0x82, c >> 16);
}

/* un inode (mode, liens, taille, une étendue de n blocs à start ; n = 0 : sans bloc) */
static void make_inode(uint8_t *ino, uint32_t num, uint32_t mode, uint32_t links, uint64_t size, uint64_t start,
		       uint32_t n, uint32_t now)
{
	memset(ino, 0, ISZ);
	w16(ino + 0x00, mode);
	w32(ino + 0x04, (uint32_t)size);
	w32(ino + 0x6C, (uint32_t)(size >> 32));
	w32(ino + 0x08, now);
	w32(ino + 0x0C, now);
	w32(ino + 0x10, now);
	w16(ino + 0x1A, links);
	w32(ino + 0x1C, n * (B / 512));
	w16(ino + 0x80, 32); /* i_extra_isize */
	w32(ino + 0x90, now); /* i_crtime */
	if (mode) {
		w32(ino + 0x20, EXTENTS_FL);
		uint8_t *h = ino + 0x28; /* l'en-tête d'étendues dans i_block */
		w16(h, 0xF30A);
		w16(h + 2, n ? 1 : 0);
		w16(h + 4, 4);
		w16(h + 6, 0);
		if (n) {
			uint8_t *e = h + 12;
			w32(e, 0);
			w16(e + 4, n);
			w16(e + 6, (uint32_t)(start >> 32));
			w32(e + 8, (uint32_t)start);
		}
	}
	inode_csum(ino, num);
}

/* une entrée de répertoire -> sa longueur */
static uint32_t dirent(uint8_t *p, uint32_t ino, uint32_t rec, const char *name, uint8_t type)
{
	uint32_t n = 0;
	while (name[n])
		n++;
	w32(p, ino);
	w16(p + 4, rec);
	p[6] = (uint8_t)n;
	p[7] = type;
	memcpy(p + 8, name, n);
	return rec;
}

/* la queue de somme d'un bloc de répertoire (metadata_csum) */
static void dir_tail(uint8_t *b, uint32_t ino, uint32_t gen)
{
	uint8_t *t = b + B - 12;
	memset(t, 0, 12);
	w16(t + 4, 12);
	t[7] = 0xDE;
	uint8_t le[4];
	w32(le, ino);
	uint32_t c = ext4_crc32c(seed, le, 4);
	w32(le, gen);
	c = ext4_crc32c(c, le, 4);
	w32(t + 8, ext4_crc32c(c, b, B - 12));
}

static void desc_csum(uint8_t *d, uint32_t g)
{
	uint8_t le[4];
	w16(d + 0x1E, 0);
	w32(le, g);
	uint32_t c = ext4_crc32c(seed, le, 4);
	c = ext4_crc32c(c, d, DESC);
	w16(d + 0x1E, c & 0xFFFF);
}

long mkext4(const struct mkext4_opts *o, ext4_wr wr, void *ctx, struct mkext4_info *info)
{
	struct layout l;
	long r;
	l.blocks = o->bytes / B;
	if (l.blocks < 2048)
		return -22;
	if (l.blocks > 0xFFFFFFFFull)
		l.blocks = 0xFFFFFFFFull;
	l.groups = (l.blocks + BPG - 1) / BPG;
	uint64_t inodes = o->bytes / INODE_RATIO;
	l.ipg = (inodes + l.groups - 1) / l.groups;
	l.ipg = (l.ipg + 15) / 16 * 16; /* un bloc d'inodes entier (16 inodes de 256 octets) */
	if (l.ipg < 32)
		l.ipg = 32;
	if (l.ipg > BPG)
		l.ipg = BPG;
	l.itable_blocks = l.ipg * ISZ / B;
	l.gdt_blocks = (l.groups * DESC + B - 1) / B;
	if (l.gdt_blocks > sizeof gdt_buf / B)
		return -22;
	/* le dernier groupe doit tenir ses métadonnées et un peu de données ; sinon il est abandonné */
	uint64_t last = l.groups - 1;
	if (l.groups > 1 && group_size(&l, last) < meta_blocks(&l, last) + 64) {
		l.blocks = last * BPG;
		l.groups--;
		l.gdt_blocks = (l.groups * DESC + B - 1) / B;
	}
	if (group_size(&l, 0) < meta_blocks(&l, 0) + 16)
		return -22;
	/* le journal : 1024 blocs au moins, 1/64 du volume, 16384 (64 Mio) au plus, dans le groupe 2 (pas de copie du
	 * superbloc) s'il existe, sinon après les données de départ du groupe 0 */
	l.jblocks = l.blocks / 64;
	if (l.jblocks < 1024)
		l.jblocks = 1024;
	if (l.jblocks > 16384)
		l.jblocks = 16384;
	if (l.groups > 2 && group_size(&l, 2) >= meta_blocks(&l, 2) + l.jblocks) {
		l.jgroup = 2;
		l.jstart = group_bitmap(&l, 2) + 2 + l.itable_blocks;
	} else {
		l.jgroup = 0;
		uint64_t room = group_size(&l, 0) - meta_blocks(&l, 0) - 2; /* (racine, lost+found) */
		if (l.jblocks > room / 2)
			l.jblocks = room / 2 >= 1024 ? room / 2 : 0;
		if (!l.jblocks)
			return -22;
		l.jstart = group_bitmap(&l, 0) + 2 + l.itable_blocks + 2;
	}
	uint64_t itab0 = group_bitmap(&l, 0) + 2;
	uint64_t root_blk = itab0 + l.itable_blocks, lf_blk = root_blk + 1;

	/* la graine des sommes */
	seed = ext4_crc32c(~0u, o->uuid, 16);

	/* ---- les groupes : bitmaps (écrites pour tous), descripteurs */
	gdt = gdt_buf;
	memset(gdt_buf, 0, (__SIZE_TYPE__)(l.gdt_blocks * B));
	uint64_t free_total = 0;
	static uint8_t bm[2 * B]; /* bitmap des blocs, puis des inodes, d'un groupe */
	for (uint64_t g = 0; g < l.groups; g++) {
		memset(bm, 0, sizeof bm);
		uint64_t size = group_size(&l, g), used = meta_blocks(&l, g);
		for (uint64_t i = 0; i < used; i++)
			bset(bm, i);
		if (g == 0) { /* la racine et lost+found */
			bset(bm, root_blk - group_first(0));
			bset(bm, lf_blk - group_first(0));
			used += 2;
		}
		if (g == l.jgroup) {
			for (uint64_t i = 0; i < l.jblocks; i++)
				bset(bm, l.jstart - group_first(g) + i);
			used += l.jblocks;
		}
		for (uint64_t i = size; i < BPG; i++) /* (au-delà de la fin du volume : occupé) */
			bset(bm, i);
		uint8_t *ib = bm + B;
		uint64_t used_ino = g == 0 ? FIRST_INO : 0;
		for (uint64_t i = 0; i < used_ino; i++)
			bset(ib, i);
		for (uint64_t i = l.ipg; i < B * 8; i++) /* (le bourrage de la bitmap des inodes) */
			bset(ib, i);
		uint64_t bb = group_bitmap(&l, g);
		if ((r = wr(ctx, bb, 2, bm)))
			return r;
		uint8_t *d = gdt + g * DESC;
		w32(d + 0x00, (uint32_t)bb);
		w32(d + 0x04, (uint32_t)(bb + 1));
		w32(d + 0x08, (uint32_t)(bb + 2));
		w16(d + 0x0C, (uint32_t)(size - used));
		w16(d + 0x0E, (uint32_t)(l.ipg - used_ino));
		w16(d + 0x10, g == 0 ? 2 : 0);
		w16(d + 0x12, g == 0 ? BG_ITABLE_ZEROED : BG_INODE_UNINIT);
		w16(d + 0x18, ext4_crc32c(seed, bm, BPG / 8) & 0xFFFF);
		w16(d + 0x1A, ext4_crc32c(seed, ib, l.ipg / 8) & 0xFFFF);
		w16(d + 0x1C, (uint32_t)(l.ipg - used_ino));
		desc_csum(d, (uint32_t)g);
		free_total += size - used;
	}

	/* ---- la table d'inodes du groupe 0 : à zéro, puis les inodes 1 à 11 */
	memset(blk, 0, B);
	for (uint64_t i = 0; i < l.itable_blocks; i++)
		if ((r = wr(ctx, itab0 + i, 1, blk)))
			return r;
	static uint8_t it[B]; /* le premier bloc de la table : inodes 1 à 16 */
	memset(it, 0, B);
	for (uint32_t n = 1; n <= FIRST_INO; n++) {
		uint8_t *ino = it + (n - 1) * ISZ;
		if (n == ROOT_INO)
			make_inode(ino, n, 0x41ED, 3, B, root_blk, 1, o->now);
		else if (n == LF_INO)
			make_inode(ino, n, 0x41C0, 2, B, lf_blk, 1, o->now);
		else if (n == JOURNAL_INO)
			make_inode(ino, n, 0x8180, 1, l.jblocks * B, l.jstart, (uint32_t)l.jblocks, o->now);
		else
			make_inode(ino, n, 0, 0, 0, 0, 0, 0); /* (réservé : vide, avec sa somme) */
	}
	if ((r = wr(ctx, itab0, 1, it)))
		return r;

	/* ---- les répertoires */
	memset(blk, 0, B);
	uint32_t off = dirent(blk, ROOT_INO, 12, ".", 2);
	off += dirent(blk + off, ROOT_INO, 12, "..", 2);
	dirent(blk + off, LF_INO, B - 12 - off, "lost+found", 2);
	dir_tail(blk, ROOT_INO, 0);
	if ((r = wr(ctx, root_blk, 1, blk)))
		return r;
	memset(blk, 0, B);
	off = dirent(blk, LF_INO, 12, ".", 2);
	dirent(blk + off, ROOT_INO, B - 12 - off, "..", 2);
	dir_tail(blk, LF_INO, 0);
	if ((r = wr(ctx, lf_blk, 1, blk)))
		return r;

	/* ---- le journal : son superbloc (jbd2 v2, propre), le reste n'est pas lu tant qu'il est propre */
	memset(blk, 0, B);
	uint8_t *j = blk;
#define BE32(p, v) ((p)[0] = (uint8_t)((v) >> 24), (p)[1] = (uint8_t)((v) >> 16), (p)[2] = (uint8_t)((v) >> 8), (p)[3] = (uint8_t)(v))
	BE32(j + 0, 0xC03B3998u);
	BE32(j + 4, 4u);                 /* superbloc v2 */
	BE32(j + 12, B);                 /* s_blocksize */
	BE32(j + 16, (uint32_t)l.jblocks); /* s_maxlen */
	BE32(j + 20, 1u);                /* s_first */
	BE32(j + 24, 1u);                /* s_sequence */
	BE32(j + 28, 0u);                /* s_start : vide */
	memcpy(j + 48, o->uuid, 16);     /* s_uuid */
	BE32(j + 64, 1u);                /* s_nr_users */
	memcpy(j + 256, o->uuid, 16);    /* s_users[0] */
	if ((r = wr(ctx, l.jstart, 1, blk)))
		return r;

	/* ---- le superbloc */
	memset(sb, 0, sizeof sb);
	uint64_t free_inodes = l.ipg * l.groups - FIRST_INO;
	w32(sb + S_INODES, (uint32_t)(l.ipg * l.groups));
	w32(sb + S_BLOCKS, (uint32_t)l.blocks);
	w32(sb + S_RBLOCKS, (uint32_t)(l.blocks / 20));
	w32(sb + S_FREE_BLOCKS, (uint32_t)free_total);
	w32(sb + S_FREE_INODES, (uint32_t)free_inodes);
	w32(sb + S_FIRST_DATA, 0);
	w32(sb + S_LOG_BLOCK, 2);
	w32(sb + S_LOG_CLUSTER, 2);
	w32(sb + S_BPG, BPG);
	w32(sb + S_CPG, BPG);
	w32(sb + S_IPG, (uint32_t)l.ipg);
	w32(sb + S_WTIME, o->now);
	w16(sb + S_MAX_MNT, 0xFFFF);
	w16(sb + S_MAGIC, 0xEF53);
	w16(sb + S_STATE, 1);
	w16(sb + S_ERRORS, 1);
	w32(sb + S_LASTCHECK, o->now);
	w32(sb + S_REV, 1);
	w32(sb + S_FIRST_INO, FIRST_INO);
	w16(sb + S_INODE_SIZE, ISZ);
	w32(sb + S_COMPAT, COMPAT_HAS_JOURNAL | COMPAT_EXT_ATTR);
	w32(sb + S_INCOMPAT, INCOMPAT_FILETYPE | INCOMPAT_EXTENTS);
	w32(sb + S_RO_COMPAT, RO_SPARSE_SUPER | RO_LARGE_FILE | RO_HUGE_FILE | RO_DIR_NLINK | RO_EXTRA_ISIZE |
				  RO_METADATA_CSUM);
	memcpy(sb + S_UUID, o->uuid, 16);
	for (int i = 0; o->label && o->label[i] && i < 16; i++)
		sb[S_NAME + i] = (uint8_t)o->label[i];
	w32(sb + S_JOURNAL_INUM, JOURNAL_INO);
	for (int i = 0; i < 4; i++)
		w32(sb + S_HASH_SEED + 4 * i, o->hash_seed[i]);
	sb[S_DEF_HASH] = 1; /* half_md4 */
	sb[S_JNL_BACKUP] = 1; /* EXT3_JNL_BACKUP_BLOCKS : la copie de i_block du journal */
	w16(sb + S_DESC_SIZE, 0);
	w32(sb + S_MKFS_TIME, o->now);
	memcpy(sb + S_JNL_BLOCKS, it + (JOURNAL_INO - 1) * ISZ + 0x28, 60); /* i_block */
	w32(sb + S_JNL_BLOCKS + 15 * 4, 0); /* i_size_high */
	w32(sb + S_JNL_BLOCKS + 16 * 4, (uint32_t)(l.jblocks * B));
	w16(sb + S_MIN_EXTRA, 32);
	w16(sb + S_WANT_EXTRA, 32);
	w32(sb + S_FLAGS, 1); /* hachage signé (comme mke2fs sur x86) */
	sb[S_CSUM_TYPE] = 1;
	/* le superbloc et les descripteurs, puis leurs copies (sparse_super) : s_block_group_nr de chaque copie */
	for (uint64_t g = 0; g < l.groups; g++) {
		if (!has_backup(g))
			continue;
		w16(sb + S_GROUP_NR, (uint32_t)g);
		w32(sb + S_CSUM, ext4_crc32c(~0u, sb, S_CSUM));
		memset(blk, 0, B);
		memcpy(blk + (g == 0 ? 1024 : 0), sb, 1024);
		if ((r = wr(ctx, group_first(g), 1, blk)))
			return r;
		if ((r = wr(ctx, group_first(g) + 1, l.gdt_blocks, gdt)))
			return r;
	}
	if (info) {
		info->blocks = l.blocks;
		info->inodes = l.ipg * l.groups;
		info->groups = l.groups;
		info->journal_blocks = l.jblocks;
		info->free_blocks = free_total;
	}
	return 0;
}
